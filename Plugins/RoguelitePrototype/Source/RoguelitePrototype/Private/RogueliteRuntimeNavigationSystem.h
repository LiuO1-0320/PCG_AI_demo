#pragma once
#include "NavigationSystem.h"
#include "NavMesh/RecastNavMesh.h"
#include "NavMesh/NavMeshBoundsVolume.h"
#include "RogueliteRuntimeNavigationSystem.generated.h"

/** Selected only by the B3B map's serialized WorldSettings. Never changes global defaults. */
UCLASS()
class URogueliteRuntimeNavigationSystem : public UNavigationSystemV1
{
    GENERATED_BODY()
public:
    virtual void Configure(const UNavigationSystemConfig& Config) override;
};
UCLASS()
class ARogueliteRuntimeRecastNavMesh : public ARecastNavMesh
{
    GENERATED_BODY()
public:
    ARogueliteRuntimeRecastNavMesh(const FObjectInitializer& Initializer);
};
/** A non-colliding box supplies bounds through UE's actual GetComponentsBoundingBox API. */
UCLASS()
class ARogueliteRuntimeNavigationBounds : public ANavMeshBoundsVolume
{
    GENERATED_BODY()
public:
    ARogueliteRuntimeNavigationBounds(const FObjectInitializer& Initializer);
    void ConfigureBounds(const FBox& Bounds);
private:
    UPROPERTY() TObjectPtr<class UBoxComponent> Coverage;
};
