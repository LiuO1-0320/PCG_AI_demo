#pragma once
#include "CoreMinimal.h"
#include "NavigationSystem.h"
#include "RogueliteRoomTemplateNavigationSystem.generated.h"

/** Isolated P03-D test agent, configured before registering Editor/PIE nav data. */
UCLASS()
class URogueliteRoomTemplateNavigationSystem : public UNavigationSystemV1
{
    GENERATED_BODY()
public:
    virtual void Configure(const UNavigationSystemConfig& Config) override;
};
