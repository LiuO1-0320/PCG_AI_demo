#pragma once

#include "Components/StaticMeshComponent.h"
#include "Gameplay/RogueliteCombatSliceEncounter.h"
#include "RogueliteEncounterSyncReadTestTypes.generated.h"

class ARogueliteRuntimeLoopAssembly;

/** Test-owned virtual getter with observable, synchronous side effects. */
UCLASS(Transient,NotBlueprintable)
class URogueliteEncounterSyncGetter : public UStaticMeshComponent
{
    GENERATED_BODY()
public:
    virtual ECollisionEnabled::Type GetCollisionEnabled() const override;
    TWeakObjectPtr<ARogueliteRuntimeLoopAssembly> Source;
    TWeakObjectPtr<URogueliteSliceEncounter> Encounter;
    mutable int32 QueryCount=0;
    mutable bool bChanged=false;
    mutable int32 ReentryCalls=0,ReentryDepth=0,MaximumReentryDepth=0;
    mutable bool bOuterSlotPreserved=true;
    TFunction<bool()> IsOuterReadCurrent;
    int32 MutationMode=0;
};

/** An unknown native encounter must retain the original query sequence. */
UCLASS(Transient,NotBlueprintable)
class URogueliteEncounterSyncUnknownEncounter : public URogueliteSliceEncounter
{
    GENERATED_BODY()
};
