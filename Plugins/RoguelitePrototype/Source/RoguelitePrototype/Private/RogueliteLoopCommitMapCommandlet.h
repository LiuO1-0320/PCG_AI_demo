#pragma once
#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "RogueliteLoopCommitMapCommandlet.generated.h"
UCLASS()
class URogueliteLoopCommitMapCommandlet : public UCommandlet
{
    GENERATED_BODY()
public:
    URogueliteLoopCommitMapCommandlet();
    virtual int32 Main(const FString& Params) override;
};
