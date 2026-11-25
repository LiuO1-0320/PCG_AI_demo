#include "CastleMeleeCombatComponent.h"

#include "../CastleDemo.h"
#include "AlphaBlend.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "CoreGlobals.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"

DEFINE_LOG_CATEGORY_STATIC(LogCastleMelee, Log, All);

namespace
{
const FName HitWindowName(TEXT("CastleMeleeHitWindow"));
const FName MontageWindowClassName(TEXT("AnimNotify_PlayMontageNotifyWindow"));
const FName StrikeSlot(TEXT("DefaultSlot"));

bool IsStrikeSlotOccupied(const UAnimInstance& Anim)
{
    // The evaluation proxy can still show the previous frame. Check live instances, including a blend out.
    for (const FAnimMontageInstance* Instance : Anim.MontageInstances)
    {
        if (Instance && IsValid(Instance->Montage.Get()) && Instance->Montage->IsValidSlot(StrikeSlot) &&
            (Instance->IsActive() || Instance->GetWeight() > UE_SMALL_NUMBER))
        {
            return true;
        }
    }
    return false;
}

bool HasCompatibleStrikeSlot(const USkeletalMeshComponent* Mesh, const UAnimMontage* Montage)
{
    const USkeletalMesh* SkeletalMesh = IsValid(Mesh) ? Mesh->GetSkeletalMeshAsset() : nullptr;
    const UAnimInstance* Anim = IsValid(Mesh) ? Mesh->GetAnimInstance() : nullptr;
    return IsValid(SkeletalMesh) && IsValid(Anim) && !Anim->OnMontageStarted.IsBound() &&
        IsValid(Montage) && Montage->GetSkeleton() &&
        SkeletalMesh->GetSkeleton() == Montage->GetSkeleton() && Montage->SlotAnimTracks.Num() == 1 &&
        Montage->SlotAnimTracks[0].SlotName == StrikeSlot && Anim->GetSlotNodeGlobalWeight(StrikeSlot) > UE_SMALL_NUMBER;
}

const FAnimNotifyEvent* FindHitWindow(const UAnimMontage* Montage)
{
    if (!IsValid(Montage) || Montage->HasRootMotion() || Montage->bEnableRootMotionTranslation ||
        Montage->bEnableRootMotionRotation || !FMath::IsFinite(Montage->GetPlayLength()) ||
        !FMath::IsFinite(Montage->RateScale) || Montage->RateScale <= 0.f)
    {
        return nullptr;
    }

    const FAnimNotifyEvent* Window = nullptr;
    for (const FAnimNotifyEvent& Notify : Montage->Notifies)
    {
        const UAnimNotifyState* State = Notify.NotifyStateClass.Get();
        if (!State || State->GetClass()->GetFName() != MontageWindowClassName || FName(*State->GetNotifyName()) != HitWindowName)
        {
            continue;
        }
        if (Window)
        {
            return nullptr;
        }
        Window = &Notify;
    }
    if (!Window)
    {
        return nullptr;
    }
    const float Start = Window->GetTriggerTime();
    const float End = Window->GetEndTriggerTime();
    return Start > 0.f && End > Start && End < Montage->GetPlayLength() ? Window : nullptr;
}

bool HasLiveAttackParticipants(const ACastleEnemy* Enemy, const ACastlePlayer* Player)
{
    if (!IsValid(Enemy) || !IsValid(Player) || !Enemy->HasAuthority() || !Enemy->bUseMeleeV1 || Enemy->bBoss || Enemy->bElite ||
        Enemy->bDead || !Enemy->bActive || Enemy->Health <= 0.f || Player->bDead || Player->Health <= 0.f)
    {
        return false;
    }
    const ACastleDirector* Director = Enemy->Director;
    const UWorld* World = Enemy->GetWorld();
    return IsValid(World) && World == Player->GetWorld() && IsValid(Director) && Director->GetWorld() == World &&
        Director->bReady && !Director->bWon && IsValid(Enemy->GetController()) && IsValid(Player->GetController());
}

bool InRange(const ACastleEnemy& Enemy, const ACastlePlayer& Player, float Distance, float Height)
{
    const FVector Offset = Player.GetActorLocation() - Enemy.GetActorLocation();
    return FMath::Abs(Offset.Z) <= Height && Offset.SizeSquared2D() <= FMath::Square(Distance);
}
}

UCastleMeleeCombatComponent::UCastleMeleeCombatComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
}

