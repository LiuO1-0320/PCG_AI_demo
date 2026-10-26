#pragma once
#include "../RogueliteRuntimeNavigationSystem.h"
#include "RogueliteOpenPassageIntegrationNavigation.generated.h"

// Map-local test configuration only. Reuses the original Recast class/readiness API.
UCLASS()
class URogueliteOpenPassageIntegrationNavigation : public URogueliteRuntimeNavigationSystem
{
    GENERATED_BODY()
public:
    virtual void Configure(const UNavigationSystemConfig& Config) override;
};
