#pragma once
#include "CoreMinimal.h"

// Fixed overhead orientation: world +X is up and world +Y is right.
// Use the same transform for room centers, passage ends and moving markers.
inline FVector2D CastleMapOffset(const FVector& WorldOffset, float Scale)
{
    return FVector2D(WorldOffset.Y*Scale,-WorldOffset.X*Scale);
}
