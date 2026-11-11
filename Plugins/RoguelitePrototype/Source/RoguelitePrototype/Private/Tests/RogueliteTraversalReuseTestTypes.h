#pragma once
#include "Components/StaticMeshComponent.h"
#include "RogueliteTraversalReuseTestTypes.generated.h"

class ARogueliteRuntimeLoopAssembly;

/** Isolated negative: a const virtual getter is not a purity guarantee. */
UCLASS(Transient,NotBlueprintable)
class URogueliteTraversalGetterTestComponent : public UStaticMeshComponent
{
    GENERATED_BODY()
public:
    virtual ECollisionEnabled::Type GetCollisionEnabled() const override;
    TWeakObjectPtr<ARogueliteRuntimeLoopAssembly> Source;
    mutable int32 QueryCount=0;
    bool bArmed=false;
    mutable bool bChanged=false;
};
