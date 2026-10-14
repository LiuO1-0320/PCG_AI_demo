#pragma once
#include "RogueliteSeedAssembly.h"

/** Value-only, independent of topology generation, template spawning and connection reservations. */
struct ROGUELITEPROTOTYPE_API FRogueliteRoomTypeAssignmentResult
{
    bool bSucceeded=false;
    FString Reason,SettingsDigest,StableDigest;
    int32 FailedNodeId=INDEX_NONE;
    TArray<ERogueliteSeedRoomType> Types; // Ordered by stable NodeId.
    TArray<FName> EventDefinitionIds;
    TArray<int32> Counts; // Enum numeric order: Combat, Challenge, SupplyShop, Final, Event.
};
class ROGUELITEPROTOTYPE_API FRogueliteRoomTypeAssigner
{
public:
    static FRogueliteRoomTypeAssignmentResult Assign(const FRogueliteRoomGraph& Graph,const FRogueliteRoomTypeAssignmentSettings& Settings,
        const TArray<TArray<ERogueliteSeedRoomType>>& CompatibleTypesByNode={});
    static FString DescribeSettings(const FRogueliteRoomTypeAssignmentSettings& Settings);
};
