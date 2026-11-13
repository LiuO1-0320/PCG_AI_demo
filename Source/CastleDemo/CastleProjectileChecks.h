#pragma once

#include "CoreMinimal.h"

class ACastleDirector;
class ACastlePlayer;

// Opt-in game-world check. The caller should ResetRun afterward because the
// player's private last-damage timestamp intentionally follows real damage.
bool CastleCheckEnemyProjectiles(ACastleDirector* Director, ACastlePlayer* Player, FString& FailureReason);
