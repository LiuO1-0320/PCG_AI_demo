#pragma once
#include "RogueliteLoopPlanning.h"
#include "RogueliteRuntimeSeedAssembly.h"
#include "RogueliteLoopPlanningTestRig.generated.h"
USTRUCT(BlueprintType)
struct ROGUELITEPROTOTYPE_API FRogueliteLoopCaseEvidence
{
    GENERATED_BODY()
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) FString Case;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) int32 Seed=0;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) bool bTreeSucceeded=false;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) FString TreeReason;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) FString LayoutDigest;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) FRogueliteLoopPlan Plan;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) bool bSceneUnchanged=false;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) bool bCleanupComplete=false;
};
UCLASS()
class ROGUELITEPROTOTYPE_API ARogueliteLoopPlanningTestRig : public ARogueliteSeedAssembly
{
    GENERATED_BODY()
public:
    ARogueliteLoopPlanningTestRig();
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Loop Planning") FRogueliteLoopPlanningSettings LoopSettings;
    UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Transient,Category="Loop Planning") FRogueliteLoopPlan LoopPlan;
    UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Transient,Category="Loop Planning") FString Summary;
    UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Transient,Category="Loop Tests") bool bChecksPassed=false;
    UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Transient,Category="Loop Tests") TArray<FRogueliteLoopCaseEvidence> Cases;
    UFUNCTION(CallInEditor,Category="Loop Planning",meta=(DisplayName="Generate Tree Layout / 生成树布局")) void GenerateTreeLayout();
    UFUNCTION(CallInEditor,Category="Loop Planning",meta=(DisplayName="Analyze Loop Candidates / 分析回环候选")) void AnalyzeLoopCandidates();
    UFUNCTION(CallInEditor,Category="Loop Planning",meta=(DisplayName="Display Candidate Summary / 显示候选摘要")) void DisplayCandidateSummary();
    UFUNCTION(CallInEditor,Category="Loop Planning",meta=(DisplayName="Clear Owned Test Batch / 清理本测试批次")) void ClearOwnedTestBatch();
    UFUNCTION(CallInEditor,Category="Loop Tests",meta=(DisplayName="Run B4-B1 Checks / 执行回环规划测试")) void RunLoopChecks();
private:
    bool OwnMap() const;
    FRogueliteSeedBatch Fixture(FRogueliteSeedLayoutSettings& Source);
};
UCLASS()
class ROGUELITEPROTOTYPE_API ARogueliteRuntimeLoopPlanningTestRig : public ARogueliteRuntimeSeedAssembly
{
    GENERATED_BODY()
public:
    ARogueliteRuntimeLoopPlanningTestRig();
    virtual void BeginPlay() override;
    UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Transient,Category="Loop Runtime Test") FRogueliteLoopPlan LoopPlan;
    UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Transient,Category="Loop Runtime Test") bool bPlanningUnchanged=false;
private:
    void AnalyzeSpawnedTree();
};
