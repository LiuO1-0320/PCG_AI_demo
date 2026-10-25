#pragma once
#include "CoreMinimal.h"
#include "RogueliteFloorProfile.h"
#include "Widgets/SCompoundWidget.h"
#include "UObject/StrongObjectPtr.h"
class IDetailsView;
class ARogueliteWorkbenchPreview;
struct FDesignerWorkbenchSession
{
    int32 PreviewSeed=FMath::Rand();
    TWeakObjectPtr<URogueliteFloorProfile> Profile;
};
class SDesignerWorkbenchPanel : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SDesignerWorkbenchPanel){} SLATE_END_ARGS()
    void Construct(const FArguments&,TSharedRef<FDesignerWorkbenchSession> InSession,TFunction<void(AActor*)> OnResult);
private:
    TSharedPtr<FDesignerWorkbenchSession> Session;
    TArray<TSharedPtr<FString>> Options;
    TArray<TStrongObjectPtr<URogueliteFloorProfile>> Profiles;
    TSharedPtr<IDetailsView> RulesView,DeveloperView;
    TFunction<void(AActor*)> Changed;
    FString Status=TEXT("尚未生成"),Message=TEXT("选择楼层预设后点击 Generate。已有批次请先 Clear。"),Raw,Inventory;
    TWeakObjectPtr<ARogueliteWorkbenchPreview> LastSource;
    bool bDeveloper=false;
    void Select(int32 Index);
    FReply Execute(int32 Action);
    FString CurrentSummary() const;
};
