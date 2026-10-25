#include "Misc/AutomationTest.h"
#include "RogueliteDungeonMap.h"
#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMapGraphTest,"Roguelite.DungeonMap.LogicAndDeterminism",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMapGraphTest::RunTest(const FString&)
{
    const FRogueliteGraphGenerationSettings Settings;
    for(int32 Seed=0;Seed<100;++Seed)
    {
        const auto Original=FRogueliteRoomGraphGenerator::Generate(Seed,Settings);const auto A=URogueliteDungeonMapQuery::PreviewGraph(Seed,Settings);const auto B=URogueliteDungeonMapQuery::PreviewGraph(Seed,Settings);
        TestTrue(TEXT("B1 digest/counts identical"),A.bValid&&A.GraphDigest==Original.Graph.StableDigest&&A.Rooms.Num()==Original.Graph.Nodes.Num()&&A.Connections.Num()==Original.Graph.Edges.Num());
        TestEqual(TEXT("Deterministic snapshot"),A.StableDataDigest,B.StableDataDigest);
        for(const auto& R:A.Rooms){auto Neighbors=Original.Graph.GetNeighborIds(R.NodeId);Neighbors.Sort();TestTrue(TEXT("Stable neighbors"),Neighbors==R.Neighbors);TestFalse(TEXT("B1 does not invent types/world positions"),R.bRoomTypeAssigned||R.bHasWorldGeometry||R.bExplorationKnown);}
    }
    auto Bad=Settings;Bad.MaxDegree=0;TestFalse(TEXT("Invalid graph is empty"),URogueliteDungeonMapQuery::PreviewGraph(2,Bad).bValid);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMapLocalTest,"Roguelite.DungeonMap.OneHopPrivacy",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMapLocalTest::RunTest(const FString&)
{
    auto S=URogueliteDungeonMapQuery::PreviewGraph(2,FRogueliteGraphGenerationSettings());
    for(auto& R:S.Rooms){R.bRoomTypeAssigned=true;R.RoomType=ERogueliteSeedRoomType(R.NodeId%4);}
    for(const auto& Center:S.Rooms)
    {
        const auto Local=URogueliteDungeonMapQuery::FilterOneHop(S,Center.NodeId);
        TestTrue(TEXT("Explicit current room located"),Local.bLocated);TestEqual(TEXT("One hop room count"),Local.Rooms.Num(),Center.Neighbors.Num()+1);TestEqual(TEXT("Incident edges only"),Local.Connections.Num(),Center.Neighbors.Num());
        for(const auto& R:Local.Rooms){TestTrue(TEXT("No distant nodes"),R.NodeId==Center.NodeId||Center.Neighbors.Contains(R.NodeId));TestTrue(TEXT("Type label preserved"),R.bRoomTypeAssigned&&uint8(R.RoomType)==R.NodeId%4);for(int32 N:R.Neighbors)TestTrue(TEXT("No distant neighbor IDs leaked"),Local.Rooms.ContainsByPredicate([&](const auto& V){return V.NodeId==N;}));}
    }
    TestFalse(TEXT("Unknown current room not assumed Start"),URogueliteDungeonMapQuery::FilterOneHop(S,INDEX_NONE).bLocated);
    TestFalse(TEXT("Nonexistent node unlocated"),URogueliteDungeonMapQuery::FilterOneHop(S,9999).bLocated);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMapRectangleTest,"Roguelite.DungeonMap.RotatedFootprint",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMapRectangleTest::RunTest(const FString&)
{
    for(float Yaw:{0.f,90.f,180.f,270.f})
    {
        const FTransform T(FRotator(0,Yaw,0),FVector(1234,-567,80));auto P=URogueliteDungeonMapQuery::Rectangle(T,3000,2500,20);
        TestEqual(TEXT("Four corners"),P.Num(),4);TestTrue(TEXT("True transformed corner"),P[0].Equals(FVector2D(T.TransformPosition(FVector(-1520,-1270,0))),.01));
        TestTrue(TEXT("Length retained after rotation"),FMath::IsNearlyEqual(FVector2D::Distance(P[0],P[1]),3040.,.01));TestTrue(TEXT("Width retained after rotation"),FMath::IsNearlyEqual(FVector2D::Distance(P[1],P[2]),2540.,.01));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMapLoopLocalTest,"Roguelite.DungeonMap.CommittedOneHopAndEvent",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMapLoopLocalTest::RunTest(const FString&)
{
    FRogueliteDungeonMapSnapshot S;S.bValid=S.bSpatial=true;
    for(int32 I=0;I<5;++I){auto& R=S.Rooms.AddDefaulted_GetRef();R.NodeId=I;R.bRoomTypeAssigned=true;R.RoomType=ERogueliteSeedRoomType(I);if(I==4)R.EventDefinitionId=TEXT("TreasureCave");}
    const auto Edge=[&](int32 A,int32 B,bool Committed,bool Loop){auto& E=S.Connections.AddDefaulted_GetRef();E.EdgeId=S.Connections.Num()-1;E.NodeA=A;E.NodeB=B;E.bCommitted=Committed;E.bCommittedLoop=Loop;};
    Edge(0,1,true,false);Edge(1,2,true,false);Edge(2,3,true,false);Edge(0,4,true,true);Edge(1,4,true,false);Edge(0,3,false,true);
    const auto V=URogueliteDungeonMapQuery::FilterOneHop(S,0);
    TestEqual(TEXT("Tree plus actual loop neighbors only"),V.Rooms.Num(),3);TestEqual(TEXT("Only committed incident edges"),V.Connections.Num(),2);
    for(const auto& R:V.Rooms)if(R.NodeId!=0)TestTrue(TEXT("No neighbor-neighbor topology leakage"),R.Neighbors==TArray<int32>{0});
    const auto* Event=V.Rooms.FindByPredicate([](const auto& R){return R.NodeId==4;});TestTrue(TEXT("Event retains independent type and definition"),Event&&Event->RoomType==ERogueliteSeedRoomType::Event&&Event->EventDefinitionId==TEXT("TreasureCave"));
    auto Changed=S;Changed.Rooms[4].EventDefinitionId=TEXT("OtherEvent");TestNotEqual(TEXT("Event definition contributes to stable data"),URogueliteDungeonMapQuery::ComputeStableDigest(S),URogueliteDungeonMapQuery::ComputeStableDigest(Changed));
    TestFalse(TEXT("Null source never current"),URogueliteDungeonMapQuery::IsSnapshotCurrent(nullptr,S));
    return true;
}
#endif
