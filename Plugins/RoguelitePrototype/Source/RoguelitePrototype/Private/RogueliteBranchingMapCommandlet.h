#pragma once
#include "Commandlets/Commandlet.h"
#include "RogueliteBranchingMapCommandlet.generated.h"
UCLASS()
class URogueliteBranchingMapCommandlet : public UCommandlet
{
    GENERATED_BODY()
public:
    URogueliteBranchingMapCommandlet();
    virtual int32 Main(const FString& Params) override;
};
