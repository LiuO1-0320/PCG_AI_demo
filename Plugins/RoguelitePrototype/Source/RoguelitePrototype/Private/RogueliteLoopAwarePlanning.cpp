#include "RogueliteLoopAwarePlanning.h"
#include "RogueliteSeedAssemblyInternal.h"
#include "RogueliteGenerationGeometry.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Math/RandomStream.h"

namespace
{
using G=FRogueliteRoomGraphGenerator;
using namespace RogueliteSeedAssemblyInternal;
constexpr auto SpatialPurpose=static_cast<ERogueliteGraphSeedPurpose>(0x63b29a15u);
bool Editor(AActor* A){return IsValid(A)&&A->GetWorld()&&A->GetWorld()->WorldType==EWorldType::Editor;}
FString SettingsText(const FRogueliteSeedLayoutSettings& S,const FRogueliteLoopAwareSettings& V)
{const FString A=RogueliteGenerationGeometry::SettingsText(S);FString B;FRogueliteLoopAwareSettings::StaticStruct()->ExportText(B,&V,nullptr,nullptr,PPF_None,nullptr);return A+TEXT("|SpatialDomain=63b29a15|")+B;}
FBox RoomBox(const FRogueliteTemplateDescriptor& D)
{return FBox(FVector(-D.Dimensions.X*.5-D.WallThickness,-D.Dimensions.Y*.5-D.WallThickness,-D.FloorThickness),FVector(D.Dimensions.X*.5+D.WallThickness,D.Dimensions.Y*.5+D.WallThickness,D.Dimensions.Z));}
bool Intersects(const FBox& A,const FBox& B)
{return A.IsValid&&B.IsValid&&FMath::Min(A.Max.X,B.Max.X)-FMath::Max(A.Min.X,B.Min.X)>.1&&FMath::Min(A.Max.Y,B.Max.Y)-FMath::Max(A.Min.Y,B.Min.Y)>.1&&FMath::Min(A.Max.Z,B.Max.Z)-FMath::Max(A.Min.Z,B.Min.Z)>.1;}
FRogueliteDoorSocket Placed(const FRogueliteDoorSocket& D,const FTransform& T)
{auto R=D;R.WorldPosition=T.TransformPosition(D.LocalPosition);R.WorldOutwardDirection=T.TransformVectorNoScale(D.LocalOutwardDirection);return R;}
FString Pose(const FTransform& T)
{const auto P=T.GetLocation();return FString::Printf(TEXT("%.3f,%.3f,%.3f,%d"),P.X,P.Y,P.Z,FMath::RoundToInt(FRotator::NormalizeAxis(T.Rotator().Yaw)));}
FString WorldDigest(AActor* Owner)
{return FRogueliteLoopCommitQueries::GeometryDigest(Owner,FRogueliteSeedBatch(),FRogueliteCommittedLoopBatch());}
// V3: only current-floor, already legal template groups enter this draw.
// A template is weighted once, independently of how many poses it can produce.
TArray<int32> F1WeightedOrder(const TArray<CatalogEntry>& Catalog,const TArray<int32>& Eligible,int32 Node,const FRogueliteSeedLayoutSettings& Source)
{
    TArray<int32> Pool,Order;
    for(int32 I:Eligible)if(Catalog.IsValidIndex(I)&&Catalog[I].Option.Weight>0
        &&Source.Templates.ContainsByPredicate([&](const auto& T){return T.Id==Catalog[I].Option.Id&&T.TemplateClass==Catalog[I].Option.TemplateClass;}))Pool.AddUnique(I);
    Pool.Sort([&](int32 A,int32 B){return Catalog[A].Option.Id.ToString()<Catalog[B].Option.Id.ToString();});
    FRandomStream R(G::DeriveSeed(Source.Seed,ERogueliteGraphSeedPurpose::TemplateSelection,Source.AlgorithmVersion,Source.ConfigVersion,Node));
    while(!Pool.IsEmpty())
    {
        int32 Sum=0;for(int32 I:Pool)Sum+=Catalog[I].Option.Weight;
        int32 Pick=R.RandRange(1,Sum),Chosen=0;
        for(int32 I=0;I<Pool.Num();++I)if((Pick-=Catalog[Pool[I]].Option.Weight)<=0){Chosen=I;break;}
        Order.Add(Pool[Chosen]);Pool.RemoveAt(Chosen);
    }
    return Order;
}
void F1OrderTrace(const TCHAR* Stage,int32 Node,int32 Type,int32 Degree,int32 Attempt,const TArray<int32>& Order,const TArray<CatalogEntry>& Catalog)
{
    FString Names;for(int32 I:Order)Names+=FString::Printf(TEXT("%s:%d;"),*Catalog[I].Option.Id.ToString(),Catalog[I].Option.Weight);
    UE_LOG(LogTemp,Display,TEXT("F1_ORDER Stage=%s Node=%d Type=%d Degree=%d Attempt=%d Order=%s First=%s"),Stage,Node,Type,Degree,Attempt,*Names,Order.IsEmpty()?TEXT("None"):*Catalog[Order[0]].Option.Id.ToString());
}
struct Spec
{
    FRogueliteDoorSocket Start,End;FBox Bounds;
};
Spec Corridor(const FRogueliteSeedCorridorProfile& P,float Length)
{
    // Reuse C1's actual local endpoint convention, parameterized with the validated profile.
    Spec R;FRogueliteCorridorSocket A,B;const auto* C=GetDefault<ARogueliteParametricCorridor>();
    if(!C->TryGetCorridorSocket(ERogueliteCorridorEndpoint::Start,A)||!C->TryGetCorridorSocket(ERogueliteCorridorEndpoint::End,B))return R;
    R.Start=A.DoorSocket;R.End=B.DoorSocket;
    for(auto* D:{&R.Start,&R.End}){D->LocalPosition=D->LocalOutwardDirection*(Length*.5);D->DoorWidth=P.DoorWidth;D->DoorHeight=P.DoorHeight;D->WallThickness=P.WallThickness;}
    R.Bounds=FBox(FVector(-Length*.5,-P.Width*.5-P.WallThickness,-P.FloorThickness),FVector(Length*.5,P.Width*.5+P.WallThickness,P.Height+P.WallThickness));return R;
}
TArray<int32> TreePath(const FRogueliteRoomGraph& Gph,int32 A,int32 B)
{
    TArray<int32> Parent;Parent.Init(-1,Gph.Nodes.Num());Parent[A]=A;TArray<int32> Queue{A};
    for(int32 H=0;H<Queue.Num()&&Parent[B]<0;++H)for(int32 N:Gph.GetNeighborIds(Queue[H]))if(Parent[N]<0){Parent[N]=Queue[H];Queue.Add(N);}
    TArray<int32> Path;if(Parent[B]<0)return Path;for(int32 N=B;N!=A;N=Parent[N])Path.Insert(N,0);Path.Insert(A,0);return Path;
}
FRogueliteSeedPlannedRoom MakeRoom(const Inputs& I,int32 Node,const CatalogEntry& T,const FTransform& Pose,const FRogueliteSeedLayoutSettings& Source)
{
    FRogueliteSeedPlannedRoom R;R.Record.NodeId=Node;R.Record.RoomType=I.Types[Node];R.Record.TemplateId=T.Option.Id;R.Record.TemplateClass=T.Class;
    R.Record.EncounterProfile=I.Encounters[Node];R.Record.EventDefinitionId=I.EventDefinitions[Node];R.Record.PlacedTransform=Pose;R.Record.Dimensions=T.Descriptor.Dimensions;
    R.Record.WallThickness=T.Descriptor.WallThickness;R.Record.FloorThickness=T.Descriptor.FloorThickness;R.Record.MemberSignature=T.MemberSignature;
    if(!RogueliteGenerationGeometry::IsLegacyDefaults(Source))
    {R.Record.GenerationGeometry=Source.GenerationGeometry;R.Record.EffectiveDoors=RogueliteGenerationGeometry::EffectiveDoors(T.Class->GetDefaultObject<ARogueliteRoomTemplate>(),Source);}
    R.Record.LogicalDegree=I.Graph.Nodes[Node].Degree;R.Record.TemplateCapacity=T.Capacity;R.Sockets=T.Sockets;R.Occupancy=RoomBox(T.Descriptor).TransformBy(Pose);return R;
}
struct Solver
{
    AActor* Owner;const Inputs& Input;const FRogueliteSeedLayoutSettings& Source;const FRogueliteLoopAwareSettings& S;
    FRogueliteLoopAwareEvidence& E;FTransform Start;FRandomStream Random;
    bool Exhausted=false;FString Why;
    bool Charge()
    {if(E.SpatialChecks>=S.MaxSpatialChecks){Exhausted=true;Why=TEXT("SpatialCheckBudgetExhausted");return false;}++E.SpatialChecks;return true;}
    bool Free(const FBox& B,const FRogueliteSeedSpatialPlan& P,const FBox& Reserved)
    {
        if(!Charge())return false;
        for(const auto& R:P.Rooms)if(Intersects(B,R.Occupancy)){Why=TEXT("PlannedRoomOverlap");return false;}
        for(const auto& R:P.Edges)if(Intersects(B,R.Occupancy)){Why=TEXT("PlannedCorridorOverlap");return false;}
        if(Intersects(B,Reserved)){Why=TEXT("ReservedLoopEnvelopeWouldBeBlocked");return false;}return true;
    }
    bool WorldFree(const FBox& B)
    {
        if(!Charge())return false;auto* W=Owner->GetWorld();
        for(TActorIterator<ARogueliteParametricRoom> It(W);It;++It)if(!It->IsActorBeingDestroyed())
        {const auto* R=*It;FRogueliteTemplateDescriptor D;D.Dimensions=FVector(R->RoomLength,R->RoomWidth,R->RoomHeight);D.WallThickness=R->WallThickness;D.FloorThickness=R->FloorThickness;if(Intersects(B,RoomBox(D).TransformBy(R->GetActorTransform()))){Why=TEXT("ExternalParameterRoomOccupancy");return false;}}
        for(TActorIterator<ARogueliteParametricCorridor> It(W);It;++It)if(!It->IsActorBeingDestroyed()&&Intersects(B,It->GetWorldOccupancyBounds())){Why=TEXT("ExternalCorridorOccupancy");return false;}
        FCollisionQueryParams Q;Q.AddIgnoredActor(Owner);if(W->OverlapBlockingTestByChannel(B.GetCenter(),FQuat::Identity,ECC_Pawn,FCollisionShape::MakeBox(B.GetExtent()-FVector(.1)),Q)){Why=TEXT("ExternalPawnBlockingGeometry");return false;}return true;
    }
    bool Geometry(const FRogueliteSeedPlannedRoom& A,ERogueliteRoomSide SA,const FRogueliteSeedPlannedRoom& B,ERogueliteRoomSide SB,
        const FRogueliteSeedCorridorProfile& Profile,const FRogueliteLoopPlanningSettings& Limits,FRogueliteSeedPlannedEdge& Edge)
    {
        if(!Charge())return false;const auto* DA=A.Sockets.FindByPredicate([&](const auto& D){return D.Side==SA;});const auto* DB=B.Sockets.FindByPredicate([&](const auto& D){return D.Side==SB;});if(!DA||!DB)return false;
        const auto C=Corridor(Profile,1000);float Length=0;
        if(!FRogueliteLoopPlanner::FixedSocketGeometry(Placed(*DA,A.Record.PlacedTransform),Placed(*DB,B.Record.PlacedTransform),C.Start,C.End,B.Record.PlacedTransform,C.Bounds,Limits,Edge.Transform,Length,Edge.Occupancy,Why))return false;
        Edge.Profile=Profile;Edge.Record.ParentNode=A.Record.NodeId;Edge.Record.ChildNode=B.Record.NodeId;Edge.Record.ParentSocket=SA;Edge.Record.ChildSocket=SB;Edge.Record.CorridorLength=Length;Edge.Record.CorridorProfile=Profile.Id;return true;
    }
    bool Finish(FRogueliteSeedSpatialPlan& P,FRogueliteLoopIntent* Intent)
    {
        const auto* Root=P.Rooms.FindByPredicate([&](const auto& R){return R.Record.NodeId==Input.Graph.StartNodeId;});if(!Root)return false;
        const FQuat Rot=Start.GetRotation()*Root->Record.PlacedTransform.GetRotation().Inverse();const FVector Offset=Start.GetLocation()-Rot.RotateVector(Root->Record.PlacedTransform.GetLocation());const FTransform T(Rot,Offset);
        // Check the complete data plan against the real world before formal Static registration.
        for(const auto& R:P.Rooms)if(!WorldFree(R.Occupancy.TransformBy(T)))return false;
        for(const auto& C:P.Edges)if(!WorldFree(C.Occupancy.TransformBy(T)))return false;
        if(Intent&&!WorldFree(Intent->ReservedOccupancy.TransformBy(T)))return false;
        for(auto& R:P.Rooms){const auto Old=R.Record.PlacedTransform;R.Record.PlacedTransform=FTransform(Rot*Old.GetRotation(),Rot.RotateVector(Old.GetLocation())+Offset);R.Occupancy=R.Occupancy.TransformBy(T);}
        for(auto& C:P.Edges){const auto Old=C.Transform;C.Transform=FTransform(Rot*Old.GetRotation(),Rot.RotateVector(Old.GetLocation())+Offset);C.Occupancy=C.Occupancy.TransformBy(T);}
        if(Intent){const auto Old=Intent->CorridorTransform;Intent->CorridorTransform=FTransform(Rot*Old.GetRotation(),Rot.RotateVector(Old.GetLocation())+Offset);Intent->ReservedOccupancy=Intent->ReservedOccupancy.TransformBy(T);Intent->bSpatiallyFeasible=true;}
        P.Rooms.Sort([](const auto& A,const auto& B){return A.Record.NodeId<B.Record.NodeId;});P.Edges.Sort([](const auto& A,const auto& B){return A.Record.EdgeId<B.Record.EdgeId;});return true;
    }
    bool Branches(FRogueliteSeedSpatialPlan& P,FRogueliteLoopIntent* Intent)
    {
        if(Exhausted)return false;if(P.Rooms.Num()==Input.Graph.Nodes.Num())return Finish(P,Intent);
        const FRogueliteRoomGraphEdge* Next=nullptr;int32 Parent=-1,Child=-1;
        for(const auto& Edge:Input.Graph.Edges)
        {
            const bool A=P.Rooms.ContainsByPredicate([&](const auto& R){return R.Record.NodeId==Edge.NodeA;}),B=P.Rooms.ContainsByPredicate([&](const auto& R){return R.Record.NodeId==Edge.NodeB;});
            if(A!=B){Next=&Edge;Parent=A?Edge.NodeA:Edge.NodeB;Child=A?Edge.NodeB:Edge.NodeA;break;}
        }
        if(!Next){Why=TEXT("TreeExpansionReferenceFailure");return false;}
        const auto ParentRoom=*P.Rooms.FindByPredicate([&](const auto& R){return R.Record.NodeId==Parent;});
        auto Ports=ParentRoom.Sockets;Ports.RemoveAll([&](const auto& D){return ParentRoom.UsedSockets.Contains(D.Side)||(Intent&&((Parent==Intent->NodeA&&D.Side==Intent->SocketA)||(Parent==Intent->NodeB&&D.Side==Intent->SocketB)));});
        struct Choice{int32 Template;FRogueliteDoorSocket A,B;FRogueliteSeedCorridorProfile Profile;float Length;};TArray<Choice> Choices;
        for(const auto& A:Ports)for(int32 TI:TemplateOrder(Input.Catalog,Input.Types[Child],Input.Graph.Nodes[Child].Degree,Child,Source,Input.Graph))for(const auto& B:Input.Catalog[TI].Sockets)for(const auto& Profile:Input.Profiles)for(float L:Profile.LengthCandidates)
            if(L>=S.MinTreeCorridorLength&&L<=S.MaxTreeCorridorLength&&FMath::Abs(A.DoorWidth-B.DoorWidth)<.1&&FMath::Abs(A.DoorWidth-Profile.DoorWidth)<.1&&FMath::Abs(A.DoorHeight-B.DoorHeight)<.1&&FMath::Abs(A.DoorHeight-Profile.DoorHeight)<.1)Choices.Add({TI,A,B,Profile,L});
        // This stream belongs to spatial search only, never to topology/type/content.
        if(S.SpatialPlanningVersion==3)
        {
            TArray<int32> Eligible;for(const auto& C:Choices)Eligible.AddUnique(C.Template);
            const auto Order=F1WeightedOrder(Input.Catalog,Eligible,Child,Source);
            F1OrderTrace(TEXT("Branch"),Child,uint8(Input.Types[Child]),Input.Graph.Nodes[Child].Degree,E.LayoutAttempts,Order,Input.Catalog);
            TArray<Choice> Grouped;
            for(int32 T:Order)
            {
                auto Group=Choices.FilterByPredicate([&](const auto& C){return C.Template==T;});
                for(int32 I=Group.Num()-1;I>0;--I)Group.Swap(I,Random.RandRange(0,I));
                Grouped.Append(Group);
            }
            Choices=MoveTemp(Grouped);
        }
        else for(int32 I=Choices.Num()-1;I>0;--I)Choices.Swap(I,Random.RandRange(0,I));
        const auto BeforeRooms=P.Rooms;const auto BeforeEdges=P.Edges;
        for(int32 I=0;I<FMath::Min(Choices.Num(),Source.MaxCandidatesPerEdge);++I)
        {
            if(E.BacktrackStates>=S.MaxBacktrackStates){Exhausted=true;Why=TEXT("BacktrackStateBudgetExhausted");return false;}++E.BacktrackStates;
            const auto& C=Choices[I];const auto Desc=Corridor(C.Profile,C.Length);FTransform CT,BT;
            if(S.SpatialPlanningVersion==3)UE_LOG(LogTemp,Display,TEXT("F1_TRY Stage=Branch Node=%d Attempt=%d Candidate=%d Template=%s"),Child,E.LayoutAttempts,I,*Input.Catalog[C.Template].Option.Id.ToString());
            if(!ARogueliteRoomAssembly::ComputeCorridorSocketTransforms(Placed(C.A,ParentRoom.Record.PlacedTransform),C.B,Desc.Start,Desc.End,.1f,CT,BT,Why)){if(S.SpatialPlanningVersion==3)UE_LOG(LogTemp,Display,TEXT("F1_REJECT Stage=Branch Node=%d Template=%s Reason=%s"),Child,*Input.Catalog[C.Template].Option.Id.ToString(),*Why);continue;}
            auto Room=MakeRoom(Input,Child,Input.Catalog[C.Template],BT,Source);const auto Box=Desc.Bounds.TransformBy(CT);const auto Reserved=Intent?Intent->ReservedOccupancy:FBox(ForceInit);
            if(Intersects(Box,Room.Occupancy)||!Free(Room.Occupancy,P,Reserved)||!Free(Box,P,Reserved)){if(S.SpatialPlanningVersion==3)UE_LOG(LogTemp,Display,TEXT("F1_REJECT Stage=Branch Node=%d Template=%s Reason=%s"),Child,*Input.Catalog[C.Template].Option.Id.ToString(),Intersects(Box,Room.Occupancy)?TEXT("CorridorRoomOverlap"):*Why);continue;}
            auto* PR=P.Rooms.FindByPredicate([&](const auto& R){return R.Record.NodeId==Parent;});PR->UsedSockets.Add(C.A.Side);Room.UsedSockets.Add(C.B.Side);P.Rooms.Add(Room);
            const auto* A=P.Rooms.FindByPredicate([&](const auto& R){return R.Record.NodeId==Next->NodeA;});const auto* B=P.Rooms.FindByPredicate([&](const auto& R){return R.Record.NodeId==Next->NodeB;});
            auto Limits=FRogueliteLoopAwarePlanner::LoopSettings(S);Limits.MinCorridorLength=S.MinTreeCorridorLength;Limits.MaxCorridorLength=S.MaxTreeCorridorLength;FRogueliteSeedPlannedEdge Edge;
            const auto SA=Parent==Next->NodeA?C.A.Side:C.B.Side,SB=Parent==Next->NodeA?C.B.Side:C.A.Side;
            if(Geometry(*A,SA,*B,SB,C.Profile,Limits,Edge))
            {Edge.Record.EdgeId=Next->EdgeId;Edge.Record.AcceptedCandidate=I;P.Edges.Add(Edge);if(Branches(P,Intent))return true;}
            if(S.SpatialPlanningVersion==3)UE_LOG(LogTemp,Display,TEXT("F1_BACKTRACK Stage=Branch Node=%d Template=%s Reason=%s"),Child,*Input.Catalog[C.Template].Option.Id.ToString(),*Why);
            P.Rooms=BeforeRooms;P.Edges=BeforeEdges;if(Exhausted)return false;
        }
        Why=Exhausted?Why:TEXT("TreeBranchSpatialCandidatesExhausted");return false;
    }
    bool CycleWeighted(FRogueliteSeedSpatialPlan& P,FRogueliteLoopIntent& Intent)
    {
        const int32 K=Intent.TreePath.Num();if(K<4)return false;
        int32 Counts[4]={1,1,1,1};for(int32 I=4;I<K;++I)++Counts[Random.RandRange(0,2)];const int32 Sign=Random.RandRange(0,1)?1:-1;
        const FVector Axes[]={FVector(1,0,0),FVector(0,Sign,0),FVector(-1,0,0),FVector(0,-Sign,0)};TArray<FVector> Dirs;
        for(int32 Side=0;Side<4;++Side)for(int32 I=0;I<Counts[Side];++I)Dirs.Add(Axes[Side]);
        struct Oriented{int32 Template;FQuat Rotation;FRogueliteDoorSocket In,Out;};TArray<TArray<Oriented>> OptionsByNode;OptionsByNode.SetNum(K);
        const auto& Profile=Input.Profiles[Random.RandRange(0,Input.Profiles.Num()-1)];
        for(int32 I=0;I<K;++I)
        {
            const int32 Node=Intent.TreePath[I],Degree=Input.Graph.Nodes[Node].Degree+((I==0||I==K-1)?1:0);TArray<Oriented> All;TArray<int32> Eligible;
            for(int32 T:TemplateOrder(Input.Catalog,Input.Types[Node],Degree,Node,Source,Input.Graph))for(int32 Yaw=0;Yaw<4;++Yaw)
            {
                const FQuat Q=FRotator(0,Yaw*90,0).Quaternion();const FVector In=-Dirs[(I+K-1)%K],Out=Dirs[I];const auto& Ports=Input.Catalog[T].Sockets;
                const auto* A=Ports.FindByPredicate([&](const auto& D){return Q.RotateVector(D.LocalOutwardDirection).Equals(In,.0001);});const auto* B=Ports.FindByPredicate([&](const auto& D){return Q.RotateVector(D.LocalOutwardDirection).Equals(Out,.0001);});
                if(A&&B&&A->Side!=B->Side&&FMath::Abs(A->DoorWidth-Profile.DoorWidth)<.1&&FMath::Abs(B->DoorWidth-Profile.DoorWidth)<.1&&FMath::Abs(A->DoorHeight-Profile.DoorHeight)<.1&&FMath::Abs(B->DoorHeight-Profile.DoorHeight)<.1){All.Add({T,Q,*A,*B});Eligible.AddUnique(T);}
            }
            const auto Order=F1WeightedOrder(Input.Catalog,Eligible,Node,Source);F1OrderTrace(TEXT("Cycle"),Node,uint8(Input.Types[Node]),Degree,E.LayoutAttempts,Order,Input.Catalog);
            if(Order.IsEmpty()){Why=TEXT("AuthoredPortDirectionOrTemplateCapacityUnsatisfied");return false;}
            for(int32 T:Order){auto Group=All.FilterByPredicate([&](const auto& O){return O.Template==T;});for(int32 J=Group.Num()-1;J>0;--J)Group.Swap(J,Random.RandRange(0,J));OptionsByNode[I].Append(Group);}
        }
        // Existing closure equations and bounds are unchanged. Only template/pose
        // alternatives are now visited in weighted-group order with bounded rollback.
        TArray<float> Sampled;for(int32 I=0;I<K;++I)Sampled.Add(Profile.LengthCandidates[Random.RandRange(0,Profile.LengthCandidates.Num()-1)]);
        TArray<Oriented> Selected;Selected.SetNum(K);
        const auto Close=[&](FRogueliteLoopIntent& Trial)
        {
            P.Rooms.Reset();P.Edges.Reset();auto Lengths=Sampled;FVector Closure=FVector::ZeroVector;int32 SolveX=-1,SolveY=K-1;
            for(int32 I=0;I<K;++I){Closure+=Selected[I].Rotation.RotateVector(Selected[I].Out.LocalPosition-Selected[I].In.LocalPosition)+Dirs[I]*Lengths[I];if(Dirs[I].X<-.5)SolveX=I;}
            if(SolveX<0)return false;Lengths[SolveX]+=Closure.X;Lengths[SolveY]+=Closure.Y*Sign;
            for(int32 I=0;I<K;++I){const float Min=I==K-1?S.MinLoopCorridorLength:S.MinTreeCorridorLength,Max=I==K-1?S.MaxLoopCorridorLength:S.MaxTreeCorridorLength;if(!FMath::IsFinite(Lengths[I])||Lengths[I]<Min||Lengths[I]>Max||Lengths[I]<=2*Profile.WallThickness){Why=TEXT("JointClosureLengthOutsideConfiguredBounds");return false;}}
            FVector Position=FVector::ZeroVector;
            for(int32 I=0;I<K;++I){auto R=MakeRoom(Input,Trial.TreePath[I],Input.Catalog[Selected[I].Template],FTransform(Selected[I].Rotation,Position),Source);if(!Free(R.Occupancy,P,FBox(ForceInit)))return false;if(I>0)R.UsedSockets.Add(Selected[I].In.Side);if(I<K-1)R.UsedSockets.Add(Selected[I].Out.Side);P.Rooms.Add(R);if(I<K-1)Position+=Selected[I].Rotation.RotateVector(Selected[I].Out.LocalPosition)+Dirs[I]*Lengths[I]-Selected[I+1].Rotation.RotateVector(Selected[I+1].In.LocalPosition);}
            auto Limits=FRogueliteLoopAwarePlanner::LoopSettings(S);Limits.MinCorridorLength=S.MinTreeCorridorLength;Limits.MaxCorridorLength=S.MaxTreeCorridorLength;
            for(int32 I=0;I<K-1;++I){const auto* GE=Input.Graph.Edges.FindByPredicate([&](const auto& V){return (V.NodeA==Trial.TreePath[I]&&V.NodeB==Trial.TreePath[I+1])||(V.NodeB==Trial.TreePath[I]&&V.NodeA==Trial.TreePath[I+1]);});if(!GE)return false;const bool Forward=GE->NodeA==Trial.TreePath[I];FRogueliteSeedPlannedEdge Edge;if(!Geometry(P.Rooms[Forward?I:I+1],Forward?Selected[I].Out.Side:Selected[I+1].In.Side,P.Rooms[Forward?I+1:I],Forward?Selected[I+1].In.Side:Selected[I].Out.Side,Profile,Limits,Edge)||!Free(Edge.Occupancy,P,FBox(ForceInit)))return false;Edge.Record.EdgeId=GE->EdgeId;Edge.Record.AcceptedCandidate=E.LayoutAttempts;P.Edges.Add(Edge);}
            FRogueliteSeedPlannedEdge CloseEdge;Trial.SocketA=Selected[0].In.Side;Trial.SocketB=Selected.Last().Out.Side;
            if(!Geometry(P.Rooms[0],Trial.SocketA,P.Rooms.Last(),Trial.SocketB,Profile,FRogueliteLoopAwarePlanner::LoopSettings(S),CloseEdge)||!Free(CloseEdge.Occupancy,P,FBox(ForceInit)))return false;
            Trial.CorridorProfile=Profile.Id;Trial.CorridorLength=CloseEdge.Record.CorridorLength;Trial.CorridorTransform=CloseEdge.Transform;Trial.ReservedOccupancy=CloseEdge.Occupancy;return Branches(P,&Trial);
        };
        TFunction<bool(int32)> Visit;
        Visit=[&](int32 I)
        {
            if(Exhausted)return false;
            if(I==K){auto Trial=Intent;if(Close(Trial)){Intent=Trial;return true;}UE_LOG(LogTemp,Display,TEXT("F1_REJECT Stage=CycleLayout Attempt=%d Reason=%s"),E.LayoutAttempts,*Why);return false;}
            for(int32 J=0;J<FMath::Min(OptionsByNode[I].Num(),Source.MaxCandidatesPerEdge);++J)
            {
                if(E.BacktrackStates>=S.MaxBacktrackStates){Exhausted=true;Why=TEXT("BacktrackStateBudgetExhausted");return false;}++E.BacktrackStates;
                Selected[I]=OptionsByNode[I][J];UE_LOG(LogTemp,Display,TEXT("F1_TRY Stage=Cycle Node=%d Attempt=%d Candidate=%d Template=%s"),Intent.TreePath[I],E.LayoutAttempts,J,*Input.Catalog[Selected[I].Template].Option.Id.ToString());
                if(Visit(I+1))return true;if(Exhausted)return false;
            }
            return false;
        };
        return Visit(0);
    }
    bool Cycle(FRogueliteSeedSpatialPlan& P,FRogueliteLoopIntent& Intent)
    {
        if(S.SpatialPlanningVersion==3)return CycleWeighted(P,Intent);
        const int32 K=Intent.TreePath.Num();if(K<4)return false;
        // An orthogonal cycle skeleton is one bounded construction strategy, not a fixed map.
        // Extra path nodes subdivide independently chosen sides. Templates, offsets, lengths,
        // orientation, endpoints and all off-cycle branches remain solved from this Seed.
        int32 Counts[4]={1,1,1,1};for(int32 I=4;I<K;++I)++Counts[Random.RandRange(0,2)];const int32 Sign=Random.RandRange(0,1)?1:-1;
        const FVector Axes[]={FVector(1,0,0),FVector(0,Sign,0),FVector(-1,0,0),FVector(0,-Sign,0)};TArray<FVector> Dirs;
        for(int32 Side=0;Side<4;++Side)for(int32 I=0;I<Counts[Side];++I)Dirs.Add(Axes[Side]);
        struct Oriented{int32 Template;FQuat Rotation;FRogueliteDoorSocket In,Out;};TArray<Oriented> Selected;
        const auto& Profile=Input.Profiles[Random.RandRange(0,Input.Profiles.Num()-1)];
        for(int32 I=0;I<K;++I)
        {
            const int32 Node=Intent.TreePath[I],Extra=(I==0||I==K-1)?1:0;TArray<Oriented> Options;
            for(int32 T:TemplateOrder(Input.Catalog,Input.Types[Node],Input.Graph.Nodes[Node].Degree+Extra,Node,Source,Input.Graph))for(int32 Yaw=0;Yaw<4;++Yaw)
            {
                const FQuat Q=FRotator(0,Yaw*90,0).Quaternion();const FVector In=-Dirs[(I+K-1)%K],Out=Dirs[I];
                const auto& Ports=Input.Catalog[T].Sockets;const auto* A=Ports.FindByPredicate([&](const auto& D){return Q.RotateVector(D.LocalOutwardDirection).Equals(In,.0001);});const auto* B=Ports.FindByPredicate([&](const auto& D){return Q.RotateVector(D.LocalOutwardDirection).Equals(Out,.0001);});
                if(A&&B&&A->Side!=B->Side&&FMath::Abs(A->DoorWidth-Profile.DoorWidth)<.1&&FMath::Abs(B->DoorWidth-Profile.DoorWidth)<.1&&FMath::Abs(A->DoorHeight-Profile.DoorHeight)<.1&&FMath::Abs(B->DoorHeight-Profile.DoorHeight)<.1)Options.Add({T,Q,*A,*B});
            }
            if(Options.IsEmpty()){Why=TEXT("AuthoredPortDirectionOrTemplateCapacityUnsatisfied");return false;}Selected.Add(Options[Random.RandRange(0,Options.Num()-1)]);
        }
        TArray<float> Lengths;FVector Closure=FVector::ZeroVector;int32 SolveX=-1,SolveY=K-1;
        for(int32 I=0;I<K;++I)
        {
            Lengths.Add(Profile.LengthCandidates[Random.RandRange(0,Profile.LengthCandidates.Num()-1)]);
            Closure+=Selected[I].Rotation.RotateVector(Selected[I].Out.LocalPosition-Selected[I].In.LocalPosition)+Dirs[I]*Lengths[I];
            if(Dirs[I].X<-.5)SolveX=I;
        }
        if(SolveX<0)return false;Lengths[SolveX]+=Closure.X;Lengths[SolveY]+=Closure.Y*Sign;
        for(int32 I=0;I<K;++I)
        {const float Min=I==K-1?S.MinLoopCorridorLength:S.MinTreeCorridorLength,Max=I==K-1?S.MaxLoopCorridorLength:S.MaxTreeCorridorLength;if(!FMath::IsFinite(Lengths[I])||Lengths[I]<Min||Lengths[I]>Max||Lengths[I]<=2*Profile.WallThickness){Why=TEXT("JointClosureLengthOutsideConfiguredBounds");return false;}}
        FVector Position=FVector::ZeroVector;
        for(int32 I=0;I<K;++I)
        {
            auto R=MakeRoom(Input,Intent.TreePath[I],Input.Catalog[Selected[I].Template],FTransform(Selected[I].Rotation,Position),Source);
            if(!Free(R.Occupancy,P,FBox(ForceInit)))return false;
            if(I>0)R.UsedSockets.Add(Selected[I].In.Side);if(I<K-1)R.UsedSockets.Add(Selected[I].Out.Side);P.Rooms.Add(R);
            if(I<K-1)Position+=Selected[I].Rotation.RotateVector(Selected[I].Out.LocalPosition)+Dirs[I]*Lengths[I]-Selected[I+1].Rotation.RotateVector(Selected[I+1].In.LocalPosition);
        }
        auto Limits=FRogueliteLoopAwarePlanner::LoopSettings(S);Limits.MinCorridorLength=S.MinTreeCorridorLength;Limits.MaxCorridorLength=S.MaxTreeCorridorLength;
        for(int32 I=0;I<K-1;++I)
        {
            const auto* GraphEdge=Input.Graph.Edges.FindByPredicate([&](const auto& Edge){return (Edge.NodeA==Intent.TreePath[I]&&Edge.NodeB==Intent.TreePath[I+1])||(Edge.NodeB==Intent.TreePath[I]&&Edge.NodeA==Intent.TreePath[I+1]);});if(!GraphEdge)return false;
            const bool Forward=GraphEdge->NodeA==Intent.TreePath[I];FRogueliteSeedPlannedEdge Edge;
            if(!Geometry(P.Rooms[Forward?I:I+1],Forward?Selected[I].Out.Side:Selected[I+1].In.Side,P.Rooms[Forward?I+1:I],Forward?Selected[I+1].In.Side:Selected[I].Out.Side,Profile,Limits,Edge)||!Free(Edge.Occupancy,P,FBox(ForceInit)))return false;
            Edge.Record.EdgeId=GraphEdge->EdgeId;Edge.Record.AcceptedCandidate=E.LayoutAttempts;P.Edges.Add(Edge);
        }
        FRogueliteSeedPlannedEdge Close;Intent.SocketA=Selected[0].In.Side;Intent.SocketB=Selected.Last().Out.Side;
        if(!Geometry(P.Rooms[0],Intent.SocketA,P.Rooms.Last(),Intent.SocketB,Profile,FRogueliteLoopAwarePlanner::LoopSettings(S),Close)||!Free(Close.Occupancy,P,FBox(ForceInit)))return false;
        Intent.CorridorProfile=Profile.Id;Intent.CorridorLength=Close.Record.CorridorLength;Intent.CorridorTransform=Close.Transform;Intent.ReservedOccupancy=Close.Occupancy;
        return Branches(P,&Intent);
    }
};
}

