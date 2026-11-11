#include "Gameplay/RogueliteCombatSlicePlayer.h"

/* PERF1A_BEGIN */
#include "ProfilingDebugging/CpuProfilerTrace.h"
/* PERF1A_END */
#include "Gameplay/RogueliteCombatSliceActors.h"
#include "RogueliteDynamicGameplayBridge.h"
#include "RogueliteRuntimeLoopAssembly.h"
#include "Gameplay/RogueliteGameplayObservation.h"
#include "ProfilingDebugging/CountersTrace.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "EnhancedPlayerInput.h"
#include "Engine/LocalPlayer.h"
#include "Engine/Canvas.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "InputCoreTypes.h"
#include "DrawDebugHelpers.h"

TRACE_DECLARE_INT_COUNTER(PerfPlayerTracked, TEXT("PERF21B/Player/TrackedRead"));
TRACE_DECLARE_INT_COUNTER(PerfPlayerStrong, TEXT("PERF21B/Player/OrdinaryStrong"));
TRACE_DECLARE_INT_COUNTER(PerfPlayerDenied, TEXT("PERF21B/Player/ReadDenied"));
TRACE_DECLARE_INT_COUNTER(PerfPlayerStrongBinding, TEXT("PERF21B/Player/HasLiveBinding"));

namespace RogueliteSliceInput
{
void MapAxis(UInputMappingContext* Context,UInputAction* Action,FKey Key,bool Negate,bool Swizzle)
{
    auto& Mapping=Context->MapKey(Action,Key);
    if(Negate) Mapping.Modifiers.Add(NewObject<UInputModifierNegate>(Context));
    if(Swizzle) Mapping.Modifiers.Add(NewObject<UInputModifierSwizzleAxis>(Context));
}
}

ARogueliteSlicePlayer::ARogueliteSlicePlayer()
{
    PrimaryActorTick.bCanEverTick=true;
    AutoPossessAI=EAutoPossessAI::Disabled;
    OverrideInputComponentClass=UEnhancedInputComponent::StaticClass();
    GetCapsuleComponent()->InitCapsuleSize(34.f,88.f);
    GetCapsuleComponent()->SetCollisionObjectType(ECC_Pawn);
    GetCapsuleComponent()->SetCollisionResponseToAllChannels(ECR_Block);
    GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Camera,ECR_Ignore);
    GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    GetCapsuleComponent()->SetCanEverAffectNavigation(false);
    GetMesh()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    GetMesh()->SetCanEverAffectNavigation(false);
    GetCharacterMovement()->SetMovementMode(MOVE_None);
    GetCharacterMovement()->MaxWalkSpeed=600.f;
    GetCharacterMovement()->JumpZVelocity=420.f;
    GetCharacterMovement()->bOrientRotationToMovement=false;
    bUseControllerRotationYaw=true;
    FirstPersonCamera=CreateDefaultSubobject<UCameraComponent>(TEXT("G1BFirstPersonCamera"));
    FirstPersonCamera->SetupAttachment(GetCapsuleComponent());
    FirstPersonCamera->SetRelativeLocation(FVector(0,0,64));
    FirstPersonCamera->bUsePawnControlRotation=true;
    HealthComponent=CreateDefaultSubobject<URogueliteSliceHealth>(TEXT("G1BPlayerHealth"));
}

void ARogueliteSlicePlayer::BeginPlay()
{
    Super::BeginPlay();
    HealthComponent->OnKilled.AddUObject(this,&ARogueliteSlicePlayer::PlayerKilled);
}

void ARogueliteSlicePlayer::Restart()
{
    FRogueliteGameplayObservationWrite ObservationWrite(AActor::GetWorld(),TEXT("Player Restart"));
    Super::Restart();
    // Possess/client restart restores the engine default movement mode; keep the waiting pawn inert.
    if(!HasLiveBinding())UnbindFromDungeon();
}

