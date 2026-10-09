#pragma once

#include "CoreMinimal.h"
#include "Fonts/SlateFontInfo.h"

bool CastleIsChinese();
void CastleSetChinese(bool bChinese, bool bSave = true);
FString CastleText(const FString& English);
FSlateFontInfo CastleUIFont(float Size);
