#pragma once

// Exploration dimensions in centimetres. The vault and queen's arena have their own layouts.
namespace CastleExploration
{
    inline constexpr float RoomHalf = 1100.f;
    inline constexpr float CellSpacing = 2400.f;
    inline constexpr float PassageLength = CellSpacing - 2.f * RoomHalf;
}