bool UCastleMeleeCombatComponent::IsInAttackPosition(const ACastlePlayer* Player, bool bCanSee) const
{
    const ACastleEnemy* Enemy = Cast<ACastleEnemy>(GetOwner());
    if (!bCanSee || !HasLiveAttackParticipants(Enemy, Player) || Enemy->GetWorld()->GetTimeSeconds() < RetryAfter ||
        !FMath::IsFinite(StartDistance) || !FMath::IsFinite(MaxHeightDifference) ||
        !FMath::IsFinite(StrikeReach) || !FMath::IsFinite(StrikeRadius) ||
        StartDistance <= 0.f || MaxHeightDifference <= 0.f || StrikeReach <= 0.f || StrikeRadius <= 0.f ||
        !InRange(*Enemy, *Player, StartDistance, MaxHeightDifference))
    {
        return false;
    }
    const USkeletalMeshComponent* Mesh = Enemy->GetMesh();
    return HasCompatibleStrikeSlot(Mesh, StrikeMontage.Get()) && !IsStrikeSlotOccupied(*Mesh->GetAnimInstance()) &&
        FindHitWindow(StrikeMontage.Get());
}

bool UCastleMeleeCombatComponent::TryStart(ACastlePlayer* Player, bool bCanSee)
{
    ACastleEnemy* Enemy = Cast<ACastleEnemy>(GetOwner());
    if (!bCanSee || bStarting || Phase != EPhase::Idle || !HasLiveAttackParticipants(Enemy, Player) ||
        Enemy->GetWorld()->GetTimeSeconds() < RetryAfter || !InRange(*Enemy, *Player, StartDistance, MaxHeightDifference))
    {
        return false;
    }

    USkeletalMeshComponent* Mesh = Enemy->GetMesh();
    UAnimInstance* Anim = IsValid(Mesh) ? Mesh->GetAnimInstance() : nullptr;
    UAnimMontage* Montage = StrikeMontage.Get();
    if (IsValid(Anim) && IsStrikeSlotOccupied(*Anim))
    {
        return false;
    }
    const FAnimNotifyEvent* HitWindow = FindHitWindow(Montage);
    if (!IsInAttackPosition(Player, bCanSee) || !HitWindow)
    {
        if (!bReportedInvalidConfig)
        {
            if (IsValid(Anim) && Anim->OnMontageStarted.IsBound())
            {
                UE_LOG(LogCastleMelee, Warning, TEXT("%s: cannot safely identify StrikeMontage with a bound OnMontageStarted listener"), *GetNameSafe(Enemy));
            }
            else
            {
                UE_LOG(LogCastleMelee, Warning, TEXT("%s: unusable StrikeMontage or CastleMeleeHitWindow"), *GetNameSafe(Enemy));
            }
            bReportedInvalidConfig = true;
        }
        return false;
    }
    const FVector Direction = (Player->GetActorLocation() - Enemy->GetActorLocation()).GetSafeNormal2D();
    if (Direction.IsNearlyZero())
    {
        return false;
    }

    TGuardValue<bool> StartingGuard(bStarting, true);
    UWorld* World = Enemy->GetWorld();
    const uint64 AttackId = ++LastAttackId;
    CurrentAttackId = AttackId;
    Target = Player;
    Director = Enemy->Director.Get();
    OwnerController = Enemy->GetController();
    TargetController = Player->GetController();
    AttackWorld = World;
    AttackMesh = Mesh;
    AttackAnim = Anim;
    AttackMontage = Montage;
    AttackWindowNotify = HitWindow->NotifyStateClass.Get();
    WindowStart = HitWindow->GetTriggerTime();
    WindowEnd = HitWindow->GetEndTriggerTime();
    AttackFloor = Director->Floor;
    AttackRunSeed = Director->RunSeed;
    AttackRegion = Director->Region;
    StrikeDirection = Direction;
    bResolved = false;
    bSampledAtTick = false;
    Phase = EPhase::Windup;

    if (AAIController* Controller = Cast<AAIController>(OwnerController.Get()))
    {
        Controller->ClearFocus(EAIFocusPriority::Gameplay);
        Controller->StopMovement();
    }
    if (CurrentAttackId != AttackId || !IsCurrentContext(Player))
    {
        if (CurrentAttackId == AttackId)
        {
            Cancel();
        }
        return false;
    }
    UCharacterMovementComponent* Movement = Enemy->GetCharacterMovement();
    Movement->bOrientRotationToMovement = false;
    Movement->bUseControllerDesiredRotation = false;
    Movement->StopMovementImmediately();
    Enemy->SetActorRotation(Direction.Rotation());
    if (CurrentAttackId != AttackId || !IsCurrentContext(Player))
    {
        if (CurrentAttackId == AttackId)
        {
            Cancel();
        }
        return false;
    }

    // Recheck after StopMovement/SetActorRotation, which may invoke external callbacks.
    if (IsStrikeSlotOccupied(*Anim))
    {
        Cancel();
        return false;
    }
    if (Anim->OnMontageStarted.IsBound())
    {
        if (!bReportedInvalidConfig)
        {
            UE_LOG(LogCastleMelee, Warning, TEXT("%s: StrikeMontage playback ownership changed during startup"), *GetNameSafe(Enemy));
            bReportedInvalidConfig = true;
        }
        Cancel();
        return false;
    }

    TArray<int32, TInlineAllocator<4>> PriorInstanceIds;
    for (const FAnimMontageInstance* Existing : Anim->MontageInstances)
    {
        if (Existing && Existing->Montage == Montage)
        {
            PriorInstanceIds.Add(Existing->GetInstanceID());
        }
    }
    const float PlayDuration = Anim->Montage_Play(Montage, 1.f, EMontagePlayReturnType::Duration, 0.f, false);
    FAnimMontageInstance* Instance = nullptr;
    if (IsValid(Anim))
    {
        int32 MatchingNewInstances = 0;
        for (FAnimMontageInstance* Candidate : Anim->MontageInstances)
        {
            if (Candidate && Candidate->Montage == Montage && !PriorInstanceIds.Contains(Candidate->GetInstanceID()))
            {
                ++MatchingNewInstances;
                Instance = Candidate;
            }
        }
        if (MatchingNewInstances != 1 || Instance != Anim->GetActiveInstanceForMontage(Montage))
        {
            Instance = nullptr;
        }
    }
    if (CurrentAttackId != AttackId || !IsCurrentContext(Player))
    {
        if (CurrentAttackId == AttackId)
        {
            Cancel();
        }
        // A playback started during Montage_Play's synchronous callbacks need not be ours.
        return false;
    }
    if (!FMath::IsFinite(PlayDuration) || PlayDuration <= 0.f || !Instance)
    {
        if (!bReportedInvalidConfig)
        {
            UE_LOG(LogCastleMelee, Warning, TEXT("%s: StrikeMontage failed to play"), *GetNameSafe(Enemy));
            bReportedInvalidConfig = true;
        }
        Cancel();
        return false;
    }

    MontageInstanceId = Instance->GetInstanceID();
    WatchdogAt = World->GetTimeSeconds() + FMath::Min(PlayDuration + 0.75f, 8.f);
    // Keep this attack's Montage notifies running if the owner goes off-camera.
    if (Mesh->VisibilityBasedAnimTickOption == EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered)
    {
        bChangedVisibilityTick = true;
        Mesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::OnlyTickMontagesWhenNotRendered;
    }
    Anim->OnPlayMontageNotifyBegin.AddUniqueDynamic(this, &UCastleMeleeCombatComponent::OnNotifyBegin);
    Anim->OnPlayMontageNotifyEnd.AddUniqueDynamic(this, &UCastleMeleeCombatComponent::OnNotifyEnd);
    Instance->OnMontageEnded.BindUObject(this, &UCastleMeleeCombatComponent::OnMontageEnd, AttackId, MontageInstanceId);
    Instance->OnMontageBlendingOutStarted.BindUObject(this, &UCastleMeleeCombatComponent::OnMontageBlendOut, AttackId, MontageInstanceId);
    bReportedInvalidConfig = false;
    ++StartedCount;
    UE_LOG(LogCastleMelee, Verbose, TEXT("Start Enemy=%s Attack=%llu Instance=%d"), *GetNameSafe(Enemy), AttackId, MontageInstanceId);
    return true;
}

