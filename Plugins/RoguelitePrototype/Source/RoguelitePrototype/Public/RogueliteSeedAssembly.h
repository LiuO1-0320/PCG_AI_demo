#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RogueliteRoomGraph.h"
#include "RogueliteRoomTemplate.h"
#include "RogueliteRoomAssembly.h"
#include "UObject/StrongObjectPtr.h"
#include "RogueliteSeedAssembly.generated.h"

UENUM(BlueprintType)
enum class ERogueliteSeedRoomType : uint8 { Combat=0, Challenge=1, SupplyShop=2, Final=3, Event=4 };

USTRUCT(BlueprintType)
struct ROGUELITEPROTOTYPE_API FRogueliteRoomTypeRule
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere,BlueprintReadWrite) ERogueliteSeedRoomType Type=ERogueliteSeedRoomType::Combat;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) int32 Weight=1;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) int32 MinCount=0;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) int32 MaxCount=256;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) bool bAllowMainPath=true;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) bool bAllowBranch=true;
};
USTRUCT(BlueprintType)
struct ROGUELITEPROTOTYPE_API FRogueliteRoomTypeAssignmentSettings
{
    GENERATED_BODY()
    FRogueliteRoomTypeAssignmentSettings();
    UPROPERTY(EditAnywhere,BlueprintReadWrite) int32 Version=2;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) ERogueliteSeedRoomType StartType=ERogueliteSeedRoomType::Combat;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) TArray<FRogueliteRoomTypeRule> Rules;
    // A definition label only. No sub-dungeon actors, teleport or chest behavior.
    UPROPERTY(EditAnywhere,BlueprintReadWrite) FName EventDefinitionId=TEXT("TreasureCave");
};

USTRUCT(BlueprintType)
struct ROGUELITEPROTOTYPE_API FRogueliteSeedTemplateOption
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FName Id;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TSoftClassPtr<ARogueliteRoomTemplate> TemplateClass;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TArray<ERogueliteSeedRoomType> AllowedTypes;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="1")) int32 Weight = 1;
};
USTRUCT(BlueprintType)
struct ROGUELITEPROTOTYPE_API FRogueliteSeedEncounterOption
{
    GENERATED_BODY()
    // A separate configurable reference label, not enemy spawning or encounter behavior.
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FName ProfileId;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TArray<ERogueliteSeedRoomType> AllowedTypes;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="1")) int32 Weight = 1;
};
USTRUCT(BlueprintType)
struct ROGUELITEPROTOTYPE_API FRogueliteSeedCorridorProfile
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FName Id = TEXT("ShortConnector");
    UPROPERTY(EditAnywhere, BlueprintReadWrite) bool bEnabled = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TArray<float> LengthCandidates = {1000, 2000, 3000};
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float Width = 500;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float Height = 350;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float WallThickness = 20;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float FloorThickness = 20;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float DoorWidth = 200;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float DoorHeight = 250;
};
USTRUCT(BlueprintType)
struct ROGUELITEPROTOTYPE_API FRogueliteSeedLayoutSettings
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 Seed = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 MinMainNodes = 3;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 MaxMainNodes = 3;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 MinBranches = 1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 MaxBranches = 1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 MinBranchDepth = 1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 MaxBranchDepth = 1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 MaxNodes = 4;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 MaxDegree = 3;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 GraphAttempts = 8;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 AlgorithmVersion = 1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 ConfigVersion = 1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 TemplateCatalogVersion = 1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 SolverVersion = 1;
    // Ignored in AlgorithmVersion=1, including its configuration/layout summaries.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="V2") int32 MinNodes=2;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="V2") int32 MinDecisionNodes=0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="V2") int32 MaxDecisionNodes=24;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="V2") int32 MinNestedDecisionNodes=0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="V2") int32 MaxBranchGenerationDepth=3;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="V2") int32 RecursiveBranchWeight=3;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="V2") FRogueliteRoomTypeAssignmentSettings TypeAssignment;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="1",ClampMax="256")) int32 MaxCandidatesPerEdge = 64;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TArray<FRogueliteSeedTemplateOption> Templates;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TArray<FRogueliteSeedEncounterOption> Encounters;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TArray<FRogueliteSeedCorridorProfile> Corridors;
    // Order Combat/Challenge/SupplyShop. Final is assigned by the graph's Final role.
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TArray<int32> MainTypeWeights = {1, 0, 0};
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TArray<int32> BranchTypeWeights = {1, 1, 1};
    UPROPERTY(EditAnywhere, BlueprintReadWrite) bool bCheckNavigation = false;
    FRogueliteGraphGenerationSettings GraphSettings() const;
};

