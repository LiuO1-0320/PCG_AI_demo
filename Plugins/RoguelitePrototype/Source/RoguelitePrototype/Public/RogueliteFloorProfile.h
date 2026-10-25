#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "RogueliteRuntimeLoopAssembly.h"
#include "RogueliteFloorProfile.generated.h"

UENUM(BlueprintType)
enum class ERogueliteFloorFinalPolicy : uint8 { FloorExit, BossFinal };
UENUM(BlueprintType)
enum class ERogueliteFloorStartPolicy : uint8 { TopologyStart_SafeHouseReserved };
UENUM(BlueprintType)
enum class ERogueliteFloorValidationPurpose : uint8 { DeveloperValidation, RuntimeGenerationValidation };
USTRUCT(BlueprintType)
struct ROGUELITEPROTOTYPE_API FRogueliteFloorSearchBudget
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere,BlueprintReadWrite) int32 MaxLayoutAttempts=96;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) int32 MaxBacktrackStates=12000;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) int32 MaxSpatialChecks=60000;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) int32 GraphAttempts=32;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) int32 MaxCandidatesPerEdge=128;
};
/** Authoring data only. One resolver maps this to the existing generator settings. */
USTRUCT(BlueprintType)
struct ROGUELITEPROTOTYPE_API FRogueliteFloorDefinition
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Identity") FName ProfileId;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Identity") FText DisplayName;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Identity") int32 FloorIndex=1;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Developer Diagnostics") int32 ProfileVersion=1;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Rooms") int32 MinRoomCount=4;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Rooms") int32 MaxRoomCount=4;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Rooms") int32 MinReferencePathRooms=3;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Rooms") int32 MaxReferencePathRooms=3;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Branching") int32 MinBranches=1;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Branching") int32 MaxBranches=1;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Branching") int32 MinBranchDepth=1;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Branching") int32 MaxBranchDepth=1;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Branching") int32 MinDecisionNodes=1;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Branching") int32 MaxDecisionNodes=1;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Branching") int32 MinNestedDecisionNodes=0;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Branching") int32 MaxBranchGenerationDepth=1;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Branching") int32 MaxDegree=3;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Loops",meta=(ClampMin="0",ClampMax="1")) int32 MinLoops=0;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Loops",meta=(ClampMin="0",ClampMax="1")) int32 MaxLoops=0;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Room Types") FRogueliteRoomTypeAssignmentSettings RoomTypes;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Templates") TArray<FRogueliteSeedTemplateOption> AllowedTemplates;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Corridor",meta=(Units="cm")) float MinCorridorLength=500;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Corridor",meta=(Units="cm")) float MaxCorridorLength=1000;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Corridor",meta=(Units="cm")) float CorridorWidth=500;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Corridor",meta=(Units="cm")) float CorridorHeight=350;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Corridor",meta=(Units="cm")) float WallThickness=20;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Corridor",meta=(Units="cm")) float FloorThickness=20;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Corridor",meta=(Units="cm")) float DoorWidth=200;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Corridor",meta=(Units="cm")) float DoorHeight=250;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Developer Diagnostics") int32 SpatialPlanningVersion=2;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Developer Diagnostics") FRogueliteFloorSearchBudget SearchBudget;
    // Explicit metadata contracts. None of these constructs gameplay.
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Reserved Gameplay") ERogueliteFloorStartPolicy StartRolePolicy=ERogueliteFloorStartPolicy::TopologyStart_SafeHouseReserved;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Reserved Gameplay") ERogueliteFloorFinalPolicy FinalRolePolicy=ERogueliteFloorFinalPolicy::FloorExit;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Reserved Gameplay") bool bLastFloor=false;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Reserved Gameplay") FName EncounterDifficultyProfileId;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Reserved Gameplay") FName EventEligibility;
};
USTRUCT(BlueprintType)
struct ROGUELITEPROTOTYPE_API FRogueliteResolvedFloor
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly) bool bValid=false;
    UPROPERTY(BlueprintReadOnly) FName ProfileId;
    UPROPERTY(BlueprintReadOnly) int32 FloorIndex=0;
    UPROPERTY(BlueprintReadOnly) int32 UsedSeed=0;
    UPROPERTY(BlueprintReadOnly) bool bDerivedFromRunSeed=false;
    UPROPERTY(BlueprintReadOnly) FString ProfileDigest;
    UPROPERTY(BlueprintReadOnly) FString ConfigurationDigest;
    UPROPERTY(BlueprintReadOnly) FString ErrorCode;
    UPROPERTY(BlueprintReadOnly) FString Message;
    UPROPERTY(BlueprintReadOnly) bool bGameplayPoliciesImplemented=false;
    UPROPERTY(BlueprintReadOnly) FRogueliteSeedLayoutSettings Source;
    UPROPERTY(BlueprintReadOnly) FRogueliteLoopAwareSettings Planning;
};
UCLASS(BlueprintType)
class ROGUELITEPROTOTYPE_API URogueliteFloorProfile : public UDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Floor Rules") FRogueliteFloorDefinition Rules;
    UFUNCTION(BlueprintPure,Category="Roguelite|Floor") FRogueliteResolvedFloor ResolvePreview(int32 PreviewSeed) const;
    UFUNCTION(BlueprintPure,Category="Roguelite|Floor") FRogueliteResolvedFloor ResolveRun(int32 RunSeed) const;
};
class ROGUELITEPROTOTYPE_API FRogueliteFloorProfileResolver
{
public:
    static FRogueliteFloorDefinition Defaults(int32 FloorIndex);
    static FString Digest(const FRogueliteFloorDefinition& Rules);
    static FString SettingsDigest(const FRogueliteSeedLayoutSettings& Source,const FRogueliteLoopAwareSettings& Planning);
    static int32 DeriveFloorSeed(int32 RunSeed,int32 FloorIndex,int32 ProfileVersion=1);
    static FRogueliteResolvedFloor Resolve(const FRogueliteFloorDefinition& Rules,int32 Seed,bool FromRunSeed=false);
    static FString FriendlyFailure(const FString& Raw);
    // Thin adapter, no second planner. Success proves GeometryValidated only.
    static bool GenerateRuntime(AActor* Owner,const FRogueliteResolvedFloor& Config,ERogueliteFloorValidationPurpose Purpose,
        const FRoguelitePreparedTemplateCatalog& Catalog,FRogueliteSeedBatch& Batch,FRogueliteCommittedLoopBatch& Loops,FRogueliteRuntimeLoopResult& Result);
};
