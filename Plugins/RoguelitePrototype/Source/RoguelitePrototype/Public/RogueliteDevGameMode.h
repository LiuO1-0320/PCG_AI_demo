#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "RogueliteDevGameMode.generated.h"

/** Empty development mode. Uses only engine-native player classes. */
UCLASS()
class ROGUELITEPROTOTYPE_API ARogueliteDevGameMode : public AGameModeBase
{
    GENERATED_BODY()

public:
    ARogueliteDevGameMode();
    virtual void StartPlay() override;
};
