#if WITH_DEV_AUTOMATION_TESTS
#include "RogueliteRoomGraph.h"
#include "RogueliteRoomTypeAssignment.h"
#include "RogueliteBranchingTestSettings.h"
#include "Misc/AutomationTest.h"
#include "Math/RandomStream.h"
#include "UObject/UObjectIterator.h"

namespace
{
using G=FRogueliteRoomGraphGenerator;
constexpr auto Flags=EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter;
FString ResultText(const FRogueliteGraphGenerationResult& R)
{
    FString T=FString::Printf(TEXT("%d,%d,%d,%d,%d,%s,%s|%s"),R.bSucceeded,R.bUsedFallback,R.AttemptsUsed,R.AcceptedAttemptIndex,int32(R.Error),*R.Reason,*R.FallbackReason,*G::DescribeGraph(R.Graph));
    for(const auto& A:R.Attempts)T+=FString::Printf(TEXT("|a=%d,%d,%d,%d,%d,%d,%s"),A.AttemptIndex,A.TopologySeed,A.MainPathNodes,A.BranchCount,A.bAccepted,int32(A.Error),*A.Reason);return T;
}
bool IndependentTree(FAutomationTestBase& Test,const FRogueliteRoomGraph& Graph,const FRogueliteGraphGenerationSettings& S)
{
    TArray<int32> Degree,Children;Degree.Init(0,Graph.Nodes.Num());Children.Init(0,Graph.Nodes.Num());TSet<uint64> Edges;
    bool Passed=Test.TestEqual(TEXT("Independent N-1 edges"),Graph.Edges.Num(),Graph.Nodes.Num()-1);
    for(const auto& E:Graph.Edges)
    {
        if(!Graph.FindNode(E.NodeA)||!Graph.FindNode(E.NodeB)){Test.AddError(TEXT("Bad endpoint"));return false;}
        const uint64 Key=(uint64(uint32(FMath::Min(E.NodeA,E.NodeB)))<<32)|uint32(FMath::Max(E.NodeA,E.NodeB));
        Passed&=Test.TestTrue(TEXT("No self/duplicate"),E.NodeA!=E.NodeB&&!Edges.Contains(Key));Edges.Add(Key);
        ++Degree[E.NodeA];++Degree[E.NodeB];++Children[E.NodeA];Passed&=Test.TestTrue(TEXT("Parents precede children"),E.NodeA<E.NodeB&&Graph.Nodes[E.NodeB].ParentNodeId==E.NodeA);
    }
    TSet<int32> Seen{Graph.StartNodeId};TArray<int32> Queue{Graph.StartNodeId};
    for(int32 H=0;H<Queue.Num();++H)for(const auto& E:Graph.Edges)
    {const int32 Next=E.NodeA==Queue[H]?E.NodeB:E.NodeB==Queue[H]?E.NodeA:INDEX_NONE;if(Next!=INDEX_NONE&&!Seen.Contains(Next)){Seen.Add(Next);Queue.Add(Next);}}
    Passed&=Test.TestEqual(TEXT("Independent connectivity"),Seen.Num(),Graph.Nodes.Num());Passed&=Test.TestEqual(TEXT("Final is a leaf"),Degree[Graph.FinalNodeId],1);
    int32 Decisions=0,Nested=0;
    for(const auto& N:Graph.Nodes)
    {
        Passed&=Test.TestTrue(TEXT("Degree and rooted child count"),Degree[N.NodeId]==N.Degree&&N.Degree<=S.MaxDegree&&Children[N.NodeId]==N.ChildCount);
        Passed&=Test.TestTrue(TEXT("Decision means at least two children"),N.bDecisionNode==(Children[N.NodeId]>=2));
        Decisions+=Children[N.NodeId]>=2;Nested+=Children[N.NodeId]>=2&&N.Role==ERogueliteGraphNodeRole::Branch;
        Passed&=Test.TestTrue(TEXT("Generation budget"),N.BranchGenerationDepth<=S.MaxBranchGenerationDepth);
    }
    Passed&=Test.TestTrue(TEXT("Decision and nested minima"),Decisions>=S.MinDecisionNodes&&Decisions<=S.MaxDecisionNodes&&Nested>=S.MinNestedDecisionNodes);
    FString Reason;Passed&=Test.TestTrue(TEXT("Production V2 metadata validation"),G::ValidateGraph(Graph,S,Reason));if(!Passed)Test.AddError(Reason);return Passed;
}
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRogueliteV2HundredTest,"Roguelite.BranchingV2.HundredSeeds",Flags)
bool FRogueliteV2HundredTest::RunTest(const FString&)
{
    const auto S=RogueliteBranchingTestSettings().GraphSettings();TSet<FString> Shapes;int32 Fallbacks=0,Rejected=0,MaxLevel=0;
    for(int32 Seed=0;Seed<100;++Seed)
    {
        const auto A=G::Generate(Seed,S),B=G::Generate(Seed,S);if(!TestTrue(TEXT("V2 success"),A.bSucceeded)){AddError(ResultText(A));return false;}
        TestEqual(TEXT("Full graph/order/retries deterministic"),ResultText(A),ResultText(B));if(!IndependentTree(*this,A.Graph,S))return false;
        Shapes.Add(A.Graph.TopologyDigest);Fallbacks+=A.bUsedFallback;for(const auto& Attempt:A.Attempts)Rejected+=!Attempt.bAccepted;
        int32 Level=0;for(const auto& Node:A.Graph.Nodes)Level=FMath::Max(Level,Node.BranchGenerationDepth);MaxLevel=FMath::Max(MaxLevel,Level);
        AddInfo(FString::Printf(TEXT("B4_V2_SEED Seed=%d Nodes=%d Edges=%d GenerationDepth=%d Attempts=%d Fallback=%d Graph=%s"),Seed,A.Graph.Nodes.Num(),A.Graph.Edges.Num(),Level,A.AttemptsUsed,A.bUsedFallback,*A.Graph.StableDigest));
        if(!Seed)AddInfo(TEXT("B4_V2_DEEP_FIXTURE ")+G::DescribeGraph(A.Graph));
    }
    TestTrue(TEXT("Multiple seed shapes"),Shapes.Num()>1);TestTrue(TEXT("Actual secondary generation"),MaxLevel>=2);
    AddInfo(FString::Printf(TEXT("B4_V2_STATS Seeds=100 Success=100 UniqueShapes=%d Fallbacks=%d RejectedCandidates=%d MaxGeneration=%d"),Shapes.Num(),Fallbacks,Rejected,MaxLevel));return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRogueliteV2TypesTest,"Roguelite.BranchingV2.TypeQuotasAndCombinations",Flags)
