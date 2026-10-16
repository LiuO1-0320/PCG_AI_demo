#if WITH_DEV_AUTOMATION_TESTS
#include "RogueliteLoopPlanning.h"
#include "RogueliteBranchingTestSettings.h"
#include "Misc/AutomationTest.h"
#include "Math/RandomStream.h"
#include "UObject/UObjectIterator.h"
namespace {constexpr auto Flags=EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter;using G=FRogueliteRoomGraphGenerator;}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRogueliteLoopTopologyTest,"Roguelite.Loops.Topology100Seeds",Flags)
bool FRogueliteLoopTopologyTest::RunTest(const FString&)
{
    FRogueliteLoopPlanningSettings S;int32 Total=0,WithCandidates=0;TSet<FString> Digests;
    TSet<AActor*> Before;for(TObjectIterator<AActor> I;I;++I)Before.Add(*I);
    for(int32 Seed=0;Seed<100;++Seed)
    {
        const auto Settings=RogueliteBranchingTestSettings(Seed);const auto Graph=G::Generate(Seed,Settings.GraphSettings());if(!TestTrue(TEXT("Actual V2 graph generated"),Graph.bSucceeded))return false;
        TArray<int32> Caps;Caps.Init(3,Graph.Graph.Nodes.Num());const auto A=FRogueliteLoopPlanner::Topology(Graph.Graph,Caps,S),B=FRogueliteLoopPlanner::Topology(Graph.Graph,Caps,S);
        TestTrue(TEXT("Original Tree valid"),A.bTreeValid);TestEqual(TEXT("Candidate ordering/summary stable"),A.StableDigest,B.StableDigest);TestEqual(TEXT("Candidate count stable"),A.Candidates.Num(),B.Candidates.Num());
        TSet<FName> Ids;TSet<int32> Pairs;
        for(int32 I=0;I<A.Candidates.Num();++I)
        {
            const auto& C=A.Candidates[I];TestTrue(TEXT("No duplicate pair/ID/self"),C.NodeA<C.NodeB&&!Ids.Contains(C.CandidateId)&&!Pairs.Contains(C.NodeA*256+C.NodeB));Ids.Add(C.CandidateId);Pairs.Add(C.NodeA*256+C.NodeB);TestEqual(TEXT("Order stable"),C.CandidateId,B.Candidates[I].CandidateId);
            TestTrue(TEXT("Topology alone never claims spatial or committed selection"),!C.bSocketCompatible&&!C.bSelected&&C.Feasibility!=ERogueliteLoopFeasibility::StraightPrecheckPassed);
            if(C.bTopologyValid)
            {
                ++Total;TestTrue(TEXT("Capacity/degree budget retained"),Graph.Graph.Nodes[C.NodeA].Degree+1<=Caps[C.NodeA]&&Graph.Graph.Nodes[C.NodeB].Degree+1<=Caps[C.NodeB]);
                TestTrue(TEXT("No Final shortcut"),C.NodeA!=Graph.Graph.FinalNodeId&&C.NodeB!=Graph.Graph.FinalNodeId);
                TestTrue(TEXT("Meaningful span"),C.TreeDistance>=S.MinLoopTreeDistance);TestEqual(TEXT("Hypothetical independent cycle delta"),Graph.Graph.Edges.Num()+1-Graph.Graph.Nodes.Num()+1,1);TestEqual(TEXT("Recorded cycle delta"),C.IndependentCycleDelta,1);
                TSet<int32> Seen{Graph.Graph.StartNodeId};TArray<int32> Queue{Graph.Graph.StartNodeId};
                for(int32 H=0;H<Queue.Num();++H)
                {
                    const auto Add=[&](int32 X,int32 Y){const int32 Next=X==Queue[H]?Y:Y==Queue[H]?X:-1;if(Next>=0&&!Seen.Contains(Next)){Seen.Add(Next);Queue.Add(Next);}};
                    for(const auto& E:Graph.Graph.Edges)Add(E.NodeA,E.NodeB);Add(C.NodeA,C.NodeB);
                }
                TestEqual(TEXT("Hypothetical extra edge preserves connectivity"),Seen.Num(),Graph.Graph.Nodes.Num());
            }
        }
        WithCandidates+=A.TopologyPairs>0;Digests.Add(A.StableDigest);
        TestEqual(TEXT("Tree digest unaffected by loop query"),Graph.Graph.StableDigest,G::Generate(Seed,Settings.GraphSettings()).Graph.StableDigest);
        TestFalse(TEXT("Pure query has no batch/world identity"),A.SourceBatchId.IsValid()||A.SourceWorld.IsValid());
    }
    TSet<AActor*> After;for(TObjectIterator<AActor> I;I;++I)After.Add(*I);bool Same=Before.Num()==After.Num();for(auto* Actor:Before)Same&=After.Contains(Actor);TestTrue(TEXT("Pure loop queries create no Actors"),Same);
    AddInfo(FString::Printf(TEXT("LOOP_TOPO_STATS Seeds=100 WithPairs=%d TotalPairs=%d UniqueDigests=%d OriginalTreeUnchanged=1 SpatialNotClaimed=1"),WithCandidates,Total,Digests.Num()));return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRogueliteLoopMathTest,"Roguelite.Loops.FixedSocketGeometry",Flags)
