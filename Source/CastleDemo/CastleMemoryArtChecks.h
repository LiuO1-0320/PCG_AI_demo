#pragma once

#include "CoreMinimal.h"

class ACastleDirector;
class ACastlePlayer;

// Opt-in runtime check for the first-floor visual replacement and original collision.
bool CastleCheckMemoryRoomArt(ACastleDirector* Director, ACastlePlayer* Player, FString& Error);
