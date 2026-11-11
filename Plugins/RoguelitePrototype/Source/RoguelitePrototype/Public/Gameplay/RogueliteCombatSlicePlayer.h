#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/HUD.h"
#include "GameFramework/PlayerController.h"
#include "TimerManager.h"
#include "RogueliteCombatSlicePlayer.generated.h"

class ARogueliteRuntimeLoopAssembly;
class ARogueliteSlicePlayer;
class UCameraComponent;
class UInputAction;
class UInputMappingContext;
class URogueliteSliceHealth;
struct FInputActionValue;

/** An accepted native weapon event. Hit resolution occurs after the accepted-event notification. */
struct ROGUELITEPROTOTYPE_API FRogueliteSliceShot
{
    FGuid Id, BatchId;
    TWeakObjectPtr<ARogueliteSlicePlayer> Shooter;
    FVector PawnLocation=FVector::ZeroVector,Origin=FVector::ZeroVector,Direction=FVector::ForwardVector;
    FHitResult Hit;
    bool bHit=false;
    float Damage=50.f;
};

DECLARE_MULTICAST_DELEGATE_OneParam(FRogueliteSliceShotAccepted,const FRogueliteSliceShot&);

/** Ordinary-operation diagnostics. These counters grant no Gameplay permission. */
enum class ERoguelitePlayerReadOperation : uint8 { Tick, Move, Look };
struct FRoguelitePlayerReadCounters
{
    uint64 Tracked[3]{}, Strong[3]{}, Rejected[3]{};
    uint64 RefreshAttempts=0, RefreshSucceeded=0, ReentryRejected=0, StrongBindingCalls=0;
};
/** Independent G1B FPS prototype. It waits without collision or gravity for a valid dungeon. */
UCLASS()
class ROGUELITEPROTOTYPE_API ARogueliteSlicePlayer : public ACharacter
{
    GENERATED_BODY()
public:
    ARogueliteSlicePlayer();
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    virtual void Restart() override;
    virtual void UnPossessed() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;

    bool BindToDungeon(ARogueliteRuntimeLoopAssembly* Source,const FTransform& Spawn,FString& Reason);
    void UnbindFromDungeon();
    bool HasLiveBinding() const;
    /** Consume immediately for Tick/Move/Look. Never a saved Ready result. */
    bool ReadOrdinaryGameplayBinding(ERoguelitePlayerReadOperation Operation);
    const FRoguelitePlayerReadCounters& GetOrdinaryReadCounters() const { return OrdinaryReadCounters; }
    FGuid GetBoundBatch() const { return BoundBatch; }
    ARogueliteRuntimeLoopAssembly* GetDungeonSource() const;
    URogueliteSliceHealth* GetHealthComponent() const { return HealthComponent; }
    bool FindAcceptedShot(FGuid Id,FRogueliteSliceShot& OutShot) const;
    bool TryFire();
    bool TryReload();
    void CancelReload(const FString& Reason=TEXT("Cancelled"));
    bool IsReloading() const { return bReloading; }
    float GetReloadRemainingSeconds() const;
    float ReceiveTestDamage(float Damage,FGuid EventId,AActor* Causer);
    const FRogueliteSliceShot& GetLastShot() const { return LastShot; }
    FRogueliteSliceShotAccepted OnShotAccepted;

