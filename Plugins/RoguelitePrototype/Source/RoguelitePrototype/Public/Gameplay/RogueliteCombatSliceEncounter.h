#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "RogueliteDynamicGameplayBridge.h"
#include "RogueliteCombatSliceEncounter.generated.h"

class ARogueliteRuntimeLoopAssembly;
class ARogueliteSlicePlayer;
class ARogueliteSliceEnemy;
class ARogueliteSliceGate;
class ARogueliteSliceRecoveryProbe;
class URogueliteSliceHealth;
class UWorld;
struct FRogueliteSliceShot;
struct FRogueliteSeedRoomRecord;
struct FRogueliteRoomConnection;
struct FRogueliteEncounterSynchronousRead;

UENUM()
enum class ERogueliteSliceEncounterState : uint8
{ Uninitialized, AwaitingEntry, Preparation, CombatActive, Cleared, Cancelled };

/** One independently owned Normal Combat encounter over an existing runtime batch. */
UCLASS(ClassGroup=(Roguelite))
class ROGUELITEPROTOTYPE_API URogueliteSliceEncounter : public UActorComponent
{
    GENERATED_BODY()
public:
    URogueliteSliceEncounter();
    virtual void TickComponent(float DeltaTime,ELevelTick TickType,FActorComponentTickFunction* ThisTickFunction) override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    bool Initialize(ARogueliteRuntimeLoopAssembly* Source,ARogueliteSlicePlayer* Player,FString& Reason);
    /** G2 opt-in: one explicit qualified node; Legacy Initialize keeps its original selection. */
    bool InitializeRoom(ARogueliteRuntimeLoopAssembly* Source,ARogueliteSlicePlayer* Player,int32 NodeId,FString& Reason);
    bool AuthorizesMemberDamage(const ARogueliteSliceEnemy* Enemy,const FRogueliteSliceShot& Shot) const;
    bool IsCoordinated() const { return bCoordinated; }
    bool IsRecoveryBusy() const { return bRecoveryRequested&&!bParkFinished&&!bRecoveryTimedOut; }
    bool NeedsRecoveryService() const { return State==ERogueliteSliceEncounterState::Cleared&&!bParkFinished&&!bRecoveryTimedOut; }
    bool IsRecoveryParked() const { return bParkFinished; }
    bool SelectRecoveryConnection(FGuid Connection,FString& Reason);
    void AdvanceScheduledRecovery();
    void Shutdown(const FString& Reason);
    ERogueliteSliceEncounterState GetState() const { return State; }
    int32 GetTargetNodeId() const { return TargetNodeId; }
    int32 GetCurrentRoomNodeId() const { return CurrentRoomNodeId; }
    int32 GetRemainingEnemyCount() const;
    int32 GetActivationCount() const { return ActivationCount; }
    int32 GetClearCount() const { return ClearCount; }
    int32 GetSpawnedEnemyCount() const { return SpawnedEnemyCount; }
    int32 GetGameplayActorCount() const;
    FGuid GetEntryConnectionId() const { return EntryConnectionId; }
    FGuid GetExitConnectionId() const { return ExitConnectionId; }
    const TArray<FGuid>& GetIncidentConnectionIds() const { return IncidentConnectionIds; }
    TArray<TWeakObjectPtr<ARogueliteSliceEnemy>> GetEnemies() const { return Enemies; }
    ARogueliteSliceGate* GetGate(FGuid Connection) const;
    ERogueliteGameplayPassage GetExitPassageState() const;
    FString GetLastReason() const { return LastReason; }
    FString GetRecoveryDiagnostic() const { return LastRecoveryDiagnostic; }
    FString Describe() const;
    void SetSpawnFailureForValidation(bool Enabled) { bFailSecondSpawn=Enabled; }
private:
#if WITH_DEV_AUTOMATION_TESTS
    friend struct FRogueliteEncounterSyncReadTestAccess;
#endif
    bool CurrentIdentity(FString& Reason, FRogueliteEncounterSynchronousRead* Read = nullptr) const;
    bool CanStartSynchronousRead() const;
    FRogueliteEncounterSynchronousRead* ActiveSynchronousRead = nullptr;
    bool SelectTarget(FString& Reason,int32 RequestedNode=INDEX_NONE);
    bool SpawnMembersAndGates(FString& Reason);
    bool GroundPosition(const FRogueliteSeedRoomRecord& Room,const FVector& Requested,AActor* Ignore,
        bool ProtectDoors,FVector& OutCenter,FString& Reason) const;
    bool ContainsRoom(const FRogueliteSeedRoomRecord& Room,const FVector& Center,bool FullCapsule) const;
    int32 FindPlayerRoom(const FVector& Center,bool FullCapsule) const;
    bool ReadConnection(FGuid Id,FRogueliteRoomConnection& Out) const;
    void TrackEntry(float DeltaTime, FRogueliteEncounterSynchronousRead* Read = nullptr);
    bool EnterPreparation(FGuid Connection,FString& Reason);
    bool ValidateIncidentConnections(FString& Reason) const;
    bool ValidateClosedGate(FGuid Id,FString& Reason) const;
    bool CompleteRoster(FString& Reason) const;
    void AcceptedShot(const FRogueliteSliceShot& Shot);
    void MemberKilled(URogueliteSliceHealth* Health);
    void PlayerKilled(URogueliteSliceHealth* Health);
    UFUNCTION() void OwnedActorDestroyed(AActor* Actor);
    void CommitCleared();
    void UpdateRecovery();
    bool StartParking(FString& Reason);
    FString RecoveryDiagnostics(const FString& Detail,bool IncludeEvidence=false,FString* OutBlocker=nullptr) const;
    void AddOwned(AActor* Actor);
    void CleanOwned();

