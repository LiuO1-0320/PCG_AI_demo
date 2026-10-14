#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "RogueliteSeedAssembly.h"
#include "RogueliteDungeonMap.generated.h"

UENUM(BlueprintType)
enum class ERogueliteMapExploration : uint8 { Unexplored, Discovered, Cleared };

/** Value-only display data. No Actor/UWorld/UObject references or reservation ownership. */
USTRUCT(BlueprintType)
struct ROGUELITEPROTOTYPE_API FRogueliteDungeonMapRoom
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly) int32 NodeId=INDEX_NONE;
    UPROPERTY(BlueprintReadOnly) FString Role;
    UPROPERTY(BlueprintReadOnly) int32 Degree=0;
    UPROPERTY(BlueprintReadOnly) TArray<int32> Neighbors;
    UPROPERTY(BlueprintReadOnly) bool bRoomTypeAssigned=false;
    UPROPERTY(BlueprintReadOnly) ERogueliteSeedRoomType RoomType=ERogueliteSeedRoomType::Combat;
    UPROPERTY(BlueprintReadOnly) FName TemplateId;
    UPROPERTY(BlueprintReadOnly) FName EventDefinitionId; // Data label, never a cached event Actor.
    UPROPERTY(BlueprintReadOnly) FString TemplateClassPath;
    UPROPERTY(BlueprintReadOnly) bool bHasWorldGeometry=false;
    UPROPERTY(BlueprintReadOnly) FTransform WorldTransform=FTransform::Identity;
    UPROPERTY(BlueprintReadOnly) FVector Dimensions=FVector::ZeroVector;
    UPROPERTY(BlueprintReadOnly) TArray<FVector2D> Footprint;
    // Reserved contract; false means no exploration gameplay source exists yet.
    UPROPERTY(BlueprintReadOnly) bool bExplorationKnown=false;
    UPROPERTY(BlueprintReadOnly) ERogueliteMapExploration Exploration=ERogueliteMapExploration::Unexplored;
};
USTRUCT(BlueprintType)
struct ROGUELITEPROTOTYPE_API FRogueliteDungeonMapConnection
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly) int32 EdgeId=INDEX_NONE;
    UPROPERTY(BlueprintReadOnly) int32 NodeA=INDEX_NONE;
    UPROPERTY(BlueprintReadOnly) int32 NodeB=INDEX_NONE;
    UPROPERTY(BlueprintReadOnly) FString Role;
    UPROPERTY(BlueprintReadOnly) FString Mode=TEXT("StraightCorridor");
    UPROPERTY(BlueprintReadOnly) FGuid ConnectionId;
    // These are copied observations from C2, never a writable occupancy registry.
    UPROPERTY(BlueprintReadOnly) bool bReservationActive=false;
    UPROPERTY(BlueprintReadOnly) bool bLastGeometryValidated=false;
    UPROPERTY(BlueprintReadOnly) bool bHasWorldGeometry=false;
    UPROPERTY(BlueprintReadOnly) FVector Start=FVector::ZeroVector;
    UPROPERTY(BlueprintReadOnly) FVector End=FVector::ZeroVector;
    UPROPERTY(BlueprintReadOnly) FVector StartOutward=FVector::ZeroVector;
    UPROPERTY(BlueprintReadOnly) FVector EndOutward=FVector::ZeroVector;
    UPROPERTY(BlueprintReadOnly) FVector Dimensions=FVector::ZeroVector;
    UPROPERTY(BlueprintReadOnly) TArray<FVector2D> Footprint;
};
USTRUCT(BlueprintType)
struct ROGUELITEPROTOTYPE_API FRogueliteDungeonMapSnapshot
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly) bool bValid=false;
    UPROPERTY(BlueprintReadOnly) bool bSpatial=false;
    UPROPERTY(BlueprintReadOnly) int32 Seed=0;
    UPROPERTY(BlueprintReadOnly) int32 AlgorithmVersion=0;
    UPROPERTY(BlueprintReadOnly) int32 ConfigVersion=0;
    // Catalog/solver values describe current source settings; B2 did not store their historical values separately.
    UPROPERTY(BlueprintReadOnly) int32 TemplateCatalogVersion=0;
    UPROPERTY(BlueprintReadOnly) int32 SolverVersion=0;
    UPROPERTY(BlueprintReadOnly) FGuid BatchId;
    UPROPERTY(BlueprintReadOnly) FString WorldIdentity;
    UPROPERTY(BlueprintReadOnly) FString SourceIdentity;
    UPROPERTY(BlueprintReadOnly) FString WorldKind;
    UPROPERTY(BlueprintReadOnly) FString ConfigurationDigest;
    UPROPERTY(BlueprintReadOnly) FString GraphDigest;
    UPROPERTY(BlueprintReadOnly) FString LayoutDigest;
    UPROPERTY(BlueprintReadOnly) FString StableDataDigest;
    UPROPERTY(BlueprintReadOnly) FString Revision;
    UPROPERTY(BlueprintReadOnly) FString Stage=TEXT("Empty");
    UPROPERTY(BlueprintReadOnly) FString Navigation=TEXT("NotChecked");
    UPROPERTY(BlueprintReadOnly) bool bGeometryValidated=false;
    UPROPERTY(BlueprintReadOnly) bool bDungeonReady=false;
    UPROPERTY(BlueprintReadOnly) FString Reason;
    UPROPERTY(BlueprintReadOnly) TArray<FRogueliteDungeonMapRoom> Rooms;
    UPROPERTY(BlueprintReadOnly) TArray<FRogueliteDungeonMapConnection> Connections;
};
/** No global node list, distant neighbor IDs, encounters, rewards or authored internals. */
USTRUCT(BlueprintType)
struct ROGUELITEPROTOTYPE_API FRogueliteDungeonLocalMapView
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly) bool bLocated=false;
    UPROPERTY(BlueprintReadOnly) int32 CurrentNodeId=INDEX_NONE;
    UPROPERTY(BlueprintReadOnly) int32 Seed=0;
    UPROPERTY(BlueprintReadOnly) FGuid BatchId;
    UPROPERTY(BlueprintReadOnly) FString SourceRevision;
    UPROPERTY(BlueprintReadOnly) FString Reason;
    UPROPERTY(BlueprintReadOnly) TArray<FRogueliteDungeonMapRoom> Rooms;
    UPROPERTY(BlueprintReadOnly) TArray<FRogueliteDungeonMapConnection> Connections;
};