bool ARogueliteSlicePlayer::BindToDungeon(ARogueliteRuntimeLoopAssembly* Source,const FTransform& Spawn,FString& Reason)
{
    if(!HealthComponent||!HealthComponent->IsAlive())
    {
        Reason=TEXT("A dead player cannot bind or respawn within this prototype run.");BindingStatus=Reason;return false;
    }
    const auto* PC=Cast<APlayerController>(GetController());
    if(!IsInGameThread()||!IsValid(Source)||!GetWorld()||!GetWorld()->IsGameWorld()
        ||Source->GetWorld()!=GetWorld()||!PC||!PC->IsLocalController()||!PC->GetLocalPlayer()
        ||PC->GetPawn()!=this||GetOwner()!=PC)
    {
        Reason=TEXT("G1B player binding requires its real possessed local PlayerController, same GameWorld and actual Pawn owner.");
        BindingStatus=Reason;return false;
    }
    if(!Source->IsDungeonReady()||!Source->GetTraversalSnapshot().bCurrent||!Source->Batch.BatchId.IsValid())
    {
        Reason=TEXT("G1B player waits for the source's original strict navigation Ready and current traversal.");
        BindingStatus=Reason;return false;
    }
    if(!GetActorScale3D().Equals(FVector::OneVector,.0001)||!Spawn.GetScale3D().Equals(FVector::OneVector,.0001)
        ||Spawn.ContainsNaN())
    {
        Reason=TEXT("G1B player spawn requires a finite, unit-scale transform; Actor scaling is unsupported.");
        BindingStatus=Reason;return false;
    }
    if(BoundSource.Get()==Source&&BoundBatch==Source->Batch.BatchId&&HasLiveBinding())
    {
        Reason=TEXT("G1B player is already bound to this current batch.");return true;
    }
    FCollisionQueryParams Query(SCENE_QUERY_STAT(G1B_PlayerSpawn),false,this);
    if(GetWorld()->OverlapBlockingTestByChannel(Spawn.GetLocation(),FQuat::Identity,ECC_Pawn,
        FCollisionShape::MakeCapsule(34.f,88.f),Query))
    {
        Reason=TEXT("G1B player spawn capsule overlaps existing blocking geometry.");
        BindingStatus=Reason;return false;
    }
    FRogueliteGameplayObservationWrite ObservationWrite(AActor::GetWorld(),TEXT("Player dungeon bind"));
    UnbindFromDungeon();
    if(!FRogueliteDynamicGameplayBridge::Enable(Source,Source->Batch.BatchId,Reason))
    {BindingStatus=Reason;return false;}
    SetActorLocationAndRotation(Spawn.GetLocation(),Spawn.GetRotation(),false,nullptr,ETeleportType::TeleportPhysics);
    if(!FRogueliteDynamicGameplayBridge::RegisterComponent(Source,Source->Batch.BatchId,this,GetCapsuleComponent(),
        ERogueliteDynamicParticipant::Pawn,Reason))
    {BindingStatus=Reason;return false;}
    BoundSource=Source;BoundBatch=Source->Batch.BatchId;
    AcceptedShots.Empty();LastShot=FRogueliteSliceShot();NextShotTime=0;
    GetCharacterMovement()->StopMovementImmediately();
    GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    GetCharacterMovement()->SetMovementMode(MOVE_Walking);
    GetController()->SetControlRotation(Spawn.Rotator());
    BindingStatus=TEXT("Ready - WASD / mouse / Space");
    Reason=TEXT("Actual local PlayerController ownership verified; exact root capsule registered before Pawn collision and walking.");
    return true;
}

void ARogueliteSlicePlayer::UnbindFromDungeon()
{
/* PERF1A_BEGIN */
    TRACE_CPUPROFILER_EVENT_SCOPE(PERF1A_Player_UnbindFromDungeon);
/* PERF1A_END */

    FRogueliteGameplayObservationWrite ObservationWrite(AActor::GetWorld(),TEXT("Player dungeon unbind"));
    CancelReload(TEXT("Dungeon binding ended"));
    GetCharacterMovement()->StopMovementImmediately();
    GetCharacterMovement()->SetMovementMode(MOVE_None);
    StopJumping();
    GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    if(auto* Source=BoundSource.Get()) FRogueliteDynamicGameplayBridge::UnregisterComponent(Source,GetCapsuleComponent());
    BoundSource.Reset();BoundBatch.Invalidate();BindingStatus=TEXT("Waiting for dungeon");
    AcceptedShots.Empty();LastShot=FRogueliteSliceShot();
}

ARogueliteRuntimeLoopAssembly* ARogueliteSlicePlayer::GetDungeonSource() const
{
    return BoundSource.Get();
}

bool ARogueliteSlicePlayer::FindAcceptedShot(FGuid Id,FRogueliteSliceShot& OutShot) const
{
/* PERF1A_BEGIN */
    TRACE_CPUPROFILER_EVENT_SCOPE(PERF1A_Player_FindAcceptedShot);
/* PERF1A_END */

    if(!Id.IsValid()||!HasLiveBinding())return false;
    if(const auto* Recorded=AcceptedShots.Find(Id);Recorded&&Recorded->Shooter.Get()==this&&Recorded->BatchId==BoundBatch)
    {OutShot=*Recorded;return true;}
    return false;
}

