#pragma once
#include "../RogueliteRuntimeNavigationSystem.h"
#include "RogueliteRoomRoleNavigation.generated.h"

// Independent G1 test-map configuration. Original B3B/B4B3 settings remain untouched.
UCLASS()
class URogueliteRoomRoleNavigation : public URogueliteRuntimeNavigationSystem
{
    GENERATED_BODY()
public:
    virtual void Configure(const UNavigationSystemConfig& Config) override;
};