bool FRogueliteLoopMathTest::RunTest(const FString&)
{
    FRogueliteDoorSocket A,B,Start,End;for(auto* D:{&A,&B,&Start,&End}){D->bValid=true;D->DoorWidth=200;D->DoorHeight=250;D->WallThickness=20;}
    A.LocalPosition=FVector(0,1020,0);A.WorldPosition=A.LocalPosition;A.LocalOutwardDirection=A.WorldOutwardDirection=FVector(0,1,0);
    B.LocalPosition=FVector(0,-1020,0);B.WorldPosition=FVector(0,2980,0);B.LocalOutwardDirection=B.WorldOutwardDirection=FVector(0,-1,0);
    Start.LocalPosition=Start.WorldPosition=FVector(-1000,0,0);Start.LocalOutwardDirection=Start.WorldOutwardDirection=FVector(-1,0,0);End.LocalPosition=End.WorldPosition=FVector(1000,0,0);End.LocalOutwardDirection=End.WorldOutwardDirection=FVector(1,0,0);
    const FBox Bounds(FVector(-1000,-270,-20),FVector(1000,270,370));const FTransform FixedB(FVector(0,4000,0));FRogueliteLoopPlanningSettings S;FTransform Transform;FBox Box;float Length;FString Reason;
    const auto Check=[&](const FRogueliteDoorSocket& Target,const FTransform& T){return FRogueliteLoopPlanner::FixedSocketGeometry(A,Target,Start,End,T,Bounds,S,Transform,Length,Box,Reason);};
    TestTrue(TEXT("Actual C2 fixed-frame formula"),Check(B,FixedB));TestEqual(TEXT("Threshold distance, not centers"),Length,1960.f);TestTrue(TEXT("Corridor center"),Transform.GetLocation().Equals(FVector(0,2000,0),.001));TestTrue(TEXT("Parameter occupancy"),Box.IsValid&&Box.GetSize().Equals(FVector(540,1960,390),.001));
    auto Bad=B;Bad.WorldOutwardDirection=FVector(1,0,0);TestFalse(TEXT("Wrong normals rejected"),Check(Bad,FixedB));TestTrue(TEXT("Corner remains unproved diagnosis"),Reason.StartsWith(TEXT("RequiresCornerCorridor:")));
    Bad=B;Bad.WorldPosition.X=100;TestFalse(TEXT("Lateral offset rejected"),Check(Bad,FTransform(FVector(100,4000,0))));
    Bad=B;Bad.WorldPosition.Z=100;TestFalse(TEXT("Elevation mismatch rejected"),Check(Bad,FTransform(FVector(0,4000,100))));TestTrue(TEXT("Height reason explicit"),Reason.Contains(TEXT("HeightDifference")));
    Bad=B;Bad.DoorHeight=240;TestFalse(TEXT("Door height mismatch"),Check(Bad,FixedB));TestTrue(TEXT("Specs reason explicit"),Reason.Contains(TEXT("SpecificationMismatch")));
    TestFalse(TEXT("No moving fixed RoomB"),Check(B,FTransform(FVector(10,4000,0))));TestTrue(TEXT("Fixed target reason explicit"),Reason.Contains(TEXT("FixedRoomTransformMismatch")));
    S.MinCorridorLength=3000;TestFalse(TEXT("Minimum length retained"),Check(B,FixedB));return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRogueliteLoopFailureTest,"Roguelite.Loops.BudgetsAndSeedIsolation",Flags)
