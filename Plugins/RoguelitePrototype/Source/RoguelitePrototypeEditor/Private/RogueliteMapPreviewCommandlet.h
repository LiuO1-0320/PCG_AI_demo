#pragma once
#include "Commandlets/Commandlet.h"
#include "RogueliteMapPreviewCommandlet.generated.h"
UCLASS()
class URogueliteMapPreviewCommandlet : public UCommandlet
{
    GENERATED_BODY()
public:
    URogueliteMapPreviewCommandlet();
    virtual int32 Main(const FString& Params) override;
};
