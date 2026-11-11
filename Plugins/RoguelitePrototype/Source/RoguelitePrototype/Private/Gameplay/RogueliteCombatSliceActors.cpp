#include "Gameplay/RogueliteCombatSliceActors.h"

/* PERF1A_BEGIN */
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "ProfilingDebugging/MiscTrace.h"
/* PERF1A_END */
#include "Gameplay/RogueliteCombatSlicePlayer.h"
#include "Gameplay/RogueliteCombatSliceEncounter.h"
#include "RogueliteDynamicGameplayBridge.h"
#include "RogueliteRuntimeLoopAssembly.h"
#include "Gameplay/RogueliteGameplayObservation.h"
#include "Components/CapsuleComponent.h"
#include "Components/BoxComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Engine/OverlapResult.h"
#include "DrawDebugHelpers.h"
#include "UObject/ConstructorHelpers.h"
#include "AIController.h"

namespace RogueliteSliceDynamicEndpoints
{
struct FCommittedEndpoints
{
    FRogueliteRoomConnection Record;
    FRogueliteDoorSocket A,B;
    int32 NodeA=INDEX_NONE,NodeB=INDEX_NONE;
};
bool Resolve(ARogueliteRuntimeLoopAssembly* Source,FGuid Batch,FGuid Id,FCommittedEndpoints& Out,FString& Why)
{
    if(!IsInGameThread()||!IsValid(Source)||!Source->GetWorld()||!Source->GetWorld()->IsGameWorld()
        ||!Batch.IsValid()||Source->Batch.BatchId!=Batch||!Id.IsValid()||!Source->IsDungeonReady()
        ||!IsValid(Source->Batch.Registry)||Source->Batch.Registry->GetOwner()!=Source)
    {Why=TEXT("Dynamic connection requires the same current Ready GameWorld/Source/Batch and original C2 registry.");return false;}
    const auto Traversal=Source->GetTraversalSnapshot();
    const auto* Edge=Traversal.Edges.FindByPredicate([&](const auto& Row){return Row.ConnectionId==Id;});
    const auto Records=Source->Batch.Registry->GetConnectionRecords();
    const auto* Record=Records.FindByPredicate([&](const auto& Row){return Row.Id==Id&&Row.bActive
        &&Row.bGeometryValid&&Row.ValidationStatus==ERogueliteConnectionValidation::Passed;});
    if(!Traversal.bCurrent||!Edge||!Record||Record->Mode!=ERogueliteConnectionMode::StraightCorridor
        ||!IsValid(Record->RoomA)||!IsValid(Record->RoomB)||!IsValid(Record->Corridor)
        ||Record->RoomA->GetWorld()!=Source->GetWorld()||Record->RoomB->GetWorld()!=Source->GetWorld())
    {Why=TEXT("Connection is not a current committed Tree/Loop edge with valid C2 endpoints.");return false;}
    const auto* RoomA=Source->Batch.Rooms.FindByPredicate([&](const auto& Row){return Row.Room==Record->RoomA;});
    const auto* RoomB=Source->Batch.Rooms.FindByPredicate([&](const auto& Row){return Row.Room==Record->RoomB;});
    if(!RoomA||!RoomB||!((Edge->NodeA==RoomA->NodeId&&Edge->NodeB==RoomB->NodeId)
        ||(Edge->NodeB==RoomA->NodeId&&Edge->NodeA==RoomB->NodeId))
        ||!Record->RoomA->TryGetDoorSocket(Record->SocketA,Out.A)
        ||!Record->RoomB->TryGetDoorSocket(Record->SocketB,Out.B))
    {Why=TEXT("Actual batch NodeId, Room and Socket mapping does not match the authoritative connection.");return false;}
    Out.Record=*Record;Out.NodeA=RoomA->NodeId;Out.NodeB=RoomB->NodeId;return true;
}
bool SupportedEndpoint(UWorld* World,AActor* Ignore,ARogueliteParametricRoom* Room,const FVector& Feet,FString& Why)
{
    FCollisionQueryParams Query(SCENE_QUERY_STAT(G1B_RecoveryEndpoint),false,Ignore);
    FHitResult Ground;
    if(!IsValid(Room)||!World->LineTraceSingleByChannel(Ground,Feet+FVector(0,0,100),Feet-FVector(0,0,20),ECC_Visibility,Query)
        ||Ground.GetActor()!=Room||Ground.ImpactNormal.Z<.9f||FMath::Abs(Ground.ImpactPoint.Z-Feet.Z)>3.f)
    {Why=TEXT("Actual C2 recovery endpoint has no verified nearby room floor.");return false;}
    const FVector Local=Room->GetActorTransform().InverseTransformPosition(Feet);
    if(FMath::Abs(Local.X)+36>Room->RoomLength*.5||FMath::Abs(Local.Y)+36>Room->RoomWidth*.5
        ||World->OverlapBlockingTestByChannel(Feet+FVector(0,0,90),FQuat::Identity,ECC_Pawn,FCollisionShape::MakeCapsule(34,88),Query))
    {Why=TEXT("Actual C2 recovery endpoint cannot contain an unobstructed standing capsule.");return false;}
    return true;
}
}

