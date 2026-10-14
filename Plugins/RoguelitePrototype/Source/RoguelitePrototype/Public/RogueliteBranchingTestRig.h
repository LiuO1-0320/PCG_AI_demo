#pragma once
#include "RogueliteSeedAssembly.h"
#include "RogueliteRuntimeSeedAssembly.h"
#include "RogueliteBranchingTestRig.generated.h"

USTRUCT(BlueprintType)
struct ROGUELITEPROTOTYPE_API FRogueliteBranchingRoomEvidence
{
    GENERATED_BODY()
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) int32 NodeId=-1;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) int32 ParentNodeId=-1;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) int32 Degree=0;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) int32 Capacity=0;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) int32 GenerationDepth=0;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) bool bDecision=false;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) ERogueliteSeedRoomType RoomType=ERogueliteSeedRoomType::Combat;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) FName TemplateId;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) FName EventDefinitionId;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) FTransform Transform=FTransform::Identity;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) FVector Dimensions=FVector::ZeroVector;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) FString MemberSignature;
};
USTRUCT(BlueprintType)
struct ROGUELITEPROTOTYPE_API FRogueliteBranchingEdgeEvidence
{
    GENERATED_BODY()
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) int32 EdgeId=-1;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) int32 ParentNode=-1;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) int32 ChildNode=-1;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) FGuid ConnectionId;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) ERogueliteRoomSide ParentSocket=ERogueliteRoomSide::North;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) ERogueliteRoomSide ChildSocket=ERogueliteRoomSide::South;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) FTransform CorridorTransform=FTransform::Identity;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) float CorridorLength=0;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) FRogueliteAssemblyResult Validation;
};
USTRUCT(BlueprintType)
struct ROGUELITEPROTOTYPE_API FRogueliteBranchingCaseEvidence
{
    GENERATED_BODY()
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) FString Case;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) int32 Seed=0;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) FGuid BatchId;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) bool bSucceeded=false;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) bool bCleanupComplete=false;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) FString Reason;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) FString GraphDigest;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) FString ConfigurationDigest;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) FString LayoutDigest;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) TArray<FRogueliteBranchingRoomEvidence> Rooms;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) TArray<FRogueliteBranchingEdgeEvidence> Edges;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) TArray<FRogueliteSeedCandidateRecord> Candidates;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) FRogueliteAssemblyResult FailureValidation;
};

UCLASS()
class ROGUELITEPROTOTYPE_API ARogueliteBranchingEditorTestRig : public ARogueliteSeedAssembly
{
    GENERATED_BODY()
public:
    ARogueliteBranchingEditorTestRig();
    UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Transient,Category="B4-A Test") TArray<FRogueliteBranchingCaseEvidence> Cases;
    UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Transient,Category="B4-A Test") TArray<int32> SuccessfulSeeds;
    UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Transient,Category="B4-A Test") bool bChecksPassed=false;
    UFUNCTION(CallInEditor,Category="B4-A Test",meta=(DisplayName="Run B4-A Editor Checks / 多级分岔编辑器测试")) void RunBranchingChecks();
    UFUNCTION(CallInEditor,Category="B4-A Preview",meta=(DisplayName="Generate B4-A Editor Preview / 生成预览")) void GenerateBranchingPreview();
    UFUNCTION(CallInEditor,Category="B4-A Preview",meta=(DisplayName="Clear B4-A Editor Preview / 清理预览")) void ClearBranchingPreview();
private:
    bool OwnEditorMap() const;
};
UCLASS()
class ROGUELITEPROTOTYPE_API ARogueliteBranchingRuntimeTestRig : public ARogueliteRuntimeSeedAssembly
{
    GENERATED_BODY()
public:
    ARogueliteBranchingRuntimeTestRig();
    virtual void BeginPlay() override;
    UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Transient,Category="B4-A Result") FRogueliteBranchingCaseEvidence RuntimeCase;
    UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Transient,Category="B4-A Result") bool bRuntimeGeometryChecksPassed=false;
private:
    void CaptureGeometry();
};
