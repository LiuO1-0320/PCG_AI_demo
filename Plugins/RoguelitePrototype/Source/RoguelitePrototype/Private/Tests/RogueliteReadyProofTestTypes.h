#pragma once
#include "RogueliteRuntimeLoopAssembly.h"
#include "RogueliteReadyProofTestTypes.generated.h"

// Isolated native test types, never part of a FloorProfile/template catalog.
UCLASS(Transient,NotBlueprintable)
class ARogueliteReadyProofUnknownSource : public ARogueliteRuntimeLoopAssembly
{
    GENERATED_BODY()
public:
    virtual FString GetNavigationEpoch() const override;
    mutable int32 EpochCalls=0;
    int32 EpochMode=0;
    FString InjectedEpoch;
};

UCLASS(Transient,NotBlueprintable)
class URogueliteReadyProofUnknownReadiness : public URogueliteNavigationReadinessComponent
{
    GENERATED_BODY()
};
