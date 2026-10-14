#pragma once
#include "Commandlets/Commandlet.h"
#include "RogueliteSeedAssemblyMapCommandlet.generated.h"
UCLASS()
class URogueliteSeedAssemblyMapCommandlet : public UCommandlet
{
    GENERATED_BODY()
public:
    URogueliteSeedAssemblyMapCommandlet();
    virtual int32 Main(const FString& Params) override;
};
