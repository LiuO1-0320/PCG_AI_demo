#pragma once
#include "CoreMinimal.h"
#include "RogueliteSeedAssembly.h"
#include "RoguelitePreparedTemplates.generated.h"

/** Authored local data captured in Editor, never a socket reservation or a live probe. */
USTRUCT(BlueprintType)
struct ROGUELITEPROTOTYPE_API FRoguelitePreparedTemplateRow
{
    GENERATED_BODY()
    UPROPERTY() FName Id;
    UPROPERTY() TSoftClassPtr<ARogueliteRoomTemplate> TemplateClass;
    UPROPERTY() FString OptionDigest;
    UPROPERTY() FRogueliteTemplateDescriptor Descriptor;
    UPROPERTY() TArray<FRogueliteDoorSocket> Sockets;
    UPROPERTY() FString Signature;
    UPROPERTY() FString MemberSignature;
    UPROPERTY() FRogueliteTemplateDescriptor SourceDescriptor;
    UPROPERTY() TArray<FRogueliteDoorConfig> EffectiveDoors;
};

USTRUCT(BlueprintType)
struct ROGUELITEPROTOTYPE_API FRoguelitePreparedTemplateCatalog
{
    GENERATED_BODY()
    UPROPERTY() int32 Version=1;
    UPROPERTY() int32 TemplateCatalogVersion=0;
    UPROPERTY() FString Digest;
    UPROPERTY() TArray<FRoguelitePreparedTemplateRow> Rows;
    UPROPERTY() FString GeometrySettingsDigest;
    FString ComputeDigest() const;
    bool IsCurrent(const FRogueliteSeedLayoutSettings& Settings,FString& Reason) const;
    static FString OptionDigest(const FRogueliteSeedTemplateOption& Option);
    static bool CaptureEditor(AActor* Owner,const FRogueliteSeedLayoutSettings& Settings,
        FRoguelitePreparedTemplateCatalog& Out,FString& Reason);
};