bool UCastleMeleeCombatComponent::IsCurrentContext(const ACastlePlayer* CurrentPlayer) const
{
    const ACastleEnemy* Enemy = Cast<ACastleEnemy>(GetOwner());
    ACastlePlayer* Player = Target.Get();
    const ACastleDirector* CurrentDirector = Director.Get();
    const USkeletalMeshComponent* Mesh = AttackMesh.Get();
    return Player == CurrentPlayer && HasLiveAttackParticipants(Enemy, Player) &&
        Enemy->GetWorld() == AttackWorld.Get() && Enemy->GetController() == OwnerController.Get() &&
        Player->GetController() == TargetController.Get() && IsValid(CurrentDirector) && Enemy->Director == CurrentDirector &&
        CurrentDirector->Floor == AttackFloor && CurrentDirector->RunSeed == AttackRunSeed && CurrentDirector->Region == AttackRegion &&
        IsValid(Mesh) && Mesh == Enemy->GetMesh() && Mesh->GetAnimInstance() && Mesh->GetAnimInstance() == AttackAnim.Get() &&
        Enemy->IsInDecisionRange(Player);
}

bool UCastleMeleeCombatComponent::IsCurrentPlayback(const FBranchingPointNotifyPayload& Payload) const
{
    return IsInGameThread() && CurrentAttackId != 0 && MontageInstanceId != INDEX_NONE && Payload.MontageInstanceID == MontageInstanceId &&
        Payload.SkelMeshComponent == AttackMesh.Get() && Payload.SequenceAsset == AttackMontage.Get() &&
        Payload.NotifyEvent && AttackWindowNotify.IsValid() && Payload.NotifyEvent->NotifyStateClass.Get() == AttackWindowNotify.Get() &&
        Payload.NotifyEvent->GetTriggerTime() == WindowStart && Payload.NotifyEvent->GetEndTriggerTime() == WindowEnd &&
        IsCurrentContext(Target.Get());
}