bool FRogueliteV2TypesTest::RunTest(const FString&)
{
    TestEqual(TEXT("Serialized Final remains 3"),StaticEnum<ERogueliteSeedRoomType>()->GetValueByNameString(TEXT("Final")),int64(3));
    TestEqual(TEXT("Event appended at 4"),StaticEnum<ERogueliteSeedRoomType>()->GetValueByNameString(TEXT("Event")),int64(4));
    bool AFound=false,BFound=false,CFound=false;int32 Tested=0;
    for(int32 Seed=0;Seed<1000;++Seed)
    {
        const auto Settings=RogueliteBranchingTestSettings(Seed);const auto Graph=G::Generate(Seed,Settings.GraphSettings());
        const auto A=FRogueliteRoomTypeAssigner::Assign(Graph.Graph,Settings.TypeAssignment),B=FRogueliteRoomTypeAssigner::Assign(Graph.Graph,Settings.TypeAssignment);
        if(!TestTrue(TEXT("Independent assignment succeeds"),Graph.bSucceeded&&A.bSucceeded)){AddError(A.Reason);return false;}
        TestEqual(TEXT("Stable type/event output digest"),A.StableDigest,B.StableDigest);TestTrue(TEXT("Topology unchanged"),Graph.Graph.StableDigest==G::Generate(Seed,Settings.GraphSettings()).Graph.StableDigest);
        for(const auto& Rule:Settings.TypeAssignment.Rules)TestTrue(TEXT("Every quota retained"),A.Counts[uint8(Rule.Type)]>=Rule.MinCount&&A.Counts[uint8(Rule.Type)]<=Rule.MaxCount);
        for(int32 Node=0;Node<A.Types.Num();++Node)TestTrue(TEXT("EventId only for Event"),A.Types[Node]==ERogueliteSeedRoomType::Event?A.EventDefinitionIds[Node]==FName(TEXT("TreasureCave")):A.EventDefinitionIds[Node].IsNone());
        for(const auto& Parent:Graph.Graph.Nodes)
        {
            TArray<int32> Children,Ids;for(const auto& Edge:Graph.Graph.Edges)if(Edge.NodeA==Parent.NodeId){Children.Add(Edge.NodeB);Ids.Add(Edge.EdgeId);}
            if(A.Types[Parent.NodeId]!=ERogueliteSeedRoomType::Combat)continue;
            for(int32 I=0;I<Children.Num();++I)for(int32 J=I+1;J<Children.Num();++J)
            {
                const auto X=A.Types[Children[I]],Y=A.Types[Children[J]];
                const bool IsA=X==ERogueliteSeedRoomType::Combat&&Y==ERogueliteSeedRoomType::Combat;
                const bool IsB=(X==ERogueliteSeedRoomType::Challenge&&Y==ERogueliteSeedRoomType::SupplyShop)||(Y==ERogueliteSeedRoomType::Challenge&&X==ERogueliteSeedRoomType::SupplyShop);
                const bool IsC=(X==ERogueliteSeedRoomType::Combat&&Y==ERogueliteSeedRoomType::Event)||(Y==ERogueliteSeedRoomType::Combat&&X==ERogueliteSeedRoomType::Event);
                if((IsA&&!AFound)||(IsB&&!BFound)||(IsC&&!CFound))AddInfo(FString::Printf(TEXT("B4_TYPE_COMBO Kind=%s Seed=%d Parent=%d Children=%d,%d Edges=%d,%d Types=%d,%d Role=%d Graph=%s Config=%s Assignment=%s"),IsA?TEXT("A"):IsB?TEXT("B"):TEXT("C"),Seed,Parent.NodeId,Children[I],Children[J],Ids[I],Ids[J],uint8(X),uint8(Y),int32(Parent.Role),*Graph.Graph.StableDigest,*A.SettingsDigest,*A.StableDigest));
                AFound|=IsA;BFound|=IsB;CFound|=IsC;
            }
        }
        ++Tested;if(Seed>=99&&AFound&&BFound&&CFound)break;
    }
    TestTrue(TEXT("Combat to Combat + Combat observed"),AFound);TestTrue(TEXT("Combat to Challenge + Shop observed"),BFound);TestTrue(TEXT("Combat to Combat + Event observed"),CFound);
    AddInfo(FString::Printf(TEXT("B4_TYPES Seeds=%d Combos=%d,%d,%d"),Tested,AFound,BFound,CFound));return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRogueliteV2FailureTest,"Roguelite.BranchingV2.FailureFallbackAndMalformed",Flags)
