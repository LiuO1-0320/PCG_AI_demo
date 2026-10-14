#include "RogueliteRuntimeSeedAssembly.h"
#include "RogueliteSeedAssemblyInternal.h"
#include "RoguelitePrototype.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "RogueliteNavigationReadinessComponent.h"

using namespace RogueliteSeedAssemblyInternal;
namespace
{
    FRogueliteSeedBatch Failed(AActor* Owner,FRogueliteSeedBatch B,const FString& Reason)
    {
        FString Cleanup;const bool Cleared=FRogueliteSeedLayoutAssembler::Clear(Owner,B,Cleanup);
        B.bSucceeded=false;B.bGeometryValid=false;B.Reason=Reason+(Cleared?TEXT(" | Runtime batch cleared."):TEXT(" | Cleanup refused: ")+Cleanup);
        UE_LOG(LogRoguelitePrototype,Warning,TEXT("B3A_FAILED Seed=%d Cleanup=%d %s"),B.Seed,Cleared,*B.Reason);return B;
    }
    template<class T> T* Deferred(AActor* Owner,UClass* Class,const FTransform& FinalTransform,const FGuid& Id,TFunction<void(AActor*)> Configure)
    {
        FActorSpawnParameters P;P.Owner=Owner;P.bDeferConstruction=true;P.ObjectFlags|=RF_Transient;
        P.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        P.TransformScaleMethod=ESpawnActorScaleMethod::OverrideRootScale;
        P.CustomPreSpawnInitialization=MoveTemp(Configure);
        T* A=Owner->GetWorld()->SpawnActor<T>(Class,FinalTransform,P);if(A)A->Tags.Add(BatchTag(Id));return A;
    }
    bool OpenSide(const FRogueliteSeedPlannedRoom& R,ERogueliteRoomSide Side){return R.UsedSockets.Contains(Side);}
}

