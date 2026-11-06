#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameFramework/Character.h"
#include "RogueliteCombatSliceActors.generated.h"

class ARogueliteRuntimeLoopAssembly;
class ARogueliteSlicePlayer;
class URogueliteSliceHealth;
class UStaticMeshComponent;
class UBoxComponent;
class AAIController;
struct FRogueliteSliceShot;

/** Reserved terminal reasons are data only; this slice implements damage/Killed only. */
UENUM()
enum class ERogueliteSliceMemberResolution : uint8 { None, Killed, Resolved, Recruited };
DECLARE_MULTICAST_DELEGATE_OneParam(FRogueliteSliceHealthKilled,URogueliteSliceHealth*);

/** Same-run health with accepted-damage identity and a single legal death notification. */
UCLASS(ClassGroup=(Roguelite))
class ROGUELITEPROTOTYPE_API URogueliteSliceHealth : public UActorComponent
{
    GENERATED_BODY()
public:
    URogueliteSliceHealth();
    virtual void BeginPlay() override;
    bool IsAlive() const;
    float GetHealth() const { return CurrentHealth; }
    float ApplyDamageOnce(float Amount,FGuid EventId,AActor* Causer);
    ERogueliteSliceMemberResolution GetResolution() const { return Resolution; }
    FRogueliteSliceHealthKilled OnKilled;
    UPROPERTY(EditAnywhere,Category="G1B Health",meta=(ClampMin="1")) float MaxHealth=100.f;
    UPROPERTY(VisibleInstanceOnly,Transient,Category="G1B Health") int32 DamageEvents=0;
private:
    UPROPERTY(VisibleInstanceOnly,Transient,Category="G1B Health") float CurrentHealth=100.f;
    UPROPERTY(VisibleInstanceOnly,Transient,Category="G1B Health") ERogueliteSliceMemberResolution Resolution=ERogueliteSliceMemberResolution::None;
    TSet<FGuid> AppliedEvents;
};

/** Independent fixed-position test enemy. No Castle, controller, BT, EQS or navigation AI. */
UCLASS()
class ROGUELITEPROTOTYPE_API ARogueliteSliceEnemy : public ACharacter
{
    GENERATED_BODY()
public:
    ARogueliteSliceEnemy();
    virtual void Tick(float DeltaSeconds) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    bool Configure(ARogueliteRuntimeLoopAssembly* Source,FGuid Batch,int32 NodeId,FGuid MemberId,
        const FTransform& Spawn,FString& Reason);
    bool Activate(ARogueliteSlicePlayer* PlayerTarget);
    float ReceiveShot(const FRogueliteSliceShot& Shot);
    float ReceiveShot(ARogueliteSlicePlayer* Shooter,FGuid ShotId);
    bool IsDormant() const;
    bool IsActive() const;
    FGuid GetMemberId() const { return EncounterMemberId; }
    int32 GetNodeId() const { return RoomNodeId; }
    ARogueliteRuntimeLoopAssembly* GetDungeonSource() const;
    FGuid GetBoundBatch() const { return BoundBatch; }
    void Quiesce();
    UPROPERTY(VisibleAnywhere,Category="G1B Enemy") TObjectPtr<URogueliteSliceHealth> Health;
    UPROPERTY(EditAnywhere,Category="G1B Enemy",meta=(ClampMin="0.1")) float AttackInterval=1.f;
    UPROPERTY(EditAnywhere,Category="G1B Enemy",meta=(ClampMin="0")) float AttackDamage=2.f;
    UPROPERTY(VisibleInstanceOnly,Transient,Category="G1B Enemy") int32 AttackCount=0;
private:
    bool HasCurrentBinding() const;
    bool ContainsStandingPlayer(const ARogueliteSlicePlayer* PlayerTarget) const;
    void HealthKilled(URogueliteSliceHealth* Sender);
    UPROPERTY(VisibleAnywhere,Category="G1B Enemy") TObjectPtr<UStaticMeshComponent> TestBody;
    TWeakObjectPtr<ARogueliteRuntimeLoopAssembly> BoundSource;
    TWeakObjectPtr<ARogueliteSlicePlayer> Target;
    FGuid BoundBatch,EncounterMemberId;
    int32 RoomNodeId=INDEX_NONE;
    bool bConfigured=false,bDormant=true,bActive=false,bLeaseRegistered=false;
    double NextAttackAt=0;
};

/** Independent dynamic barrier at one actual committed connection; never owns a reservation. */
UCLASS()
class ROGUELITEPROTOTYPE_API ARogueliteSliceGate : public AActor
{
    GENERATED_BODY()
public:
    ARogueliteSliceGate();
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    bool Configure(ARogueliteRuntimeLoopAssembly* Source,FGuid Batch,int32 RoomNodeId,FGuid ConnectionId,FString& Reason);
    bool CanClose(FString& Reason) const;
    bool SetClosed(bool Closed,FString& Reason);
    bool IsClosed() const;
    FGuid GetConnectionId() const { return Connection; }
    UBoxComponent* GetBlocker() const;
    void Quiesce();
private:
    bool HasCurrentBinding() const;
    UPROPERTY(VisibleAnywhere,Category="G1B Gate") TObjectPtr<UBoxComponent> Blocker;
    UPROPERTY(VisibleAnywhere,Category="G1B Gate") TObjectPtr<UStaticMeshComponent> BarrierVisual;
    TWeakObjectPtr<ARogueliteRuntimeLoopAssembly> BoundSource;
    FGuid BoundBatch,Connection;
    FTransform RegisteredPose=FTransform::Identity;
    int32 BoundRoomNode=INDEX_NONE;
    bool bConfigured=false,bLeaseRegistered=false,bClosed=false;
};

/** Explicit passage-verification agent, distinct from gameplay Player/Enemy and the old verifier. */
UCLASS()
class ROGUELITEPROTOTYPE_API ARogueliteSliceRecoveryProbe : public ACharacter
{
    GENERATED_BODY()
public:
    ARogueliteSliceRecoveryProbe();
    virtual void Restart() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    bool Configure(ARogueliteRuntimeLoopAssembly* Source,FGuid Batch,FGuid ConnectionId,FString& Reason);
    AAIController* GetRecoveryController() const;
    FVector GetStartFeet() const { return StartFeet; }
    FVector GetGoalFeet() const { return GoalFeet; }
    void Quiesce();
private:
    UPROPERTY(VisibleAnywhere,Category="G1B Recovery") TObjectPtr<UStaticMeshComponent> ProbeVisual;
    TWeakObjectPtr<ARogueliteRuntimeLoopAssembly> BoundSource;
    TWeakObjectPtr<AAIController> OwnedController;
    FGuid BoundBatch,Connection;
    FVector StartFeet=FVector::ZeroVector,GoalFeet=FVector::ZeroVector;
    bool bConfigured=false,bLeaseRegistered=false;
};
