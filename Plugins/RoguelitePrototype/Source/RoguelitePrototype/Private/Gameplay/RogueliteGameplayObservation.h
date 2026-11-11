#pragma once

#include "RogueliteRuntimeLoopAssembly.h"
#include "UObject/ObjectKey.h"

// A notification fence. It owns no world objects, validity or Ready result.
struct FRogueliteGameplayObservationWrite final
{
    FRogueliteGameplayObservationWrite(UWorld* World, const TCHAR* Reason)
        : Key(World), bGameThread(IsInGameThread())
    {
        ARogueliteRuntimeLoopAssembly::BeginGameplayObservationWrite(World, Reason);
    }
    ~FRogueliteGameplayObservationWrite()
    {
        ARogueliteRuntimeLoopAssembly::EndGameplayObservationWrite(Key, bGameThread);
    }
    FRogueliteGameplayObservationWrite(const FRogueliteGameplayObservationWrite&) = delete;
    FRogueliteGameplayObservationWrite& operator=(const FRogueliteGameplayObservationWrite&) = delete;
private:
    FObjectKey Key;
    bool bGameThread;
};

struct FRogueliteObservedRoomValue
{
    int32 NodeId=INDEX_NONE;
    FTransform Transform;
    FVector Dimensions=FVector::ZeroVector;
};

// Private Source-owned value. Actor references are identity keys, never HUD data.
struct FRogueliteGameplayObservationValue
{
    uint64 Epoch=0;
    FObjectKey Source, World, Registry, Readiness, Navigation, NavData;
    TArray<FObjectKey> NavigationData;
    FGuid Batch;
    int32 SourceSeed=0, Seed=0, GraphRevision=0, GeometryRevision=0;
    FString ConfigurationDigest, GraphDigest, LayoutDigest, GeometryDigest, NavigationEpoch;
    FTransform SourceTransform;
    TArray<FRogueliteObservedRoomValue> Rooms;
};

struct FRogueliteGameplayObservationState
{
    ERogueliteGameplayReadMode Mode=ERogueliteGameplayReadMode::DeveloperStrict;
    uint64 Epoch=1, UnsupportedSerial=0;
    bool bRefreshing=false;
    FString Reason=TEXT("No strong observation published");
    TOptional<FRogueliteGameplayObservationValue> Published;
};