    UPROPERTY(VisibleInstanceOnly,Transient,Category="G1B Encounter") ERogueliteSliceEncounterState State=ERogueliteSliceEncounterState::Uninitialized;
    UPROPERTY(VisibleInstanceOnly,Transient,Category="G1B Encounter") FString LastReason;
    UPROPERTY(VisibleInstanceOnly,Transient,Category="G1B Encounter") int32 TargetNodeId=INDEX_NONE;
    UPROPERTY(VisibleInstanceOnly,Transient,Category="G1B Encounter") int32 CurrentRoomNodeId=INDEX_NONE;
    UPROPERTY(VisibleInstanceOnly,Transient,Category="G1B Encounter") int32 ActivationCount=0;
    UPROPERTY(VisibleInstanceOnly,Transient,Category="G1B Encounter") int32 ClearCount=0;
    UPROPERTY(VisibleInstanceOnly,Transient,Category="G1B Encounter") int32 SpawnedEnemyCount=0;
    TWeakObjectPtr<ARogueliteRuntimeLoopAssembly> BoundSource;
    TWeakObjectPtr<ARogueliteSlicePlayer> BoundPlayer;
    TWeakObjectPtr<UWorld> BoundWorld;
    FGuid BoundBatch,EntryConnectionId,ExitConnectionId,PendingEntryConnection;
    int32 SourceSeed=0,GraphSeed=0,GraphRevision=0,GeometryRevision=0,StartNodeId=INDEX_NONE,LastFullRoomNodeId=INDEX_NONE;
    FString TraversalDigest;
    FString LastRecoveryDiagnostic;
    TArray<FGuid> IncidentConnectionIds,MemberIds;
    TArray<TWeakObjectPtr<ARogueliteSliceEnemy>> Enemies;
    TArray<TWeakObjectPtr<AActor>> OwnedActors;
    TMap<FGuid,TWeakObjectPtr<ARogueliteSliceGate>> Gates;
    TSet<FGuid> KilledMembers,SeenShots;
    FVector PreviousPlayerPosition=FVector::ZeroVector,ParkFeet=FVector::ZeroVector;
    float PreviousPlayerSpeed2D=0;
    TWeakObjectPtr<ARogueliteSliceRecoveryProbe> RecoveryProbe;
    bool bCoordinated=false;
    bool bHavePreviousPosition=false,bCrossedEntryPlane=false,bCleaning=false,bFailSecondSpawn=false;
    bool bRecoveryRequested=false,bParkRequested=false,bParkFinished=false,bRecoveryTimedOut=false;
    int32 RecoveryAttempts=0;
    double RecoveryStartedAt=0,NextRecoveryAttemptAt=0,NextPreparationAttemptAt=0;
};