int32 FRogueliteLoopAwarePlanner::SpatialSeed(int32 Seed,int32 Config,int32 Attempt)
{return G::DeriveSeed(Seed,SpatialPurpose,2,Config,0,Attempt);}
FRogueliteLoopPlanningSettings FRogueliteLoopAwarePlanner::LoopSettings(const FRogueliteLoopAwareSettings& S)
{FRogueliteLoopPlanningSettings R;R.MinLoops=S.MinLoops;R.MaxLoops=S.MaxLoops;R.MinLoopTreeDistance=S.MinLoopTreeDistance;R.MinCorridorLength=S.MinLoopCorridorLength;R.MaxCorridorLength=S.MaxLoopCorridorLength;R.bAllowCornerCandidates=false;return R;}
bool FRogueliteLoopAwarePlanner::ValidateSettings(const FRogueliteLoopAwareSettings& S,FString& Reason)
{
    if(!S.bEnableLoopAwarePlanning){Reason=TEXT("OptInRequired: old B2/B3A APIs/defaults remain available unchanged.");return false;}
    if((S.SpatialPlanningVersion!=2&&S.SpatialPlanningVersion!=3)||S.ConfigVersion<1||S.MinLoops<0||S.MaxLoops<S.MinLoops||S.MaxLoops>1){Reason=TEXT("UnsupportedSpatialVersionOrLoopBudget: spatial2 legacy/spatial3 weighted support exactly zero/one loop, never silently lower MinLoops.");return false;}
    if(S.MaxLayoutAttempts<1||S.MaxLayoutAttempts>512||S.MaxLoopIntentCandidates<1||S.MaxLoopIntentCandidates>256||S.MaxBacktrackStates<1||S.MaxBacktrackStates>100000||S.MaxSpatialChecks<1||S.MaxSpatialChecks>500000){Reason=TEXT("InvalidBoundedSearchBudget");return false;}
    for(float V:{S.MinTreeCorridorLength,S.MaxTreeCorridorLength,S.MinLoopCorridorLength,S.MaxLoopCorridorLength})if(!FMath::IsFinite(V)||V<100||V>100000){Reason=TEXT("InvalidC1LengthLimits");return false;}
    if(S.MaxTreeCorridorLength<S.MinTreeCorridorLength||S.MaxLoopCorridorLength<S.MinLoopCorridorLength){Reason=TEXT("InvertedLengthLimits");return false;}
    auto L=LoopSettings(S);return FRogueliteLoopPlanner::ValidateSettings(L,Reason);
}
static FRogueliteLoopAwareSpatialPlan PlanWithInputs(AActor* Owner,const FRogueliteSeedLayoutSettings& Source,const FRogueliteLoopAwareSettings& S,const FTransform& Start,const FRoguelitePreparedTemplateCatalog* Prepared)
{
    FRogueliteLoopAwareSpatialPlan P;auto& E=P.Evidence;E.Seed=Source.Seed;E.DerivedSpatialSeed=FRogueliteLoopAwarePlanner::SpatialSeed(Source.Seed,S.ConfigVersion);E.SpatialPlanningVersion=S.SpatialPlanningVersion;E.MinLoops=S.MinLoops;E.MaxLoops=S.MaxLoops;
    P.Start=Start;P.Owner=Owner;P.World=IsValid(Owner)?Owner->GetWorld():nullptr;P.AuthoringOwnerTransform=IsValid(Owner)?Owner->GetActorTransform():FTransform::Identity;P.SourceSettingsDigest=G::StableTextDigest(SettingsText(Source,S));
    const auto Fail=[&](const FString& Stage,const FString& Why){E.Stage=Stage;E.Reason=Why;E.bCleanupComplete=true;P.Tree.bValid=false;return MoveTemp(P);};
    if(!(Prepared?(IsValid(Owner)&&Owner->GetWorld()&&Owner->GetWorld()->IsGameWorld()):Editor(Owner))||!Rigid(Start)||Source.AlgorithmVersion!=2||!FRogueliteLoopAwarePlanner::ValidateSettings(S,E.Reason))return Fail(TEXT("InvalidInputs"),E.Reason.IsEmpty()?TEXT("Editor, rigid Start and normal V2 graph required."):E.Reason);
    P.SourceGeometryDigest=WorldDigest(Owner);P.Tree.Metadata.Seed=Source.Seed;P.Tree.Metadata.BatchId=FGuid::NewGuid();Inputs I;
    if(!BuildInputs(Owner,Source,P.Tree.Metadata,I,E.Reason,Prepared)){E.GraphDigest=P.Tree.Metadata.GraphDigest;return Fail(P.Tree.Metadata.GraphDigest.IsEmpty()?TEXT("GraphRejected"):TEXT("CatalogOrRoomTypeRejected"),E.Reason);}
    P.SourceCatalogDigest=P.Tree.Metadata.ConfigurationDigest;P.Tree.Graph=I.Graph;P.Tree.bGraphGenerated=true;E.GraphDigest=I.Graph.StableDigest;E.RoomCount=I.Graph.Nodes.Num();E.TreeEdgeCount=I.Graph.Edges.Num();
    if(E.RoomCount>32)return Fail(TEXT("InvalidInputs"),TEXT("Bounded planner currently accepts at most32 nodes; no topology changed."));
    FString Types;TArray<int32> Caps;
    for(const auto& N:I.Graph.Nodes){int32 Cap=0;for(const auto& T:I.Catalog)if(T.Option.AllowedTypes.Contains(I.Types[N.NodeId])&&IsTemplateRoleAllowed(T.Option.Id,I.Graph,N.NodeId,Source))Cap=FMath::Max(Cap,T.Capacity);Caps.Add(Cap);Types+=FString::Printf(TEXT("|%d:%d:%s"),N.NodeId,uint8(I.Types[N.NodeId]),*I.EventDefinitions[N.NodeId].ToString());}
    E.RoomTypeDigest=G::StableTextDigest(Types);for(const auto& T:I.Catalog)P.Tree.RetainedClasses.Add(TStrongObjectPtr<UClass>(T.Class));
    TArray<FRogueliteLoopIntent> Intents;
    if(S.MaxLoops>0)
    {
        auto Query=FRogueliteLoopPlanner::Topology(I.Graph,Caps,FRogueliteLoopAwarePlanner::LoopSettings(S));
        for(const auto& C:Query.Candidates)if(C.bTopologyValid)
        {FRogueliteLoopIntent Intent;Intent.NodeA=C.NodeA;Intent.NodeB=C.NodeB;Intent.TreeDistance=C.TreeDistance;Intent.TreePath=TreePath(I.Graph,C.NodeA,C.NodeB);Intent.RequiredCapacityA=I.Graph.Nodes[C.NodeA].Degree+1;Intent.RequiredCapacityB=I.Graph.Nodes[C.NodeB].Degree+1;Intent.DerivedSpatialSeed=E.DerivedSpatialSeed;Intent.StableId=FName(*G::StableTextDigest(FString::Printf(TEXT("LoopIntentSpatial2|%s|%d,%d|%d"),*E.GraphDigest,C.NodeA,C.NodeB,S.ConfigVersion)));Intent.Priority=FRogueliteLoopAwarePlanner::SpatialSeed(Source.Seed,S.ConfigVersion,C.NodeA*256+C.NodeB);Intents.Add(Intent);}
        Intents.Sort([](const auto& A,const auto& B){return A.Priority!=B.Priority?A.Priority<B.Priority:A.StableId.ToString()<B.StableId.ToString();});if(Intents.Num()>S.MaxLoopIntentCandidates)Intents.SetNum(S.MaxLoopIntentCandidates);
        E.Intents=Intents;
        if(Intents.IsEmpty()&&S.MinLoops>0)return Fail(TEXT("LoopIntentUnsatisfied"),TEXT("No legal non-Final topology pair within actual template/graph degree capacity. MinLoops unchanged."));
    }
    Solver Solver{Owner,I,Source,S,E,Start,FRandomStream(E.DerivedSpatialSeed)};bool Solved=false;
    const int32 SearchAttempts=S.MinLoops==0&&!Intents.IsEmpty()?S.MaxLayoutAttempts-1:S.MaxLayoutAttempts;
    for(int32 Attempt=0;Attempt<SearchAttempts&&!Solver.Exhausted;++Attempt)
    {
        ++E.LayoutAttempts;Solver.Random.Initialize(FRogueliteLoopAwarePlanner::SpatialSeed(Source.Seed,S.ConfigVersion,Attempt));P.Tree.Rooms.Reset();P.Tree.Edges.Reset();
        if(!Intents.IsEmpty())
        {auto Intent=Intents[Attempt%Intents.Num()];if(Solver.Cycle(P.Tree,Intent)){E.Intents={Intent};E.FeasibleIntentCount=1;Solved=true;break;}}
        else
        {
            const auto Order=TemplateOrder(I.Catalog,I.Types[0],I.Graph.Nodes[0].Degree,0,Source,I.Graph);if(!Order.IsEmpty())
            {const int32 T=Order[Attempt%Order.Num()];if(S.SpatialPlanningVersion==3){F1OrderTrace(TEXT("Root"),0,uint8(I.Types[0]),I.Graph.Nodes[0].Degree,E.LayoutAttempts,Order,I.Catalog);UE_LOG(LogTemp,Display,TEXT("F1_TRY Stage=Root Node=0 Attempt=%d Candidate=%d Template=%s"),E.LayoutAttempts,Attempt%Order.Num(),*I.Catalog[T].Option.Id.ToString());}P.Tree.Rooms.Add(MakeRoom(I,0,I.Catalog[T],FTransform::Identity,Source));if(Solver.Branches(P.Tree,nullptr)){Solved=true;break;}}
        }
        E.Attempts.Add(FString::Printf(TEXT("Attempt=%d Intent=%s States=%d Checks=%d Failure=%s"),Attempt,!Intents.IsEmpty()?*Intents[Attempt%Intents.Num()].StableId.ToString():TEXT("None"),E.BacktrackStates,E.SpatialChecks,*Solver.Why));
    }
    if(!Solved&&S.MinLoops==0&&!Intents.IsEmpty()&&!Solver.Exhausted)
    {
        ++E.LayoutAttempts;E.bUsedOptionalTreeFallback=true;P.Tree.Rooms.Reset();P.Tree.Edges.Reset();const auto Order=TemplateOrder(I.Catalog,I.Types[0],I.Graph.Nodes[0].Degree,0,Source,I.Graph);
        if(!Order.IsEmpty()){if(S.SpatialPlanningVersion==3){F1OrderTrace(TEXT("OptionalTreeRoot"),0,uint8(I.Types[0]),I.Graph.Nodes[0].Degree,E.LayoutAttempts,Order,I.Catalog);UE_LOG(LogTemp,Display,TEXT("F1_TRY Stage=OptionalTreeRoot Node=0 Attempt=%d Candidate=0 Template=%s"),E.LayoutAttempts,*I.Catalog[Order[0]].Option.Id.ToString());}P.Tree.Rooms.Add(MakeRoom(I,0,I.Catalog[Order[0]],FTransform::Identity,Source));Solved=Solver.Branches(P.Tree,nullptr);if(Solved){E.Intents.Reset();E.FeasibleIntentCount=0;}}
    }
    if(!Solved)return Fail(Solver.Exhausted?TEXT("SearchBudgetExhausted"):TEXT("TreeOrLoopSpatialUnsatisfied"),Solver.Why+TEXT("; no formal Actors/leases spawned; original V2 topology preserved."));
    E.bTreePlanned=true;P.Tree.bValid=true;P.Tree.Reason=TEXT("Complete joint data plan. Actual C2/Loop commit still pending.");
    FString Text=SettingsText(Source,S)+TEXT("|graph=")+E.GraphDigest+TEXT("|types=")+E.RoomTypeDigest;
    for(const auto& R:P.Tree.Rooms)Text+=FString::Printf(TEXT("|node=%d,%s,%s,%s,%s"),R.Record.NodeId,*R.Record.TemplateId.ToString(),*GetPathNameSafe(R.Record.TemplateClass.Get()),*Pose(R.Record.PlacedTransform),*R.Record.MemberSignature);
    for(const auto& C:P.Tree.Edges)Text+=FString::Printf(TEXT("|tree=%d,%d,%d,%d,%d,%s,%.3f,%s"),C.Record.EdgeId,C.Record.ParentNode,C.Record.ChildNode,uint8(C.Record.ParentSocket),uint8(C.Record.ChildSocket),*C.Profile.Id.ToString(),C.Record.CorridorLength,*Pose(C.Transform));
    for(const auto& L:E.Intents)if(L.bSpatiallyFeasible)Text+=FString::Printf(TEXT("|intent=%s,%d,%d,%d,%d,%s,%.3f,%s"),*L.StableId.ToString(),L.NodeA,L.NodeB,uint8(L.SocketA),uint8(L.SocketB),*L.CorridorProfile.ToString(),L.CorridorLength,*Pose(L.CorridorTransform));
    E.StablePlanText=Text;E.StablePlanDigest=G::StableTextDigest(Text);E.Stage=E.FeasibleIntentCount?TEXT("SpatiallyFeasibleLoop"):TEXT("TreeSpatialPlanValid");E.Reason=TEXT("Joint closure constraints and reserved envelope solved before formal Static registration; not a committed connection.");return P;
}
FRogueliteLoopAwareSpatialPlan FRogueliteLoopAwarePlanner::Plan(AActor* Owner,const FRogueliteSeedLayoutSettings& Source,const FRogueliteLoopAwareSettings& S,const FTransform& Start)
{return PlanWithInputs(Owner,Source,S,Start,nullptr);}
FRogueliteLoopAwareSpatialPlan FRogueliteLoopAwarePlanner::PlanRuntime(AActor* Owner,const FRogueliteSeedLayoutSettings& Source,const FRogueliteLoopAwareSettings& S,const FTransform& Start,const FRoguelitePreparedTemplateCatalog& Catalog)
{return PlanWithInputs(Owner,Source,S,Start,&Catalog);}
bool FRogueliteLoopAwarePlanner::IsRuntimeCurrent(AActor* Owner,const FRogueliteSeedLayoutSettings& Source,const FRogueliteLoopAwareSettings& S,const FRogueliteLoopAwareSpatialPlan& P,const FRoguelitePreparedTemplateCatalog& Catalog,FString& Reason)
{
    if(!IsValid(Owner)||!Owner->GetWorld()||!Owner->GetWorld()->IsGameWorld()||!P.Tree.bValid||!P.Unconsumed.IsValid()||!*P.Unconsumed
        ||P.Owner.Get()!=Owner||P.World.Get()!=Owner->GetWorld()||!Owner->GetActorTransform().Equals(P.AuthoringOwnerTransform,.0001)
        ||P.SourceSettingsDigest!=G::StableTextDigest(SettingsText(Source,S))||P.SourceGeometryDigest!=WorldDigest(Owner)||!Catalog.IsCurrent(Source,Reason))
    {Reason=TEXT("Stale runtime plan/World/configuration/catalog/geometry; no formal actor spawned.");return false;}
    FRogueliteSeedBatch Metadata;Inputs I;
    if(!BuildInputs(Owner,Source,Metadata,I,Reason,&Catalog)||Metadata.ConfigurationDigest!=P.SourceCatalogDigest)
    {Reason=TEXT("Prepared runtime descriptors or source inputs changed.");return false;}
    return true;
}
bool FRogueliteLoopAwarePlanner::IsCurrent(AActor* Owner,const FRogueliteSeedLayoutSettings& Source,const FRogueliteLoopAwareSettings& S,const FRogueliteLoopAwareSpatialPlan& P,FString& Reason)
{
    if(!Editor(Owner)||!P.Unconsumed.IsValid()||!*P.Unconsumed||P.Owner.Get()!=Owner||P.World.Get()!=Owner->GetWorld()||!P.Tree.bValid||!Owner->GetActorTransform().Equals(P.AuthoringOwnerTransform,.0001)||P.SourceSettingsDigest!=G::StableTextDigest(SettingsText(Source,S))||P.SourceGeometryDigest!=WorldDigest(Owner))
    {Reason=TEXT("StalePlanOrWorld: source configuration, actual geometry/obstacles, owner or World changed. No spawn performed.");return false;}
    FRogueliteSeedBatch Metadata;Metadata.BatchId=FGuid::NewGuid();RogueliteSeedAssemblyInternal::Inputs Inputs;
    if(!RogueliteSeedAssemblyInternal::BuildInputs(Owner,Source,Metadata,Inputs,Reason)||Metadata.ConfigurationDigest!=P.SourceCatalogDigest)
    {Reason=TEXT("StaleCatalogOrTemplate: authored descriptors/content/type assignment changed. ")+Reason;return false;}
    Reason=TEXT("Current pre-registration plan; actual C2 checks still mandatory.");return true;
}

#include "Tests/RogueliteWeightedTemplateSelectionTests.inl"

#include "Tests/RogueliteRoomRoleWeightsTests.inl"
