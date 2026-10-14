#pragma once

#include "CoreMinimal.h"
#include "NavigationSystem.h"
#include "RogueliteC2NavigationSystem.generated.h"

/** Map-local navigation for the C2 test map, including its PIE copy. */
UCLASS()
class ROGUELITEPROTOTYPE_API URogueliteC2NavigationSystem : public UNavigationSystemV1
{
    GENERATED_BODY()

public:
    virtual void Configure(const UNavigationSystemConfig& Config) override;
};
