#include "RogueliteDesignerPanel.h"
#include "RogueliteDesignerWorkbench.h"
#include "Editor.h"
#include "IDetailsView.h"
#include "PropertyEditorModule.h"
#include "PropertyEditorDelegates.h"
#include "Modules/ModuleManager.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Text/STextBlock.h"
namespace { UWorld* EditorWorld(){return GEditor?GEditor->GetEditorWorldContext().World():nullptr;}
bool Diagnostic(const FPropertyAndParent& P){if(P.Property.GetMetaData(TEXT("Category"))==TEXT("Developer Diagnostics"))return true;for(const auto* A:P.ParentProperties)if(A->GetMetaData(TEXT("Category"))==TEXT("Developer Diagnostics"))return true;return false;}
bool Reserved(const FPropertyAndParent& P){return P.Property.GetMetaData(TEXT("Category"))==TEXT("Reserved Gameplay");}
}
void SDesignerWorkbenchPanel::Select(int32 I)
{
    Session->Profile=Profiles.IsValidIndex(I)?Profiles[I].Get():nullptr;
    RulesView->SetObject(Session->Profile.Get());DeveloperView->SetObject(Session->Profile.Get());
    Inventory.Empty();Message=TEXT("预设已选择；已有批次保持原配置。修改规则后需 Clear 再 Generate。");
}
FString SDesignerWorkbenchPanel::CurrentSummary() const
{
    FString Why;auto* A=FDesignerWorkbench::Find(EditorWorld(),Why);
    if(!A)return TEXT("当前工作台：0房 / 0走廊 / 0回环。其他生成器批次不在此处管理。");
    const bool Current=A->IsProfileCurrent(Why);
    return FString::Printf(TEXT("当前批次实际使用：%s | Seed %d | 房间 %d | 走廊 %d（Tree %d + Loop %d）\n%s"),*A->AppliedRules.ProfileId.ToString(),A->UsedPreviewSeed,A->Batch.Rooms.Num(),A->Batch.Edges.Num()+A->CommittedLoops.Edges.Num(),A->Batch.Edges.Num(),A->CommittedLoops.Edges.Num(),Current?TEXT("点击 Validate 检查当前几何；导航尚未测试。"):*FRogueliteFloorProfileResolver::FriendlyFailure(Why));
}
FReply SDesignerWorkbenchPanel::Execute(int32 Action)
{
    FDesignerWorkbenchResult R;
    if(Action==0)R=FDesignerWorkbench::Generate(EditorWorld(),Session->Profile.Get(),Session->PreviewSeed);
    else if(Action==1)R=FDesignerWorkbench::Clear(EditorWorld());
    else R=FDesignerWorkbench::Validate(EditorWorld());
    Status=R.Status;Message=R.Message;Raw=R.RawReason;LastSource=R.Source;
    if(R.Source.IsValid())Changed(R.Source.Get());else if(Action==1&&R.bSucceeded)Changed(nullptr);
    return FReply::Handled();
}
void SDesignerWorkbenchPanel::Construct(const FArguments&,TSharedRef<FDesignerWorkbenchSession> InSession,TFunction<void(AActor*)> OnResult)
{
    Session=InSession;Changed=MoveTemp(OnResult);
    for(const auto& Path:FDesignerWorkbench::ProfilePaths()){auto* P=LoadObject<URogueliteFloorProfile>(nullptr,*Path);Profiles.Emplace(P);Options.Add(MakeShared<FString>(P?P->Rules.DisplayName.ToString():TEXT("预设资产未创建：")+Path));}
    auto& Module=FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));
    FDetailsViewArgs Args;Args.bUpdatesFromSelection=false;Args.bShowOptions=false;Args.bShowPropertyMatrixButton=false;Args.NameAreaSettings=FDetailsViewArgs::HideNameArea;
    RulesView=Module.CreateDetailView(Args);RulesView->SetIsPropertyVisibleDelegate(FIsPropertyVisible::CreateLambda([](const FPropertyAndParent& P){return !Diagnostic(P)&&!Reserved(P);}));
    DeveloperView=Module.CreateDetailView(Args);DeveloperView->SetIsPropertyVisibleDelegate(FIsPropertyVisible::CreateLambda([](const FPropertyAndParent& P){return P.Property.GetFName()==TEXT("Rules")||Diagnostic(P);}));
    int32 Initial=0;FString Why;if(auto* A=FDesignerWorkbench::Find(EditorWorld(),Why)){Session->PreviewSeed=A->UsedPreviewSeed;for(int32 I=0;I<Profiles.Num();++I)if(Profiles[I].Get()==A->Profile)Initial=I;}
    Select(Initial);
    ChildSlot[SNew(SScrollBox)+SScrollBox::Slot()[SNew(SVerticalBox)
      +SVerticalBox::Slot().AutoHeight().Padding(8)[SNew(STextBlock).Text(FText::FromString(TEXT("Dungeon Generator / 地牢生成工作台\n这是策划开发预览。正式玩家开局不需要输入 Seed。")))]
      +SVerticalBox::Slot().AutoHeight().Padding(8)[SNew(SComboBox<TSharedPtr<FString>>).OptionsSource(&Options).InitiallySelectedItem(Options[Initial]).OnGenerateWidget_Lambda([](TSharedPtr<FString> V){return SNew(STextBlock).Text(FText::FromString(*V));}).OnSelectionChanged_Lambda([this](TSharedPtr<FString> V,ESelectInfo::Type){Select(Options.IndexOfByKey(V));})[SNew(STextBlock).Text_Lambda([this](){return Session->Profile.IsValid()?Session->Profile->Rules.DisplayName:FText::FromString(TEXT("请选择已安装预设"));})]]
      +SVerticalBox::Slot().AutoHeight().Padding(8)[SNew(SHorizontalBox)
        +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(STextBlock).Text(FText::FromString(TEXT("Opening Mode： ")))]
        +SHorizontalBox::Slot().AutoWidth()[SNew(SComboButton).ButtonContent()[SNew(STextBlock).Text_Lambda([this](){return FText::FromString(Session->Profile.IsValid()&&Session->Profile->Rules.OpeningMode==ERogueliteOpeningMode::OpenPassage?TEXT("Open Passage"):TEXT("Legacy"));})].MenuContent()[SNew(SVerticalBox)
          +SVerticalBox::Slot().AutoHeight()[SNew(SButton).Text(FText::FromString(TEXT("Legacy"))).OnClicked_Lambda([this](){if(auto* P=Session->Profile.Get()){P->Modify();P->Rules.OpeningMode=ERogueliteOpeningMode::Legacy;Status=TEXT("配置已修改，请保存Profile；现有批次需Clear后重新生成。");}return FReply::Handled();})]
          +SVerticalBox::Slot().AutoHeight()[SNew(SButton).Text(FText::FromString(TEXT("Open Passage"))).OnClicked_Lambda([this](){if(auto* P=Session->Profile.Get()){P->Modify();P->Rules.OpeningMode=ERogueliteOpeningMode::OpenPassage;Status=TEXT("配置已修改，请保存Profile；仅新生成实例采用500cm有效高度。");}return FReply::Handled();})]]]
        +SHorizontalBox::Slot().AutoWidth().Padding(12,0)[SNew(SCheckBox).IsChecked_Lambda([this](){return Session->Profile.IsValid()&&Session->Profile->Rules.bGenerateCorridorCeiling?ECheckBoxState::Checked:ECheckBoxState::Unchecked;}).OnCheckStateChanged_Lambda([this](ECheckBoxState S){if(auto* P=Session->Profile.Get()){P->Modify();P->Rules.bGenerateCorridorCeiling=S==ECheckBoxState::Checked;Status=TEXT("配置已修改，请保存Profile；不会自动改动现有走廊。");}})[SNew(STextBlock).Text(FText::FromString(TEXT("Generate Corridor Ceiling")))]]]
      +SVerticalBox::Slot().AutoHeight().Padding(8)[SNew(STextBlock).AutoWrapText(true).Text(FText::FromString(TEXT("生成后可在走廊Details覆盖顶板；Validate保留覆盖。Clear/Regenerate重新应用Profile；不跨Seed迁移。修改预设后请在Advanced保存Profile。")))]
      +SVerticalBox::Slot().AutoHeight().Padding(8)[SNew(SHorizontalBox)
        +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(STextBlock).Text(FText::FromString(TEXT("下次预览 Seed： ")))]
        +SHorizontalBox::Slot().AutoWidth()[SNew(SNumericEntryBox<int32>).AllowSpin(false).Value_Lambda([this](){return TOptional<int32>(Session->PreviewSeed);}).OnValueChanged_Lambda([this](int32 V){Session->PreviewSeed=V;})]
        +SHorizontalBox::Slot().AutoWidth().Padding(8,0)[SNew(SButton).Text(FText::FromString(TEXT("Randomize Seed"))).OnClicked_Lambda([this](){Session->PreviewSeed=FMath::Rand();return FReply::Handled();})]]
      +SVerticalBox::Slot().AutoHeight().Padding(8)[SNew(SHorizontalBox)
        +SHorizontalBox::Slot().AutoWidth()[SNew(SButton).Text(FText::FromString(TEXT("Generate Dungeon"))).OnClicked_Lambda([this](){return Execute(0);})]
        +SHorizontalBox::Slot().AutoWidth().Padding(8,0)[SNew(SButton).Text(FText::FromString(TEXT("Clear Dungeon"))).OnClicked_Lambda([this](){return Execute(1);})]
        +SHorizontalBox::Slot().AutoWidth()[SNew(SButton).Text(FText::FromString(TEXT("Validate Dungeon"))).OnClicked_Lambda([this](){return Execute(2);})]]
      +SVerticalBox::Slot().AutoHeight().Padding(8)[SNew(STextBlock).AutoWrapText(true).Text_Lambda([this](){return FText::FromString(CurrentSummary());})]
      +SVerticalBox::Slot().AutoHeight().Padding(8)[SNew(STextBlock).AutoWrapText(true).Text_Lambda([this](){FString Why;if(Status.Contains(TEXT("Geometry Validated"))){if(!LastSource.IsValid()||LastSource->GetWorld()!=EditorWorld()||GEditor->PlayWorld)return FText::FromString(TEXT("Generation Status：旧批次已失效；请读取当前World或重新生成。"));if(!LastSource->IsProfileCurrent(Why))return FText::FromString(TEXT("Generation Status：配置已改变，需重新生成。\n")+Why);}return FText::FromString(TEXT("Generation Status：")+Status+TEXT("\n")+Message);})]
      +SVerticalBox::Slot().AutoHeight().Padding(8)[SNew(STextBlock).AutoWrapText(true).Text(FText::FromString(TEXT("走廊长度：12–15米（所有楼层，Tree与回环均受硬限制）。\nFloor 1：4房3廊0环。Floor 2/3为待平衡范围；当前最多1环。\n安全屋、最远Boss、正式快速Ready及楼层Gameplay尚未实现。")))]
      +SVerticalBox::Slot().AutoHeight().Padding(8)[SNew(SExpandableArea).InitiallyCollapsed(true).HeaderContent()[SNew(STextBlock).Text(FText::FromString(TEXT("Advanced / 楼层规则与模板（修改后需保存）")))].BodyContent()[SNew(SVerticalBox)
        +SVerticalBox::Slot().AutoHeight()[SNew(SButton).Text(FText::FromString(TEXT("Save Floor Profile"))).OnClicked_Lambda([this](){FDesignerWorkbench::SaveProfile(Session->Profile.Get(),Session->PreviewSeed,Message);return FReply::Handled();})]
        +SVerticalBox::Slot().AutoHeight()[RulesView.ToSharedRef()]
        +SVerticalBox::Slot().AutoHeight()[SNew(SButton).Text(FText::FromString(TEXT("Inspect Actual Templates / 读取模板"))).OnClicked_Lambda([this](){Inventory=FDesignerWorkbench::TemplateInfo(EditorWorld(),Session->Profile.Get(),Session->PreviewSeed);return FReply::Handled();})]
        +SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).AutoWrapText(true).Text_Lambda([this](){return FText::FromString(Inventory);})]
        +SVerticalBox::Slot().AutoHeight()[SNew(SCheckBox).OnCheckStateChanged_Lambda([this](ECheckBoxState S){bDeveloper=S==ECheckBoxState::Checked;})[SNew(STextBlock).Text(FText::FromString(TEXT("Show Developer Diagnostics")))]]
        +SVerticalBox::Slot().AutoHeight()[SNew(SVerticalBox).Visibility_Lambda([this](){return bDeveloper?EVisibility::Visible:EVisibility::Collapsed;})
          +SVerticalBox::Slot().AutoHeight()[DeveloperView.ToSharedRef()]
          +SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).AutoWrapText(true).Text_Lambda([this](){FString Why;auto* A=FDesignerWorkbench::Find(EditorWorld(),Why);return FText::FromString(Raw+(A?FString::Printf(TEXT("\nBatch=%s GraphRev=%d GeometryRev=%d\n%s"),*A->Batch.BatchId.ToString(),A->CommittedLoops.GraphRevision,A->CommittedLoops.GeometryRevision,*A->Latest.Reason):TEXT("")));})]]]]]];
}