URogueliteSliceHealth::URogueliteSliceHealth() { PrimaryComponentTick.bCanEverTick=false; }
void URogueliteSliceHealth::BeginPlay()
{
    Super::BeginPlay();
    MaxHealth=FMath::IsFinite(MaxHealth)?FMath::Max(1.f,MaxHealth):100.f;
    CurrentHealth=MaxHealth;Resolution=ERogueliteSliceMemberResolution::None;DamageEvents=0;AppliedEvents.Reset();
}
bool URogueliteSliceHealth::IsAlive() const
{return Resolution==ERogueliteSliceMemberResolution::None&&FMath::IsFinite(CurrentHealth)&&CurrentHealth>0;}
float URogueliteSliceHealth::ApplyDamageOnce(float Amount,FGuid EventId,AActor* Causer)
{
    if(!IsInGameThread()||!FMath::IsFinite(Amount)||Amount<=0||!EventId.IsValid()||!IsAlive()
        ||!IsValid(GetOwner())||GetOwner()->IsActorBeingDestroyed()||!IsValid(Causer)
        ||Causer->GetWorld()!=GetWorld()||AppliedEvents.Contains(EventId))return 0;
    AppliedEvents.Add(EventId);++DamageEvents;
    const float Applied=FMath::Min(CurrentHealth,Amount);CurrentHealth-=Applied;
    if(CurrentHealth<=0)
    {
        CurrentHealth=0;Resolution=ERogueliteSliceMemberResolution::Killed;
        // Commit terminal state before callbacks: reentrant or duplicate damage cannot kill twice.
        OnKilled.Broadcast(this);
    }
    return Applied;
}

