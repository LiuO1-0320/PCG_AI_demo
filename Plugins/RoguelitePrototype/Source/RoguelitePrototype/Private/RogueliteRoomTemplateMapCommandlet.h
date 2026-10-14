#pragma once
#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "RogueliteRoomTemplateMapCommandlet.generated.h"
UCLASS()
class URogueliteRoomTemplateMapCommandlet : public UCommandlet
{
    GENERATED_BODY()
public:
    URogueliteRoomTemplateMapCommandlet();
    virtual int32 Main(const FString& Params) override;
};
