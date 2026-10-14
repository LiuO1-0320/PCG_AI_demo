#pragma once
#include "RogueliteRoomGraph.h"

// Explicit version dispatch; no Actor, World, spatial coordinates or socket state.
namespace RogueliteGraphV2
{
    FString DescribeSettings(const FRogueliteGraphGenerationSettings& Settings);
    FString DescribeTopology(const FRogueliteRoomGraph& Graph);
    bool ValidateSettings(const FRogueliteGraphGenerationSettings& Settings, ERogueliteGraphError& Error, FString& Reason);
    bool ValidateGraph(const FRogueliteRoomGraph& Graph, const FRogueliteGraphGenerationSettings& Settings, FString& Reason);
    FRogueliteGraphGenerationResult Generate(int32 Seed, const FRogueliteGraphGenerationSettings& Settings);
}
