#pragma once
#include "CoreMinimal.h"
#include "RogueliteLoopCommit.h"
#include "RogueliteLoopAwarePlanning.generated.h"

/** Separate spatial version. It never changes the RoomGraph algorithm version. */
USTRUCT(BlueprintType)
struct ROGUELITEPROTOTYPE_API FRogueliteLoopAwareSettings
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere,BlueprintReadWrite) bool bEnableLoopAwarePlanning=false;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) int32 SpatialPlanningVersion=2;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) int32 ConfigVersion=1;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) int32 MinLoops=1;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) int32 MaxLoops=1;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) int32 MinLoopTreeDistance=3;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) int32 MaxLayoutAttempts=96;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) int32 MaxLoopIntentCandidates=32;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) int32 MaxBacktrackStates=12000;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) int32 MaxSpatialChecks=60000;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) float MinTreeCorridorLength=1000;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) float MaxTreeCorridorLength=20000;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) float MinLoopCorridorLength=100;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) float MaxLoopCorridorLength=10000;
};

/** Provisional data only: no ConnectionId, Actor ownership or socket reservation. */
USTRUCT(BlueprintType)
struct ROGUELITEPROTOTYPE_API FRogueliteLoopIntent
{
    GENERATED_BODY()
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) FName StableId;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) int32 NodeA=-1;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) int32 NodeB=-1;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) int32 TreeDistance=0;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) int32 ExpectedCycles=1;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) int32 RequiredCapacityA=0;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) int32 RequiredCapacityB=0;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) int32 Priority=0;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) int32 DerivedSpatialSeed=0;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) TArray<int32> TreePath;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) ERogueliteRoomSide SocketA=ERogueliteRoomSide::North;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) ERogueliteRoomSide SocketB=ERogueliteRoomSide::South;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) FName CorridorProfile;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) float CorridorLength=0;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) FTransform CorridorTransform;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) FBox ReservedOccupancy=FBox(ForceInit);
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) bool bSpatiallyFeasible=false;
};

USTRUCT(BlueprintType)
struct ROGUELITEPROTOTYPE_API FRogueliteLoopAwareEvidence
{
    GENERATED_BODY()
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) int32 Seed=0;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) int32 DerivedSpatialSeed=0;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) int32 SpatialPlanningVersion=2;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) FString Stage=TEXT("NotStarted");
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) FString Reason;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) FString GraphDigest;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) FString RoomTypeDigest;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) FString StablePlanDigest;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) FString StablePlanText;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) FString LayoutDigest;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) FString TraversalDigest;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) int32 MinLoops=0;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) int32 MaxLoops=0;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) int32 LayoutAttempts=0;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) int32 BacktrackStates=0;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) int32 SpatialChecks=0;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) int32 FeasibleIntentCount=0;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) int32 PrecheckedCount=0;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) int32 CommittedLoops=0;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) int32 IndependentCycles=0;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) int32 RoomCount=0;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) int32 TreeEdgeCount=0;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) bool bTreePlanned=false;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) bool bTreeBuilt=false;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) bool bC2Validated=false;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) bool bSucceeded=false;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) bool bCleanupComplete=false;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) bool bUsedOptionalTreeFallback=false;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) bool bNavigationReady=false;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) TArray<FRogueliteLoopIntent> Intents;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) TArray<FString> Attempts;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) FRogueliteFixedLoopCommitResult CommitResult;
};

/** Native plan: pure placement data after the existing bounded authored-template probes. */
struct ROGUELITEPROTOTYPE_API FRogueliteLoopAwareSpatialPlan
{
    FRogueliteSeedSpatialPlan Tree;
    FRogueliteLoopAwareEvidence Evidence;
    TWeakObjectPtr<UWorld> World;
    TWeakObjectPtr<AActor> Owner;
    FString SourceGeometryDigest,SourceSettingsDigest,SourceCatalogDigest;
    FTransform Start,AuthoringOwnerTransform;
    // Copies share expiration. A materialized/failed batch cannot replay an old plan GUID.
    TSharedPtr<bool> Unconsumed=MakeShared<bool>(true);
};

// Test-only controls are native, restricted to the independent rig's explicit tag.
enum class ERogueliteLoopAwareTestFault : uint8 { None, CancelAfterTree, FailLoopCommit, CancelAfterLoop };

class ROGUELITEPROTOTYPE_API FRogueliteLoopAwarePlanner
{
public:
    static bool ValidateSettings(const FRogueliteLoopAwareSettings& Settings,FString& Reason);
    static int32 SpatialSeed(int32 Seed,int32 ConfigVersion=1,int32 Attempt=0);
    static FRogueliteLoopPlanningSettings LoopSettings(const FRogueliteLoopAwareSettings& Settings);
    static FRogueliteLoopAwareSpatialPlan Plan(AActor* Owner,const FRogueliteSeedLayoutSettings& Source,
        const FRogueliteLoopAwareSettings& Settings,const FTransform& Start);
    static bool IsCurrent(AActor* Owner,const FRogueliteSeedLayoutSettings& Source,
        const FRogueliteLoopAwareSettings& Settings,const FRogueliteLoopAwareSpatialPlan& Plan,FString& Reason);
    static bool MaterializeEditor(AActor* Owner,const FRogueliteSeedLayoutSettings& Source,
        const FRogueliteLoopAwareSettings& Settings,const FRogueliteLoopAwareSpatialPlan& Plan,
        FRogueliteSeedBatch& Batch,FRogueliteCommittedLoopBatch& Loops,FRogueliteLoopAwareEvidence& Evidence,
        ERogueliteLoopAwareTestFault Fault=ERogueliteLoopAwareTestFault::None);
    static bool Clear(AActor* Owner,FRogueliteSeedBatch& Batch,FRogueliteCommittedLoopBatch& Loops,FString& Reason);
};
