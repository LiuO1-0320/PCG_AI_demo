#pragma once
#include "CoreMinimal.h"
#include "RogueliteLoopAwarePlanning.h"
#include "RogueliteLoopAwareTestRig.generated.h"

USTRUCT(BlueprintType)
struct ROGUELITEPROTOTYPE_API FRogueliteLoopAwareCase
{
    GENERATED_BODY()
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) FString Name;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) bool bPassed=false;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) bool bSceneRestored=false;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) FRogueliteLoopAwareEvidence Result;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) TArray<FRogueliteAssemblyResult> TreeValidations;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) TArray<FString> ActualGeometry;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) TArray<FGuid> ActualConnectionIds;
};

/** Independent Editor authoring/diagnostics. Never a runtime dungeon generator. */
UCLASS()
class ROGUELITEPROTOTYPE_API ARogueliteLoopAwareTestRig : public AActor
{
    GENERATED_BODY()
public:
    ARogueliteLoopAwareTestRig();
    virtual void BeginPlay() override;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Loop Aware|Source") FRogueliteSeedLayoutSettings Settings;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Loop Aware|Planning") FRogueliteLoopAwareSettings Planning;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Loop Aware|Batch") FRogueliteSeedBatch Batch;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Loop Aware|Batch") FRogueliteCommittedLoopBatch CommittedLoops;
    UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Transient,Category="Loop Aware|Result") FRogueliteLoopAwareEvidence Latest;
    UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Transient,Category="Loop Aware|Result") FRogueliteLoopTraversalSnapshot Traversal;
    UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Transient,Category="Loop Aware|Result") FString Summary;
    // Full probe arrays remain queryable by MCP/Blueprint, without building thousands of Details rows.
    UPROPERTY(BlueprintReadOnly,Transient,Category="Loop Aware|Tests") TArray<FRogueliteLoopAwareCase> Cases;
    UPROPERTY(BlueprintReadOnly,Transient,Category="Loop Aware|Tests") TArray<FRogueliteLoopAwareCase> BaselineCases;
    UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Transient,Category="Loop Aware|Tests") TArray<FString> Evidence;
    UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Transient,Category="Loop Aware|Tests") bool bChecksPassed=false;
    UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Transient,Category="Loop Aware|Tests") bool bBaselinePassed=false;
    UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Transient,Category="Loop Aware|Tests") bool bRuntimeMutationRefused=false;
    UFUNCTION(CallInEditor,Category="Loop Aware|Actions",meta=(DisplayName="Analyze Spatial Plan / 分析联合空间规划")) void AnalyzeSpatialPlan();
    UFUNCTION(CallInEditor,Category="Loop Aware|Actions",meta=(DisplayName="Generate Loop Aware Dungeon / 生成回环感知地牢")) void GenerateDungeon();
    UFUNCTION(CallInEditor,Category="Loop Aware|Actions",meta=(DisplayName="Validate Committed Loops / 验证真实连接")) void ValidateCommittedLoops();
    UFUNCTION(CallInEditor,Category="Loop Aware|Actions",meta=(DisplayName="Clear Owned Batch / 清理自有批次")) void ClearOwnedBatch();
    UFUNCTION(CallInEditor,Category="Loop Aware|Tests",meta=(DisplayName="Run Legacy 20 Seed Baseline / 复测旧版20种子")) void RunBaseline();
    UFUNCTION(CallInEditor,Category="Loop Aware|Tests",meta=(DisplayName="Run Loop Aware Checks / 执行回环感知测试")) void RunChecks();
private:
    bool OwnMap() const;
    TUniquePtr<FRogueliteLoopAwareSpatialPlan> PendingPlan;
};
