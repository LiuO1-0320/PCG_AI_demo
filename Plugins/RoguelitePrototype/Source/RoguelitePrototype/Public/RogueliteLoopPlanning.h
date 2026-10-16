#pragma once
#include "CoreMinimal.h"
#include "RogueliteSeedAssembly.h"
#include "RogueliteLoopPlanning.generated.h"

UENUM(BlueprintType)
enum class ERogueliteLoopFeasibility : uint8 { TopologyValid, SocketCompatible, StraightPrecheckPassed, RequiresCornerCorridor, Rejected };
UENUM(BlueprintType)
enum class ERogueliteLoopPlanningState : uint8 { Complete, InvalidSettings, InvalidSource, LoopRequirementUnsatisfied };

USTRUCT(BlueprintType)
struct ROGUELITEPROTOTYPE_API FRogueliteLoopPlanningSettings
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere,BlueprintReadWrite) int32 MinLoops=0;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) int32 MaxLoops=2;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) int32 MaxLoopCandidates=256;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) int32 MinLoopTreeDistance=3;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) int32 MaxCandidateChecks=2048;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) bool bAllowCornerCandidates=true;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) bool bAllowAuthoredSealedSockets=true;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) int32 LoopAlgorithmVersion=1;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) int32 ConfigVersion=1;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) bool bOverrideLoopSeed=false;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) int32 LoopSelectionSeed=0;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(Units="cm")) float MinCorridorLength=100;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(Units="cm")) float MaxCorridorLength=10000;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(Units="cm")) float PositionTolerance=.1f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) float AngleToleranceDegrees=.01f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(Units="cm")) float CapsuleRadius=34;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(Units="cm")) float CapsuleHalfHeight=88;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(Units="cm")) float CapsuleClearance=2;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) int32 TreeDistanceWeight=100;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) int32 DifferentTypeWeight=25;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) int32 LengthPerMeterPenalty=1;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) int32 AlreadyOpenWeight=5;
};

USTRUCT(BlueprintType)
struct ROGUELITEPROTOTYPE_API FRogueliteLoopCandidate
{
    GENERATED_BODY()
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) FName CandidateId;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) int32 NodeA=-1;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) int32 NodeB=-1;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) FName TemplateA;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) FName TemplateB;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) FString RoomAPath;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) FString RoomBPath;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) ERogueliteSeedRoomType TypeA=ERogueliteSeedRoomType::Combat;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) ERogueliteSeedRoomType TypeB=ERogueliteSeedRoomType::Combat;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) FString RoleA;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) FString RoleB;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) int32 TreeDistance=0;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) int32 IndependentCycleDelta=0;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) bool bTopologyValid=false;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) bool bSocketCompatible=false;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) bool bHasSocketPair=false;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) TArray<ERogueliteRoomSide> AvailableSocketsA;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) TArray<ERogueliteRoomSide> AvailableSocketsB;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) ERogueliteRoomSide SocketA=ERogueliteRoomSide::North;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) ERogueliteRoomSide SocketB=ERogueliteRoomSide::North;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) FRogueliteDoorSocket DoorA;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) FRogueliteDoorSocket DoorB;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) bool bRequiresEnableA=false;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) bool bRequiresEnableB=false;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) FName CorridorProfile;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) float CorridorLength=0;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) FVector CorridorDimensions=FVector::ZeroVector;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) FTransform CorridorTransform=FTransform::Identity;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) FBox CorridorOccupancy=FBox(ForceInit);
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) ERogueliteLoopFeasibility Feasibility=ERogueliteLoopFeasibility::Rejected;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) FString Reason;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) int64 Score=0;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) int64 StableRank=0;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) bool bSelected=false;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) FString SelectionReason;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) TArray<FRogueliteAssemblyProbe> PrecheckProbes;
};

/** Independent planning value data. No Gameplay Edge or C2 reservation is created. */
USTRUCT(BlueprintType)
struct ROGUELITEPROTOTYPE_API FRogueliteLoopPlan
{
    GENERATED_BODY()
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) ERogueliteLoopPlanningState State=ERogueliteLoopPlanningState::InvalidSource;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) bool bTreeValid=false;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) bool bRequirementsSatisfied=false;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) bool bCandidateBudgetExhausted=false;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) FString Reason;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) int32 Seed=0;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) int32 DerivedLoopSeed=0;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) int32 TemplateCatalogVersion=0;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) FGuid SourceBatchId;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) FString SourceWorldPath;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) FString GraphDigest;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) FString LayoutDigest;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) FString SourceSettingsDigest;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) FString SourceStateDigest;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) FString PlanningSettingsDigest;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) FString StableDigest;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) int32 TopologyPairs=0;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) int32 SocketCompatiblePairs=0;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) int32 StraightPassedPairs=0;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) int32 CornerRequiredPairs=0;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) int32 CandidateChecks=0;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) TArray<FRogueliteLoopCandidate> Candidates;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) TArray<FName> SelectedCandidates;
    // Native weak identity checks, excluded from deterministic digest and serialized display data.
    TWeakObjectPtr<UWorld> SourceWorld;
    TArray<TWeakObjectPtr<ARogueliteRoomTemplate>> SourceRooms;
};

class ROGUELITEPROTOTYPE_API FRogueliteLoopPlanner
{
public:
    static FString DescribeSettings(const FRogueliteLoopPlanningSettings& Settings);
    static bool ValidateSettings(const FRogueliteLoopPlanningSettings& Settings,FString& Reason);
    static bool CheckTopologyPair(const FRogueliteRoomGraph& Graph,const TArray<int32>& Capacities,
        const FRogueliteLoopPlanningSettings& Settings,int32 A,int32 B,int32& Distance,FString& Reason);
    // Pure graph query: no World, random global stream, Actor or connection mutation.
    static FRogueliteLoopPlan Topology(const FRogueliteRoomGraph& Graph,const TArray<int32>& Capacities,const FRogueliteLoopPlanningSettings& Settings);
    static FRogueliteLoopPlan Analyze(AActor* Owner,const FRogueliteSeedBatch& Batch,
        const FRogueliteSeedLayoutSettings& SourceSettings,const FRogueliteLoopPlanningSettings& Settings);
    static bool IsCurrent(AActor* Owner,const FRogueliteSeedBatch& Batch,const FRogueliteSeedLayoutSettings& SourceSettings,
        const FRogueliteLoopPlanningSettings& Settings,const FRogueliteLoopPlan& Plan,FString& Reason);
    static bool FixedSocketGeometry(const FRogueliteDoorSocket& A,const FRogueliteDoorSocket& B,
        const FRogueliteDoorSocket& Start,const FRogueliteDoorSocket& End,const FTransform& FixedB,
        const FBox& LocalCorridorBounds,const FRogueliteLoopPlanningSettings& Settings,
        FTransform& OutTransform,float& OutLength,FBox& OutOccupancy,FString& Reason);
};