bool FRogueliteLoopFailureTest::RunTest(const FString&)
{
    const auto Input=RogueliteBranchingTestSettings(0);const auto Graph=G::Generate(0,Input.GraphSettings());TArray<int32> Caps;Caps.Init(3,8);FRogueliteLoopPlanningSettings S;FString Reason;int32 Distance;
    TestFalse(TEXT("Self rejected explicitly"),FRogueliteLoopPlanner::CheckTopologyPair(Graph.Graph,Caps,S,0,0,Distance,Reason));TestTrue(TEXT("Self reason"),Reason.Contains(TEXT("SelfLoop")));
    TestFalse(TEXT("Already adjacent rejected"),FRogueliteLoopPlanner::CheckTopologyPair(Graph.Graph,Caps,S,0,1,Distance,Reason));TestTrue(TEXT("Adjacent reason"),Reason.Contains(TEXT("AlreadyAdjacent")));
    Caps[6]=Graph.Graph.Nodes[6].Degree;TestFalse(TEXT("Template capacity rejected"),FRogueliteLoopPlanner::CheckTopologyPair(Graph.Graph,Caps,S,4,6,Distance,Reason));TestTrue(TEXT("Capacity reason"),Reason.Contains(TEXT("Capacity")));
    S.MinLoops=S.MaxLoops=1;Caps.Init(1,8);const auto Failed=FRogueliteLoopPlanner::Topology(Graph.Graph,Caps,S);TestTrue(TEXT("Impossible minimum explicit, not reduced"),Failed.State==ERogueliteLoopPlanningState::LoopRequirementUnsatisfied&&!Failed.bRequirementsSatisfied&&S.MinLoops==1);
    S=FRogueliteLoopPlanningSettings();S.MaxCandidateChecks=1;const auto Limited=FRogueliteLoopPlanner::Topology(Graph.Graph,Caps,S);TestTrue(TEXT("Check budget bounded and retained"),Limited.bCandidateBudgetExhausted&&Limited.CandidateChecks==1);
    S.MinLoops=3;S.MaxLoops=2;TestFalse(TEXT("Invalid budget rejected"),FRogueliteLoopPlanner::ValidateSettings(S,Reason));
    S=FRogueliteLoopPlanningSettings();Caps.Init(3,8);const auto Before=FRogueliteLoopPlanner::Topology(Graph.Graph,Caps,S);
    for(int32 I=0;I<1000;++I){FRandomStream Other(G::DeriveSeed(I,ERogueliteGraphSeedPurpose::RoomContent));Other.GetUnsignedInt();}
    TestEqual(TEXT("Other-purpose draws cannot alter loop order"),Before.StableDigest,FRogueliteLoopPlanner::Topology(Graph.Graph,Caps,S).StableDigest);
    S.bOverrideLoopSeed=true;S.LoopSelectionSeed=12345;const auto Different=FRogueliteLoopPlanner::Topology(Graph.Graph,Caps,S);TestTrue(TEXT("Loop seed distinct plan contract"),Before.StableDigest!=Different.StableDigest);TestEqual(TEXT("Original V2 graph unaffected"),Graph.Graph.StableDigest,G::Generate(0,Input.GraphSettings()).Graph.StableDigest);
    FRogueliteGraphGenerationSettings V1;const auto Legacy=G::Generate(1337,V1);FRogueliteLoopPlanner::Topology(Legacy.Graph,TArray<int32>{},S);TestEqual(TEXT("Original V1 digest unaffected"),Legacy.Graph.StableDigest,G::Generate(1337,V1).Graph.StableDigest);return true;
}
#endif
