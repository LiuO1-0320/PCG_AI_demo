#pragma once
#include "../RogueliteRuntimeNavigationSystem.h"
#include "RogueliteFloorTemplateWeightNavigation.generated.h"

// Independent F1 test-map configuration. Original B3B/B4B3 settings remain untouched.
UCLASS()
class URogueliteFloorTemplateWeightNavigation : public URogueliteRuntimeNavigationSystem
{
    GENERATED_BODY()
public:
    virtual void Configure(const UNavigationSystemConfig& Config) override;
};
