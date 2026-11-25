#pragma once

#include "Components/ActorComponent.h"
#include "CastleMeleeCombatComponent.generated.h"

class ACastleDirector;
class ACastleEnemy;
class ACastlePlayer;
class AController;
class UAnimInstance;
class UAnimMontage;
class UAnimNotifyState;
class USkeletalMeshComponent;
struct FAnimMontageInstance;
struct FBranchingPointNotifyPayload;

UCLASS(ClassGroup=(Castle), meta=(BlueprintSpawnableComponent))
class CASTLEDEMO_API UCastleMeleeCombatComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    enum class EPhase : uint8 { Idle, Windup, Active, Recovery };

    UCastleMeleeCombatComponent();

    UPROPERTY(EditAnywhere, Category="Castle|Melee")
    TObjectPtr<UAnimMontage> StrikeMontage;

    UPROPERTY(EditAnywhere, Category="Castle|Melee", meta=(ClampMin="1", Units="cm"))
    float StartDistance = 145.f;

    UPROPERTY(EditAnywhere, Category="Castle|Melee", meta=(ClampMin="1", Units="cm"))
    float StrikeReach = 120.f;

    UPROPERTY(EditAnywhere, Category="Castle|Melee", meta=(ClampMin="1", Units="cm"))
    float StrikeRadius = 22.f;

    UPROPERTY(EditAnywhere, Category="Castle|Melee", meta=(ClampMin="1", Units="cm"))
    float MaxHeightDifference = 55.f;

    bool IsInAttackPosition(const ACastlePlayer* Player, bool bCanSee) const;
    bool TryStart(ACastlePlayer* Player, bool bCanSee);
    bool ShouldHoldMovement() const { return Phase != EPhase::Idle; }
    void Advance(ACastlePlayer* CurrentPlayer, float DeltaSeconds);
    void Cancel();

    EPhase GetPhase() const { return Phase; }
    uint64 GetAttackId() const { return CurrentAttackId; }
    int32 GetStartedCount() const { return StartedCount; }
    int32 GetWindowCount() const { return WindowCount; }
    int32 GetHitCount() const { return HitCount; }
    int32 GetNotifyBeginSampleCount() const { return NotifyBeginSampleCount; }
    int32 GetNotifyBeginHitCount() const { return NotifyBeginHitCount; }
    int32 GetTickSampleCount() const { return TickSampleCount; }
    int32 GetOutsideVolumeSampleCount() const { return OutsideVolumeSampleCount; }
    int32 GetSweepCount() const { return SweepCount; }
    int32 GetBlockedSweepCount() const { return BlockedSweepCount; }
    int32 GetMissedSweepCount() const { return MissedSweepCount; }
    int32 GetSameFrameWindowCount() const { return SameFrameWindowCount; }
    int32 GetCompletedCount() const { return CompletedCount; }
    int32 GetCanceledCount() const { return CanceledCount; }

protected:
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
    UFUNCTION()
    void OnNotifyBegin(FName Name, const FBranchingPointNotifyPayload& Payload);

    UFUNCTION()
    void OnNotifyEnd(FName Name, const FBranchingPointNotifyPayload& Payload);

    void OnMontageEnd(UAnimMontage* Montage, bool bInterrupted, uint64 AttackId, int32 InstanceId);
    void OnMontageBlendOut(UAnimMontage* Montage, bool bInterrupted, uint64 AttackId, int32 InstanceId);
    bool IsCurrentPlayback(const FBranchingPointNotifyPayload& Payload) const;
    bool IsCurrentContext(const ACastlePlayer* CurrentPlayer) const;
    void Finish(bool bCompleted, bool bStopPlayback);
    void SampleActiveWindow(ACastlePlayer& Player, const FAnimMontageInstance& Instance, bool bFromNotifyBegin);
    void AttemptHit(ACastleEnemy& Enemy, ACastlePlayer& Player, bool bFromNotifyBegin);

    TWeakObjectPtr<ACastlePlayer> Target;
    TWeakObjectPtr<ACastleDirector> Director;
    TWeakObjectPtr<AController> OwnerController;
    TWeakObjectPtr<AController> TargetController;
    TWeakObjectPtr<UWorld> AttackWorld;
    TWeakObjectPtr<USkeletalMeshComponent> AttackMesh;
    TWeakObjectPtr<UAnimInstance> AttackAnim;
    TWeakObjectPtr<UAnimMontage> AttackMontage;
    TWeakObjectPtr<UAnimNotifyState> AttackWindowNotify;
    FString AttackRegion;
    FVector StrikeDirection = FVector::ForwardVector;
    int32 AttackFloor = 0;
    int32 AttackRunSeed = 0;
    int32 MontageInstanceId = INDEX_NONE;
    float WindowStart = 0.f;
    float WindowEnd = 0.f;
    double WatchdogAt = 0.0;
    double RetryAfter = 0.0;
    uint64 LastAttackId = 0;
    uint64 CurrentAttackId = 0;
    uint64 WindowBeginFrame = 0;
    EPhase Phase = EPhase::Idle;
    bool bStarting = false;
    bool bResolved = false;
    bool bSampledAtTick = false;
    bool bReportedInvalidConfig = false;
    bool bChangedVisibilityTick = false;
    int32 StartedCount = 0;
    int32 WindowCount = 0;
    int32 HitCount = 0;
    int32 NotifyBeginSampleCount = 0;
    int32 NotifyBeginHitCount = 0;
    int32 TickSampleCount = 0;
    int32 OutsideVolumeSampleCount = 0;
    int32 SweepCount = 0;
    int32 BlockedSweepCount = 0;
    int32 MissedSweepCount = 0;
    int32 SameFrameWindowCount = 0;
    int32 CompletedCount = 0;
    int32 CanceledCount = 0;
};
