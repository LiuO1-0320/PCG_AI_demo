#pragma once
#include "Commandlets/Commandlet.h"
#include "RogueliteRuntimeSeedMapCommandlet.generated.h"
UCLASS()
class URogueliteRuntimeSeedMapCommandlet : public UCommandlet
{
    GENERATED_BODY()
public:
    URogueliteRuntimeSeedMapCommandlet();
    virtual int32 Main(const FString& Params) override;
};
