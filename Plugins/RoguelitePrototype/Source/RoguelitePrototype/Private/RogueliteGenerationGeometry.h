#pragma once
#include "RogueliteSeedAssembly.h"

// Shared configuration/instance adapter. Never generates topology or owns socket leases.
namespace RogueliteGenerationGeometry
{
    bool IsLegacyDefaults(const FRogueliteSeedLayoutSettings& Settings);
    FString SettingsText(const FRogueliteSeedLayoutSettings& Settings);
    FString SettingsDigest(const FRogueliteSeedLayoutSettings& Settings);
    FString GeometryDigest(const FRogueliteSeedLayoutSettings& Settings);
    FString LegacyCatalogText(const UScriptStruct* Type, const void* Value);
    bool Validate(const FRogueliteSeedLayoutSettings& Settings, FString& Reason);
    TArray<FRogueliteDoorConfig> Doors(const ARogueliteParametricRoom* Room);
    TArray<FRogueliteDoorConfig> EffectiveDoors(const ARogueliteParametricRoom* Source, const FRogueliteSeedLayoutSettings& Settings);
    // Editor metadata instance or not-yet-registered deferred Runtime instance only.
    bool ConfigureTemplate(ARogueliteRoomTemplate* Room, const FRogueliteSeedLayoutSettings& Settings, FString& Reason);
    void ApplyPlannedDoors(ARogueliteRoomTemplate* Room, const FRogueliteSeedRoomRecord& Record);
}