FRogueliteSeedBatch FRogueliteRuntimeSeedAssembler::SpawnPlannedActors(AActor* Owner,const FRogueliteSeedSpatialPlan& Plan)
{
    auto B=Plan.Metadata;
    if(!IsValid(Owner) || !Owner->GetWorld() || !Owner->GetWorld()->IsGameWorld() || !Plan.bValid)
        return Failed(Owner,B,TEXT("Runtime spawn requires GameWorld and valid complete spatial plan. ")+Plan.Reason);
    // Plan cannot be reused as a second reservation authority. Actual commit remains C2 only.
    auto* Registry=Deferred<ARogueliteRoomAssembly>(Owner,ARogueliteRoomAssembly::StaticClass(),FTransform::Identity,B.BatchId,{});
    if(!Registry)return Failed(Owner,B,TEXT("Runtime registry spawn failed."));
    B.Registry=Registry;B.OwnedActors.Add(Registry);Registry->bCheckNavigation=false;Registry->FinishSpawning(FTransform::Identity);
    for(const auto& R:Plan.Rooms)
    {
        auto* A=Deferred<ARogueliteRoomTemplate>(Owner,R.Record.TemplateClass.Get(),R.Record.PlacedTransform,B.BatchId,[R](AActor* Actor)
        {
            auto* Room=CastChecked<ARogueliteRoomTemplate>(Actor);
            Room->RoomLength=R.Record.Dimensions.X;Room->RoomWidth=R.Record.Dimensions.Y;Room->RoomHeight=R.Record.Dimensions.Z;
            Room->WallThickness=R.Record.WallThickness;Room->FloorThickness=R.Record.FloorThickness;
            Room->NorthDoor.bEnabled=OpenSide(R,ERogueliteRoomSide::North);Room->SouthDoor.bEnabled=OpenSide(R,ERogueliteRoomSide::South);
            Room->EastDoor.bEnabled=OpenSide(R,ERogueliteRoomSide::East);Room->WestDoor.bEnabled=OpenSide(R,ERogueliteRoomSide::West);
        });
        if(!A)return Failed(Owner,B,TEXT("Planned room deferred spawn failed."));
        B.OwnedActors.Add(A);const FTransform FirstTransform=A->GetActorTransform();A->FinishSpawning(R.Record.PlacedTransform);
        const auto V=A->ValidateTemplate();
        if(!FirstTransform.Equals(R.Record.PlacedTransform,.0001) || !A->GetActorTransform().Equals(R.Record.PlacedTransform,.0001)
            || !V.bValid || V.bDeferredContent || !A->HasActorBegunPlay() || MembersText(A)!=R.Record.MemberSignature
            || !FVector(A->RoomLength,A->RoomWidth,A->RoomHeight).Equals(R.Record.Dimensions,.0001)
            || A->WallThickness!=R.Record.WallThickness || A->FloorThickness!=R.Record.FloorThickness
            || A->GetDoorSockets().Num()!=R.UsedSockets.Num())
            return Failed(Owner,B,FString::Printf(TEXT("Node=%d actual Blueprint geometry/content/Transform differs from plan; no relocation attempted."),R.Record.NodeId));
        auto Record=R.Record;Record.Room=A;B.Rooms.Add(Record);
        UE_LOG(LogRoguelitePrototype,Display,TEXT("B3A_ROOM Node=%d Path=%s FinalAtInitialSpawn=1 Begun=1 Members=%d Scale=%s"),
            Record.NodeId,*A->GetPathName(),A->GetTemplateMemberStates().Num(),*A->GetActorScale3D().ToString());
    }
    for(const auto& E:Plan.Edges)
    {
        auto* C=Deferred<ARogueliteParametricCorridor>(Owner,ARogueliteParametricCorridor::StaticClass(),E.Transform,B.BatchId,[E](AActor* Actor)
        {
            auto* Corridor=CastChecked<ARogueliteParametricCorridor>(Actor);Corridor->CorridorLength=E.Record.CorridorLength;
            Corridor->CorridorWidth=E.Profile.Width;Corridor->CorridorHeight=E.Profile.Height;Corridor->WallThickness=E.Profile.WallThickness;
            Corridor->FloorThickness=E.Profile.FloorThickness;Corridor->DoorWidth=E.Profile.DoorWidth;Corridor->DoorHeight=E.Profile.DoorHeight;
        });
        if(!C)return Failed(Owner,B,TEXT("Planned corridor deferred spawn failed."));
        B.OwnedActors.Add(C);const auto Initial=C->GetActorTransform();C->FinishSpawning(E.Transform);
        if(!Initial.Equals(E.Transform,.0001) || !C->GetActorTransform().Equals(E.Transform,.0001) || !C->HasActorBegunPlay()
            || !C->GetWorldOccupancyBounds().IsValid)return Failed(Owner,B,TEXT("Corridor initial/final Transform or geometry mismatch."));
        auto Record=E.Record;Record.Corridor=C;B.Edges.Add(Record);
    }
    B.Reason=TEXT("ActorsSpawned at planned transforms. No connections/geometry/NavigationReady claim yet.");return B;
}

