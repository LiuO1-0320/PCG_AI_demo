#pragma once
#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "RogueliteCorridorAssemblyMapCommandlet.generated.h"
UCLASS()
class URogueliteCorridorAssemblyMapCommandlet : public UCommandlet
{
    GENERATED_BODY()
public:
    URogueliteCorridorAssemblyMapCommandlet();
    virtual int32 Main(const FString& Params) override;
};
