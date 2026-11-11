#pragma once
#include "CoreMinimal.h"
#include "RogueliteRuntimeSeedAssembly.h"
#include "RogueliteLoopAwarePlanning.h"
#include "RogueliteNavigationReadinessComponent.h"
#include "RogueliteLoopAwareTestRig.h"
#include "RogueliteLoopCommitTestRig.h"
#include "RogueliteRuntimeLoopAssembly.generated.h"

class URogueliteFloorProfile;
struct FRogueliteReadyQueryProof;
struct FRogueliteGameplayObservationState;
struct FRogueliteGameplayObservationDeleter
{
    void operator()(FRogueliteGameplayObservationState* State) const;
};
struct FRogueliteGameplayObservationValue;
struct FRogueliteGameplayObservationWrite;
struct FObjectKey;

enum class ERogueliteGameplayReadMode : uint8 { DeveloperStrict, TrackedGameplay };
enum class ERogueliteGameplayObservationStatus : uint8 { ValidObservation, RequiresStrongValidation, InvalidRevoked };

UENUM()
enum class ERogueliteLoopTestPreset : uint8 { EightRoomSingleLoop, FourRoomTree };

USTRUCT(BlueprintType)
struct ROGUELITEPROTOTYPE_API FRogueliteRuntimeLoopResult
{
    GENERATED_BODY()
    UPROPERTY() int32 Seed=0;
    UPROPERTY() FString Stage;
    UPROPERTY() FString Reason;
    UPROPERTY() bool bSucceeded=false;
    UPROPERTY() bool bCleanupComplete=false;
    UPROPERTY() bool bProbeFreePlanning=false;
    UPROPERTY() bool bRegistrationPassed=false;
    UPROPERTY() float GeometrySeconds=0;
    UPROPERTY() FRogueliteLoopAwareEvidence Plan;
    UPROPERTY() TArray<FString> Registration;
    UPROPERTY() TArray<FString> ActualTransforms;
    UPROPERTY() TArray<FGuid> ConnectionIds;
    UPROPERTY() TArray<FRogueliteAssemblyResult> Connections;
    UPROPERTY() FRogueliteLoopTraversalSnapshot Traversal;
    UPROPERTY() TObjectPtr<AActor> TestObstacle;
};

enum class ERogueliteRuntimeLoopFault : uint8 { None, CancelLoopSpawn, RejectLoopC2, CancelAfterCommit };

class ROGUELITEPROTOTYPE_API FRogueliteRuntimeLoopAssembler
{
public:
    static bool Generate(AActor* Owner,const FRogueliteSeedLayoutSettings& Source,const FRogueliteLoopAwareSettings& Planning,
        const FRoguelitePreparedTemplateCatalog& Catalog,FRogueliteSeedBatch& Batch,FRogueliteCommittedLoopBatch& Loops,
        FRogueliteRuntimeLoopResult& Result,ERogueliteRuntimeLoopFault Fault=ERogueliteRuntimeLoopFault::None);
    static bool Clear(AActor* Owner,FRogueliteSeedBatch& Batch,FRogueliteCommittedLoopBatch& Loops,FString& Reason);
    static bool Revalidate(AActor* Owner,FRogueliteSeedBatch& Batch,const FRogueliteSeedLayoutSettings& Settings,
        FRogueliteCommittedLoopBatch& Loops,FString& Reason);
    // HF03-C profile path only; original Revalidate remains a legacy entry point.
    static bool RevalidateConfigured(AActor* Owner,FRogueliteSeedBatch& Batch,const FRogueliteSeedLayoutSettings& Settings,
        FRogueliteCommittedLoopBatch& Loops,FString& Reason);
    static bool Current(AActor* Owner,const FRogueliteSeedBatch& Batch,const FRogueliteSeedLayoutSettings& Settings,
        const FRogueliteCommittedLoopBatch& Loops);
    static FRogueliteSeedLayoutSettings SourcePreset(int32 Seed,bool Small=false);
private:
    friend class ARogueliteRuntimeLoopAssembly;
    // The same full Current implementation, with an optional move-only handoff.
    static bool CurrentWithTraversal(AActor* Owner,const FRogueliteSeedBatch& Batch,
        const FRogueliteSeedLayoutSettings& Settings,const FRogueliteCommittedLoopBatch& Loops,
        FRogueliteLoopTraversalSnapshot* OutTraversal);
};

