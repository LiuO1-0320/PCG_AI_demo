#pragma once

#include "CastleDemo.h"
#include "CastleWeaponProjectile.generated.h"

UCLASS()
class CASTLEDEMO_API ACastleWeaponProjectile : public ACastleBolt
{
    GENERATED_BODY()
public:
    ACastleWeaponProjectile();
    static bool Fire(ACastlePlayer& Player, FVector MuzzleLocation, float ShotDamage, float InaccuracyDegrees, float FanAngle);
};
