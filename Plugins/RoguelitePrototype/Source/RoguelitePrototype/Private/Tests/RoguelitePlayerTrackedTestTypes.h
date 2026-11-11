#pragma once
#include "Camera/CameraComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Gameplay/RogueliteCombatSlicePlayer.h"
#include "Tests/RogueliteCombatSliceRig.h"
#include "RoguelitePlayerTrackedTestTypes.generated.h"

UCLASS(Transient, NotBlueprintable)
class ARoguelitePlayerTrackedUnknownPlayer : public ARogueliteSlicePlayer
{
    GENERATED_BODY()
};
UCLASS(Transient, NotBlueprintable)
class ARoguelitePlayerTrackedUnknownRig : public ARogueliteCombatSliceRig
{
    GENERATED_BODY()
};
UCLASS(Transient, NotBlueprintable)
class URoguelitePlayerTrackedUnknownCamera : public UCameraComponent
{
    GENERATED_BODY()
};
UCLASS(Transient, NotBlueprintable)
class URoguelitePlayerTrackedReentryMesh : public UStaticMeshComponent
{
    GENERATED_BODY()
public:
    virtual ECollisionEnabled::Type GetCollisionEnabled() const override;
    TWeakObjectPtr<ARogueliteSlicePlayer> Player;
    mutable bool bInCallback=false;
    mutable int32 Calls=0,ReentryCalls=0;
    mutable bool bEveryReentryRejected=true;
};
