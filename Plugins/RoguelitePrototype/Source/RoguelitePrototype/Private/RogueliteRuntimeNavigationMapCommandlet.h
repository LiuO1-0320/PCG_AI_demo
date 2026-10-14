#pragma once
#include "Commandlets/Commandlet.h"
#include "RogueliteRuntimeNavigationMapCommandlet.generated.h"
UCLASS()
class URogueliteRuntimeNavigationMapCommandlet : public UCommandlet
{
    GENERATED_BODY()
public:
    URogueliteRuntimeNavigationMapCommandlet();
    virtual int32 Main(const FString& Params) override;
};