ARogueliteSliceEnemy::ARogueliteSliceEnemy()
{
    PrimaryActorTick.bCanEverTick=true;AutoPossessAI=EAutoPossessAI::Disabled;
    GetCapsuleComponent()->InitCapsuleSize(34.f,88.f);
    GetCapsuleComponent()->SetCollisionObjectType(ECC_Pawn);
    GetCapsuleComponent()->SetCollisionResponseToAllChannels(ECR_Block);
    GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Camera,ECR_Ignore);
    GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    GetCapsuleComponent()->SetCanEverAffectNavigation(false);
    GetMesh()->SetCollisionEnabled(ECollisionEnabled::NoCollision);GetMesh()->SetCanEverAffectNavigation(false);
    GetCharacterMovement()->SetMovementMode(MOVE_None);
    GetCharacterMovement()->bRunPhysicsWithNoController=false;
    GetCharacterMovement()->bOrientRotationToMovement=false;
    Health=CreateDefaultSubobject<URogueliteSliceHealth>(TEXT("G1BEnemyHealth"));Health->MaxHealth=60.f;
    TestBody=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("G1BEnemyBody"));
    TestBody->SetupAttachment(GetCapsuleComponent());TestBody->SetMobility(EComponentMobility::Movable);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> EnemyBodyResource(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    if(EnemyBodyResource.Succeeded())TestBody->SetStaticMesh(EnemyBodyResource.Object);
    TestBody->SetRelativeScale3D(FVector(.6,.6,1.5));
    TestBody->SetCollisionEnabled(ECollisionEnabled::NoCollision);TestBody->SetCanEverAffectNavigation(false);
}
bool ARogueliteSliceEnemy::Configure(ARogueliteRuntimeLoopAssembly* Source,FGuid Batch,int32 NodeId,FGuid MemberId,
    const FTransform& Spawn,FString& Why,URogueliteSliceEncounter* Encounter)
{
    if(bConfigured)
    {
        const bool Same=HasCurrentBinding()&&BoundSource.Get()==Source&&BoundBatch==Batch
            &&RoomNodeId==NodeId&&EncounterMemberId==MemberId&&GetActorTransform().Equals(Spawn,.01)&&DamageEncounter.Get()==Encounter;
        Why=Same?TEXT("Enemy is already configured; existing lease preserved."):TEXT("Enemy instances cannot be reused across identities or after quiescence.");return Same;
    }
    if(!IsInGameThread()||!IsValid(Source)||!GetWorld()||!GetWorld()->IsGameWorld()||Source->GetWorld()!=GetWorld()
        ||GetOwner()!=Source||!Batch.IsValid()||Source->Batch.BatchId!=Batch||!MemberId.IsValid()
        ||!Source->IsDungeonReady()||Spawn.ContainsNaN()||!Spawn.GetScale3D().Equals(FVector::OneVector,.0001)
        ||!GetActorScale3D().Equals(FVector::OneVector,.0001)||!Health||!Health->IsAlive())
    {Why=TEXT("Enemy requires actual Source ownership, current Ready GameWorld/Batch, valid member and unit-scale spawn.");return false;}
    if(Encounter&&(!IsValid(Encounter)||Encounter->GetOwner()!=Source||Encounter->GetWorld()!=GetWorld()||!Encounter->IsCoordinated()||Encounter->GetTargetNodeId()!=NodeId))
    {Why=TEXT("Scoped enemy requires its actual Source-owned encounter and matching NodeId.");return false;}
    const auto Traversal=Source->GetTraversalSnapshot();
    const auto* Node=Traversal.Nodes.FindByPredicate([&](const auto& Row){return Row.NodeId==NodeId;});
    const auto* Room=Source->Batch.Rooms.FindByPredicate([&](const auto& Row){return Row.NodeId==NodeId;});
    if(!Traversal.bCurrent||!Node||Node->Role==TEXT("Start")||Node->Role==TEXT("Final")||!Room||!IsValid(Room->Room)
        ||Room->RoomType!=ERogueliteSeedRoomType::Combat||!Source->Settings.RoomTemplateRoles.bEnabled
        ||!Source->Settings.RoomTemplateRoles.NormalTemplateIds.Contains(Room->TemplateId))
    {Why=TEXT("Enemy target must be an actual current Normal Combat node, not Start/Final or an unqualified template.");return false;}
    const FVector Local=Room->PlacedTransform.InverseTransformPosition(Spawn.GetLocation());
    if(FMath::Abs(Local.X)+36>Room->Dimensions.X*.5||FMath::Abs(Local.Y)+36>Room->Dimensions.Y*.5
        ||Local.Z<88||Local.Z+88>Room->Dimensions.Z)
    {Why=TEXT("Enemy standing capsule is outside the actual room interior.");return false;}
    FCollisionQueryParams Query(SCENE_QUERY_STAT(G1B_EnemyPlacement),false,this);
    if(GetWorld()->OverlapBlockingTestByChannel(Spawn.GetLocation(),FQuat::Identity,ECC_Pawn,
        FCollisionShape::MakeCapsule(34,88),Query))
    {Why=TEXT("Enemy capsule would overlap world geometry or another participant.");return false;}
    FHitResult Ground;
    if(!GetWorld()->LineTraceSingleByChannel(Ground,Spawn.GetLocation(),Spawn.GetLocation()-FVector(0,0,150),ECC_Visibility,Query)
        ||Ground.GetActor()!=Room->Room.Get()||Ground.ImpactNormal.Z<.9f
        ||FMath::Abs(Room->PlacedTransform.InverseTransformPosition(Ground.ImpactPoint).Z)>1.f
        ||Spawn.GetLocation().Z-88-Ground.ImpactPoint.Z>10.f)
    {
        Why=FString::Printf(TEXT("Enemy has no verified nearby room floor support. Hit=%s Component=%s Impact=%s LocalZ=%.3f FeetGap=%.3f"),
            *GetNameSafe(Ground.GetActor()),*GetNameSafe(Ground.GetComponent()),*Ground.ImpactPoint.ToString(),
            Room->PlacedTransform.InverseTransformPosition(Ground.ImpactPoint).Z,Spawn.GetLocation().Z-88-Ground.ImpactPoint.Z);return false;
    }
    FRogueliteGameplayObservationWrite ObservationWrite(AActor::GetWorld(),TEXT("Enemy configuration"));
    if(!FRogueliteDynamicGameplayBridge::Enable(Source,Batch,Why))return false;
    SetActorLocationAndRotation(Spawn.GetLocation(),Spawn.GetRotation(),false,nullptr,ETeleportType::TeleportPhysics);
    if(!FRogueliteDynamicGameplayBridge::RegisterComponent(Source,Batch,this,GetCapsuleComponent(),ERogueliteDynamicParticipant::Pawn,Why))return false;
    BoundSource=Source;BoundBatch=Batch;RoomNodeId=NodeId;EncounterMemberId=MemberId;DamageEncounter=Encounter;bScopedDamage=Encounter!=nullptr;
    bConfigured=bLeaseRegistered=true;bDormant=true;bActive=false;
    GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    GetCharacterMovement()->StopMovementImmediately();GetCharacterMovement()->SetMovementMode(MOVE_None);
    Health->OnKilled.AddUObject(this,&ARogueliteSliceEnemy::HealthKilled);
    Why=TEXT("Exact enemy capsule registered before collision; current Normal Combat member starts Dormant.");return true;
}
bool ARogueliteSliceEnemy::HasCurrentBinding() const
{
/* PERF1A_BEGIN */
    TRACE_CPUPROFILER_EVENT_SCOPE(PERF1A_Enemy_HasCurrentBinding);
/* PERF1A_END */

    const auto* Source=BoundSource.Get();
    return bConfigured&&bLeaseRegistered&&IsValid(Source)&&GetOwner()==Source&&Source->GetWorld()==GetWorld()
        &&BoundBatch.IsValid()&&Source->Batch.BatchId==BoundBatch&&Source->bGeometryValidated
        &&FRogueliteDynamicGameplayBridge::IsComponentIsolated(Source,BoundBatch,GetCapsuleComponent())
        &&Source->GetTraversalSnapshot().bCurrent;
}
bool ARogueliteSliceEnemy::IsDormant() const {return bDormant&&!bActive&&Health&&Health->IsAlive();}
bool ARogueliteSliceEnemy::IsActive() const {return bActive&&Health&&Health->IsAlive()&&HasCurrentBinding();}
ARogueliteRuntimeLoopAssembly* ARogueliteSliceEnemy::GetDungeonSource() const {return BoundSource.Get();}
bool ARogueliteSliceEnemy::ContainsStandingPlayer(const ARogueliteSlicePlayer* PlayerTarget) const
{
    const auto* Source=BoundSource.Get();if(!Source||!IsValid(PlayerTarget))return false;
    const auto* Room=Source->Batch.Rooms.FindByPredicate([&](const auto& Row){return Row.NodeId==RoomNodeId;});
    if(!Room||!IsValid(Room->Room))return false;
    const FVector Local=Room->PlacedTransform.InverseTransformPosition(PlayerTarget->GetActorLocation());
    const auto* Capsule=PlayerTarget->GetCapsuleComponent();
    return FMath::Abs(Local.X)+Capsule->GetScaledCapsuleRadius()<=Room->Dimensions.X*.5
        &&FMath::Abs(Local.Y)+Capsule->GetScaledCapsuleRadius()<=Room->Dimensions.Y*.5
        &&Local.Z>=Capsule->GetScaledCapsuleHalfHeight()-3
        &&Local.Z+Capsule->GetScaledCapsuleHalfHeight()<=Room->Dimensions.Z+3;
}
bool ARogueliteSliceEnemy::Activate(ARogueliteSlicePlayer* PlayerTarget)
{
/* PERF1A_BEGIN */
    TRACE_CPUPROFILER_EVENT_SCOPE(PERF1A_Enemy_Activate);
/* PERF1A_END */

    if(!IsInGameThread()||!HasCurrentBinding()||!Health||!Health->IsAlive()||!IsValid(PlayerTarget)
        ||!PlayerTarget->HasLiveBinding()||PlayerTarget->GetDungeonSource()!=BoundSource.Get()
        ||PlayerTarget->GetBoundBatch()!=BoundBatch||!ContainsStandingPlayer(PlayerTarget))return false;
    if(bScopedDamage&&(!DamageEncounter.IsValid()||DamageEncounter->GetState()!=ERogueliteSliceEncounterState::CombatActive||DamageEncounter->GetTargetNodeId()!=RoomNodeId))return false;
    if(bActive)return Target.Get()==PlayerTarget;
    if(!bDormant)return false;
    Target=PlayerTarget;bDormant=false;bActive=true;
    NextAttackAt=GetWorld()->GetTimeSeconds()+FMath::Max(.1f,AttackInterval);return true;
}
float ARogueliteSliceEnemy::ReceiveShot(ARogueliteSlicePlayer* Shooter,FGuid ShotId)
{
    FRogueliteSliceShot Recorded;if(!IsValid(Shooter)||!Shooter->FindAcceptedShot(ShotId,Recorded))return 0;
    return ReceiveShot(Recorded);
}
float ARogueliteSliceEnemy::ReceiveShot(const FRogueliteSliceShot& Shot)
{
/* PERF1A_BEGIN */
    TRACE_CPUPROFILER_EVENT_SCOPE(PERF1A_Enemy_ReceiveRecordedShot);
/* PERF1A_END */

    auto* Shooter=Shot.Shooter.Get();FRogueliteSliceShot Recorded;
    if(!IsInGameThread()||!IsActive()||!IsValid(Shooter)||!Shooter->HasLiveBinding()
        ||Shooter->GetDungeonSource()!=BoundSource.Get()||Shooter->GetBoundBatch()!=BoundBatch
        ||!Shot.Id.IsValid()||!Shooter->FindAcceptedShot(Shot.Id,Recorded)||Recorded.Id!=Shot.Id
        ||Recorded.BatchId!=BoundBatch||Recorded.Shooter.Get()!=Shooter||!Recorded.bHit
        ||!Recorded.Hit.bBlockingHit||Recorded.Hit.GetActor()!=this)return 0;
    if(bScopedDamage&&(!DamageEncounter.IsValid()||!DamageEncounter->AuthorizesMemberDamage(this,Recorded)))return 0;
    // Damage and the hit come only from the actual accepted-shot ledger, never the supplied copy.
    return Health->ApplyDamageOnce(Recorded.Damage,Recorded.Id,Shooter);
}
void ARogueliteSliceEnemy::Tick(float DeltaSeconds)
{
/* PERF1A_BEGIN */
    TRACE_CPUPROFILER_EVENT_SCOPE(PERF1A_Enemy_Tick);
    TRACE_CPUPROFILER_EVENT_SCOPE_CONDITIONAL(PERF1A_Enemy_Dormant, bDormant && !bActive);
    TRACE_CPUPROFILER_EVENT_SCOPE_CONDITIONAL(PERF1A_Enemy_Active, bActive);
/* PERF1A_END */

    Super::Tick(DeltaSeconds);if(!bConfigured||!bLeaseRegistered)return;
    auto* Source=BoundSource.Get();
    if(!IsValid(Source)||Source->GetWorld()!=GetWorld()||Source->Batch.BatchId!=BoundBatch
        ||!Source->Batch.bSucceeded||!Source->bGeometryValidated||GetWorld()->bIsTearingDown)
    {Quiesce();return;}
    // Preserve collision evidence if an unauthorized change invalidated the fingerprint;
    // the original WatchGeometry, rather than this enemy tick, must handle that rejection.
    if(!HasCurrentBinding()){bActive=false;return;}
    auto* PlayerTarget=Target.Get();
    if(!bActive||!Health->IsAlive()||!IsValid(PlayerTarget)||!PlayerTarget->HasLiveBinding()
        ||PlayerTarget->GetDungeonSource()!=Source||PlayerTarget->GetBoundBatch()!=BoundBatch
        ||!ContainsStandingPlayer(PlayerTarget))return;

/* PERF1A_BEGIN */
    TRACE_CPUPROFILER_EVENT_SCOPE(PERF1A_Enemy_ActiveAttackWork);
/* PERF1A_END */
    FVector Aim=PlayerTarget->GetActorLocation()-GetActorLocation();Aim.Z=0;
    if(!Aim.IsNearlyZero())SetActorRotation(Aim.Rotation());
    const double Now=GetWorld()->GetTimeSeconds();if(Now<NextAttackAt)return;
    NextAttackAt=Now+FMath::Max(.1f,AttackInterval);
    const FVector Origin=GetActorLocation()+FVector(0,0,45);
    const FVector Goal=PlayerTarget->GetActorLocation()+FVector(0,0,35);
    if(FVector::DistSquared(Origin,Goal)>FMath::Square(3000.f))return;
    FCollisionQueryParams Query(SCENE_QUERY_STAT(G1B_EnemyShot),false,this);FHitResult Hit;
    if(GetWorld()->LineTraceSingleByChannel(Hit,Origin,Goal,ECC_Visibility,Query)&&Hit.GetActor()==PlayerTarget)
    {
        ++AttackCount;PlayerTarget->ReceiveTestDamage(AttackDamage,FGuid::NewGuid(),this);
        DrawDebugLine(GetWorld(),Origin,Hit.ImpactPoint,FColor::Red,false,.15f,0,1.f);
    }
}
void ARogueliteSliceEnemy::HealthKilled(URogueliteSliceHealth* Sender)
{
    if(Sender!=Health)return;Quiesce();TestBody->SetVisibility(false,true);
}
void ARogueliteSliceEnemy::Quiesce()
{
    FRogueliteGameplayObservationWrite ObservationWrite(AActor::GetWorld(),TEXT("ARogueliteSliceEnemy quiesce"));
    bActive=bDormant=false;Target.Reset();
    GetCharacterMovement()->StopMovementImmediately();GetCharacterMovement()->SetMovementMode(MOVE_None);
    GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    if(bLeaseRegistered)
    {
        bLeaseRegistered=false;
        if(auto* Source=BoundSource.Get())FRogueliteDynamicGameplayBridge::UnregisterComponent(Source,GetCapsuleComponent());
    }
}
void ARogueliteSliceEnemy::EndPlay(const EEndPlayReason::Type Reason)
{
    FRogueliteGameplayObservationWrite ObservationWrite(AActor::GetWorld(),TEXT("Enemy EndPlay"));
    Quiesce();if(Health)Health->OnKilled.RemoveAll(this);Super::EndPlay(Reason);
}