void UCastleMeleeCombatComponent::OnNotifyBegin(FName Name, const FBranchingPointNotifyPayload& Payload)
{
    if (Name == HitWindowName && Phase == EPhase::Windup && !Payload.bReachedEnd && IsCurrentPlayback(Payload))
    {
        Phase = EPhase::Active;
        WindowBeginFrame = GFrameCounter;
        ++WindowCount;
        UE_LOG(LogCastleMelee, Verbose, TEXT("Active Enemy=%s Attack=%llu Instance=%d"), *GetNameSafe(GetOwner()), CurrentAttackId, MontageInstanceId);

        // Native branching points are synchronous. One montage update may reach both bounds before Actor Tick runs again.
        if (const FAnimMontageInstance* Instance = AttackAnim->GetMontageInstanceForID(MontageInstanceId))
        {
            SampleActiveWindow(*Target.Get(), *Instance, true);
        }
    }
}

void UCastleMeleeCombatComponent::OnNotifyEnd(FName Name, const FBranchingPointNotifyPayload& Payload)
{
    if (Name == HitWindowName && Phase == EPhase::Active && Payload.bReachedEnd && IsCurrentPlayback(Payload))
    {
        if (WindowBeginFrame == GFrameCounter)
        {
            ++SameFrameWindowCount;
            UE_LOG(LogCastleMelee, Verbose, TEXT("SameFrameWindow Enemy=%s Attack=%llu Instance=%d Frame=%llu Resolved=%d"),
                *GetNameSafe(GetOwner()), CurrentAttackId, MontageInstanceId, GFrameCounter, bResolved);
        }
        Phase = EPhase::Recovery;
        UE_LOG(LogCastleMelee, Verbose, TEXT("Recovery Enemy=%s Attack=%llu Instance=%d"), *GetNameSafe(GetOwner()), CurrentAttackId, MontageInstanceId);
    }
}

void UCastleMeleeCombatComponent::OnMontageEnd(UAnimMontage* Montage, bool bInterrupted, uint64 AttackId, int32 InstanceId)
{
    if (AttackId != CurrentAttackId || InstanceId != MontageInstanceId || Montage != AttackMontage.Get())
    {
        return;
    }
    const bool bCompleted = !bInterrupted && Phase == EPhase::Recovery && IsCurrentContext(Target.Get());
    if (!bInterrupted && Phase != EPhase::Recovery && !bReportedInvalidConfig)
    {
        UE_LOG(LogCastleMelee, Warning, TEXT("%s: StrikeMontage ended without completing CastleMeleeHitWindow"), *GetNameSafe(GetOwner()));
        bReportedInvalidConfig = true;
    }
    Finish(bCompleted, false);
}

void UCastleMeleeCombatComponent::OnMontageBlendOut(UAnimMontage* Montage, bool bInterrupted, uint64 AttackId, int32 InstanceId)
{
    if (bInterrupted && AttackId == CurrentAttackId && InstanceId == MontageInstanceId && Montage == AttackMontage.Get())
    {
        Finish(false, false);
    }
}

