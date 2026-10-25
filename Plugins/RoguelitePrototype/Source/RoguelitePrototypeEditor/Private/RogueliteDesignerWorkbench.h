#pragma once
#include "CoreMinimal.h"
#include "RogueliteWorkbenchPreview.h"
#include "RogueliteDungeonMap.h"
struct FDesignerWorkbenchResult
{
    bool bSucceeded=false;
    FString Status=TEXT("尚未生成"),Message,RawReason;
    FRogueliteDungeonMapSnapshot Map;
    TWeakObjectPtr<ARogueliteWorkbenchPreview> Source;
};
/** Editor-only orchestration. C2, topology and spatial algorithms remain in their original services. */
class FDesignerWorkbench
{
public:
    static ARogueliteWorkbenchPreview* Find(UWorld* World,FString& Reason);
    static FDesignerWorkbenchResult Generate(UWorld* World,URogueliteFloorProfile* Profile,int32 PreviewSeed);
    static FDesignerWorkbenchResult Clear(UWorld* World);
    static FDesignerWorkbenchResult Validate(UWorld* World);
    static FString TemplateInfo(UWorld* World,URogueliteFloorProfile* Profile,int32 PreviewSeed);
    static bool SaveProfile(URogueliteFloorProfile* Profile,int32 PreviewSeed,FString& Reason);
    static TArray<FString> ProfilePaths();
};