bool ARogueliteSlicePlayer::TryReload()
{
/* PERF1A_BEGIN */
    TRACE_CPUPROFILER_EVENT_SCOPE(PERF1A_Player_TryReload);
/* PERF1A_END */

    auto Reject=[this](const TCHAR* Why){ReloadStatus=Why;return false;};
    if(bReloading)return false; // Repeated input never resets or stacks the existing timer.
    UWorld* W=GetWorld();
    if(!W||W->bIsTearingDown||IsActorBeingDestroyed()||!HealthComponent||!HealthComponent->IsAlive()||!HasLiveBinding())
        return Reject(TEXT("Reload unavailable: no living player in a current dungeon"));
    if(bResolvingShot)return Reject(TEXT("Reload unavailable while a shot is resolving"));
    if(MagazineCapacity<=0||Ammo<0||Ammo>MagazineCapacity||!FMath::IsFinite(ReloadDurationSeconds)||ReloadDurationSeconds<=0)
        return Reject(TEXT("Invalid prototype reload configuration"));
    if(Ammo==MagazineCapacity)return Reject(TEXT("Magazine full - Prototype infinite reserve"));
    ReloadWorld=W;ReloadSource=BoundSource;ReloadBatch=BoundBatch;ReloadCapacity=MagazineCapacity;
    ReloadRequest=FGuid::NewGuid();bReloading=true;++ReloadStartedCount;
    ReloadStatus=TEXT("Reloading - Prototype infinite reserve");
    W->GetTimerManager().SetTimer(ReloadTimer,FTimerDelegate::CreateUObject(this,&ARogueliteSlicePlayer::CompleteReload,ReloadRequest),ReloadDurationSeconds,false);
    UE_LOG(LogTemp,Display,TEXT("G1B_RELOAD_START Player=%s Batch=%s Request=%s Ammo=%d Capacity=%d Duration=%.3f"),*GetName(),*ReloadBatch.ToString(),*ReloadRequest.ToString(),Ammo,ReloadCapacity,ReloadDurationSeconds);
    return true;
}

void ARogueliteSlicePlayer::CancelReload(const FString& Reason)
{
/* PERF1A_BEGIN */
    TRACE_CPUPROFILER_EVENT_SCOPE(PERF1A_Player_CancelReload);
/* PERF1A_END */

    const bool WasReloading=bReloading;
    if(UWorld* W=ReloadWorld.Get())W->GetTimerManager().ClearTimer(ReloadTimer);
    ReloadTimer.Invalidate();bReloading=false;ReloadWorld.Reset();ReloadSource.Reset();ReloadBatch.Invalidate();ReloadRequest.Invalidate();ReloadCapacity=0;
    if(WasReloading)
    {
        ++ReloadCancelledCount;ReloadStatus=TEXT("Reload cancelled: ")+Reason;
        UE_LOG(LogTemp,Display,TEXT("G1B_RELOAD_CANCEL Player=%s Ammo=%d Reason=%s"),*GetName(),Ammo,*Reason);
    }
}

float ARogueliteSlicePlayer::GetReloadRemainingSeconds() const
{
    const UWorld* W=ReloadWorld.Get();
    return bReloading&&W?FMath::Max(0.f,W->GetTimerManager().GetTimerRemaining(ReloadTimer)):0.f;
}

void ARogueliteSlicePlayer::CompleteReload(FGuid RequestId)
{
/* PERF1A_BEGIN */
    TRACE_CPUPROFILER_EVENT_SCOPE(PERF1A_Player_CompleteReload);
/* PERF1A_END */

    // A stale callback cannot clear a newer reload, or refill a different World/Source/Batch.
    if(!bReloading||RequestId!=ReloadRequest)return;
    UWorld* W=GetWorld();
    if(!W||W!=ReloadWorld.Get()||W->bIsTearingDown||IsActorBeingDestroyed()||!HealthComponent||!HealthComponent->IsAlive()
        ||!HasLiveBinding()||BoundSource!=ReloadSource||BoundBatch!=ReloadBatch||MagazineCapacity!=ReloadCapacity||Ammo<0||Ammo>ReloadCapacity)
    {CancelReload(TEXT("Original player/World/Source/Batch or magazine is no longer valid"));return;}
    const int32 Capacity=ReloadCapacity;const FGuid Batch=ReloadBatch;
    W->GetTimerManager().ClearTimer(ReloadTimer);ReloadTimer.Invalidate();bReloading=false;
    ReloadWorld.Reset();ReloadSource.Reset();ReloadBatch.Invalidate();ReloadRequest.Invalidate();ReloadCapacity=0;
    Ammo=Capacity;++ReloadCompletedCount;ReloadStatus=TEXT("Reload complete - Prototype infinite reserve");
    UE_LOG(LogTemp,Display,TEXT("G1B_RELOAD_COMPLETE Player=%s Batch=%s Ammo=%d"),*GetName(),*Batch.ToString(),Ammo);
}

