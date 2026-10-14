#include "RogueliteRoomTypeAssignment.h"
#include "Math/RandomStream.h"

static_assert(uint8(ERogueliteSeedRoomType::Combat)==0 && uint8(ERogueliteSeedRoomType::Challenge)==1
    && uint8(ERogueliteSeedRoomType::SupplyShop)==2 && uint8(ERogueliteSeedRoomType::Final)==3 && uint8(ERogueliteSeedRoomType::Event)==4,
    "Serialized existing room type values must never move.");

FRogueliteRoomTypeAssignmentSettings::FRogueliteRoomTypeAssignmentSettings()
{
    for(int32 I=0;I<5;++I){FRogueliteRoomTypeRule R;R.Type=ERogueliteSeedRoomType(I);
        if(R.Type==ERogueliteSeedRoomType::Final){R.MinCount=R.MaxCount=1;R.bAllowBranch=false;}Rules.Add(R);}
}
FString FRogueliteRoomTypeAssigner::DescribeSettings(const FRogueliteRoomTypeAssignmentSettings& S)
{
    auto Ordered=S.Rules;Ordered.Sort([](const auto& A,const auto& B){return uint8(A.Type)<uint8(B.Type);});
    FString T=FString::Printf(TEXT("B4TypesV%d|start=%d|event=%s|matching=mandatory-then-weighted-capacity-v1"),S.Version,uint8(S.StartType),*S.EventDefinitionId.ToString());
    for(const auto& R:Ordered)T+=FString::Printf(TEXT("|rule=%d,%d,%d,%d,%d,%d"),uint8(R.Type),R.Weight,R.MinCount,R.MaxCount,R.bAllowMainPath,R.bAllowBranch);
    return T;
}
FRogueliteRoomTypeAssignmentResult FRogueliteRoomTypeAssigner::Assign(const FRogueliteRoomGraph& G,const FRogueliteRoomTypeAssignmentSettings& S,
    const TArray<TArray<ERogueliteSeedRoomType>>& Compatible)
{
    FRogueliteRoomTypeAssignmentResult Result;const int32 N=G.Nodes.Num();
    Result.SettingsDigest=FRogueliteRoomGraphGenerator::StableTextDigest(DescribeSettings(S));
    const auto Fail=[&](const FString& Why,int32 Node=INDEX_NONE){Result.bSucceeded=false;Result.Types.Reset();Result.EventDefinitionIds.Reset();Result.Counts.Reset();Result.StableDigest.Reset();Result.Reason=Why;Result.FailedNodeId=Node;return Result;};
    if(S.Version!=2 || N<2 || N>256 || S.Rules.Num()!=5 || uint8(S.StartType)>4 || S.StartType==ERogueliteSeedRoomType::Final
        || !G.FindNode(G.StartNodeId) || !G.FindNode(G.FinalNodeId) || (!Compatible.IsEmpty() && Compatible.Num()!=N))
        return Fail(TEXT("V2 RoomType assignment requires valid ordered graph, five rules, version 2 and a non-Final Start constraint."));
    TArray<FRogueliteRoomTypeRule> Rules;Rules.SetNum(5);TArray<bool> Seen;Seen.Init(false,5);int32 MinSum=0,MaxSum=0;
    for(const auto& R:S.Rules)
    {
        const int32 I=uint8(R.Type);
        if(I>4 || Seen[I] || R.Weight<0 || R.Weight>10000 || R.MinCount<0 || R.MaxCount<R.MinCount || R.MaxCount>256
            || (R.MinCount>0 && R.Weight==0))return Fail(TEXT("Invalid/duplicate type rule, negative/zero required weight or ordered quota."));
        Seen[I]=true;Rules[I]=R;MinSum+=R.MinCount;MaxSum+=R.Weight?R.MaxCount:0;
    }
    if(Rules[3].MinCount!=1 || Rules[3].MaxCount!=1 || !Rules[3].Weight || MinSum>N || MaxSum<N)
        return Fail(FString::Printf(TEXT("RoomType quotas cannot fit N=%d: total minima=%d maxima=%d; Final must be exactly one."),N,MinSum,MaxSum));
    TArray<TArray<bool>> Allowed;Allowed.SetNum(N);TArray<TArray<int32>> TypeOrder;TypeOrder.SetNum(N);
    for(int32 Node=0;Node<N;++Node)
    {
        if(G.Nodes[Node].NodeId!=Node)return Fail(TEXT("RoomType graph NodeId/order is not stable."),Node);
        Allowed[Node].Init(false,5);auto& Pool=TypeOrder[Node];const auto& Meta=G.Nodes[Node];
        for(int32 Type=0;Type<5;++Type)
        {
            const auto& R=Rules[Type];const bool Fixed=Node==G.FinalNodeId?Type==3:Node==G.StartNodeId?Type==uint8(S.StartType):Type!=3;
            const bool Position=Meta.Role==ERogueliteGraphNodeRole::Branch?R.bAllowBranch:R.bAllowMainPath;
            const bool CompatibleType=Compatible.IsEmpty() || Compatible[Node].Contains(ERogueliteSeedRoomType(Type));
            if(Fixed && Position && CompatibleType && R.Weight>0 && R.MaxCount>0){Allowed[Node][Type]=true;Pool.Add(Type);}
        }
        if(Pool.IsEmpty())return Fail(FString::Printf(TEXT("Node=%d Role=%d Degree=%d has no legal RoomType under start/final/position/template capacity constraints."),Node,int32(Meta.Role),Meta.Degree),Node);
        auto Remaining=Pool;Pool.Reset();FRandomStream Random(FRogueliteRoomGraphGenerator::DeriveSeed(G.Seed,ERogueliteGraphSeedPurpose::RoomType,G.AlgorithmVersion,G.ConfigVersion,Node));
        while(!Remaining.IsEmpty())
        {
            int32 Sum=0;for(int32 Type:Remaining)Sum+=Rules[Type].Weight;int32 Pick=Random.RandRange(1,Sum),Index=0;
            for(int32 I=0;I<Remaining.Num();++I)if((Pick-=Rules[Remaining[I]].Weight)<=0){Index=I;break;}
            Pool.Add(Remaining[Index]);Remaining.RemoveAt(Index);
        }
    }
    // Expand bounded per-type capacity into slots. Mandatory slots are filled first;
    // subsequent augmenting paths preserve their occupancy, hence all hard minima.
    TArray<int32> SlotTypes,Mandatory;
    for(int32 Type=0;Type<5;++Type)for(int32 I=0;I<FMath::Min(N,Rules[Type].Weight?Rules[Type].MaxCount:0);++I)
    {const int32 Slot=SlotTypes.Add(Type);if(I<Rules[Type].MinCount)Mandatory.Add(Slot);}
    TArray<int32> NodeSlots,SlotNodes;NodeSlots.Init(INDEX_NONE,N);SlotNodes.Init(INDEX_NONE,SlotTypes.Num());
    TArray<TArray<int32>> NodeOrder;NodeOrder.SetNum(5);
    for(int32 Type=0;Type<5;++Type)
    {
        for(int32 Node=0;Node<N;++Node)if(Allowed[Node][Type])NodeOrder[Type].Add(Node);
        FRandomStream Random(FRogueliteRoomGraphGenerator::DeriveSeed(G.Seed,ERogueliteGraphSeedPurpose::RoomType,G.AlgorithmVersion,G.ConfigVersion,N+Type));
        for(int32 I=NodeOrder[Type].Num()-1;I>0;--I)NodeOrder[Type].Swap(I,Random.RandRange(0,I));
    }
    TFunction<bool(int32,TArray<bool>&)> FillSlot;
    FillSlot=[&](int32 Slot,TArray<bool>& Visited)
    {
        for(int32 Node:NodeOrder[SlotTypes[Slot]])if(!Visited[Node])
        {
            Visited[Node]=true;const int32 Old=NodeSlots[Node];
            if(Old==INDEX_NONE || FillSlot(Old,Visited)){NodeSlots[Node]=Slot;SlotNodes[Slot]=Node;return true;}
        }
        return false;
    };
    for(int32 Slot:Mandatory){TArray<bool> Visited;Visited.Init(false,N);if(!FillSlot(Slot,Visited))
        return Fail(FString::Printf(TEXT("RoomType=%d minimum quota=%d is unsatisfiable for legal nodes/template capacity; topology is unchanged."),SlotTypes[Slot],Rules[SlotTypes[Slot]].MinCount));}
    TFunction<bool(int32,TArray<bool>&)> FillNode;
    FillNode=[&](int32 Node,TArray<bool>& Visited)
    {
        for(int32 Type:TypeOrder[Node])for(int32 Slot=0;Slot<SlotTypes.Num();++Slot)if(SlotTypes[Slot]==Type && !Visited[Slot])
        {
            Visited[Slot]=true;const int32 Old=SlotNodes[Slot];
            if(Old==INDEX_NONE || FillNode(Old,Visited)){NodeSlots[Node]=Slot;SlotNodes[Slot]=Node;return true;}
        }
        return false;
    };
    for(int32 Node=0;Node<N;++Node)if(NodeSlots[Node]==INDEX_NONE)
    {TArray<bool> Visited;Visited.Init(false,SlotTypes.Num());if(!FillNode(Node,Visited))return Fail(FString::Printf(TEXT("Node=%d cannot fit RoomType maximum quotas/eligibility; graph not modified."),Node),Node);}
    Result.Counts.Init(0,5);
    FString Text=FString::Printf(TEXT("B4AssignmentV2|graph=%s|settings=%s"),*G.StableDigest,*Result.SettingsDigest);
    for(int32 Node=0;Node<N;++Node)
    {
        const auto Type=ERogueliteSeedRoomType(SlotTypes[NodeSlots[Node]]);const bool Event=Type==ERogueliteSeedRoomType::Event;
        if(Event && S.EventDefinitionId.IsNone())return Fail(FString::Printf(TEXT("Event Node=%d requires EventDefinitionId; no event gameplay instantiated."),Node),Node);
        Result.Types.Add(Type);Result.EventDefinitionIds.Add(Event?S.EventDefinitionId:NAME_None);++Result.Counts[uint8(Type)];
        Text+=FString::Printf(TEXT("|node=%d,%d,%s"),Node,uint8(Type),*Result.EventDefinitionIds.Last().ToString());
    }
    Result.StableDigest=FRogueliteRoomGraphGenerator::StableTextDigest(Text);Result.bSucceeded=true;
    Result.Reason=TEXT("Independent weighted RoomType matching satisfies start/final, position and all quotas; Event is data only.");return Result;
}