void UCastleMeleeCombatComponent::Advance(ACastlePlayer* CurrentPlayer, float /*DeltaSeconds*/)
{
    if (Phase == EPhase::Idle || bStarting)
    {
        return;
    }
    if (!IsCurrentContext(CurrentPlayer) || AttackWorld->GetTimeSeconds() >= WatchdogAt)
    {
        Cancel();
        return;
    }

    const FAnimMontageInstance* Instance = AttackAnim->GetMontageInstanceForID(MontageInstanceId);
    if (!Instance || Instance->Montage != AttackMontage.Get())
    {
        Cancel();
        return;
    }
    SampleActiveWindow(*CurrentPlayer, *Instance, false);
}

void UCastleMeleeCombatComponent::SampleActiveWindow(ACastlePlayer& Player, const FAnimMontageInstance& Instance, bool bFromNotifyBegin)
{
    if (Phase != EPhase::Active || bResolved)
    {
        return;
    }

    const UWorld* World = AttackWorld.Get();
    const UAnimInstance* Anim = AttackAnim.Get();
    const float Position = Instance.GetPosition();
    const float StartTolerance = bFromNotifyBegin ? UE_KINDA_SMALL_NUMBER : 0.f;
    const bool bPlaybackValid = !bStarting && World && !World->bIsTearingDown && !World->IsPaused() &&
        World->GetTimeSeconds() < WatchdogAt && Anim && Instance.IsActive() && Instance.IsPlaying() &&
        Instance.GetPlayRate() > 0.f && Instance.GetInstanceID() == MontageInstanceId && Instance.Montage == AttackMontage.Get() &&
        Position >= WindowStart - StartTolerance && Position < WindowEnd && Instance.GetWeight() > UE_SMALL_NUMBER;

    // During Begin the instance's weight has been updated, but the graph has not yet evaluated this montage.
    // Verify the slot node itself here and keep the evaluated montage weight requirement on Actor Tick.
    float SlotWeight = 0.f;
    if (bPlaybackValid)
    {
        SlotWeight = bFromNotifyBegin ? Anim->GetSlotNodeGlobalWeight(StrikeSlot) : Anim->GetSlotMontageGlobalWeight(StrikeSlot);
    }
    if (!(SlotWeight > UE_SMALL_NUMBER))
    {
        if (bFromNotifyBegin)
        {
            UE_LOG(LogCastleMelee, Verbose,
                TEXT("OpeningSampleRejected Enemy=%s Attack=%llu Instance=%d Frame=%llu Position=%.6f PlaybackValid=%d InstanceWeight=%.4f SlotWeight=%.4f"),
                *GetNameSafe(GetOwner()), CurrentAttackId, MontageInstanceId, GFrameCounter, Position,
                bPlaybackValid, Instance.GetWeight(), SlotWeight);
        }
        return;
    }

    AttemptHit(*CastChecked<ACastleEnemy>(GetOwner()), Player, bFromNotifyBegin);
}

