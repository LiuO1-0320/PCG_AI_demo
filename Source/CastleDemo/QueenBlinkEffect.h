#pragma once

#include "GameFramework/Actor.h"
#include "QueenBlinkEffect.generated.h"

UCLASS()
class AQueenBlinkEffect : public AActor
{
    GENERATED_BODY()
public:
    AQueenBlinkEffect();
    void Start(bool bAtDestination);
    virtual void Tick(float DeltaSeconds) override;
private:
    UPROPERTY() TObjectPtr<class UInstancedStaticMeshComponent> Sparks;
    UPROPERTY() TObjectPtr<class UPointLightComponent> Glow;
    bool bArrival = false;
    float Age = 0.f;
    void UpdateEffect();
};
