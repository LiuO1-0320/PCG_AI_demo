#pragma once
#include "CoreMinimal.h"
#include "RogueliteLoopPlanning.h"
#include "RogueliteLoopCommit.generated.h"

UENUM(BlueprintType)
enum class ERogueliteLoopCommitStage : uint8 { Planned, Prechecked, DoorsOpened, GeometryBuilt, C2Validated, Committed };

USTRUCT(BlueprintType)
struct ROGUELITEPROTOTYPE_API FRogueliteCommittedLoopEdge
{
    GENERATED_BODY()
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) FName CandidateId;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) int32 EdgeId=-1;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) int32 NodeA=-1;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) int32 NodeB=-1;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) ERogueliteRoomSide SocketA=ERogueliteRoomSide::North;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) ERogueliteRoomSide SocketB=ERogueliteRoomSide::South;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) FGuid ConnectionId;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) FName ProfileId;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) TObjectPtr<ARogueliteParametricCorridor> Corridor;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) FRogueliteDoorConfig BeforeDoorA;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) FRogueliteDoorConfig BeforeDoorB;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) FString PlanDigest;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) FRogueliteAssemblyResult Validation;
};

/** Committed geometry metadata only. C2 Connections remain the only reservation authority. */
USTRUCT(BlueprintType)
struct ROGUELITEPROTOTYPE_API FRogueliteCommittedLoopBatch
{
    GENERATED_BODY()
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) FGuid SourceBatchId;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) FString BaseGraphDigest;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) FString BaseLayoutDigest;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) int32 GraphRevision=0;
    // Increments also on rolled-back geometry changes. Old plans/Ready must be refreshed.
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) int32 GeometryRevision=0;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) FString GeometryDigest;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) TArray<FRogueliteCommittedLoopEdge> Edges;
};

USTRUCT(BlueprintType)
struct ROGUELITEPROTOTYPE_API FRogueliteFixedLoopCommitResult
{
    GENERATED_BODY()
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) bool bSucceeded=false;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) ERogueliteLoopCommitStage Stage=ERogueliteLoopCommitStage::Planned;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) FString Reason;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) FName CandidateId;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) bool bRollbackComplete=true;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) bool bOriginalTreeUnchanged=true;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) bool bCorridorSpawned=false;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) bool bDoorsOpened=false;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) bool bC2Validated=false;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) bool bConnectionCommitted=false;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) bool bLoopPublished=false;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) FString SpawnedCorridorPath;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) FRogueliteAssemblyResult Assembly;
};

USTRUCT(BlueprintType)
struct ROGUELITEPROTOTYPE_API FRogueliteTraversalEdge
{
    GENERATED_BODY()
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) int32 EdgeId=-1;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) int32 NodeA=-1;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) int32 NodeB=-1;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) bool bCommittedLoop=false;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) FName CandidateId;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) FGuid ConnectionId;
};

/** Value query for future DungeonMap/B3. No World Actor pointers and no candidates. */
USTRUCT(BlueprintType)
struct ROGUELITEPROTOTYPE_API FRogueliteLoopTraversalNode
{
    GENERATED_BODY()
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) int32 NodeId=-1;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) FString Role;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) int32 Degree=0;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) ERogueliteSeedRoomType RoomType=ERogueliteSeedRoomType::Combat;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) FName TemplateId;
};

USTRUCT(BlueprintType)
struct ROGUELITEPROTOTYPE_API FRogueliteLoopTraversalSnapshot
{
    GENERATED_BODY()
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) bool bCurrent=false;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) bool bGeometryValidated=false;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) bool bNavigationReady=false;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) FString NavigationState=TEXT("NavigationPending");
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) FString Reason;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) FGuid BatchId;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) int32 Seed=0;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) int32 GraphRevision=0;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) int32 GeometryRevision=0;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) int32 IndependentCycles=0;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) FString StableDigest;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) TArray<FRogueliteLoopTraversalNode> Nodes;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) TArray<FRogueliteTraversalEdge> Edges;
};

// Native synchronous test injections; never exposed as designer/gameplay settings.
enum class ERogueliteLoopCommitTestFault : uint8 { None, DoorOpen, Spawn, Collision, CancelAfterOpen, CancelAfterSpawn, CancelAfterValidation, CancelAfterReservation };
struct ROGUELITEPROTOTYPE_API FRogueliteFixedLoopCommitRequest
{
    AActor* Owner=nullptr;
    const FRogueliteSeedLayoutSettings* SourceSettings=nullptr;
    const FRogueliteLoopPlanningSettings* LoopSettings=nullptr;
    const FRogueliteLoopPlan* Plan=nullptr;
    FName CandidateId;
    int32 ExpectedGeometryRevision=0;
    ERogueliteLoopCommitTestFault TestFault=ERogueliteLoopCommitTestFault::None;
};

class ROGUELITEPROTOTYPE_API FRogueliteLoopCommitQueries
{
public:
    static FString GeometryDigest(AActor* Owner,const FRogueliteSeedBatch& Base,const FRogueliteCommittedLoopBatch& Overlay);
    static FRogueliteLoopTraversalSnapshot Capture(AActor* Owner,const FRogueliteSeedBatch& Base,
        const FRogueliteSeedLayoutSettings& Source,const FRogueliteCommittedLoopBatch& Overlay);
    static bool Clear(AActor* Owner,FRogueliteSeedBatch& Base,FRogueliteCommittedLoopBatch& Overlay,FString& Reason);
    static bool Revalidate(AActor* Owner,FRogueliteSeedBatch& Base,FRogueliteCommittedLoopBatch& Overlay,FString& Reason);
};
