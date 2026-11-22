#pragma once

#include "Animation/AnimSingleNodeInstance.h"
#include "QueenCombatAnimation.generated.h"

UCLASS()
class UQueenCombatAnimation : public UAnimSingleNodeInstance
{
    GENERATED_BODY()
public:
    void ReactToHit(float Side);
    float HitTime = -100.f;
    float HitSide = 0.f;
protected:
    virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;
};