bool ARogueliteSlicePlayer::TryFire()
{
/* PERF1A_BEGIN */
    TRACE_CPUPROFILER_EVENT_SCOPE(PERF1A_Player_TryFire);
/* PERF1A_END */

    auto Reject=[this](const FString& Why)
    {
        ++ShotRejectedCount;LastRejectedReason=Why;LastFireOutcome=TEXT("Rejected: ")+Why;LastAppliedDamage=0;
        UE_LOG(LogTemp,Display,TEXT("G1B_SHOT_REJECT Player=%s Reason=%s"),*GetName(),*Why);return false;
    };
    if(!GetWorld()||GetWorld()->bIsTearingDown||!HealthComponent||!HealthComponent->IsAlive())return Reject(TEXT("Player is not alive in an active world"));
    if(!HasLiveBinding())return Reject(TEXT("No current dungeon binding"));
    if(bResolvingShot)return Reject(TEXT("Another shot is still resolving"));
    if(bReloading)return Reject(TEXT("Reloading - wait, then press fire again"));
    if(Ammo<=0)return Reject(TEXT("Empty ammunition - press R to reload"));
    if(!FMath::IsFinite(FireIntervalSeconds)||FireIntervalSeconds<=0||!FMath::IsFinite(ShotDamage)||ShotDamage<=0
        ||!FMath::IsFinite(WeaponRangeCm)||WeaponRangeCm<=0)return Reject(TEXT("Invalid weapon configuration"));
    const double Now=GetWorld()->GetTimeSeconds();
    if(Now<NextShotTime)return Reject(TEXT("Shot interval has not elapsed"));
    if(!IsValid(FirstPersonCamera))return Reject(TEXT("First-person camera is unavailable"));
    TGuardValue<bool> ResolvingGuard(bResolvingShot,true);
    FRogueliteSliceShot Shot;
    Shot.Id=FGuid::NewGuid();Shot.BatchId=BoundBatch;Shot.Shooter=this;Shot.PawnLocation=GetActorLocation();
    Shot.Origin=FirstPersonCamera->GetComponentLocation();Shot.Direction=FirstPersonCamera->GetForwardVector().GetSafeNormal();Shot.Damage=ShotDamage;
    --Ammo;NextShotTime=Now+FireIntervalSeconds;++ShotAcceptedCount;LastAppliedDamage=0;LastRejectedReason.Empty();
    LastFireOutcome=TEXT("Shot accepted");LastShot=Shot;AcceptedShots.Add(Shot.Id,Shot);
    // A legitimate encounter can activate here before first-shot damage; the player never activates enemies itself.
    OnShotAccepted.Broadcast(Shot);
    if(!HasLiveBinding()||BoundBatch!=Shot.BatchId||!HealthComponent->IsAlive())
    {LastFireOutcome=TEXT("Shot accepted; binding invalidated before hit resolution");return true;}
    const FVector End=Shot.Origin+Shot.Direction*WeaponRangeCm;
    FCollisionQueryParams Query(SCENE_QUERY_STAT(G1B_PlayerShot),false,this);
    Shot.bHit=GetWorld()->LineTraceSingleByChannel(Shot.Hit,Shot.Origin,End,ECC_Visibility,Query);
    LastShot=Shot;AcceptedShots.Add(Shot.Id,Shot);
    if(auto* Enemy=Cast<ARogueliteSliceEnemy>(Shot.Hit.GetActor());Shot.bHit&&Enemy)
    {
        LastAppliedDamage=Enemy->ReceiveShot(this,Shot.Id);
        if(LastAppliedDamage>0){++HitConfirmedCount;TotalDamageDealt+=LastAppliedDamage;}
    }
    LastFireOutcome=LastAppliedDamage>0?FString::Printf(TEXT("Hit confirmed: %.0f damage"),LastAppliedDamage)
        :(Shot.bHit?TEXT("Shot accepted: world or non-damageable hit"):TEXT("Shot accepted: miss"));
    DrawDebugLine(GetWorld(),Shot.Origin,Shot.bHit?Shot.Hit.ImpactPoint:End,LastAppliedDamage>0?FColor::Green:FColor::Yellow,false,.25f,0,1.f);
    UE_LOG(LogTemp,Display,TEXT("G1B_SHOT Id=%s Batch=%s Hit=%d Target=%s Damage=%.1f Ammo=%d"),
        *Shot.Id.ToString(),*Shot.BatchId.ToString(),Shot.bHit,*GetNameSafe(Shot.Hit.GetActor()),LastAppliedDamage,Ammo);
    return true;
}