bool FRogueliteV2FailureTest::RunTest(const FString&)
{
    auto S=RogueliteBranchingTestSettings().GraphSettings();S.MaxAttempts=1;S.RecursiveBranchWeight=0;
    const auto A=G::Generate(19,S),B=G::Generate(19,S);TestTrue(TEXT("Force deterministic fallback without reducing minima"),A.bSucceeded&&A.bUsedFallback&&!A.FallbackReason.IsEmpty());
    TestEqual(TEXT("Failure and fallback repeat"),ResultText(A),ResultText(B));if(A.bSucceeded)IndependentTree(*this,A.Graph,S);AddInfo(TEXT("B4_FORCED_FALLBACK ")+ResultText(A));
    S.bEnableFallback=false;const auto Failed=G::Generate(19,S);TestTrue(TEXT("Finite exhaustion, no partial graph"),!Failed.bSucceeded&&Failed.AttemptsUsed==1&&Failed.Graph.Nodes.IsEmpty());AddInfo(TEXT("B4_FORCED_EXHAUSTION ")+ResultText(Failed));
    S=RogueliteBranchingTestSettings().GraphSettings();S.MinDecisionNodes=S.MaxDecisionNodes=99;TestFalse(TEXT("Impossible decisions rejected"),G::Generate(1,S).bSucceeded);
    S=RogueliteBranchingTestSettings().GraphSettings();const auto Good=G::Generate(0,S).Graph;auto Bad=Good;Bad.Nodes[0].ChildCount=99;FString Reason;TestFalse(TEXT("Forged Decision metadata"),G::ValidateGraph(Bad,S,Reason));
    Bad=Good;Bad.Branches.Last().GenerationDepth=99;TestFalse(TEXT("Forged generation metadata"),G::ValidateGraph(Bad,S,Reason));
    auto Types=RogueliteBranchingTestSettings().TypeAssignment;Types.Rules[4].MinCount=Types.Rules[4].MaxCount=9;auto R=FRogueliteRoomTypeAssigner::Assign(Good,Types);
    TestTrue(TEXT("Impossible quota rejected without output"),!R.bSucceeded&&R.Types.IsEmpty());AddInfo(TEXT("B4_QUOTA_REJECT ")+R.Reason);
    Types=RogueliteBranchingTestSettings().TypeAssignment;Types.Rules[4].bAllowBranch=Types.Rules[4].bAllowMainPath=false;R=FRogueliteRoomTypeAssigner::Assign(Good,Types);TestFalse(TEXT("Impossible Event position minimum rejected"),R.bSucceeded);
    Types=RogueliteBranchingTestSettings().TypeAssignment;Types.EventDefinitionId=NAME_None;R=FRogueliteRoomTypeAssigner::Assign(Good,Types);TestTrue(TEXT("Event definition required, no partial assignment"),!R.bSucceeded&&R.Types.IsEmpty());
    TArray<TArray<ERogueliteSeedRoomType>> Capacity;Capacity.SetNum(Good.Nodes.Num());for(const auto& N:Good.Nodes)if(N.Degree<=2)Capacity[N.NodeId]={ERogueliteSeedRoomType::Combat,ERogueliteSeedRoomType::Challenge,ERogueliteSeedRoomType::SupplyShop,ERogueliteSeedRoomType::Final,ERogueliteSeedRoomType::Event};
    R=FRogueliteRoomTypeAssigner::Assign(Good,RogueliteBranchingTestSettings().TypeAssignment,Capacity);TestTrue(TEXT("Degree3 without compatible template explicitly refused"),!R.bSucceeded&&R.FailedNodeId!=INDEX_NONE);AddInfo(TEXT("B4_CAPACITY_REJECT ")+R.Reason);return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRogueliteV2IsolationTest,"Roguelite.BranchingV2.VersionAndWorldIsolation",Flags)
