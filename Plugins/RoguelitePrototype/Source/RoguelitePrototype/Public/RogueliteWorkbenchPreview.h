#pragma once
#include "CoreMinimal.h"
#include "RogueliteFloorProfile.h"
#include "RogueliteLoopAwareTestRig.h"
#include "RogueliteWorkbenchPreview.generated.h"

/** Thin authoring metadata over the existing batch. No layout, reservation or gameplay algorithm. */
UCLASS(HideCategories=("Loop Aware","Loop Planning"))
class ROGUELITEPROTOTYPE_API ARogueliteWorkbenchPreview : public ARogueliteLoopAwareTestRig
{
    GENERATED_BODY()
public:
    ARogueliteWorkbenchPreview();
    UPROPERTY(BlueprintReadOnly) FGuid WorkbenchId;
    UPROPERTY(BlueprintReadOnly) TObjectPtr<URogueliteFloorProfile> Profile;
    UPROPERTY(BlueprintReadOnly) FRogueliteFloorDefinition AppliedRules;
    UPROPERTY(BlueprintReadOnly) FString AppliedProfilePath;
    UPROPERTY(BlueprintReadOnly) FString AppliedProfileDigest;
    UPROPERTY(BlueprintReadOnly) FString AppliedSettingsDigest;
    UPROPERTY(BlueprintReadOnly) FString AppliedConfigurationDigest;
    UPROPERTY(BlueprintReadOnly) int32 UsedPreviewSeed=0;
    void ApplyProfile(URogueliteFloorProfile* InProfile,const FRogueliteResolvedFloor& Config);
    bool IsProfileCurrent(FString& Reason) const;
};