float ARogueliteSlicePlayer::ReceiveTestDamage(float Damage,FGuid EventId,AActor* Causer)
{
/* PERF1A_BEGIN */
    TRACE_CPUPROFILER_EVENT_SCOPE(PERF1A_Player_ReceiveTestDamage);
/* PERF1A_END */

    const auto* Enemy=Cast<ARogueliteSliceEnemy>(Causer);
    if(!HasLiveBinding()||!HealthComponent||!HealthComponent->IsAlive()||!Enemy||!Enemy->IsActive()
        ||Enemy->GetWorld()!=GetWorld()||Enemy->GetDungeonSource()!=BoundSource.Get()||Enemy->GetBoundBatch()!=BoundBatch)return 0;
    return HealthComponent->ApplyDamageOnce(Damage,EventId,Causer);
}

void ARogueliteSlicePlayer::PlayerKilled(URogueliteSliceHealth* DeadHealth)
{
    if(DeadHealth!=HealthComponent)return;
    ++PlayerDeathCount;UnbindFromDungeon();BindingStatus=TEXT("Dead - restart PIE for a new run");
}

bool ARogueliteSlicePlayer::HasLiveBinding() const
{
/* PERF1A_BEGIN */
    TRACE_CPUPROFILER_EVENT_SCOPE(PERF1A_Player_HasLiveBinding);
/* PERF1A_END */

    if(IsInGameThread())
    {
        ++OrdinaryReadCounters.StrongBindingCalls;
        TRACE_COUNTER_INCREMENT(PerfPlayerStrongBinding);
    }
    const auto* Source=BoundSource.Get();
    return IsValid(Source)&&Source->GetWorld()==GetWorld()&&BoundBatch.IsValid()&&Source->Batch.BatchId==BoundBatch
        &&FRogueliteDynamicGameplayBridge::IsComponentIsolated(Source,BoundBatch,GetCapsuleComponent())
        &&Source->GetTraversalSnapshot().bCurrent;
}

