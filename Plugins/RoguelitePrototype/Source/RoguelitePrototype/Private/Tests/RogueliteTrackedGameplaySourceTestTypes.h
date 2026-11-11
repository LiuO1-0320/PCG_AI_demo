#pragma once

#include "Components/StaticMeshComponent.h"
#include "RogueliteRuntimeLoopAssembly.h"
#include "RogueliteTrackedGameplaySourceTestTypes.generated.h"

/** Test-owned observer invoked by the real destruction/lifetime delegates. */
UCLASS(Transient, NotBlueprintable)
class URogueliteTrackedGameplayCallback : public UObject
{
    GENERATED_BODY()
public:
    UPROPERTY() TObjectPtr<ARogueliteRuntimeLoopAssembly> Source;
    uint64 PreviousEpoch = 0;
    int32 Calls = 0;
    bool bEveryOldEpochRejected = true;
    bool bEveryRefreshRejected = true;
    bool bEveryStatusNonValid = true;
    bool bEveryEpochChanged = true;
    FString Evidence;
    void Sample(const TCHAR* Event);
    UFUNCTION() void ActorDestroyed(AActor* Actor);
    UFUNCTION() void ActorEnded(AActor* Actor, EEndPlayReason::Type Reason);
};

/** A deliberately unknown virtual getter. Its real side effects must stay observable. */
UCLASS(Transient, NotBlueprintable)
class URogueliteTrackedGameplayGetter : public UStaticMeshComponent
{
    GENERATED_BODY()
public:
    virtual ECollisionEnabled::Type GetCollisionEnabled() const override;
    TWeakObjectPtr<ARogueliteRuntimeLoopAssembly> Source;
    mutable int32 Calls = 0;
    mutable int32 ReentryCalls = 0;
    mutable bool bInCallback = false;
    mutable bool bChanged = false;
    mutable bool bEveryNestedRefreshRejected = true;
    bool bAttemptRefresh = false;
    bool bChangeRoomLength = false;
};

/** Unknown native Sources must not inherit exact-native observation admission. */
UCLASS(Transient, NotBlueprintable)
class ARogueliteTrackedGameplayUnknownSource : public ARogueliteRuntimeLoopAssembly
{
    GENERATED_BODY()
};
