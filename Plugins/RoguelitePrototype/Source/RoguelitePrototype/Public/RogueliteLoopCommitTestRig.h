#pragma once
#include "CoreMinimal.h"
#include "RogueliteLoopCommit.h"
#include "RogueliteLoopCommitTestRig.generated.h"

/** Independent native fixture only; no changes to Warehouse/Junction3 Blueprint assets. */
UCLASS()
class ROGUELITEPROTOTYPE_API ARogueliteFixedLoopFixtureRoom : public ARogueliteRoomTemplate
{
    GENERATED_BODY()
public:
    ARogueliteFixedLoopFixtureRoom();
    UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> FixturePOI;
};

USTRUCT(BlueprintType)
struct ROGUELITEPROTOTYPE_API FRogueliteLoopCommitCase
{
    GENERATED_BODY()
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) FString Name;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) bool bExpectedSuccess=false;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) bool bPassed=false;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) bool bSceneRestored=false;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) FRogueliteFixedLoopCommitResult Result;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) FRogueliteLoopTraversalSnapshot Traversal;
};

UCLASS()
class ROGUELITEPROTOTYPE_API ARogueliteLoopCommitTestRig : public AActor
{
    GENERATED_BODY()
public:
    ARogueliteLoopCommitTestRig();
    virtual void BeginPlay() override;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Loop Fixture") int32 FixtureSeed=777;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Loop Fixture",meta=(ClampMin="5",ClampMax="6")) int32 FixtureRoomCount=5;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Loop Fixture") FRogueliteLoopPlanningSettings LoopSettings;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Loop Fixture") FRogueliteSeedLayoutSettings Settings;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Loop Fixture") FRogueliteSeedBatch Batch;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Loop Commit") FRogueliteCommittedLoopBatch CommittedLoops;
    UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Transient,Category="Loop Commit") FRogueliteLoopPlan Plan;
    UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Transient,Category="Loop Commit") int32 PlanGeometryRevision=0;
    UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Transient,Category="Loop Commit") FRogueliteFixedLoopCommitResult LastCommit;
    UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Transient,Category="Loop Commit") FRogueliteLoopTraversalSnapshot Traversal;
    UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Transient,Category="Loop Commit") FString Summary;
    UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Transient,Category="Loop Tests") TArray<FRogueliteLoopCommitCase> Cases;
    UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Transient,Category="Loop Tests") TArray<FString> Evidence;
    UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Transient,Category="Loop Tests") bool bChecksPassed=false;
    UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Transient,Category="Loop Tests") bool bRuntimeMutationRefused=false;
    UFUNCTION(CallInEditor,Category="Loop Commit",meta=(DisplayName="Build Fixed Tree Fixture / 创建固定树夹具")) void BuildFixture();
    UFUNCTION(CallInEditor,Category="Loop Commit",meta=(DisplayName="Analyze Fixed Loop / 分析固定回环")) void AnalyzeLoop();
    UFUNCTION(CallInEditor,Category="Loop Commit",meta=(DisplayName="Commit Fixed Loop / 提交实体回环")) void CommitLoop();
    UFUNCTION(CallInEditor,Category="Loop Commit",meta=(DisplayName="Revalidate Committed Loop / 验证已提交回环")) void RevalidateLoop();
    UFUNCTION(CallInEditor,Category="Loop Commit",meta=(DisplayName="Disconnect Owned Loop / 释放自有回环")) void DisconnectLoop();
    UFUNCTION(CallInEditor,Category="Loop Commit",meta=(DisplayName="Clear Owned Loop Fixture / 清理自有夹具")) void ClearFixture();
    UFUNCTION(CallInEditor,Category="Loop Tests",meta=(DisplayName="Run B4-B2A Checks / 执行固定回环测试")) void RunCommitChecks();
private:
    bool OwnMap() const;
    FRogueliteFixedLoopCommitRequest Request(ERogueliteLoopCommitTestFault Fault=ERogueliteLoopCommitTestFault::None) const;
};