bool ARogueliteSlicePlayer::ReadOrdinaryGameplayBinding(ERoguelitePlayerReadOperation Operation)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(PERF21B_Player_OrdinaryRead);
    const int32 Index=static_cast<int32>(Operation);
    if(!IsInGameThread()||Index<0||Index>=3)return false;
    const auto Strong=[&]()
    {
        ++OrdinaryReadCounters.Strong[Index];
        TRACE_COUNTER_INCREMENT(PerfPlayerStrong);
        return HasLiveBinding();
    };
    const auto Reject=[&](const FString& Reason)
    {
        ++OrdinaryReadCounters.Rejected[Index];
        TRACE_COUNTER_INCREMENT(PerfPlayerDenied);
        BindingStatus=Reason;
        if(auto* Movement=GetCharacterMovement())Movement->StopMovementImmediately();
        ConsumeMovementInputVector();
        return false;
    };
    if(bOrdinaryReadInProgress)
    {
        ++OrdinaryReadCounters.ReentryRejected;
        return Reject(TEXT("Ordinary Player read reentry rejected"));
    }
    auto* Source=BoundSource.Get();
    if(!IsValid(Source)||Source->GetGameplayReadMode()!=ERogueliteGameplayReadMode::TrackedGameplay)
        return Strong();
    const auto Fallback=[&]() { return Strong()?true:Reject(TEXT("Fresh strict Player binding rejected")); };
    TGuardValue<bool> ReadGuard(bOrdinaryReadInProgress,true);
    const auto* PlayerController=GetController();
    const auto* Capsule=GetCapsuleComponent();
    const auto* Movement=GetCharacterMovement();
    // Unknown implementations preserve the original strong getter order.
    if(GetClass()!=ARogueliteSlicePlayer::StaticClass()
        ||!PlayerController||PlayerController->GetClass()!=ARogueliteSlicePlayerController::StaticClass()
        ||!Capsule||Capsule->GetClass()!=UCapsuleComponent::StaticClass()
        ||!Movement||Movement->GetClass()!=UCharacterMovementComponent::StaticClass()
        ||!IsValid(HealthComponent)||HealthComponent->GetClass()!=URogueliteSliceHealth::StaticClass()
        ||!IsValid(FirstPersonCamera)||FirstPersonCamera->GetClass()!=UCameraComponent::StaticClass())
        return Fallback();
    UWorld* World=AActor::GetWorld();
    if(!IsInGameThread()||!IsValid(World)||!World->IsGameWorld()||World->bIsTearingDown
        ||IsActorBeingDestroyed()||!BoundBatch.IsValid()||!HealthComponent->IsAlive()
        ||PlayerController->GetPawn()!=this||GetOwner()!=PlayerController||!PlayerController->IsLocalController()
        ||!static_cast<const APlayerController*>(PlayerController)->GetLocalPlayer()
        ||PlayerController->AActor::GetWorld()!=World||Capsule!=GetRootComponent()
        ||Capsule->GetOwner()!=this||Movement->GetOwner()!=this||Movement->UpdatedComponent!=Capsule
        ||HealthComponent->GetOwner()!=this||FirstPersonCamera->GetOwner()!=this
        ||Capsule->GetCollisionEnabled()!=ECollisionEnabled::QueryAndPhysics
        ||!Movement->IsActive()||(Movement->MovementMode!=MOVE_Walking&&Movement->MovementMode!=MOVE_Falling))
        return Reject(TEXT("Current Player ownership, health or movement qualification rejected"));
    FString Reason;
    auto Status=Source->GetGameplayObservationStatus(Reason);
    if(Status==ERogueliteGameplayObservationStatus::RequiresStrongValidation)
    {
        ++OrdinaryReadCounters.RefreshAttempts;
        if(Source->RefreshGameplayObservation(Reason))++OrdinaryReadCounters.RefreshSucceeded;
        Status=Source->GetGameplayObservationStatus(Reason);
        if(Status==ERogueliteGameplayObservationStatus::RequiresStrongValidation)
            return Fallback(); // Includes the frozen Cleared/Recovery strict domain.
    }
    if(Status!=ERogueliteGameplayObservationStatus::ValidObservation)return Reject(Reason);
    const uint64 Epoch=Source->GetGameplayObservationEpoch();
    // Status admits the exact Source implementation before any virtual lease getters.
    if(Source->AActor::GetWorld()!=World||Source->Batch.BatchId!=BoundBatch
        ||!FRogueliteDynamicGameplayBridge::IsComponentIsolated(Source,BoundBatch,Capsule)
        ||Epoch==0||Epoch!=Source->GetGameplayObservationEpoch())
        return Reject(TEXT("Player lease or current Observation epoch rejected"));
    ++OrdinaryReadCounters.Tracked[Index];
    TRACE_COUNTER_INCREMENT(PerfPlayerTracked);
    return true;
}
void ARogueliteSlicePlayer::Tick(float DeltaSeconds)
{
/* PERF1A_BEGIN */
    TRACE_CPUPROFILER_EVENT_SCOPE(PERF1A_Player_Tick);
/* PERF1A_END */

    Super::Tick(DeltaSeconds);
    if(BoundBatch.IsValid())
    {
        const auto* Source=BoundSource.Get();
        if(IsValid(Source)&&Source->GetGameplayReadMode()==ERogueliteGameplayReadMode::TrackedGameplay)
        {
            ReadOrdinaryGameplayBinding(ERoguelitePlayerReadOperation::Tick);
        }
        else if(!ReadOrdinaryGameplayBinding(ERoguelitePlayerReadOperation::Tick))UnbindFromDungeon();
    }
}

void ARogueliteSlicePlayer::UnPossessed()
{
    FRogueliteGameplayObservationWrite ObservationWrite(AActor::GetWorld(),TEXT("Player UnPossessed"));
    UnbindFromDungeon();Super::UnPossessed();
}

void ARogueliteSlicePlayer::EndPlay(const EEndPlayReason::Type Reason)
{
    FRogueliteGameplayObservationWrite ObservationWrite(AActor::GetWorld(),TEXT("Player EndPlay"));
    UnbindFromDungeon();Super::EndPlay(Reason);
}

ARogueliteSlicePlayerController::ARogueliteSlicePlayerController()
{
    OverridePlayerInputClass=UEnhancedPlayerInput::StaticClass();
    bShowMouseCursor=false;
}

