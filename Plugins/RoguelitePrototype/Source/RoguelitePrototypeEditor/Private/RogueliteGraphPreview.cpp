#include "Modules/ModuleManager.h"
#include "RogueliteDungeonMap.h"
#include "RogueliteRuntimeSeedAssembly.h"
#include "Editor.h"
#include "Selection.h"
#include "EngineUtils.h"
#include "ToolMenus.h"
#include "Framework/Docking/TabManager.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SWindow.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SLeafWidget.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Rendering/DrawElements.h"
#include "Styling/AppStyle.h"

namespace
{
TWeakPtr<SWindow> PreviewWindow;
FString TypeLabel(const FRogueliteDungeonMapRoom& R)
{return R.bRoomTypeAssigned?StaticEnum<ERogueliteSeedRoomType>()->GetNameStringByValue(int64(R.RoomType)):TEXT("Unassigned / 未分配");}
FLinearColor RoomColor(const FRogueliteDungeonMapRoom& R)
{
    if(!R.bRoomTypeAssigned)return FLinearColor(.5f,.6f,.7f);
    switch(R.RoomType){case ERogueliteSeedRoomType::Challenge:return FLinearColor(1,.4f,.2f);case ERogueliteSeedRoomType::SupplyShop:return FLinearColor(.2f,1,.5f);case ERogueliteSeedRoomType::Final:return FLinearColor(.8f,.3f,1);default:return FLinearColor(.3f,.7f,1);}
}
class SRoomMapCanvas : public SLeafWidget
{
public:
    SLATE_BEGIN_ARGS(SRoomMapCanvas){} SLATE_END_ARGS()
    FRogueliteDungeonMapSnapshot Data;int32 Selected=INDEX_NONE;
    void Construct(const FArguments&){}
    virtual FVector2D ComputeDesiredSize(float) const override{return FVector2D(720,550);}
    virtual int32 OnPaint(const FPaintArgs&,const FGeometry& G,const FSlateRect&,FSlateWindowElementList& Out,int32 Layer,const FWidgetStyle&,bool) const override
    {
        if(!Data.bValid)return Layer;
        TMap<int32,FVector2D> Centers;TMap<int32,TArray<FVector2D>> Shapes;TArray<FVector2D> All;
        int32 I=0;
        for(const auto& R:Data.Rooms)
        {
            auto& P=Shapes.Add(R.NodeId);
            if(Data.bSpatial)P=R.Footprint;
            else{const FVector2D C((I%5)*250,(I/5)*180);P={C+FVector2D(-85,-45),C+FVector2D(85,-45),C+FVector2D(85,45),C+FVector2D(-85,45)};}
            FVector2D C=FVector2D::ZeroVector;for(auto V:P){C+=V;All.Add(V);}if(!P.IsEmpty())C/=P.Num();Centers.Add(R.NodeId,C);++I;
        }
        if(Data.bSpatial)for(const auto& E:Data.Connections)All.Append(E.Footprint);
        if(All.IsEmpty())return Layer;FVector2D Min=All[0],Max=Min;for(auto P:All){Min.X=FMath::Min(Min.X,P.X);Min.Y=FMath::Min(Min.Y,P.Y);Max.X=FMath::Max(Max.X,P.X);Max.Y=FMath::Max(Max.Y,P.Y);}
        const FVector2D Size=G.GetLocalSize();const double Scale=FMath::Min((Size.X-80)/FMath::Max(1.,Max.X-Min.X),(Size.Y-80)/FMath::Max(1.,Max.Y-Min.Y));
        const auto Project=[&](FVector2D P){return FVector2D(40+(P.X-Min.X)*Scale,Size.Y-40-(P.Y-Min.Y)*Scale);};
        const auto Line=[&](TArray<FVector2D> P,FLinearColor Color,float Thickness){for(auto& V:P)V=Project(V);FSlateDrawElement::MakeLines(Out,Layer+1,G.ToPaintGeometry(),P,ESlateDrawEffect::None,Color,true,Thickness);};
        for(const auto& E:Data.Connections)
        {
            const FLinearColor Color=E.Role==TEXT("MainPath")?FLinearColor(.9f,.9f,.9f):FLinearColor(1,.6f,.15f);
            if(Data.bSpatial){auto P=E.Footprint;if(!P.IsEmpty()){const FVector2D First=P[0];P.Add(First);Line(P,Color,2);}Line({FVector2D(E.Start.X,E.Start.Y),FVector2D(E.End.X,E.End.Y)},Color,2);}
            else if(Centers.Contains(E.NodeA)&&Centers.Contains(E.NodeB))Line({Centers[E.NodeA],Centers[E.NodeB]},Color,3);
        }
        for(const auto& R:Data.Rooms)
        {
            auto P=Shapes[R.NodeId];if(P.IsEmpty())continue;const FVector2D First=P[0];P.Add(First);Line(P,R.NodeId==Selected?FLinearColor::Yellow:RoomColor(R),R.NodeId==Selected?4:2);
            const auto Pos=Project(Centers[R.NodeId])+FVector2D(-45,-16);
            FSlateDrawElement::MakeText(Out,Layer+2,G.ToPaintGeometry(FVector2D(140,35),FSlateLayoutTransform(Pos)),FString::Printf(TEXT("%d %s\n%s"),R.NodeId,*R.Role,*TypeLabel(R)),FAppStyle::GetFontStyle("SmallFont"),ESlateDrawEffect::None,RoomColor(R));
        }
        return Layer+2;
    }
};
class SGraphPreview : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SGraphPreview){} SLATE_END_ARGS()
    TWeakObjectPtr<AActor> Source;FRogueliteDungeonMapSnapshot Data;int32 Seed=2,Selected=INDEX_NONE;bool bLive=false;double NextRead=0;
    TSharedPtr<STextBlock> Header,Detail;TSharedPtr<SVerticalBox> NodeList;TSharedPtr<SRoomMapCanvas> Canvas;
    void Construct(const FArguments&)
    {
        ChildSlot[SNew(SVerticalBox)
        +SVerticalBox::Slot().AutoHeight().Padding(6)[SNew(SHorizontalBox)
          +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(STextBlock).Text(FText::FromString(TEXT("Seed: ")))]
          +SHorizontalBox::Slot().AutoWidth()[SNew(SNumericEntryBox<int32>).AllowSpin(false).Value_Lambda([this](){return TOptional<int32>(Seed);}).OnValueChanged_Lambda([this](int32 V){Seed=V;})]
          +SHorizontalBox::Slot().AutoWidth().Padding(8,0)[SNew(SButton).Text(FText::FromString(TEXT("Preview Logical Graph / 逻辑图"))).OnClicked_Lambda([this](){bLive=false;Source.Reset();Data=URogueliteDungeonMapQuery::PreviewGraph(Seed,FRogueliteGraphGenerationSettings());RefreshWidgets();return FReply::Handled();})]
          +SHorizontalBox::Slot().AutoWidth()[SNew(SButton).Text(FText::FromString(TEXT("Read Selected Batch / 读取所选批次"))).OnClicked_Lambda([this](){ReadSelected();return FReply::Handled();})]]
        +SVerticalBox::Slot().AutoHeight().Padding(6)[SAssignNew(Header,STextBlock).AutoWrapText(true)]
        +SVerticalBox::Slot().FillHeight(1)[SNew(SHorizontalBox)
          +SHorizontalBox::Slot().FillWidth(1)[SNew(SBorder).Padding(6)[SAssignNew(Canvas,SRoomMapCanvas)]]
          +SHorizontalBox::Slot().AutoWidth().Padding(8)[SNew(SScrollBox)+SScrollBox::Slot()[SAssignNew(NodeList,SVerticalBox)]]]
        +SVerticalBox::Slot().AutoHeight().Padding(8)[SAssignNew(Detail,STextBlock).AutoWrapText(true)]];
        Data.Reason=TEXT("No graph or batch selected. Enter Seed for a logical schematic, or select a generator and read its batch.");RefreshWidgets();
    }
    void ReadSelected()
    {
        AActor* SelectedActor=nullptr;
        if(GEditor)for(FSelectionIterator It(*GEditor->GetSelectedActors());It;++It){AActor* A=Cast<AActor>(*It);if(Cast<ARogueliteSeedAssembly>(A)||Cast<ARogueliteRuntimeSeedAssembly>(A)){SelectedActor=A;break;}}
        // During PIE use the corresponding live generator, never its empty editor-world copy.
        if(GEditor&&GEditor->PlayWorld&&SelectedActor){for(TActorIterator<ARogueliteRuntimeSeedAssembly> It(GEditor->PlayWorld);It;++It)if(It->GetFName()==SelectedActor->GetFName()){SelectedActor=*It;break;}}
        bLive=true;Source=SelectedActor;Data=URogueliteDungeonMapQuery::Capture(SelectedActor);Seed=Data.Seed;RefreshWidgets();
    }
    virtual void Tick(const FGeometry& G,double Now,float Delta) override
    {
        SCompoundWidget::Tick(G,Now,Delta);if(!bLive||Now<NextRead)return;NextRead=Now+1;
        const auto Fresh=URogueliteDungeonMapQuery::Capture(Source.Get());
        if(Fresh.Revision!=Data.Revision||Fresh.bValid!=Data.bValid||Fresh.Reason!=Data.Reason){Data=Fresh;RefreshWidgets();}
    }
    void RefreshWidgets()
    {
        Canvas->Data=Data;Canvas->Selected=Selected;
        Header->SetText(FText::FromString(FString::Printf(TEXT("%s | Seed %d | Algorithm/Config %d/%d | Catalog/Solver (source settings) %d/%d\nBatch %s | %s | %s | Navigation: %s | DungeonReady: %s\nGraph %s | Map %s\n%s"),Data.bSpatial?TEXT("Actual world XY footprint / 真实空间"):TEXT("Logical schematic only / 示意坐标"),Data.Seed,Data.AlgorithmVersion,Data.ConfigVersion,Data.TemplateCatalogVersion,Data.SolverVersion,*Data.BatchId.ToString(),*Data.WorldKind,*Data.Stage,*Data.Navigation,Data.bDungeonReady?TEXT("true"):TEXT("false"),*Data.GraphDigest,*Data.StableDataDigest,*Data.Reason)));
        NodeList->ClearChildren();for(const auto& R:Data.Rooms){const int32 Id=R.NodeId;NodeList->AddSlot().AutoHeight().Padding(2)[SNew(SButton).Text(FText::FromString(FString::Printf(TEXT("Node %d | %s | %s"),Id,*R.Role,*TypeLabel(R)))).OnClicked_Lambda([this,Id](){Selected=Id;ShowNode();return FReply::Handled();})];}
        ShowNode();
    }
    void ShowNode()
    {
        Canvas->Selected=Selected;const auto* R=Data.Rooms.FindByPredicate([this](const auto& V){return V.NodeId==Selected;});
        Detail->SetText(FText::FromString(R?FString::Printf(TEXT("NodeId %d | Degree %d | %s | RoomType %s | TemplateId %s\n%s\n%s | Dimensions %s | Exploration: unavailable (reserved)"),R->NodeId,R->Degree,*R->Role,*TypeLabel(*R),*R->TemplateId.ToString(),*R->TemplateClassPath,*R->WorldTransform.ToString(),*R->Dimensions.ToString()):TEXT("Select a node in the list. White: main path; orange: optional branch. Preview performs no generation, alignment, navigation or AI tests.")));
    }
};
TSharedPtr<SWindow> OpenPreview()
{
    auto Existing=PreviewWindow.Pin();if(Existing.IsValid()&&FSlateApplication::Get().GetTopLevelWindows().Contains(Existing.ToSharedRef())){Existing->ShowWindow();Existing->BringToFront();return Existing;}
    auto Window=SNew(SWindow).Title(FText::FromString(TEXT("Roguelite Room Graph / 房间图预览"))).ClientSize(FVector2D(1100,720)).SupportsMaximize(true).SupportsMinimize(true)[SNew(SGraphPreview)];
    Window->SetOnWindowClosed(FOnWindowClosed::CreateLambda([](const TSharedRef<SWindow>& Closed){if(PreviewWindow.Pin()==Closed)PreviewWindow.Reset();}));
    PreviewWindow=Window;FSlateApplication::Get().AddWindow(Window,true);Window->ShowWindow();Window->BringToFront();return Window;
}
}
class FRoguelitePrototypeEditorModule : public IModuleInterface
{
public:
    virtual void StartupModule() override
    {
        if(IsRunningCommandlet())return;
        UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this,&FRoguelitePrototypeEditorModule::RegisterMenu));
    }
    void RegisterMenu()
    {
        FToolMenuOwnerScoped Owner(this);auto* Menu=UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Window");
        Menu->FindOrAddSection("Roguelite").AddMenuEntry("RogueliteGraphPreview",FText::FromString(TEXT("Roguelite Room Graph / 房间图预览")),FText::FromString(TEXT("Read-only Seed topology and current batch XY map.")),FSlateIcon(),FUIAction(FExecuteAction::CreateLambda([](){auto Window=OpenPreview();UE_LOG(LogTemp,Display,TEXT("E1_UI_MENU Visible=%d"),Window.IsValid()&&Window->IsVisible());}))); 
    }
    virtual void ShutdownModule() override
    {
        if(IsRunningCommandlet())return;UToolMenus::UnRegisterStartupCallback(this);UToolMenus::UnregisterOwner(this);
        if(FSlateApplication::IsInitialized())if(auto Window=PreviewWindow.Pin())Window->RequestDestroyWindow();PreviewWindow.Reset();
    }
};
IMPLEMENT_MODULE(FRoguelitePrototypeEditorModule,RoguelitePrototypeEditor)

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGraphWindowTest,"Roguelite.DungeonMap.WindowLifecycle",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
class FGraphWindowLifecycleCommand : public IAutomationLatentCommand
{
public:
    FAutomationTestBase* Test;TMap<FString,FTransform> Before;TSharedPtr<SWindow> Window;int32 Step=0;double Last=0;
    explicit FGraphWindowLifecycleCommand(FAutomationTestBase* T):Test(T)
    {if(auto* W=GEditor->GetEditorWorldContext().World())for(TActorIterator<AActor> It(W);It;++It)Before.Add(It->GetPathName(),It->GetActorTransform());}
    virtual bool Update() override
    {
        const double Now=FPlatformTime::Seconds();if(Now-Last<.25)return false;Last=Now;
        if(Step==0){Window=OpenPreview();Test->TestTrue(TEXT("Window actually visible"),Window.IsValid()&&Window->IsVisible());auto View=StaticCastSharedRef<SGraphPreview>(Window->GetContent());View->Data=URogueliteDungeonMapQuery::PreviewGraph(2,FRogueliteGraphGenerationSettings());View->RefreshWidgets();Step=1;return false;}
        if(Step==1){Window->RequestDestroyWindow();Step=2;return false;}
        if(Step==2){Test->TestFalse(TEXT("Closed window removed from Slate registry"),FSlateApplication::Get().GetTopLevelWindows().Contains(Window.ToSharedRef()));Window=OpenPreview();auto View=StaticCastSharedRef<SGraphPreview>(Window->GetContent());View->Data=URogueliteDungeonMapQuery::PreviewGraph(2,FRogueliteGraphGenerationSettings());View->RefreshWidgets();Step=3;return false;}
        Test->TestTrue(TEXT("Reopened window remains registered and visible"),Window.IsValid()&&Window->IsVisible()&&FSlateApplication::Get().GetTopLevelWindows().Contains(Window.ToSharedRef()));
        int32 Count=0;if(auto* W=GEditor->GetEditorWorldContext().World())for(TActorIterator<AActor> It(W);It;++It){++Count;const auto* T=Before.Find(It->GetPathName());Test->TestTrue(TEXT("UI leaves actors unchanged"),T&&T->Equals(It->GetActorTransform()));}
        Test->TestEqual(TEXT("UI does not spawn/delete actors"),Count,Before.Num());UE_LOG(LogTemp,Display,TEXT("E1_UI_VISIBLE_LIFECYCLE VerifiedActors=%d"),Count);return true;
    }
};
bool FGraphWindowTest::RunTest(const FString&)
{
    FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FGraphWindowLifecycleCommand>(this));return true;
}
#endif