    UPROPERTY(VisibleAnywhere,Category="G1B Player") TObjectPtr<UCameraComponent> FirstPersonCamera;
    UPROPERTY(VisibleAnywhere,Category="G1B Player") TObjectPtr<URogueliteSliceHealth> HealthComponent;
    UPROPERTY(VisibleInstanceOnly,Transient,Category="G1B Player") FString BindingStatus=TEXT("Waiting for dungeon");
    UPROPERTY(EditAnywhere,Category="G1B Weapon",meta=(ClampMin="0")) int32 Ammo=30;
    /** Slice test weapon only: unlimited reserve is not a formal ammunition economy. */
    UPROPERTY(EditAnywhere,Category="G1B Prototype Reload",meta=(ClampMin="1")) int32 MagazineCapacity=30;
    UPROPERTY(EditAnywhere,Category="G1B Prototype Reload",meta=(ClampMin="0.01",Units="s")) float ReloadDurationSeconds=1.5f;
    UPROPERTY(VisibleInstanceOnly,Transient,Category="G1B Prototype Reload") int32 ReloadStartedCount=0;
    UPROPERTY(VisibleInstanceOnly,Transient,Category="G1B Prototype Reload") int32 ReloadCompletedCount=0;
    UPROPERTY(VisibleInstanceOnly,Transient,Category="G1B Prototype Reload") int32 ReloadCancelledCount=0;
    UPROPERTY(VisibleInstanceOnly,Transient,Category="G1B Prototype Reload") FString ReloadStatus=TEXT("Prototype infinite reserve - R to reload");
    UPROPERTY(EditAnywhere,Category="G1B Weapon",meta=(ClampMin="0.05")) float FireIntervalSeconds=.25f;
    UPROPERTY(EditAnywhere,Category="G1B Weapon",meta=(ClampMin="1")) float ShotDamage=50.f;
    UPROPERTY(EditAnywhere,Category="G1B Weapon",meta=(ClampMin="1")) float WeaponRangeCm=10000.f;
    UPROPERTY(VisibleInstanceOnly,Transient,Category="G1B Weapon") int32 FireInputPressedCount=0;
    UPROPERTY(VisibleInstanceOnly,Transient,Category="G1B Weapon") int32 ShotAcceptedCount=0;
    UPROPERTY(VisibleInstanceOnly,Transient,Category="G1B Weapon") int32 ShotRejectedCount=0;
    UPROPERTY(VisibleInstanceOnly,Transient,Category="G1B Weapon") int32 HitConfirmedCount=0;
    UPROPERTY(VisibleInstanceOnly,Transient,Category="G1B Weapon") int32 PlayerDeathCount=0;
    UPROPERTY(VisibleInstanceOnly,Transient,Category="G1B Weapon") float LastAppliedDamage=0;
    UPROPERTY(VisibleInstanceOnly,Transient,Category="G1B Weapon") float TotalDamageDealt=0;
    UPROPERTY(VisibleInstanceOnly,Transient,Category="G1B Weapon") FString LastRejectedReason;
    UPROPERTY(VisibleInstanceOnly,Transient,Category="G1B Weapon") FString LastFireOutcome=TEXT("No shot fired");
    UPROPERTY(VisibleInstanceOnly,Transient,Category="G1B Gameplay") FString GameplaySummary;
private:
    void PlayerKilled(URogueliteSliceHealth* DeadHealth);
    void CompleteReload(FGuid RequestId);
    FTimerHandle ReloadTimer;
    UPROPERTY(VisibleInstanceOnly,Transient,Category="G1B Prototype Reload") bool bReloading=false;
    UPROPERTY(Transient) TWeakObjectPtr<UWorld> ReloadWorld;
    UPROPERTY(Transient) TWeakObjectPtr<ARogueliteRuntimeLoopAssembly> ReloadSource;
    FGuid ReloadBatch,ReloadRequest;
    int32 ReloadCapacity=0;
    UPROPERTY(Transient) TWeakObjectPtr<ARogueliteRuntimeLoopAssembly> BoundSource;
    FGuid BoundBatch;
    FRogueliteSliceShot LastShot;
    TMap<FGuid,FRogueliteSliceShot> AcceptedShots;
    mutable FRoguelitePlayerReadCounters OrdinaryReadCounters;
    bool bOrdinaryReadInProgress=false;
    double NextShotTime=0;
    bool bResolvingShot=false;
};

/** Native transient Enhanced Input configuration, local to this test controller. */
UCLASS()
class ROGUELITEPROTOTYPE_API ARogueliteSlicePlayerController : public APlayerController
{
    GENERATED_BODY()
public:
    ARogueliteSlicePlayerController();
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void SetupInputComponent() override;

    const UInputAction* GetMoveAction() const { return MoveAction; }
    const UInputAction* GetLookAction() const { return LookAction; }
    const UInputAction* GetJumpAction() const { return JumpAction; }
    const UInputAction* GetFireAction() const { return FireAction; }
    const UInputAction* GetReloadAction() const { return ReloadAction; }
    const UInputMappingContext* GetSliceMappingContext() const { return MappingContext; }

    UPROPERTY(VisibleInstanceOnly,Transient,Category="G1B Input") int32 MoveInputEvents=0;
    UPROPERTY(VisibleInstanceOnly,Transient,Category="G1B Input") int32 LookInputEvents=0;
    UPROPERTY(VisibleInstanceOnly,Transient,Category="G1B Input") int32 JumpInputEvents=0;
    UPROPERTY(VisibleInstanceOnly,Transient,Category="G1B Input") int32 FireInputEvents=0;
    UPROPERTY(VisibleInstanceOnly,Transient,Category="G1B Input") int32 ReloadInputEvents=0;
    UPROPERTY(VisibleInstanceOnly,Transient,Category="G1B Input") FVector2D LastMoveInput=FVector2D::ZeroVector;
    UPROPERTY(VisibleInstanceOnly,Transient,Category="G1B Input") FVector2D LastLookInput=FVector2D::ZeroVector;
protected:
    virtual void OnPossess(APawn* InPawn) override;
private:
    void CreateInputActions();
    void InstallInputContext();
    void Move(const FInputActionValue& Value);
    void Look(const FInputActionValue& Value);
    void StartJump(const FInputActionValue& Value);
    void FinishJump(const FInputActionValue& Value);
    void Fire(const FInputActionValue& Value);
    void Reload(const FInputActionValue& Value);
    UPROPERTY(Transient) TObjectPtr<UInputMappingContext> MappingContext;
    UPROPERTY(Transient) TObjectPtr<UInputAction> MoveAction;
    UPROPERTY(Transient) TObjectPtr<UInputAction> LookAction;
    UPROPERTY(Transient) TObjectPtr<UInputAction> JumpAction;
    UPROPERTY(Transient) TObjectPtr<UInputAction> FireAction;
    UPROPERTY(Transient) TObjectPtr<UInputAction> ReloadAction;
    bool bContextInstalled=false;
};

/** Minimal native diagnostic HUD; no assets or global UI configuration. */
UCLASS()
class ROGUELITEPROTOTYPE_API ARogueliteSliceHUD : public AHUD
{
    GENERATED_BODY()
public:
    virtual void DrawHUD() override;
};

/** Per-map GameMode only; never replaces project defaults. */
UCLASS()
class ROGUELITEPROTOTYPE_API ARogueliteSliceGameMode : public AGameModeBase
{
    GENERATED_BODY()
public:
    ARogueliteSliceGameMode();
};