void ARogueliteSlicePlayerController::CreateInputActions()
{
    if(MappingContext)return;
    MappingContext=NewObject<UInputMappingContext>(this,NAME_None,RF_Transient);
    MoveAction=NewObject<UInputAction>(this,NAME_None,RF_Transient);
    MoveAction->ValueType=EInputActionValueType::Axis2D;
    LookAction=NewObject<UInputAction>(this,NAME_None,RF_Transient);
    LookAction->ValueType=EInputActionValueType::Axis2D;
    JumpAction=NewObject<UInputAction>(this,NAME_None,RF_Transient);
    JumpAction->ValueType=EInputActionValueType::Boolean;
    FireAction=NewObject<UInputAction>(this,NAME_None,RF_Transient);
    FireAction->ValueType=EInputActionValueType::Boolean;
    ReloadAction=NewObject<UInputAction>(this,NAME_None,RF_Transient);
    ReloadAction->ValueType=EInputActionValueType::Boolean;
    using namespace RogueliteSliceInput;
    MapAxis(MappingContext,MoveAction,EKeys::D,false,false);
    MapAxis(MappingContext,MoveAction,EKeys::A,true,false);
    MapAxis(MappingContext,MoveAction,EKeys::W,false,true);
    MapAxis(MappingContext,MoveAction,EKeys::S,true,true);
    MapAxis(MappingContext,LookAction,EKeys::MouseX,false,false);
    MapAxis(MappingContext,LookAction,EKeys::MouseY,true,true);
    MappingContext->MapKey(JumpAction,EKeys::SpaceBar);
    MappingContext->MapKey(FireAction,EKeys::LeftMouseButton);
    MappingContext->MapKey(ReloadAction,EKeys::R);
}

void ARogueliteSlicePlayerController::SetupInputComponent()
{
    if(!InputComponent)
    {
        InputComponent=NewObject<UEnhancedInputComponent>(this,TEXT("G1BSliceControllerInput"));
        InputComponent->RegisterComponent();
    }
    Super::SetupInputComponent();CreateInputActions();
    if(auto* Enhanced=Cast<UEnhancedInputComponent>(InputComponent))
    {
        Enhanced->BindAction(MoveAction,ETriggerEvent::Triggered,this,&ARogueliteSlicePlayerController::Move);
        Enhanced->BindAction(LookAction,ETriggerEvent::Triggered,this,&ARogueliteSlicePlayerController::Look);
        Enhanced->BindAction(JumpAction,ETriggerEvent::Started,this,&ARogueliteSlicePlayerController::StartJump);
        Enhanced->BindAction(JumpAction,ETriggerEvent::Completed,this,&ARogueliteSlicePlayerController::FinishJump);
        Enhanced->BindAction(JumpAction,ETriggerEvent::Canceled,this,&ARogueliteSlicePlayerController::FinishJump);
        Enhanced->BindAction(FireAction,ETriggerEvent::Started,this,&ARogueliteSlicePlayerController::Fire);
        Enhanced->BindAction(ReloadAction,ETriggerEvent::Started,this,&ARogueliteSlicePlayerController::Reload);
    }
    InstallInputContext();
}