void UCastleMeleeCombatComponent::AttemptHit(ACastleEnemy& Enemy, ACastlePlayer& Player, bool bFromNotifyBegin)
{
    const bool bLogSample = bFromNotifyBegin || !bSampledAtTick;
    if (bFromNotifyBegin)
    {
        ++NotifyBeginSampleCount;
    }
    else
    {
        ++TickSampleCount;
        bSampledAtTick = true;
    }
    const TCHAR* SampleSource = bFromNotifyBegin ? TEXT("NotifyBegin") : TEXT("Tick");
    const UCapsuleComponent* Capsule = Player.GetCapsuleComponent();
    const FVector Start = Enemy.GetActorLocation();
    const FVector Delta = Capsule->GetComponentLocation() - Start;
    if (FMath::Abs(Delta.Z) > MaxHeightDifference || FVector::DotProduct(Delta, StrikeDirection) <= 0.f)
    {
        ++OutsideVolumeSampleCount;
        if (bLogSample)
        {
            UE_LOG(LogCastleMelee, Verbose, TEXT("StrikeSample Enemy=%s Attack=%llu Instance=%d Frame=%llu Source=%s Result=OutsideVolume"),
                *GetNameSafe(&Enemy), CurrentAttackId, MontageInstanceId, GFrameCounter, SampleSource);
        }
        return;
    }

    FHitResult Hit;
    FCollisionQueryParams Query(SCENE_QUERY_STAT(CastleMeleeStrike), false, &Enemy);
    ++SweepCount;
    const bool bBlocked = Enemy.GetWorld()->SweepSingleByChannel(Hit, Start, Start + StrikeDirection * StrikeReach,
        FQuat::Identity, ECC_Visibility, FCollisionShape::MakeSphere(StrikeRadius), Query);
    const bool bHitPlayer = bBlocked && Hit.GetActor() == &Player && Hit.GetComponent() == Capsule;
    if (!bBlocked)
    {
        ++MissedSweepCount;
    }
    else if (!bHitPlayer)
    {
        ++BlockedSweepCount;
    }
    if (bLogSample)
    {
        UE_LOG(LogCastleMelee, Verbose, TEXT("StrikeSample Enemy=%s Attack=%llu Instance=%d Frame=%llu Source=%s Result=%s Blocker=%s Component=%s"),
            *GetNameSafe(&Enemy), CurrentAttackId, MontageInstanceId, GFrameCounter, SampleSource,
            bHitPlayer ? TEXT("PlayerCapsule") : bBlocked ? TEXT("Blocked") : TEXT("Miss"),
            *GetNameSafe(Hit.GetActor()), *GetNameSafe(Hit.GetComponent()));
    }
    if (bHitPlayer)
    {
        // ApplyDamage can synchronously end an attack or destroy an actor.
        bResolved = true;
        ++HitCount;
        if (bFromNotifyBegin)
        {
            ++NotifyBeginHitCount;
        }
        UE_LOG(LogCastleMelee, Verbose, TEXT("Hit Enemy=%s Attack=%llu Instance=%d"), *GetNameSafe(&Enemy), CurrentAttackId, MontageInstanceId);
        UGameplayStatics::ApplyDamage(&Player, Enemy.AttackDamage, Enemy.GetController(), &Enemy, nullptr);
    }
}

void UCastleMeleeCombatComponent::Finish(bool bCompleted, bool bStopPlayback)
{
    if (Phase == EPhase::Idle)
    {
        return;
    }
    UAnimInstance* Anim = AttackAnim.Get();
    USkeletalMeshComponent* Mesh = AttackMesh.Get();
    UAnimMontage* Montage = AttackMontage.Get();
    const int32 InstanceId = MontageInstanceId;
    const uint64 AttackId = CurrentAttackId;
    if (!bCompleted)
    {
        if (const UWorld* World = AttackWorld.Get())
        {
            RetryAfter = World->GetTimeSeconds() + 0.35;
        }
        ++CanceledCount;
    }
    else
    {
        ++CompletedCount;
    }

    if (bChangedVisibilityTick && IsValid(Mesh) &&
        Mesh->VisibilityBasedAnimTickOption == EVisibilityBasedAnimTickOption::OnlyTickMontagesWhenNotRendered)
    {
        Mesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;
    }
    bChangedVisibilityTick = false;
    Phase = EPhase::Idle;
    CurrentAttackId = 0;
    MontageInstanceId = INDEX_NONE;
    Target.Reset();
    Director.Reset();
    OwnerController.Reset();
    TargetController.Reset();
    AttackWorld.Reset();
    AttackMesh.Reset();
    AttackAnim.Reset();
    AttackMontage.Reset();
    AttackWindowNotify.Reset();
    AttackRegion.Reset();
    UE_LOG(LogCastleMelee, Verbose, TEXT("Finish Enemy=%s Attack=%llu Instance=%d Completed=%d"),
        *GetNameSafe(GetOwner()), AttackId, InstanceId, bCompleted);

    if (!IsValid(Anim))
    {
        return;
    }
    Anim->OnPlayMontageNotifyBegin.RemoveDynamic(this, &UCastleMeleeCombatComponent::OnNotifyBegin);
    Anim->OnPlayMontageNotifyEnd.RemoveDynamic(this, &UCastleMeleeCombatComponent::OnNotifyEnd);
    if (FAnimMontageInstance* Instance = InstanceId != INDEX_NONE ? Anim->GetMontageInstanceForID(InstanceId) : nullptr;
        Instance && Instance->Montage == Montage)
    {
        Instance->OnMontageEnded.Unbind();
        Instance->OnMontageBlendingOutStarted.Unbind();
        if (bStopPlayback && Instance->IsActive())
        {
            Instance->Stop(FAlphaBlend(0.1f));
        }
    }
}

void UCastleMeleeCombatComponent::Cancel()
{
    Finish(false, true);
}

void UCastleMeleeCombatComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    Cancel();
    Super::EndPlay(EndPlayReason);
}
