#pragma once
#include "../RogueliteRuntimeNavigationSystem.h"
#include "RogueliteProductionCandidateNavigation.generated.h"
UCLASS()
class URogueliteProductionCandidateNavigation : public URogueliteRuntimeNavigationSystem
{
    GENERATED_BODY()
public:
    virtual void Configure(const UNavigationSystemConfig& Config) override;
};
