#pragma once

#include "CoreTypes.h"

class UClass;
enum class ECastlePCGRegion : uint8;

struct FCastleOrdinaryEnemyMixSlot
{
    int32 RunSeed;
    int32 Floor;
    ECastlePCGRegion Region;
    int32 RoomIndex;
    int32 SlotIndex;
};

enum class ECastleOrdinaryEnemyMixChoice : uint8
{
    Original,
    Melee,
    MissingMeleeClass,
    InvalidChance,
    InvalidMeleeClass,
    InvalidSlot
};

namespace CastleOrdinaryEnemyMix
{
    inline constexpr int32 SelectionVersion = 1;

    // Stable independent ticket in [0, 1). Selection rejects unsupported slots.
    double GetSlotSample(const FCastleOrdinaryEnemyMixSlot& Slot);
    ECastleOrdinaryEnemyMixChoice Select(float Chance, const UClass* MeleeClass, const FCastleOrdinaryEnemyMixSlot& Slot);
    const TCHAR* GetChoiceName(ECastleOrdinaryEnemyMixChoice Choice);
}
