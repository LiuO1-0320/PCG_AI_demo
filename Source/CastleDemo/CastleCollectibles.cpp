#include "CastleCollectibles.h"

const FString& FCastleCollectibleDefinition::GetName(bool bChinese) const
{
    return bChinese ? ChineseName : EnglishName;
}

FString FCastleCollectibleDefinition::GetEffect(bool bChinese) const
{
    TArray<FString> Effects;
    if (DamageBonus != 0.f)
        Effects.Add(bChinese ? FString::Printf(TEXT("子弹伤害 +%.0f"), DamageBonus)
                            : FString::Printf(TEXT("Bullet damage +%.0f"), DamageBonus));
    if (ShieldBonus != 0.f)
        Effects.Add(bChinese ? FString::Printf(TEXT("护盾上限 +%.0f，并回满护盾"), ShieldBonus)
                            : FString::Printf(TEXT("Maximum shield +%.0f. Fully restores shield"), ShieldBonus));
    if (HealthBonus != 0.f)
        Effects.Add(bChinese ? FString::Printf(TEXT("生命上限 +%.0f，并回满生命"), HealthBonus)
                            : FString::Printf(TEXT("Maximum health +%.0f. Fully restores health"), HealthBonus));
    return FString::Join(Effects, bChinese ? TEXT("；") : TEXT(" / "));
}

const FString& FCastleCollectibleDefinition::GetMemory(bool bChinese) const
{
    return bChinese ? ChineseMemory : EnglishMemory;
}

int32 CastleCollectibleIndex(int32 Kind)
{
    // Normalize the remainder without negating Kind, which could be MIN_int32.
    const int32 Remainder = Kind % 3;
    return Remainder < 0 ? Remainder + 3 : Remainder;
}

const FCastleCollectibleDefinition& CastleCollectible(int32 Kind)
{
    static const FCastleCollectibleDefinition Definitions[] = {
        {
            FName(TEXT("PaintersCharcoal")),
            TEXT("Painter's Charcoal"), TEXT("画师的炭笔"),
            TEXT("The queen once sat before my easel at dawn."),
            TEXT("女王曾在清晨坐在我的画架前。"),
            6.f, 0.f, 0.f
        },
        {
            FName(TEXT("CourtMedallion")),
            TEXT("Court Medallion"), TEXT("宫廷徽章"),
            TEXT("This medallion once opened the palace halls to me."),
            TEXT("这枚徽章曾让我走进王宫的长廊。"),
            0.f, 25.f, 0.f
        },
        {
            FName(TEXT("FadedPocketWatch")),
            TEXT("Faded Pocket Watch"), TEXT("褪色的怀表"),
            TEXT("Its hands recall the hour of my last royal portrait."),
            TEXT("指针记着我最后一次为女王作画的时刻。"),
            0.f, 0.f, 25.f
        }
    };
    return Definitions[CastleCollectibleIndex(Kind)];
}
