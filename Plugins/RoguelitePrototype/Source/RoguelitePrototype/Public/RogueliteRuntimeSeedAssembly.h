#pragma once
#include "CoreMinimal.h"
#include "RogueliteSeedAssembly.h"
#include "RogueliteRuntimeSeedAssembly.generated.h"

UENUM(BlueprintType)
enum class ERogueliteDungeonBuildStage : uint8
{
    Idle, GraphGenerated, SpatialPlanValid, ActorsSpawned, GeometryValidated, NavigationPending, NavigationReady, Failed,
    NavigationUnavailable, NavigationFailed, NavigationTimedOut
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams(FRogueliteDungeonReadinessChanged,FGuid,BatchId,ERogueliteDungeonBuildStage,Stage,bool,bReady,const FString&,Reason);

/** Native phased API. Each failure clears only this batch; navigation readiness is outside B3A. */
class ROGUELITEPROTOTYPE_API FRogueliteRuntimeSeedAssembler
{
public:
    static FRogueliteSeedBatch SpawnPlannedActors(AActor* Owner,const FRogueliteSeedSpatialPlan& Plan);
    static bool ValidateAndCommit(AActor* Owner,const FRogueliteSeedSpatialPlan& Plan,FRogueliteSeedBatch& Batch);
};

UCLASS(Blueprintable)
class ROGUELITEPROTOTYPE_API ARogueliteRuntimeSeedAssembly : public AActor
{
    GENERATED_BODY()
public:
    ARogueliteRuntimeSeedAssembly();
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Roguelite|Runtime Settings") FRogueliteSeedLayoutSettings Settings;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Roguelite|Runtime Settings") bool bGenerateOnBeginPlay=true;
    // Opt-in preserves the previously verified B3A geometry-only path and its maps.
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Roguelite|Runtime Settings") bool bEnableRuntimeNavigation=false;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Roguelite|Navigation") TObjectPtr<class URogueliteNavigationReadinessComponent> NavigationReadiness;
    UPROPERTY(BlueprintAssignable,Category="Roguelite|Navigation") FRogueliteDungeonReadinessChanged OnReadinessChanged;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Roguelite|B3A Test Only") bool bRunRuntimeChecks=false;
    UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Transient,Category="Roguelite|Runtime Result") FRogueliteSeedBatch Batch;
    UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Transient,Category="Roguelite|Runtime Result") ERogueliteDungeonBuildStage Stage=ERogueliteDungeonBuildStage::Idle;
    UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Transient,Category="Roguelite|Runtime Result") TArray<ERogueliteDungeonBuildStage> StageHistory;
    UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Transient,Category="Roguelite|Runtime Result") TArray<FString> Evidence;
    UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Transient,Category="Roguelite|Runtime Result") bool bGeometryValidated=false;
    UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Transient,Category="Roguelite|Runtime Result") bool bDungeonReady=false;
    UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Transient,Category="Roguelite|B3A Test Only") bool bRuntimeChecksPassed=false;
    UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Transient,Category="Roguelite|B3A Test Only") FRogueliteSeedBatch LastFailure;
    UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Transient,Category="Roguelite|B3A Test Only") TArray<FString> EditorRegressionEvidence;
    UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Transient,Category="Roguelite|B3A Test Only") bool bEditorRegressionPassed=false;
    UFUNCTION(CallInEditor,Category="Roguelite|B3A Test Only",meta=(DisplayName="Run Editor Regression / 验证编辑器回归")) void RunEditorRegression();
    UFUNCTION(BlueprintCallable,Category="Roguelite|Runtime Assembly") bool GenerateRuntime();
    UFUNCTION(BlueprintCallable,Category="Roguelite|Runtime Assembly") bool ClearRuntime();
    UFUNCTION(BlueprintPure,Category="Roguelite|Runtime Assembly") bool IsDungeonReady() const;
    void PublishNavigationState(FGuid BatchId,ERogueliteDungeonBuildStage Value,const FString& Reason);
    void RunRuntimeChecks();
private:
    void RecordStage(ERogueliteDungeonBuildStage Value);
    void GenerateAfterBeginPlay();
};
