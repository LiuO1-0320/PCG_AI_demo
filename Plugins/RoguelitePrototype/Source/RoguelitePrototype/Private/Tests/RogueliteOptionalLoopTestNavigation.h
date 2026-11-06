#pragma once
#include "../RogueliteRuntimeNavigationSystem.h"
#include "RogueliteOptionalLoopTestNavigation.generated.h"

// Independent R1 test-map configuration. Original B3B/B4B3 settings remain untouched.
UCLASS()
class URogueliteOptionalLoopTestNavigation : public URogueliteRuntimeNavigationSystem
{
    GENERATED_BODY()
public:
    virtual void Configure(const UNavigationSystemConfig& Config) override;
};
