#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "RogueliteDevMapCommandlet.generated.h"

/** Creates only the initial /Game/Roguelite development map; never overwrites it. */
UCLASS()
class ROGUELITEPROTOTYPE_API URogueliteDevMapCommandlet : public UCommandlet
{
    GENERATED_BODY()

public:
    URogueliteDevMapCommandlet();
    virtual int32 Main(const FString& Params) override;
};
