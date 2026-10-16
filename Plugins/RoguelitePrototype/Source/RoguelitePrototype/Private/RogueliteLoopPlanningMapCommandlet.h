#pragma once
#include "Commandlets/Commandlet.h"
#include "RogueliteLoopPlanningMapCommandlet.generated.h"
UCLASS()
class URogueliteLoopPlanningMapCommandlet : public UCommandlet
{
    GENERATED_BODY()
public:
    URogueliteLoopPlanningMapCommandlet();
    virtual int32 Main(const FString& Params) override;
};
