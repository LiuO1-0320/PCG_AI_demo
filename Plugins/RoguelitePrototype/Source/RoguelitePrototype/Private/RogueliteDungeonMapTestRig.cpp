#include "RogueliteDungeonMapTestRig.h"
#include "RoguelitePrototype.h"
#include "Engine/World.h"
#include "TimerManager.h"

namespace { FRogueliteDungeonMapSnapshot LastEditorSnapshot; }

ARogueliteDungeonMapTestRig::ARogueliteDungeonMapTestRig(){Settings.Seed=2;}
void ARogueliteDungeonMapTestRig::Check(const FString& Name,bool Passed)
{
    bMapChecksPassed&=Passed;const FString Text=FString::Printf(TEXT("%s %s"),Passed?TEXT("PASS"):TEXT("FAIL"),*Name);MapEvidence.Add(Text);UE_LOG(LogRoguelitePrototype,Display,TEXT("E1_CHECK %s"),*Text);
}
void ARogueliteDungeonMapTestRig::ReadMapSnapshot(){MapSnapshot=URogueliteDungeonMapQuery::Capture(this);}
void ARogueliteDungeonMapTestRig::RunEditorMapChecks()
{
#if WITH_EDITOR
    if(!GetWorld()||GetWorld()->IsGameWorld()||!GetWorld()->GetOutermost()->GetName().EndsWith(TEXT("L_Roguelite_MapPreview_Dev")))return;
    if(!Batch.OwnedActors.IsEmpty()){MapEvidence.Add(TEXT("Refuse to overwrite an existing batch."));return;}
    MapEvidence.Reset();bMapChecksPassed=true;
    const auto Saved=Settings;
    for(int32 I=0;I<4;++I)
    {
        Settings=Saved;Settings.MainTypeWeights={0,0,1};Settings.BranchTypeWeights={0,1,0};
        Batch=FRogueliteSeedLayoutAssembler::Generate(this,Settings,FTransform(FRotator(0,I*90.f,0),FVector(600,-400,0)));
        const auto S=URogueliteDungeonMapQuery::Capture(this);
        Check(FString::Printf(TEXT("Editor world yaw %d live mapping"),I*90),S.bValid&&S.bSpatial&&S.WorldKind==TEXT("EditorWorld")&&S.Rooms.Num()==4&&S.Connections.Num()==3);
        Check(TEXT("C2 occupancy and capsule regression"),Batch.bGeometryValid&&FRogueliteSeedLayoutAssembler::Revalidate(this,Batch));
        bool bTypeTagsCorrect=true;for(const auto& R:S.Rooms)bTypeTagsCorrect&=R.bRoomTypeAssigned&&R.RoomType==(R.Role==TEXT("Final")?ERogueliteSeedRoomType::Final:R.Role==TEXT("Branch")?ERogueliteSeedRoomType::Challenge:ERogueliteSeedRoomType::SupplyShop);
        Check(TEXT("Actual SupplyShop Challenge Final tags separate from template"),bTypeTagsCorrect);
        FString Why;FRogueliteSeedLayoutAssembler::Clear(this,Batch,Why);Check(TEXT("Cleared batch rejects cached snapshot"),!URogueliteDungeonMapQuery::Capture(this).bValid&&!URogueliteDungeonMapQuery::IsSnapshotCurrent(this,S));
    }
    Settings=Saved;Batch=FRogueliteSeedLayoutAssembler::Generate(this,Settings,GetActorTransform());const auto First=URogueliteDungeonMapQuery::Capture(this);const auto FirstBatch=Batch.BatchId;
    LastEditorSnapshot=First;
    FString Why;FRogueliteSeedLayoutAssembler::Clear(this,Batch,Why);Batch=FRogueliteSeedLayoutAssembler::Generate(this,Settings,GetActorTransform());MapSnapshot=URogueliteDungeonMapQuery::Capture(this);
    Check(TEXT("Same Seed stable map data; new Batch identity"),First.bValid&&MapSnapshot.bValid&&First.StableDataDigest==MapSnapshot.StableDataDigest&&Batch.BatchId!=FirstBatch&&!URogueliteDungeonMapQuery::IsSnapshotCurrent(this,First));
    Settings.Seed++;Check(TEXT("Seed switch cannot present previous batch"),!URogueliteDungeonMapQuery::Capture(this).bValid);Settings=Saved;
    FRogueliteSeedLayoutAssembler::Clear(this,Batch,Why);MapSnapshot=URogueliteDungeonMapQuery::Capture(this);
#endif
}
void ARogueliteDungeonMapTestRig::BeginPlay()
{
    Super::BeginPlay();if(bRunMapChecks)GetWorldTimerManager().SetTimerForNextTick(this,&ARogueliteDungeonMapTestRig::RunRuntimeMapChecks);
}
void ARogueliteDungeonMapTestRig::RunRuntimeMapChecks()
{
    if(!GetWorld()||!GetWorld()->IsGameWorld()||!GetWorld()->GetOutermost()->GetName().EndsWith(TEXT("L_Roguelite_MapPreview_Dev")))return;
    MapEvidence.Reset();bMapChecksPassed=true;const auto S=URogueliteDungeonMapQuery::Capture(this);
    Check(TEXT("Real GameWorld has value-only map"),S.bValid&&S.bSpatial&&S.WorldKind==TEXT("GameWorld")&&S.Rooms.Num()==Batch.Rooms.Num());
    if(LastEditorSnapshot.bValid)Check(TEXT("Editor Runtime stable data agrees; old World snapshot rejected"),S.StableDataDigest==LastEditorSnapshot.StableDataDigest&&!URogueliteDungeonMapQuery::IsSnapshotCurrent(this,LastEditorSnapshot));
    Check(TEXT("Geometry does not imply NavigationReady"),S.bGeometryValidated&&!S.bDungeonReady&&S.Navigation==TEXT("NotChecked"));
    bool Mapping=true;for(const auto& E:S.Connections)Mapping&=E.bReservationActive&&E.bLastGeometryValidated&&E.ConnectionId.IsValid()&&E.bHasWorldGeometry;
    Check(TEXT("Read sole C2 authority and corridor endpoints"),Mapping&&S.Connections.Num()==Batch.Edges.Num());
    const auto V=URogueliteDungeonMapQuery::GetLocalMapView(this,0);Check(TEXT("Runtime explicit one hop"),V.bLocated&&V.Rooms.Num()==S.Rooms[0].Neighbors.Num()+1);
    Check(TEXT("Runtime unknown room unlocated"),!URogueliteDungeonMapQuery::GetLocalMapView(this,INDEX_NONE).bLocated);
    const auto OldBatch=Batch.BatchId;ClearRuntime();Check(TEXT("Runtime clear invalidates previous snapshot"),!URogueliteDungeonMapQuery::IsSnapshotCurrent(this,S)&&!URogueliteDungeonMapQuery::Capture(this).bValid);
    const bool Generated=GenerateRuntime();MapSnapshot=URogueliteDungeonMapQuery::Capture(this);
    Check(TEXT("Runtime same seed stable data and different batch"),Generated&&MapSnapshot.bValid&&MapSnapshot.StableDataDigest==S.StableDataDigest&&Batch.BatchId!=OldBatch);
    const int32 Seed=Settings.Seed;Settings.Seed++;Check(TEXT("Runtime source Seed change rejected"),!URogueliteDungeonMapQuery::Capture(this).bValid);Settings.Seed=Seed;
    UE_LOG(LogRoguelitePrototype,Display,TEXT("E1_RUNTIME_RESULT Passed=%d Digest=%s Source=%s"),bMapChecksPassed,*MapSnapshot.StableDataDigest,*GetPathName());
}