bool FRogueliteV2IsolationTest::RunTest(const FString&)
{
    FRogueliteGraphGenerationSettings V1;const auto Before=G::Generate(1337,V1);V1.MinNodes=256;V1.MinDecisionNodes=999;V1.MaxBranchGenerationDepth=-1;V1.RecursiveBranchWeight=0;
    TestEqual(TEXT("V2-only settings do not enter V1 results/summary"),ResultText(Before),ResultText(G::Generate(1337,V1)));
    TMap<AActor*,FTransform> Actors;for(TObjectIterator<AActor> I;I;++I)if(!I->HasAnyFlags(RF_ClassDefaultObject))Actors.Add(*I,I->GetActorTransform());
    const auto Settings=RogueliteBranchingTestSettings();const auto Graph=G::Generate(0,Settings.GraphSettings());
    for(int32 Seed=0;Seed<100;++Seed){const auto R=G::Generate(Seed,Settings.GraphSettings());TestTrue(TEXT("V2 pure generate"),R.bSucceeded);FRogueliteRoomTypeAssigner::Assign(R.Graph,Settings.TypeAssignment);}
    int32 Count=0;for(TObjectIterator<AActor> I;I;++I)if(!I->HasAnyFlags(RF_ClassDefaultObject)){++Count;const auto* Old=Actors.Find(*I);TestTrue(TEXT("No Actor creation/transform mutation"),Old&&Old->Equals(I->GetActorTransform(),0));}
    TestEqual(TEXT("Actor count unchanged"),Count,Actors.Num());
    FRandomStream Content(G::DeriveSeed(0,ERogueliteGraphSeedPurpose::RoomContent,2,2));for(int32 I=0;I<10000;++I)Content.GetUnsignedInt();
    TestEqual(TEXT("Content draws do not change V2 topology"),Graph.Graph.StableDigest,G::Generate(0,Settings.GraphSettings()).Graph.StableDigest);return true;
}
#endif
