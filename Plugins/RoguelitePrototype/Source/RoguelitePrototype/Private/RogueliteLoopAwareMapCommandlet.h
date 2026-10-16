#pragma once
#include "Commandlets/Commandlet.h"
#include "RogueliteLoopAwareMapCommandlet.generated.h"
UCLASS()
class URogueliteLoopAwareMapCommandlet : public UCommandlet
{
    GENERATED_BODY()
public:
    URogueliteLoopAwareMapCommandlet();
    virtual int32 Main(const FString& Params) override;
};