bool FRogueliteRuntimeSeedAssembler::ValidateAndCommit(AActor* Owner,const FRogueliteSeedSpatialPlan& Plan,FRogueliteSeedBatch& B)
{
    if(!IsValid(Owner) || !Owner->GetWorld() || !Owner->GetWorld()->IsGameWorld())
    { B.bSucceeded=false;B.bGeometryValid=false;B.Reason=TEXT("Runtime validation requires valid owner/GameWorld; no actor or reservation touched.");return false; }
    if(IsValid(B.Registry) && (B.Registry->GetOwner()!=Owner || B.Registry->GetWorld()!=Owner->GetWorld() || B.BatchId!=Plan.Metadata.BatchId))
    { B.bSucceeded=false;B.bGeometryValid=false;B.Reason=TEXT("Registry/plan ownership mismatch; no foreign reservation touched.");return false; }
    if(!Plan.bValid || !IsValid(B.Registry)
        || B.Rooms.Num()!=Plan.Rooms.Num() || B.Edges.Num()!=Plan.Edges.Num())
    {B=Failed(Owner,B,TEXT("Invalid spawned runtime batch."));return false;}
    for(auto& E:B.Edges)
    {
        auto* A=B.Rooms.FindByPredicate([&](const auto& R){return R.NodeId==E.ParentNode;});
        auto* RoomB=B.Rooms.FindByPredicate([&](const auto& R){return R.NodeId==E.ChildNode;});
        if(!A || !RoomB || !IsValid(E.Corridor)){B=Failed(Owner,B,TEXT("Runtime edge actor mapping invalid."));return false;}
        const auto BeforeA=A->Room->GetActorTransform(),BeforeB=RoomB->Room->GetActorTransform(),BeforeC=E.Corridor->GetActorTransform();
        const auto R=B.Registry->TryAlignCorridorConnection(A->Room,E.ParentSocket,E.Corridor,ERogueliteCorridorEndpoint::Start,RoomB->Room,E.ChildSocket);
        E.ConnectionId=R.ConnectionId;E.Validation=R;
        if(!R.bSucceeded || !R.bCapsulePassed || !BeforeA.Equals(A->Room->GetActorTransform(),.0001)
            || !BeforeB.Equals(RoomB->Room->GetActorTransform(),.0001) || !BeforeC.Equals(E.Corridor->GetActorTransform(),.0001))
        {B.FailedEdge=E.EdgeId;B.FailureValidation=R;B=Failed(Owner,B,TEXT("Runtime connection commit failed: ")+R.Message);return false;}
        UE_LOG(LogRoguelitePrototype,Display,TEXT("B3A_CONNECTION Edge=%d Id=%s SocketA=%d SocketB=%d Probes=%d NoRegisteredActorMoved=1"),
            E.EdgeId,*E.ConnectionId.ToString(),uint8(E.ParentSocket),uint8(E.ChildSocket),R.Probes.Num());
    }
    if(!FRogueliteSeedLayoutAssembler::Revalidate(Owner,B)){B=Failed(Owner,B,TEXT("Runtime final geometry validation failed: ")+B.Reason);return false;}
    B.LayoutDigest=FRogueliteRoomGraphGenerator::StableTextDigest(FRogueliteSeedLayoutAssembler::DescribeLayout(B));
    B.Reason=TEXT("GeometryValidated. NavigationPending; no Dungeon Ready claim.");return true;
}

