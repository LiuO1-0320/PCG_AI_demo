#pragma once
#include "CoreMinimal.h"
#include "RogueliteRuntimeLoopAssembly.h"
#include "NavigationSystem.h"
#include "RogueliteCombatSliceRig.generated.h"

class ARogueliteSlicePlayer;
class ARogueliteSlicePlayerController;
class URogueliteSliceEncounter;

/** Selects and configures the existing navigation implementation for this sandbox World only. */
UCLASS()
class ROGUELITEPROTOTYPE_API URogueliteSliceNavigationConfig : public UNavigationSystemModuleConfig
{
    GENERATED_BODY()
public:
    URogueliteSliceNavigationConfig();
    virtual UNavigationSystemBase* CreateAndConfigureNavigationSystem(UWorld& World) const override;
};

/** Thin gameplay sandbox: all layout, registry and strict navigation remain original services. */
UCLASS()
class ROGUELITEPROTOTYPE_API ARogueliteCombatSliceRig : public ARogueliteRuntimeLoopAssembly
{
    GENERATED_BODY()
public:
    ARogueliteCombatSliceRig();
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual bool ClearRuntime() override;
    virtual bool GenerateRuntime() override;
    UPROPERTY(EditAnywhere,Category="Combat Slice") bool bEnableCombatEncounter=true;
    UPROPERTY(VisibleAnywhere,Category="Combat Slice") TObjectPtr<URogueliteSliceEncounter> Encounter;
    UPROPERTY(EditAnywhere,Category="G2 Two Rooms") bool bEnableTwoCombatRooms=false;
    UPROPERTY(VisibleAnywhere,Category="G2 Two Rooms") TObjectPtr<URogueliteSliceEncounter> SecondEncounter;
    UPROPERTY(VisibleInstanceOnly,Transient,Category="Combat Slice") bool bPlayerBridgeReady=false;
    UPROPERTY(VisibleInstanceOnly,Transient,Category="Combat Slice") FGuid GameplayBatchId;
    UPROPERTY(VisibleInstanceOnly,Transient,Category="Combat Slice") int32 PlayerStartNode=INDEX_NONE;
    UPROPERTY(VisibleInstanceOnly,Transient,Category="Combat Slice") FString SliceStatus=TEXT("Awaiting PIE");
    UPROPERTY(VisibleInstanceOnly,Transient,Category="Combat Slice") FString SliceFailure;
    ARogueliteSlicePlayer* GetSlicePlayer() const;
    TArray<URogueliteSliceEncounter*> GetRoomEncounters() const;
    URogueliteSliceEncounter* GetEncounterForNode(int32 NodeId) const;
    URogueliteSliceEncounter* GetActiveEncounter() const;
    int32 GetCurrentPlayerRoomNode() const;
    FGuid GetRecoveryConnectionId() const;
    bool RequestRoomRecovery(int32 RoomNodeId,FGuid Connection,FString& Reason);
private:
    void PollSlice();
    bool InitializeTwoRooms(FString& Reason);
    FGuid PreferredRecoveryConnection(int32 FromNode) const;
    void PollTwoRoomRecovery();
    FString DescribeTwoRooms() const;
    TWeakObjectPtr<URogueliteSliceEncounter> RecoveryOwner;
    bool bTwoRoomBatch=false;
    bool PlaceAndRegisterPlayer(FString& Reason);
    TWeakObjectPtr<ARogueliteSlicePlayer> Player;
    TWeakObjectPtr<ARogueliteSlicePlayerController> BoundController;
    FTimerHandle SliceTimer;
    double StartedAt=0;
    bool bGenerationRequested=false;
};
