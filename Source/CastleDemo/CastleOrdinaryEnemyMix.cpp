#include "CastleOrdinaryEnemyMix.h"

#include "CastleDemo.h"
#include "CastleQueen.h"

namespace CastleOrdinaryEnemyMix
{
namespace
{
    constexpr uint32 MixPurpose = 0x36589d2bu;
    constexpr uint32 ExplorationSalt = 0x21fe82b9u;
    constexpr uint32 TreasureCaveSalt = 0x76cb9f03u;

    uint32 GetRegionSalt(ECastlePCGRegion Region)
    {
        switch (Region)
        {
        case ECastlePCGRegion::Exploration: return ExplorationSalt;
        case ECastlePCGRegion::TreasureCave: return TreasureCaveSalt;
        default: return 0u;
        }
    }

    uint32 AppendSlotWord(uint32 Hash, uint32 Word)
    {
        Hash ^= Word;
        Hash ^= Hash >> 16;
        Hash *= 0x7feb352du;
        Hash ^= Hash >> 15;
        Hash *= 0x846ca68bu;
        return Hash ^ (Hash >> 16);
    }
}

double GetSlotSample(const FCastleOrdinaryEnemyMixSlot& Slot)
{
    uint32 Hash = AppendSlotWord(MixPurpose, static_cast<uint32>(SelectionVersion));
    Hash = AppendSlotWord(Hash, static_cast<uint32>(Slot.RunSeed));
    Hash = AppendSlotWord(Hash, static_cast<uint32>(Slot.Floor));
    Hash = AppendSlotWord(Hash, GetRegionSalt(Slot.Region));
    Hash = AppendSlotWord(Hash, static_cast<uint32>(Slot.RoomIndex));
    Hash = AppendSlotWord(Hash, static_cast<uint32>(Slot.SlotIndex));
    return static_cast<double>(Hash) / 4294967296.0;
}

ECastleOrdinaryEnemyMixChoice Select(float Chance, const UClass* MeleeClass, const FCastleOrdinaryEnemyMixSlot& Slot)
{
    using EChoice = ECastleOrdinaryEnemyMixChoice;
    if (!FMath::IsFinite(Chance) || Chance < 0.f || Chance > 1.f)
    {
        return EChoice::InvalidChance;
    }
    if (Chance == 0.f)
    {
        return EChoice::Original;
    }
    if (GetRegionSalt(Slot.Region) == 0u || Slot.Floor <= 0 || Slot.RoomIndex < 0 || Slot.SlotIndex < 0)
    {
        return EChoice::InvalidSlot;
    }
    if (!MeleeClass)
    {
        return EChoice::MissingMeleeClass;
    }
    if (!IsValid(MeleeClass) || !MeleeClass->IsChildOf(ACastleEnemy::StaticClass()) ||
        MeleeClass->IsChildOf(ACastleQueen::StaticClass()) ||
        MeleeClass->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists))
    {
        return EChoice::InvalidMeleeClass;
    }
    const ACastleEnemy* Defaults = Cast<ACastleEnemy>(MeleeClass->GetDefaultObject());
    if (!Defaults || !Defaults->bUseMeleeV1 || Defaults->bBoss || Defaults->bElite)
    {
        return EChoice::InvalidMeleeClass;
    }
    return Chance == 1.f || GetSlotSample(Slot) < static_cast<double>(Chance) ? EChoice::Melee : EChoice::Original;
}

const TCHAR* GetChoiceName(ECastleOrdinaryEnemyMixChoice Choice)
{
    switch (Choice)
    {
    case ECastleOrdinaryEnemyMixChoice::Original: return TEXT("Original");
    case ECastleOrdinaryEnemyMixChoice::Melee: return TEXT("Melee");
    case ECastleOrdinaryEnemyMixChoice::MissingMeleeClass: return TEXT("MissingMeleeClass");
    case ECastleOrdinaryEnemyMixChoice::InvalidChance: return TEXT("InvalidChance");
    case ECastleOrdinaryEnemyMixChoice::InvalidMeleeClass: return TEXT("InvalidMeleeClass");
    case ECastleOrdinaryEnemyMixChoice::InvalidSlot: return TEXT("InvalidSlot");
    }
    return TEXT("Unknown");
}
}