ARogueliteRuntimeSeedAssembly::ARogueliteRuntimeSeedAssembly()
{
    PrimaryActorTick.bCanEverTick=false;SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("RuntimeSeedRoot")));
    Settings=GetDefault<ARogueliteSeedAssembly>()->Settings;Settings.Seed=2;Settings.bCheckNavigation=false;
    NavigationReadiness=CreateDefaultSubobject<URogueliteNavigationReadinessComponent>(TEXT("NavigationReadiness"));
}
void ARogueliteRuntimeSeedAssembly::RecordStage(ERogueliteDungeonBuildStage Value)
{
    Stage=Value;StageHistory.Add(Value);UE_LOG(LogRoguelitePrototype,Display,TEXT("B3A_STAGE Owner=%s Stage=%s"),*GetPathName(),*UEnum::GetValueAsString(Value));
}
bool ARogueliteRuntimeSeedAssembly::GenerateRuntime()
{
    if(!GetWorld() || !GetWorld()->IsGameWorld() || !Batch.OwnedActors.IsEmpty())return false;
    StageHistory.Reset();bGeometryValidated=false;bDungeonReady=false;
    NavigationReadiness->Cancel();const double GeometryStart=FPlatformTime::Seconds();
    const auto Plan=FRogueliteSeedLayoutAssembler::Plan(this,Settings,GetActorTransform());
    if(Plan.bGraphGenerated)RecordStage(ERogueliteDungeonBuildStage::GraphGenerated);
    if(!Plan.bValid){Batch=Plan.Metadata;Batch.bCleanupComplete=true;Batch.Reason=Plan.Reason;RecordStage(ERogueliteDungeonBuildStage::Failed);return false;}
    RecordStage(ERogueliteDungeonBuildStage::SpatialPlanValid);
    Batch=FRogueliteRuntimeSeedAssembler::SpawnPlannedActors(this,Plan);
    if(Batch.bCleanupComplete){RecordStage(ERogueliteDungeonBuildStage::Failed);return false;}
    RecordStage(ERogueliteDungeonBuildStage::ActorsSpawned);
    if(!FRogueliteRuntimeSeedAssembler::ValidateAndCommit(this,Plan,Batch)){RecordStage(ERogueliteDungeonBuildStage::Failed);return false;}
    bGeometryValidated=true;RecordStage(ERogueliteDungeonBuildStage::GeometryValidated);RecordStage(ERogueliteDungeonBuildStage::NavigationPending);
    Evidence.Add(FString::Printf(TEXT("B3A_RUNTIME Seed=%d World=%s GameWorld=%d Rooms=%d Corridors=%d Digest=%s Geometry=1 DungeonReady=0 NavigationPending"),
        Settings.Seed,*GetWorld()->GetPathName(),GetWorld()->IsGameWorld(),Batch.Rooms.Num(),Batch.Edges.Num(),*Batch.LayoutDigest));
    UE_LOG(LogRoguelitePrototype,Display,TEXT("%s"),*Evidence.Last());
    if(bEnableRuntimeNavigation)NavigationReadiness->Start(FPlatformTime::Seconds()-GeometryStart);
    return true;
}
bool ARogueliteRuntimeSeedAssembly::ClearRuntime()
{
    if(!GetWorld() || !GetWorld()->IsGameWorld())return false;
    NavigationReadiness->Cancel();bDungeonReady=false;
    FString Reason;const bool Cleared=FRogueliteSeedLayoutAssembler::Clear(this,Batch,Reason);
    Evidence.Add(Reason);if(Cleared){bGeometryValidated=false;bDungeonReady=false;Stage=ERogueliteDungeonBuildStage::Idle;OnReadinessChanged.Broadcast(Batch.BatchId,Stage,false,Reason);}return Cleared;
}
bool ARogueliteRuntimeSeedAssembly::IsDungeonReady() const{return bDungeonReady && NavigationReadiness && NavigationReadiness->IsCurrentBatchReady();}
void ARogueliteRuntimeSeedAssembly::PublishNavigationState(FGuid Id,ERogueliteDungeonBuildStage Value,const FString& Reason)
{
    if(!GetWorld() || !GetWorld()->IsGameWorld() || GetWorld()->bIsTearingDown || Id!=Batch.BatchId || !bGeometryValidated)return;
    if(Value==ERogueliteDungeonBuildStage::NavigationReady && (!NavigationReadiness || !NavigationReadiness->IsCurrentBatchReady()))return;
    bDungeonReady=Value==ERogueliteDungeonBuildStage::NavigationReady;
    Batch.Navigation=bDungeonReady?ERogueliteNavigationCheck::Passed:
        Value==ERogueliteDungeonBuildStage::NavigationUnavailable?ERogueliteNavigationCheck::Unavailable:
        Value==ERogueliteDungeonBuildStage::NavigationPending?ERogueliteNavigationCheck::NotChecked:ERogueliteNavigationCheck::Failed;
    RecordStage(Value);Evidence.Add(Reason);OnReadinessChanged.Broadcast(Id,Value,bDungeonReady,Reason);
}
void ARogueliteRuntimeSeedAssembly::BeginPlay()
{
    Super::BeginPlay();UE_LOG(LogRoguelitePrototype,Display,TEXT("B3A_BEGIN RuntimeActor=%s GameWorld=%d"),*GetPathName(),GetWorld()->IsGameWorld());
    if(bRunRuntimeChecks)GetWorldTimerManager().SetTimerForNextTick(this,&ARogueliteRuntimeSeedAssembly::RunRuntimeChecks);
    else if(bGenerateOnBeginPlay)GetWorldTimerManager().SetTimerForNextTick(this,&ARogueliteRuntimeSeedAssembly::GenerateAfterBeginPlay);
}
void ARogueliteRuntimeSeedAssembly::GenerateAfterBeginPlay(){GenerateRuntime();}
void ARogueliteRuntimeSeedAssembly::EndPlay(const EEndPlayReason::Type Reason)
{
    GetWorldTimerManager().ClearAllTimersForObject(this);
    NavigationReadiness->Cancel();bDungeonReady=false;
    // During PIE teardown the World destroys all transient instances; never touch the Editor world.
    if(GetWorld() && !GetWorld()->bIsTearingDown)ClearRuntime();
    Super::EndPlay(Reason);
}
