#pragma once

#include "Commandlets/Commandlet.h"
#include "PCGSettings.h"
#include "CastlePCGPreview.generated.h"

UENUM()
enum class ECastlePCGPreview : uint8
{
    Exploration,
    Vault,
    Chests,
    GuardTickets
};

// Produces inspectable points. It can run on a PCG Volume without a Castle Director.
UCLASS(ClassGroup=(Procedural))
class UCastlePCGPreviewSettings : public UPCGSettings
{
    GENERATED_BODY()

public:
    UPROPERTY(VisibleAnywhere, Category="Castle")
    ECastlePCGPreview Preview = ECastlePCGPreview::Exploration;

    UPROPERTY(EditAnywhere, Category="Castle", meta=(ClampMin="1", PCG_Overridable))
    int32 RunSeed = 24680;

    UPROPERTY(EditAnywhere, Category="Castle", meta=(ClampMin="1", ClampMax="3", PCG_Overridable))
    int32 Floor = 2;

    UPROPERTY(EditAnywhere, Category="Castle", meta=(ClampMin="1", PCG_Overridable, EditCondition="Preview == ECastlePCGPreview::GuardTickets", EditConditionHides))
    int32 FirstGuardKill = 1;

    UPROPERTY(EditAnywhere, Category="Castle", meta=(ClampMin="1", ClampMax="512", PCG_Overridable, EditCondition="Preview == ECastlePCGPreview::GuardTickets", EditConditionHides))
    int32 PreviewRolls = 30;

    UPROPERTY(EditAnywhere, Category="Castle", meta=(ClampMin="3", ClampMax="10000", PCG_Overridable, EditCondition="Preview == ECastlePCGPreview::GuardTickets", EditConditionHides))
    int32 TotalTickets = 15;

    UPROPERTY(EditAnywhere, Category="Castle", meta=(PCG_Overridable, EditCondition="Preview == ECastlePCGPreview::Chests", EditConditionHides))
    bool bShowEliteDeathChest = false;

    UPROPERTY(EditAnywhere, Category="Castle", meta=(PCG_Overridable, EditCondition="Preview == ECastlePCGPreview::Chests", EditConditionHides))
    FVector EliteDeathPosition = FVector::ZeroVector;

#if WITH_EDITOR
    virtual FName GetDefaultNodeName() const override { return TEXT("CastlePreview"); }
    virtual FText GetDefaultNodeTitle() const override { return NSLOCTEXT("CastlePCG", "PreviewNode", "Castle Seeded Preview"); }
    virtual EPCGSettingsType GetType() const override { return EPCGSettingsType::Spatial; }
#endif

protected:
    virtual TArray<FPCGPinProperties> InputPinProperties() const override { return {}; }
    virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
    virtual FPCGElementPtr CreateElement() const override;
};

UCLASS()
class UCastlePCGAuthoringCommandlet : public UCommandlet
{
    GENERATED_BODY()

public:
    UCastlePCGAuthoringCommandlet();
    virtual int32 Main(const FString& Params) override;
};
