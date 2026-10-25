#include "Misc/AutomationTest.h"
#include "RogueliteDungeonMap.h"
#include "RogueliteRuntimeLoopAssembly.h"
#include "RogueliteLoopAwareTestRig.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Editor.h"
#include "Tests/AutomationEditorCommon.h"
namespace
{
// Values only. Neither HUD DTO nor this cross-World test retains UObject pointers.
FRogueliteDungeonMapSnapshot LastEditorMap;
TMap<FString,FTransform> Scene(UWorld* W)
{TMap<FString,FTransform> V;for(TActorIterator<AActor> I(W);I;++I)if(!I->IsActorBeingDestroyed())V.Add(I->GetPathName(),I->GetActorTransform());return V;}
bool SameScene(UWorld* W,const TMap<FString,FTransform>& Before)
{const auto After=Scene(W);if(Before.Num()!=After.Num())return false;for(const auto& P:Before){const auto* T=After.Find(P.Key);if(!T||!T->Equals(P.Value,.0001))return false;}return true;}
void CheckMap(FAutomationTestBase* T,AActor* Source,const FRogueliteDungeonMapSnapshot& S,int32 Rooms,int32 Edges,int32 Loops)
{
    T->TestTrue(TEXT("Live spatial map valid: ")+S.Reason,S.bValid&&S.bSpatial);T->TestEqual(TEXT("Actual rooms"),S.Rooms.Num(),Rooms);T->TestEqual(TEXT("Actual corridors"),S.Connections.Num(),Edges);T->TestEqual(TEXT("Committed loop count"),S.LoopCount,Loops);
    int32 ActualLoops=0;TSet<int32> EdgeIds;TSet<FGuid> Ids;
    for(const auto& E:S.Connections){ActualLoops+=E.bCommittedLoop;T->TestTrue(TEXT("Unique actual C2 edge/corridor observation"),E.bCommitted&&E.bReservationActive&&E.bLastGeometryValidated&&E.ConnectionId.IsValid()&&!Ids.Contains(E.ConnectionId)&&!EdgeIds.Contains(E.EdgeId)&&E.bHasWorldGeometry&&E.Length>0&&!E.CorridorIdentity.IsEmpty());Ids.Add(E.ConnectionId);EdgeIds.Add(E.EdgeId);}
    T->TestEqual(TEXT("Overlay edges actual count"),ActualLoops,Loops);
    for(const auto& R:S.Rooms)
    {
        T->TestTrue(TEXT("Value batch/geometry mapping"),R.BatchId==S.BatchId&&R.bHasWorldGeometry&&R.Footprint.Num()==4&&R.WorldTransform.GetScale3D().Equals(FVector::OneVector));
        const auto V=URogueliteDungeonMapQuery::GetLocalMapView(Source,R.NodeId);T->TestTrue(TEXT("One hop reads actual incidence"),V.bLocated&&V.Rooms.Num()==R.Neighbors.Num()+1&&V.Connections.Num()==R.Degree);
        for(const auto& E:V.Connections)T->TestTrue(TEXT("Every local edge incident on current room"),E.NodeA==R.NodeId||E.NodeB==R.NodeId);
        for(const auto& N:V.Rooms)if(N.NodeId!=R.NodeId)T->TestTrue(TEXT("Neighbors expose only current room, never cross-neighbor topology"),N.Neighbors==TArray<int32>{R.NodeId});
    }
    T->TestFalse(TEXT("Unknown current room stays unlocated"),URogueliteDungeonMapQuery::GetLocalMapView(Source,INDEX_NONE).bLocated);
}
bool TestWorld(FAutomationTestBase* T,bool Game)
{
    const auto* W=GEditor?(Game?GEditor->PlayWorld.Get():GEditor->GetEditorWorldContext().World()):nullptr;
    const bool Good=W&&W->IsGameWorld()==Game&&W->GetOutermost()->GetName().EndsWith(TEXT("L_Roguelite_RuntimeLoopNavigation_Dev"));
    if(!Good)T->AddError(TEXT("Existing B4-B3 dedicated test map in requested World required. No changes performed."));return Good;
}
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMapEditorIntegration,"ProjectVerification.E1.EditorDataAndFreshness",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMapEditorIntegration::RunTest(const FString&)
{
    if(!TestWorld(this,false)||GEditor->PlayWorld)return false;auto* W=GEditor->GetEditorWorldContext().World();const auto Before=Scene(W);
    auto* A=W->SpawnActor<ARogueliteLoopAwareTestRig>();if(!A)return false;A->SetActorLabel(TEXT("E1_Temporary_Data_Test"));
    const auto Build=[&](bool Small)
    {
        A->Settings=FRogueliteRuntimeLoopAssembler::SourcePreset(0,Small);A->Planning=FRogueliteLoopAwareSettings();A->Planning.bEnableLoopAwarePlanning=true;if(Small)A->Planning.MinLoops=A->Planning.MaxLoops=0;
        const auto P=FRogueliteLoopAwarePlanner::Plan(A,A->Settings,A->Planning,A->GetActorTransform());
        TestTrue(TEXT("Existing planner accepts configured fixture: ")+P.Evidence.Reason,P.Tree.bValid);if(!P.Tree.bValid)return false;
        TestFalse(TEXT("Unmaterialized intent is never a current map"),URogueliteDungeonMapQuery::Capture(A).bValid);
        const bool Good=FRogueliteLoopAwarePlanner::MaterializeEditor(A,A->Settings,A->Planning,P,A->Batch,A->CommittedLoops,A->Latest);TestTrue(TEXT("Existing planner materializes fixture: ")+A->Latest.Reason,Good);return Good;
    };
    FString Why;
    if(Build(true)){const auto S=URogueliteDungeonMapQuery::Capture(A);CheckMap(this,A,S,4,3,0);TestFalse(TEXT("Editor geometry never implies navigation ready"),S.bDungeonReady);TestTrue(TEXT("Clear own four-room batch"),FRogueliteLoopAwarePlanner::Clear(A,A->Batch,A->CommittedLoops,Why));TestFalse(TEXT("Clear invalidates previous data"),URogueliteDungeonMapQuery::IsSnapshotCurrent(A,S));}
    if(Build(false))
    {
        const auto S=URogueliteDungeonMapQuery::Capture(A);CheckMap(this,A,S,8,8,1);LastEditorMap=S;
        TSet<ERogueliteSeedRoomType> Types;int32 EventCount=0;
        for(const auto& R:S.Rooms){Types.Add(R.RoomType);if(R.RoomType==ERogueliteSeedRoomType::Event){++EventCount;TestEqual(TEXT("Real EventDefinitionId"),R.EventDefinitionId,FName(TEXT("TreasureCave")));}}
        TestEqual(TEXT("All five independent room types observed"),Types.Num(),5);TestTrue(TEXT("Real Event present"),EventCount>0);
        ++A->CommittedLoops.GraphRevision;TestFalse(TEXT("GraphRevision invalidates cached map"),URogueliteDungeonMapQuery::IsSnapshotCurrent(A,S));--A->CommittedLoops.GraphRevision;
        ++A->CommittedLoops.GeometryRevision;TestFalse(TEXT("GeometryRevision invalidates cached map"),URogueliteDungeonMapQuery::IsSnapshotCurrent(A,S));--A->CommittedLoops.GeometryRevision;
        ++A->Settings.Seed;TestFalse(TEXT("Seed switch rejects previous batch"),URogueliteDungeonMapQuery::Capture(A).bValid);--A->Settings.Seed;
        TestTrue(TEXT("Restored original epoch current"),URogueliteDungeonMapQuery::IsSnapshotCurrent(A,S));
        TestTrue(TEXT("Release only committed overlay through existing API"),FRogueliteLoopCommitQueries::Clear(A,A->Batch,A->CommittedLoops,Why));
        const auto Tree=URogueliteDungeonMapQuery::Capture(A);CheckMap(this,A,Tree,8,7,0);
        TestTrue(TEXT("Provisional intent remains excluded after uncommit"),!A->Latest.Intents.IsEmpty()&&!URogueliteDungeonMapQuery::IsSnapshotCurrent(A,S));
        FRogueliteLoopAwarePlanner::Clear(A,A->Batch,A->CommittedLoops,Why);
        if(Build(false)){const auto Repeat=URogueliteDungeonMapQuery::Capture(A);TestEqual(TEXT("Same seed layout DTO deterministic despite new GUIDs"),Repeat.StableDataDigest,S.StableDataDigest);TestNotEqual(TEXT("New batch not old current snapshot"),Repeat.BatchId,S.BatchId);TestFalse(TEXT("Batch replacement invalidates previous data"),URogueliteDungeonMapQuery::IsSnapshotCurrent(A,S));}
    }
    TestTrue(TEXT("Only owned test batch cleanup"),FRogueliteLoopAwarePlanner::Clear(A,A->Batch,A->CommittedLoops,Why));
    TestFalse(TEXT("Clean source returns empty invalid snapshot"),URogueliteDungeonMapQuery::Capture(A).bValid);A->Destroy();TestFalse(TEXT("Destroyed source unavailable"),URogueliteDungeonMapQuery::Capture(A).bValid);
    TestTrue(TEXT("Original Editor actors and transforms preserved"),SameScene(W,Before));AddInfo(TEXT("E1_EDITOR Actual4Tree/8Loop Event OneHop Revision Seed Batch Clear Determinism checked; original map not saved."));return true;
}
class FMapRuntimeAction : public IAutomationLatentCommand
{
public:
    TFunction<void()> Action;FAutomationTestBase* Test;double Started=FPlatformTime::Seconds();
    FMapRuntimeAction(FAutomationTestBase* T,TFunction<void()> A):Action(MoveTemp(A)),Test(T){}
    virtual bool Update() override
    {
        if(GEditor&&GEditor->PlayWorld&&GEditor->PlayWorld->HasBegunPlay()){Action();return true;}
        if(FPlatformTime::Seconds()-Started>30){Test->AddError(TEXT("PIE GameWorld did not start within30s"));return true;}return false;
    }
};
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMapRuntimeIntegration,"ProjectVerification.E1.RuntimeDataAndFreshness",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMapRuntimeIntegration::RunTest(const FString&)
{
    if(!TestWorld(this,false)||GEditor->PlayWorld)return false;
    // Automation starts a fresh PIE session; it closes pre-existing PIE before Editor-context tests.
    FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FStartPIECommand>(false));
    FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FMapRuntimeAction>(this,[this]()
    {
    if(!TestWorld(this,true))return;auto* W=GEditor->PlayWorld.Get();ARogueliteRuntimeLoopAssembly* A=nullptr;for(TActorIterator<ARogueliteRuntimeLoopAssembly> I(W);I;++I){if(A){AddError(TEXT("Exactly one existing runtime source required"));return;}A=*I;}if(!A){AddError(TEXT("Runtime test source absent"));return;}
    A->ClearRuntime();A->bNavigationEnabled=false;FString FirstDigest;FGuid FirstBatch;
    for(int32 Index=0;Index<7;++Index)
    {
        const bool Small=Index==5;A->Seed=Index<5?Index:0;A->Preset=Small?ERogueliteLoopTestPreset::FourRoomTree:ERogueliteLoopTestPreset::EightRoomSingleLoop;
        const bool Good=A->GenerateRuntime();TestTrue(TEXT("Existing runtime generator success: ")+A->LastFailureReason,Good);
        const auto S=URogueliteDungeonMapQuery::Capture(A);CheckMap(this,A,S,Small?4:8,Small?3:8,Small?0:1);TestEqual(TEXT("Real GameWorld identity"),S.WorldKind,FString(TEXT("GameWorld")));TestFalse(TEXT("Geometry-only read never creates Ready"),S.bDungeonReady);
        if(Index==0){FirstDigest=S.StableDataDigest;FirstBatch=S.BatchId;TestTrue(TEXT("Editor fixture available for World comparison"),LastEditorMap.bValid);TestEqual(TEXT("Editor/Runtime stable value geometry agrees"),S.StableDataDigest,LastEditorMap.StableDataDigest);TestFalse(TEXT("Editor World snapshot not current in PIE"),URogueliteDungeonMapQuery::IsSnapshotCurrent(A,LastEditorMap));}
        if(Index==6){TestEqual(TEXT("Same-seed runtime stable digest"),S.StableDataDigest,FirstDigest);TestNotEqual(TEXT("Runtime replacement batch unique"),S.BatchId,FirstBatch);}
        ++A->Seed;TestFalse(TEXT("Runtime exposed Seed change invalidates old batch"),URogueliteDungeonMapQuery::Capture(A).bValid);--A->Seed;
        TestTrue(TEXT("Read-only capture repeated without authority changes"),URogueliteDungeonMapQuery::Capture(A).Revision==S.Revision);
        ++A->CommittedLoops.GeometryRevision;TestFalse(TEXT("Runtime geometry epoch invalidation"),URogueliteDungeonMapQuery::IsSnapshotCurrent(A,S));--A->CommittedLoops.GeometryRevision;
        AddInfo(FString::Printf(TEXT("E1_RUNTIME Seed=%d Rooms=%d Edges=%d Loops=%d G/G=%d/%d Digest=%s Batch=%s"),S.Seed,S.Rooms.Num(),S.Connections.Num(),S.LoopCount,S.GraphRevision,S.GeometryRevision,*S.StableDataDigest,*S.BatchId.ToString()));
        TestTrue(TEXT("Runtime own batch clear"),A->ClearRuntime());TestFalse(TEXT("Runtime old batch unavailable"),URogueliteDungeonMapQuery::IsSnapshotCurrent(A,S));TestTrue(TEXT("Clear leaves no map rooms or corridors"),URogueliteDungeonMapQuery::Capture(A).Rooms.IsEmpty());
    }
    }));
    FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FEndPlayMapCommand>());
    return true;
}
#endif