USTRUCT(BlueprintType)
struct ROGUELITEPROTOTYPE_API FRogueliteRuntimeLoopCase
{
    GENERATED_BODY()
    UPROPERTY() FString Name;
    UPROPERTY() bool bPassed=false;
    UPROPERTY() bool bSceneRestored=false;
    UPROPERTY() FRogueliteRuntimeLoopResult Geometry;
    UPROPERTY() FRogueliteNavigationReadinessEvidence Navigation;
    UPROPERTY() FString Reason;
};

/** Test bootstrap, not Dungeon Workbench or a production floor/gameplay controller. */
UCLASS(HideCategories=("Roguelite|Runtime Settings","Roguelite|Runtime Result","Roguelite|B3A Test Only","Roguelite|Navigation"))
class ROGUELITEPROTOTYPE_API ARogueliteRuntimeLoopAssembly : public ARogueliteRuntimeSeedAssembly
{
    GENERATED_BODY()
public:
    ARogueliteRuntimeLoopAssembly();
    virtual ~ARogueliteRuntimeLoopAssembly() override;
    // Experimental passive-read policy. An observation never authorizes a critical gameplay action.
    void SetGameplayReadMode(ERogueliteGameplayReadMode Mode);
    ERogueliteGameplayReadMode GetGameplayReadMode() const;
    uint64 GetGameplayObservationEpoch() const;
    bool IsGameplayObservationEpochCurrent(uint64 Epoch) const;
    ERogueliteGameplayObservationStatus GetGameplayObservationStatus(FString& Reason) const;
    bool RefreshGameplayObservation(FString& Reason);
    bool GetGameplayObservedRoom(int32 NodeId,FTransform& Transform,FVector& Dimensions,uint64& Epoch) const;
    void RevokeGameplayObservation(const TCHAR* Reason);
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual bool GenerateRuntime() override;
    virtual bool ClearRuntime() override;
    virtual bool IsDungeonReady() const override;
    virtual TArray<FRogueliteSeedEdgeRecord> GetNavigationExtraEdges() const override;
    virtual FString GetNavigationEpoch() const override;
    virtual bool RevalidateRuntimeGeometry() override;
    virtual bool RequiresPhysicalCorridorNavigation() const override {return true;}
    UPROPERTY(EditAnywhere,Category="Dungeon",meta=(ToolTip="本轮测试Seed；不是正式楼层或玩法配置。")) int32 Seed=0;
    UPROPERTY(EditAnywhere,Category="Dungeon",meta=(ToolTip="Optional HF03-C shared FloorProfile. Null preserves the original test preset and all legacy behavior.")) TObjectPtr<URogueliteFloorProfile> FloorProfile;
    UPROPERTY(EditAnywhere,Category="Dungeon") ERogueliteLoopTestPreset Preset=ERogueliteLoopTestPreset::EightRoomSingleLoop;
    UPROPERTY(EditAnywhere,Category="Dungeon") bool bAutoGenerate=true;
    UPROPERTY(EditAnywhere,Category="Dungeon") bool bNavigationEnabled=true;
    UPROPERTY(VisibleInstanceOnly,Transient,Category="Dungeon|Status") FString RuntimeStatus=TEXT("Awaiting PIE");
    UPROPERTY(VisibleInstanceOnly,Transient,Category="Dungeon|Status") FString LastFailureReason;
    UPROPERTY(VisibleInstanceOnly,Transient,Category="Dungeon|Status") FString BatchSummary;
    UPROPERTY(VisibleInstanceOnly,Transient,Category="Dungeon|Status") FString NavigationSummary;
    UPROPERTY(VisibleInstanceOnly,Transient,Category="Dungeon|Status") FString MoveToSummary;
    UPROPERTY(EditAnywhere,AdvancedDisplay,Category="Dungeon|Advanced") FRogueliteLoopAwareSettings Planning;
    UPROPERTY(EditAnywhere,AdvancedDisplay,Category="Dungeon|Developer Tests") bool bRunValidationSuite=false;
    UPROPERTY(BlueprintReadOnly,Category="Dungeon|Developer Diagnostics") FRoguelitePreparedTemplateCatalog PreparedTemplates;
    UPROPERTY(BlueprintReadOnly,Transient,Category="Dungeon|Developer Diagnostics") FRogueliteCommittedLoopBatch CommittedLoops;
    UPROPERTY(BlueprintReadOnly,Transient,Category="Dungeon|Developer Diagnostics") FRogueliteRuntimeLoopResult LastGeometry;
    UPROPERTY(BlueprintReadOnly,Transient,Category="Dungeon|Developer Diagnostics") TArray<FRogueliteRuntimeLoopCase> Cases;
    UPROPERTY(BlueprintReadOnly,Transient,Category="Dungeon|Developer Diagnostics") TArray<FString> SuiteEvidence;
    UPROPERTY(BlueprintReadOnly,Transient,Category="Dungeon|Developer Diagnostics") bool bSuiteFinished=false;
    UPROPERTY(BlueprintReadOnly,Transient,Category="Dungeon|Developer Diagnostics") bool bSuitePassed=false;
    UPROPERTY(BlueprintReadOnly,Transient,Category="Dungeon|Developer Diagnostics") FString CurrentCase;
    UFUNCTION(CallInEditor,Category="Dungeon|Advanced",meta=(DisplayName="Prepare Template Catalog / 准备模板数据")) void PrepareTemplatesInEditor();
    UFUNCTION(CallInEditor,Category="Dungeon|Developer Tests",meta=(DisplayName="Run Editor Regressions / 验证原编辑器功能")) void RunEditorRegressions();
    UPROPERTY(BlueprintReadOnly,Transient,Category="Dungeon|Developer Diagnostics") bool bEditorRegressionsPassed=false;
    UPROPERTY(BlueprintReadOnly,Transient,Category="Dungeon|Developer Diagnostics") TArray<FString> EditorRegressionResults;
    UPROPERTY(BlueprintReadOnly,Transient,Category="Dungeon|Developer Diagnostics") TArray<FRogueliteLoopAwareCase> EditorLoopAwareCases;
    UPROPERTY(BlueprintReadOnly,Transient,Category="Dungeon|Developer Diagnostics") TArray<FRogueliteLoopCommitCase> EditorFixedCases;
    UFUNCTION(BlueprintPure,Category="Dungeon") FRogueliteLoopTraversalSnapshot GetTraversalSnapshot() const;
    void RunSuite();
private:
    friend class URogueliteNavigationReadinessComponent;
    friend struct FRogueliteGameplayObservationWrite;
    static void BeginGameplayObservationWrite(UWorld* World,const TCHAR* Reason);
    static void EndGameplayObservationWrite(FObjectKey World,bool bGameThread);
    ERogueliteGameplayObservationStatus CheckGameplayObservationDomain(FString& Reason) const;
    bool CaptureGameplayObservationIdentity(FRogueliteGameplayObservationValue& Value) const;
    void ObserveGameplayStrongResult(bool Ready) const;
    mutable TUniquePtr<FRogueliteGameplayObservationState,FRogueliteGameplayObservationDeleter> GameplayObservation;
#if WITH_DEV_AUTOMATION_TESTS
    friend struct FRogueliteReadyProofTestAccess;
    friend struct FRogueliteTraversalReuseTestAccess;
#endif
    static bool IsNativeReadyProofClass(const UClass* Class);
    bool CanReuseNativeTraversal(FString* Rejection=nullptr) const;
    bool TakeCurrentTraversal(FRogueliteReadyQueryProof& Request,FRogueliteLoopTraversalSnapshot& Out) const;
    bool ProduceReadyQueryProof(FRogueliteReadyQueryProof& Proof) const;
    FString FormatCurrentNavigationEpoch(const FTransform& SourceTransform) const;
    mutable bool bReadyQueryInProgress=false;
    mutable const FRogueliteReadyQueryProof* ActiveReadyQueryProof=nullptr;
    // RAII-bound to one GetTraversalSnapshot stack; never owns a saved result.
    mutable FRogueliteReadyQueryProof* ActiveTraversalRequest=nullptr;
    UFUNCTION() void ReadinessChanged(FGuid Id,ERogueliteDungeonBuildStage Value,bool Ready,const FString& Reason);
    void ConfigureSource();
    void PollSuite();
    void BeginNavigationCase();
    void WatchGeometry();
    FTimerHandle SuiteTimer,WatchTimer;
    int32 SuiteIndex=0;
    double CaseStarted=0;
    FString InitialScene;
    FString CaseInitialScene;
    TArray<FString> InitialSceneRows,CaseInitialSceneRows;
    TWeakObjectPtr<AActor> SuiteObstacle;
    bool bInterruptIssued=false;
};

