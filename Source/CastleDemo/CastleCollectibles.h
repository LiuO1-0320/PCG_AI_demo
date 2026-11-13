#pragma once

#include "CoreMinimal.h"

// Stable entries 0, 1 and 2 preserve the existing seeded reward mapping.
struct FCastleCollectibleDefinition
{
    FName Id;
    FString EnglishName;
    FString ChineseName;
    FString EnglishMemory;
    FString ChineseMemory;
    float DamageBonus = 0.f;
    float ShieldBonus = 0.f;
    float HealthBonus = 0.f;

    const FString& GetName(bool bChinese) const;
    FString GetEffect(bool bChinese) const;
    const FString& GetMemory(bool bChinese) const;
};

int32 CastleCollectibleIndex(int32 Kind);
const FCastleCollectibleDefinition& CastleCollectible(int32 Kind);