USTRUCT(BlueprintType)
struct ROGUELITEPROTOTYPE_API FRogueliteSeedRoomRecord
{
    GENERATED_BODY()
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) int32 NodeId = INDEX_NONE;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) ERogueliteSeedRoomType RoomType = ERogueliteSeedRoomType::Combat;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) FName TemplateId;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TSubclassOf<ARogueliteRoomTemplate> TemplateClass;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) FName EncounterProfile;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) FName EventDefinitionId;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<ARogueliteRoomTemplate> Room;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) FTransform PlacedTransform = FTransform::Identity;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) FVector Dimensions = FVector::ZeroVector;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) float WallThickness = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) float FloorThickness = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) FString MemberSignature;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) int32 LogicalDegree = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) int32 TemplateCapacity = 0;
};
USTRUCT(BlueprintType)
struct ROGUELITEPROTOTYPE_API FRogueliteSeedEdgeRecord
{
    GENERATED_BODY()
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) int32 EdgeId = INDEX_NONE;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) int32 ParentNode = INDEX_NONE;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) int32 ChildNode = INDEX_NONE;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<ARogueliteParametricCorridor> Corridor;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) FGuid ConnectionId;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) ERogueliteRoomSide ParentSocket = ERogueliteRoomSide::North;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) ERogueliteRoomSide ChildSocket = ERogueliteRoomSide::South;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) FName CorridorProfile;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) float CorridorLength = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) int32 AcceptedCandidate = INDEX_NONE;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) FRogueliteAssemblyResult Validation;
};
USTRUCT(BlueprintType)
struct ROGUELITEPROTOTYPE_API FRogueliteSeedCandidateRecord
{
    GENERATED_BODY()
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) int32 EdgeId = INDEX_NONE;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) int32 CandidateIndex = INDEX_NONE;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) FName TemplateId;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) ERogueliteRoomSide ParentSocket = ERogueliteRoomSide::North;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) ERogueliteRoomSide ChildSocket = ERogueliteRoomSide::South;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) FName ProfileId;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) float Length = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) bool bSucceeded = false;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) FString Reason;
};
USTRUCT(BlueprintType)
struct ROGUELITEPROTOTYPE_API FRogueliteSeedBatch
{
    GENERATED_BODY()
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) FGuid BatchId;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) int32 Seed = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) FString ConfigurationDigest;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) FString GraphDigest;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) FString LayoutDigest;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) bool bSucceeded = false;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) bool bGeometryValid = false;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) bool bCleanupComplete = false;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) FString Reason;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) ERogueliteNavigationCheck Navigation = ERogueliteNavigationCheck::NotChecked;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<ARogueliteRoomAssembly> Registry;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TArray<FRogueliteSeedRoomRecord> Rooms;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TArray<FRogueliteSeedEdgeRecord> Edges;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TArray<FRogueliteSeedCandidateRecord> Candidates;
    // Kept after batch cleanup so physical failures retain their original probes.
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) int32 FailedEdge = INDEX_NONE;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) FRogueliteAssemblyResult FailureValidation;
    UPROPERTY() TArray<TObjectPtr<AActor>> OwnedActors;
};

/** Native spatial plan only. Socket assignment here is provisional, not a C2 reservation. */
struct ROGUELITEPROTOTYPE_API FRogueliteSeedPlannedRoom
{
    FRogueliteSeedRoomRecord Record;
    TArray<FRogueliteDoorSocket> Sockets;
    TArray<ERogueliteRoomSide> UsedSockets;
    FBox Occupancy;
};
struct ROGUELITEPROTOTYPE_API FRogueliteSeedPlannedEdge
{
    FRogueliteSeedEdgeRecord Record;
    FRogueliteSeedCorridorProfile Profile;
    FTransform Transform=FTransform::Identity;
    FBox Occupancy;
};
struct ROGUELITEPROTOTYPE_API FRogueliteSeedSpatialPlan
{
    bool bGraphGenerated=false, bValid=false;
    FString Reason;
    FRogueliteSeedBatch Metadata;
    FRogueliteRoomGraph Graph;
    TArray<FRogueliteSeedPlannedRoom> Rooms;
    TArray<FRogueliteSeedPlannedEdge> Edges;
    // Plans may outlive the synchronous query. Keep authored class/SCS resources alive.
    TArray<TStrongObjectPtr<UClass>> RetainedClasses;
};

class ROGUELITEPROTOTYPE_API FRogueliteSeedLayoutAssembler
{
public:
    // Editor authoring only in B2. GameWorld/deferred runtime integration is B3.
    static FRogueliteSeedBatch Generate(AActor* Owner, const FRogueliteSeedLayoutSettings& Settings, const FTransform& StartTransform);
    static bool Clear(AActor* Owner, FRogueliteSeedBatch& Batch, FString& Reason);
    static bool Revalidate(AActor* Owner, FRogueliteSeedBatch& Batch);
    static FString DescribeLayout(const FRogueliteSeedBatch& Batch);
    // Same B1/catalog/candidate order as Generate; creates no formal dungeon actors/reservations.
    static FRogueliteSeedSpatialPlan Plan(AActor* Owner,const FRogueliteSeedLayoutSettings& Settings,const FTransform& StartTransform);
};

UCLASS(Blueprintable)
class ROGUELITEPROTOTYPE_API ARogueliteSeedAssembly : public AActor
{
    GENERATED_BODY()
public:
    ARogueliteSeedAssembly();
    virtual void PostLoad() override;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Roguelite|Seed Layout") FRogueliteSeedLayoutSettings Settings;
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Roguelite|Batch") FRogueliteSeedBatch Batch;
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Roguelite|Diagnostics") TArray<FString> Evidence;
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Roguelite|Diagnostics") bool bJunctionValidated = false;
    UFUNCTION(CallInEditor, BlueprintCallable, Category="Roguelite|Seed Actions", meta=(DisplayName="Generate Seed Layout / 生成种子布局")) void GenerateLayout();
    UFUNCTION(CallInEditor, BlueprintCallable, Category="Roguelite|Seed Actions", meta=(DisplayName="Revalidate Batch / 重新验证批次")) void RevalidateBatch();
    UFUNCTION(CallInEditor, BlueprintCallable, Category="Roguelite|Seed Actions", meta=(DisplayName="Clear Owned Batch / 清理本批次")) void ClearBatch();
    UFUNCTION(CallInEditor, BlueprintCallable, Category="Roguelite|B2 Test Only", meta=(DisplayName="Validate Junction3 / 验证三门模板")) void ValidateJunction();
    UFUNCTION(CallInEditor, BlueprintCallable, Category="Roguelite|B2 Test Only", meta=(DisplayName="Run B2 Checks / 执行B2测试")) void RunB2Checks();
    bool IsB2EditorWorld() const;
};
