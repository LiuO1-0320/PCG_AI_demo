#pragma once
#include "RogueliteRuntimeNavigationSystem.h"
#include "RogueliteWorkbenchCandidateNavigation.generated.h"

/** Independent M1B test-map configuration; does not change the accepted readiness service. */
UCLASS()
class URogueliteWorkbenchCandidateNavigation : public URogueliteRuntimeNavigationSystem
{
    GENERATED_BODY()
public:
    virtual void Configure(const UNavigationSystemConfig& Config) override;
};