ARogueliteSliceGate::ARogueliteSliceGate()
{
    PrimaryActorTick.bCanEverTick=false;
    Blocker=CreateDefaultSubobject<UBoxComponent>(TEXT("G1BGameplayGateBlocker"));SetRootComponent(Blocker);
    Blocker->SetMobility(EComponentMobility::Movable);Blocker->InitBoxExtent(FVector(25,250,250));
    Blocker->SetCollisionObjectType(ECC_WorldDynamic);Blocker->SetCollisionResponseToAllChannels(ECR_Block);
    Blocker->SetCollisionEnabled(ECollisionEnabled::NoCollision);Blocker->SetCanEverAffectNavigation(false);
    Blocker->SetGenerateOverlapEvents(false);
    BarrierVisual=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("G1BGameplayGateVisual"));BarrierVisual->SetupAttachment(Blocker);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> GateVisualResource(TEXT("/Engine/BasicShapes/Cube.Cube"));
    if(GateVisualResource.Succeeded())BarrierVisual->SetStaticMesh(GateVisualResource.Object);
    BarrierVisual->SetMobility(EComponentMobility::Movable);BarrierVisual->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    BarrierVisual->SetCanEverAffectNavigation(false);BarrierVisual->SetVisibility(false,true);
}
bool ARogueliteSliceGate::Configure(ARogueliteRuntimeLoopAssembly* Source,FGuid Batch,int32 RoomNodeId,FGuid ConnectionId,FString& Why)
{
    using namespace RogueliteSliceDynamicEndpoints;
    if(bConfigured)
    {
        const bool Same=HasCurrentBinding()&&BoundSource.Get()==Source&&BoundBatch==Batch&&BoundRoomNode==RoomNodeId&&Connection==ConnectionId;
        Why=Same?TEXT("Gate already configured; no duplicate lease."):TEXT("Cannot reuse a gate across identities or after quiescence.");return Same;
    }
    if(!IsValid(Source)||Source->GetWorld()!=GetWorld()||GetOwner()!=Source||!GetActorScale3D().Equals(FVector::OneVector,.0001))
    {Why=TEXT("Gate requires actual Source ownership, same World and unit Actor scale.");return false;}
    FCommittedEndpoints Endpoints;if(!Resolve(Source,Batch,ConnectionId,Endpoints,Why))return false;
    if(RoomNodeId!=Endpoints.NodeA&&RoomNodeId!=Endpoints.NodeB)
    {Why=TEXT("Gate's room is not incident to the specified actual connection.");return false;}
    const auto& Socket=RoomNodeId==Endpoints.NodeA?Endpoints.A:Endpoints.B;
    FRogueliteGameplayObservationWrite ObservationWrite(AActor::GetWorld(),TEXT("Gate configuration"));
    Blocker->SetBoxExtent(FVector(25,Socket.DoorWidth*.5,Socket.DoorHeight*.5));
    SetActorLocationAndRotation(Socket.WorldPosition+FVector(0,0,Socket.DoorHeight*.5),FRotationMatrix::MakeFromX(Socket.WorldOutwardDirection).Rotator());
    BarrierVisual->SetRelativeScale3D(Blocker->GetUnscaledBoxExtent()/50.f);
    if(!FRogueliteDynamicGameplayBridge::Enable(Source,Batch,Why)
        ||!FRogueliteDynamicGameplayBridge::RegisterComponent(Source,Batch,this,Blocker,ERogueliteDynamicParticipant::Gate,Why))return false;
    BoundSource=Source;BoundBatch=Batch;Connection=ConnectionId;BoundRoomNode=RoomNodeId;
    RegisteredPose=Blocker->GetComponentTransform();bConfigured=bLeaseRegistered=true;bClosed=false;
    Why=TEXT("Open gate uses the actual incident C2 socket; only its movable Box is registered.");return true;
}
bool ARogueliteSliceGate::HasCurrentBinding() const
{
    auto* Source=BoundSource.Get();
    return bConfigured&&bLeaseRegistered&&IsValid(Source)&&GetOwner()==Source&&GetWorld()==Source->GetWorld()
        &&BoundBatch.IsValid()&&Source->Batch.BatchId==BoundBatch&&GetActorScale3D().Equals(FVector::OneVector,.0001)
        &&Blocker->GetComponentTransform().Equals(RegisteredPose,.0001)
        &&FRogueliteDynamicGameplayBridge::IsComponentIsolated(Source,BoundBatch,Blocker)
        &&Source->GetTraversalSnapshot().bCurrent&&Source->IsDungeonReady();
}
bool ARogueliteSliceGate::CanClose(FString& Why) const
{
    if(!IsInGameThread()||!HasCurrentBinding()){Why=TEXT("Gate no longer has a current Source/Batch/pose/component lease.");return false;}
    FCollisionObjectQueryParams PawnObjects;PawnObjects.AddObjectTypesToQuery(ECC_Pawn);
    FCollisionQueryParams Query(SCENE_QUERY_STAT(G1B_GateCloseOccupancy),false,this);
    TArray<FOverlapResult> Occupants;
    GetWorld()->OverlapMultiByObjectType(Occupants,Blocker->GetComponentLocation(),Blocker->GetComponentQuat(),PawnObjects,
        FCollisionShape::MakeBox(Blocker->GetScaledBoxExtent()+FVector(2,2,2)),Query);
    for(const auto& Occupant:Occupants)
        if(IsValid(Occupant.GetActor())&&Occupant.GetComponent()
            &&Occupant.GetComponent()->GetCollisionResponseToChannel(ECC_WorldDynamic)==ECR_Block)
        {Why=FString::Printf(TEXT("Gate cannot close: Pawn occupies actual doorway (%s)."),*Occupant.GetActor()->GetPathName());return false;}
    Why=TEXT("Actual doorway has no Pawn occupancy; floor and walls are not treated as characters.");return true;
}
bool ARogueliteSliceGate::SetClosed(bool Closed,FString& Why)
{
    if(!IsInGameThread()||!HasCurrentBinding()){Why=TEXT("Gate state change refused for stale Source/Batch or altered pose.");return false;}
    const bool PhysicalClosed=Blocker->GetCollisionEnabled()!=ECollisionEnabled::NoCollision;
    if(PhysicalClosed!=bClosed){Why=TEXT("Gate collision was changed outside the controlled setter.");return false;}
    if(bClosed==Closed){Why=TEXT("Gate is already in the requested state; no new revision.");return true;}
    if(Closed&&!CanClose(Why))return false;
    FRogueliteGameplayObservationWrite ObservationWrite(AActor::GetWorld(),TEXT("Gate controlled collision change"));
    Blocker->SetCollisionEnabled(Closed?ECollisionEnabled::QueryAndPhysics:ECollisionEnabled::NoCollision);
    BarrierVisual->SetVisibility(Closed,true);bClosed=Closed;
    FRogueliteDynamicGameplayBridge::NotifyGateCollisionChanged(Blocker);
    Why=Closed?TEXT("Actual dynamic gate blocks Pawn; G1A notified."):TEXT("Actual dynamic gate is open; fresh passage recovery is required.");return true;
}
bool ARogueliteSliceGate::IsClosed() const
{return bClosed&&Blocker&&Blocker->GetCollisionEnabled()!=ECollisionEnabled::NoCollision;}
UBoxComponent* ARogueliteSliceGate::GetBlocker() const {return Blocker.Get();}
void ARogueliteSliceGate::Quiesce()
{
    FRogueliteGameplayObservationWrite ObservationWrite(AActor::GetWorld(),TEXT("ARogueliteSliceGate quiesce"));
    const bool Changed=Blocker->GetCollisionEnabled()!=ECollisionEnabled::NoCollision;
    Blocker->SetCollisionEnabled(ECollisionEnabled::NoCollision);BarrierVisual->SetVisibility(false,true);bClosed=false;
    if(Changed)FRogueliteDynamicGameplayBridge::NotifyGateCollisionChanged(Blocker);
    if(bLeaseRegistered)
    {bLeaseRegistered=false;if(auto* Source=BoundSource.Get())FRogueliteDynamicGameplayBridge::UnregisterComponent(Source,Blocker);}
}
void ARogueliteSliceGate::EndPlay(const EEndPlayReason::Type Reason)
{FRogueliteGameplayObservationWrite ObservationWrite(AActor::GetWorld(),TEXT("Gate EndPlay"));Quiesce();Super::EndPlay(Reason);}