void ARogueliteSlicePlayerController::InstallInputContext()
{
    if(bContextInstalled||!IsLocalController())return;
    CreateInputActions();
    if(auto* Local=GetLocalPlayer())if(auto* Enhanced=Local->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
    {
        Enhanced->AddMappingContext(MappingContext,0);bContextInstalled=true;
    }
}

void ARogueliteSlicePlayerController::BeginPlay()
{
    Super::BeginPlay();InstallInputContext();
    if(IsLocalController())SetInputMode(FInputModeGameOnly());
}

void ARogueliteSlicePlayerController::OnPossess(APawn* InPawn)
{
    Super::OnPossess(InPawn);InstallInputContext();
}

void ARogueliteSlicePlayerController::EndPlay(const EEndPlayReason::Type Reason)
{
    if(bContextInstalled)if(auto* Local=GetLocalPlayer())if(auto* Enhanced=Local->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
        Enhanced->RemoveMappingContext(MappingContext);
    bContextInstalled=false;Super::EndPlay(Reason);
}

void ARogueliteSlicePlayerController::Move(const FInputActionValue& Value)
{
/* PERF1A_BEGIN */
    TRACE_CPUPROFILER_EVENT_SCOPE(PERF1A_Input_Move);
/* PERF1A_END */

    LastMoveInput=Value.Get<FVector2D>();++MoveInputEvents;
    if(auto* FPS=Cast<ARogueliteSlicePlayer>(GetPawn());FPS&&FPS->ReadOrdinaryGameplayBinding(ERoguelitePlayerReadOperation::Move))
    {
        const FRotator Yaw(0,GetControlRotation().Yaw,0);
        FPS->AddMovementInput(FRotationMatrix(Yaw).GetUnitAxis(EAxis::X),LastMoveInput.Y);
        FPS->AddMovementInput(FRotationMatrix(Yaw).GetUnitAxis(EAxis::Y),LastMoveInput.X);
    }
}

void ARogueliteSlicePlayerController::Look(const FInputActionValue& Value)
{
/* PERF1A_BEGIN */
    TRACE_CPUPROFILER_EVENT_SCOPE(PERF1A_Input_Look);
/* PERF1A_END */

    LastLookInput=Value.Get<FVector2D>();++LookInputEvents;
    if(auto* FPS=Cast<ARogueliteSlicePlayer>(GetPawn());FPS&&FPS->ReadOrdinaryGameplayBinding(ERoguelitePlayerReadOperation::Look))
    {AddYawInput(LastLookInput.X);AddPitchInput(LastLookInput.Y);}
}

void ARogueliteSlicePlayerController::StartJump(const FInputActionValue& Value)
{
/* PERF1A_BEGIN */
    TRACE_CPUPROFILER_EVENT_SCOPE(PERF1A_Input_StartJump);
/* PERF1A_END */

    ++JumpInputEvents;
    if(auto* FPS=Cast<ARogueliteSlicePlayer>(GetPawn());FPS&&FPS->HasLiveBinding()&&Value.Get<bool>())FPS->Jump();
}

void ARogueliteSlicePlayerController::FinishJump(const FInputActionValue& Value)
{
/* PERF1A_BEGIN */
    TRACE_CPUPROFILER_EVENT_SCOPE(PERF1A_Input_FinishJump);
/* PERF1A_END */

    if(auto* FPS=Cast<ARogueliteSlicePlayer>(GetPawn()))FPS->StopJumping();
}

void ARogueliteSlicePlayerController::Fire(const FInputActionValue& Value)
{
/* PERF1A_BEGIN */
    TRACE_CPUPROFILER_EVENT_SCOPE(PERF1A_Input_Fire);
/* PERF1A_END */

    ++FireInputEvents;
    if(auto* FPS=Cast<ARogueliteSlicePlayer>(GetPawn());FPS&&Value.Get<bool>())
    {++FPS->FireInputPressedCount;FPS->TryFire();}
}

void ARogueliteSlicePlayerController::Reload(const FInputActionValue& Value)
{
/* PERF1A_BEGIN */
    TRACE_CPUPROFILER_EVENT_SCOPE(PERF1A_Input_Reload);
/* PERF1A_END */

    ++ReloadInputEvents;
    if(auto* FPS=Cast<ARogueliteSlicePlayer>(GetPawn());FPS&&Value.Get<bool>())FPS->TryReload();
}

void ARogueliteSliceHUD::DrawHUD()
{
/* PERF1A_BEGIN */
    TRACE_CPUPROFILER_EVENT_SCOPE(PERF1A_HUD_DrawHUD);
/* PERF1A_END */

    Super::DrawHUD();
    if(!Canvas)return;
    const auto* FPS=PlayerOwner?Cast<ARogueliteSlicePlayer>(PlayerOwner->GetPawn()):nullptr;
    if(!FPS)return;
    const float CenterX=Canvas->SizeX*.5f,CenterY=Canvas->SizeY*.5f;
    DrawLine(CenterX-6,CenterY,CenterX+6,CenterY,FLinearColor::White,1.f);
    DrawLine(CenterX,CenterY-6,CenterX,CenterY+6,FLinearColor::White,1.f);
    const float HP=FPS->HealthComponent?FPS->HealthComponent->GetHealth():0;
    DrawText(FString::Printf(TEXT("HP %.0f    Ammo %d/%d    Shots %d / rejected %d / hits %d"),HP,FPS->Ammo,FPS->MagazineCapacity,
        FPS->ShotAcceptedCount,FPS->ShotRejectedCount,FPS->HitConfirmedCount),FLinearColor::White,20,20);
    const FString ReloadLine=FPS->IsReloading()?FString::Printf(TEXT("Reloading %.1fs | Prototype infinite reserve"),FPS->GetReloadRemainingSeconds())
        :(FPS->Ammo==0?TEXT("Empty - press R to reload | Prototype infinite reserve"):TEXT("R: reload | Prototype infinite reserve"));
    DrawText(ReloadLine,FLinearColor::Yellow,20,42);
    DrawText(FPS->BindingStatus,FLinearColor::White,20,64);
    DrawText(FPS->LastFireOutcome,FLinearColor::White,20,86);
    if(!FPS->GameplaySummary.IsEmpty())DrawText(FPS->GameplaySummary,FLinearColor::White,20,108);
}

ARogueliteSliceGameMode::ARogueliteSliceGameMode()
{
    DefaultPawnClass=ARogueliteSlicePlayer::StaticClass();
    PlayerControllerClass=ARogueliteSlicePlayerController::StaticClass();
    HUDClass=ARogueliteSliceHUD::StaticClass();
}