UCLASS()
class ROGUELITEPROTOTYPE_API URogueliteDungeonMapQuery : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    // Call on the game thread. Requery instead of retaining an old snapshot as current data.
    UFUNCTION(BlueprintPure,Category="Roguelite|Map") static FRogueliteDungeonMapSnapshot Capture(const AActor* Source);
    UFUNCTION(BlueprintPure,Category="Roguelite|Map") static bool IsSnapshotCurrent(const AActor* Source,const FRogueliteDungeonMapSnapshot& Snapshot);
    // Live-source local query protects against batch/world changes.
    UFUNCTION(BlueprintPure,Category="Roguelite|Map") static FRogueliteDungeonLocalMapView GetLocalMapView(const AActor* Source,int32 CurrentRoomNodeId);
    static FRogueliteDungeonMapSnapshot PreviewGraph(int32 Seed,const FRogueliteGraphGenerationSettings& Settings);
    static FRogueliteDungeonLocalMapView FilterOneHop(const FRogueliteDungeonMapSnapshot& Snapshot,int32 CurrentRoomNodeId);
    static TArray<FVector2D> Rectangle(const FTransform& Transform,float Length,float Width,float Margin=0);
    static FString ComputeStableDigest(const FRogueliteDungeonMapSnapshot& Snapshot);
};