ARogueliteSliceRecoveryProbe::ARogueliteSliceRecoveryProbe()
{
    PrimaryActorTick.bCanEverTick=true;AutoPossessAI=EAutoPossessAI::Disabled;
    GetCapsuleComponent()->InitCapsuleSize(34,88);GetCapsuleComponent()->SetCollisionObjectType(ECC_Pawn);
    GetCapsuleComponent()->SetCollisionResponseToAllChannels(ECR_Block);GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Camera,ECR_Ignore);
    GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);GetCapsuleComponent()->SetCanEverAffectNavigation(false);
    GetMesh()->SetCollisionEnabled(ECollisionEnabled::NoCollision);GetMesh()->SetCanEverAffectNavigation(false);
    GetCharacterMovement()->SetMovementMode(MOVE_None);GetCharacterMovement()->bRunPhysicsWithNoController=false;
    GetCharacterMovement()->MaxWalkSpeed=600;GetCharacterMovement()->GetNavAgentPropertiesRef().AgentRadius=34;
    GetCharacterMovement()->GetNavAgentPropertiesRef().AgentHeight=176;
    ProbeVisual=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("G1BPassageRecoveryProbe"));ProbeVisual->SetupAttachment(GetCapsuleComponent());
    static ConstructorHelpers::FObjectFinder<UStaticMesh> ProbeVisualResource(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    if(ProbeVisualResource.Succeeded())ProbeVisual->SetStaticMesh(ProbeVisualResource.Object);
    ProbeVisual->SetMobility(EComponentMobility::Movable);ProbeVisual->SetRelativeScale3D(FVector(.5,.5,1.5));
    ProbeVisual->SetCollisionEnabled(ECollisionEnabled::NoCollision);ProbeVisual->SetCanEverAffectNavigation(false);
}
void ARogueliteSliceRecoveryProbe::Restart()
{
    FRogueliteGameplayObservationWrite ObservationWrite(AActor::GetWorld(),TEXT("Recovery probe Restart"));
    Super::Restart();
    if(!bLeaseRegistered){GetCharacterMovement()->StopMovementImmediately();GetCharacterMovement()->SetMovementMode(MOVE_None);}
}
bool ARogueliteSliceRecoveryProbe::Configure(ARogueliteRuntimeLoopAssembly* Source,FGuid Batch,FGuid ConnectionId,FString& Why)
{
    using namespace RogueliteSliceDynamicEndpoints;
    if(bConfigured){Why=TEXT("Recovery probes are single-identity objects; keep an existing successful probe registered until cleanup.");return false;}
    if(!IsValid(Source)||GetWorld()!=Source->GetWorld()||GetOwner()!=Source||GetController()
        ||!GetActorScale3D().Equals(FVector::OneVector,.0001))
    {Why=TEXT("Recovery probe requires Source ownership, no existing controller, same World and unit scale.");return false;}
    FCommittedEndpoints Endpoints;if(!Resolve(Source,Batch,ConnectionId,Endpoints,Why))return false;
    const FVector Start=Endpoints.A.WorldPosition-Endpoints.A.WorldOutwardDirection*300;
    const FVector Goal=Endpoints.B.WorldPosition-Endpoints.B.WorldOutwardDirection*300;
    if(!SupportedEndpoint(GetWorld(),this,Endpoints.Record.RoomA,Start,Why)
        ||!SupportedEndpoint(GetWorld(),this,Endpoints.Record.RoomB,Goal,Why))return false;
    FRogueliteGameplayObservationWrite ObservationWrite(AActor::GetWorld(),TEXT("Recovery probe configuration"));
    if(!FRogueliteDynamicGameplayBridge::Enable(Source,Batch,Why))return false;
    BoundSource=Source;BoundBatch=Batch;Connection=ConnectionId;StartFeet=Start;GoalFeet=Goal;
    SetActorLocationAndRotation(StartFeet+FVector(0,0,90),(GoalFeet-StartFeet).Rotation(),false,nullptr,ETeleportType::TeleportPhysics);
    FActorSpawnParameters Params;Params.Owner=Source;Params.ObjectFlags|=RF_Transient;
    auto* ControllerCreated=GetWorld()->SpawnActor<AAIController>(Params);
    if(!ControllerCreated){Why=TEXT("Source-owned recovery AIController could not spawn.");return false;}
    OwnedController=ControllerCreated;ControllerCreated->Possess(this);
    if(GetController()!=ControllerCreated||GetOwner()!=ControllerCreated||ControllerCreated->GetOwner()!=Source)
    {Why=TEXT("Recovery controller failed its real Source/possession/Owner contract.");Quiesce();return false;}
    if(!FRogueliteDynamicGameplayBridge::RegisterComponent(Source,Batch,this,GetCapsuleComponent(),ERogueliteDynamicParticipant::Pawn,Why))
    {Quiesce();return false;}
    bConfigured=bLeaseRegistered=true;GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    GetCharacterMovement()->SetMovementMode(MOVE_Walking);
    Why=TEXT("Explicit standing recovery probe registered at actual C2 RoomA start; BeginRecovery remains the caller's operation.");return true;
}
AAIController* ARogueliteSliceRecoveryProbe::GetRecoveryController() const {return OwnedController.Get();}
void ARogueliteSliceRecoveryProbe::Quiesce()
{
    FRogueliteGameplayObservationWrite ObservationWrite(AActor::GetWorld(),TEXT("ARogueliteSliceRecoveryProbe quiesce"));
    auto* Source=BoundSource.Get();auto* RecoveryController=OwnedController.Get();auto* World=GetWorld();
    // Capture the actual owned possession chain before UnPossess clears Pawn.Owner.
    // Only that controlled hand-back may restore Source ownership for batch cleanup.
    const bool CanReturnToSource=IsValid(Source)&&World&&!World->bIsTearingDown&&Source->GetWorld()==World
        &&IsValid(RecoveryController)&&RecoveryController->GetWorld()==World&&RecoveryController->GetOwner()==Source
        &&RecoveryController->GetPawn()==this&&GetController()==RecoveryController&&GetOwner()==RecoveryController;
    GetCharacterMovement()->StopMovementImmediately();GetCharacterMovement()->SetMovementMode(MOVE_None);
    GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    if(bLeaseRegistered)
    {bLeaseRegistered=false;if(Source)FRogueliteDynamicGameplayBridge::UnregisterComponent(Source,GetCapsuleComponent());}
    bool ReleasedOwnController=false;
    if(IsValid(RecoveryController))
    {
        if(IsValid(Source)&&World&&Source->GetWorld()==World&&RecoveryController->GetWorld()==World
            &&RecoveryController->GetOwner()==Source&&(!RecoveryController->GetPawn()||RecoveryController->GetPawn()==this))
        {
            RecoveryController->StopMovement();if(RecoveryController->GetPawn()==this)RecoveryController->UnPossess();
            ReleasedOwnController=RecoveryController->Destroy();
        }
    }
    OwnedController.Reset();
    if(CanReturnToSource&&ReleasedOwnController&&IsValid(Source)&&Source->GetWorld()==World&&GetWorld()==World
        &&!IsActorBeingDestroyed()&&!GetOwner()&&!GetController())SetOwner(Source);
}
void ARogueliteSliceRecoveryProbe::EndPlay(const EEndPlayReason::Type Reason)
{FRogueliteGameplayObservationWrite ObservationWrite(AActor::GetWorld(),TEXT("Recovery probe EndPlay"));Quiesce();Super::EndPlay(Reason);}
