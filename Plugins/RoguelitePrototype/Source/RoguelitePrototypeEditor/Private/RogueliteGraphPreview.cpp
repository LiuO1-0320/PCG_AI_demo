#include "Modules/ModuleManager.h"
#include "RogueliteDungeonMap.h"
#include "RogueliteRuntimeSeedAssembly.h"
#include "Editor.h"
#include "RogueliteDesignerPanel.h"
#include "RogueliteRoomTypeAssignment.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Selection.h"
#include "EngineUtils.h"
#include "ToolMenus.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SWindow.h"
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
    switch(R.RoomType){case ERogueliteSeedRoomType::Challenge:return FLinearColor(1,.4f,.2f);case ERogueliteSeedRoomType::SupplyShop:return FLinearColor(.2f,1,.5f);case ERogueliteSeedRoomType::Event:return FLinearColor(1,.35f,.85f);case ERogueliteSeedRoomType::Final:return FLinearColor(.8f,.3f,1);default:return FLinearColor(.3f,.7f,1);}
}
UWorld* ActiveWorld(){return GEditor?(GEditor->PlayWorld?GEditor->PlayWorld.Get():GEditor->GetEditorWorldContext().World()):nullptr;}
class SRoomMapCanvas : public SLeafWidget
{
public:
    SLATE_BEGIN_ARGS(SRoomMapCanvas){} SLATE_END_ARGS()
    FRogueliteDungeonMapSnapshot Data;int32 Selected=INDEX_NONE;bool bTopology=false;
    void Construct(const FArguments&){}
    virtual FVector2D ComputeDesiredSize(float) const override{return FVector2D(720,550);}
    virtual int32 OnPaint(const FPaintArgs&,const FGeometry& G,const FSlateRect&,FSlateWindowElementList& Out,int32 Layer,const FWidgetStyle&,bool) const override
    {
        if(!Data.bValid)return Layer;
        const bool Spatial=Data.bSpatial&&!bTopology;
        TMap<int32,FVector2D> Centers;TMap<int32,TArray<FVector2D>> Shapes;TArray<FVector2D> All;
        int32 I=0;
        for(const auto& R:Data.Rooms)
        {
            auto& P=Shapes.Add(R.NodeId);
            if(Spatial)P=R.Footprint;
            else{const FVector2D C((I%4)*270,(I/4)*220);P={C+FVector2D(-100,-65),C+FVector2D(100,-65),C+FVector2D(100,65),C+FVector2D(-100,65)};}
            FVector2D C=FVector2D::ZeroVector;for(auto V:P){C+=V;All.Add(V);}if(!P.IsEmpty())C/=P.Num();Centers.Add(R.NodeId,C);++I;
        }
        if(Spatial)for(const auto& E:Data.Connections)All.Append(E.Footprint);
        if(All.IsEmpty())return Layer;FVector2D Min=All[0],Max=Min;for(auto P:All){Min.X=FMath::Min(Min.X,P.X);Min.Y=FMath::Min(Min.Y,P.Y);Max.X=FMath::Max(Max.X,P.X);Max.Y=FMath::Max(Max.Y,P.Y);}
        const FVector2D Size=G.GetLocalSize();const double Scale=FMath::Min((Size.X-100)/FMath::Max(1.,Max.X-Min.X),(Size.Y-100)/FMath::Max(1.,Max.Y-Min.Y));
        const auto Project=[&](FVector2D P){return FVector2D(50+(P.X-Min.X)*Scale,Size.Y-50-(P.Y-Min.Y)*Scale);};
        const auto Line=[&](TArray<FVector2D> P,FLinearColor Color,float Thickness){for(auto& V:P)V=Project(V);FSlateDrawElement::MakeLines(Out,Layer+1,G.ToPaintGeometry(),P,ESlateDrawEffect::None,Color,true,Thickness);};
        const auto Text=[&](FVector2D P,const FString& Value,FLinearColor Color){FSlateDrawElement::MakeText(Out,Layer+2,G.ToPaintGeometry(FVector2D(180,60),FSlateLayoutTransform(P)),Value,FAppStyle::GetFontStyle("SmallFont"),ESlateDrawEffect::None,Color);};
        for(const auto& E:Data.Connections)
        {
            const FLinearColor Color=E.bCommittedLoop?FLinearColor(0,1,1):(E.Role==TEXT("MainPath")?FLinearColor(.9f,.9f,.9f):FLinearColor(1,.6f,.15f));
            FVector2D Mid;
            if(Spatial){auto P=E.Footprint;if(!P.IsEmpty()){const FVector2D First=P[0];P.Add(First);Line(P,Color,E.bCommittedLoop?4:2);}const FVector2D A(E.Start.X,E.Start.Y),B(E.End.X,E.End.Y);Line({A,B},Color,2);Mid=(A+B)/2;}
            else if(Centers.Contains(E.NodeA)&&Centers.Contains(E.NodeB)){Line({Centers[E.NodeA],Centers[E.NodeB]},Color,E.bCommittedLoop?5:2);Mid=(Centers[E.NodeA]+Centers[E.NodeB])/2;}
            else continue;
            Text(Project(Mid)+FVector2D(2,2),FString::Printf(TEXT("E%d %s"),E.EdgeId,E.bCommittedLoop?TEXT("Loop"):TEXT("Tree")),Color);
        }
        for(const auto& R:Data.Rooms)
        {
            auto P=Shapes[R.NodeId];if(P.IsEmpty())continue;const FVector2D First=P[0];P.Add(First);Line(P,R.NodeId==Selected?FLinearColor::Yellow:RoomColor(R),R.NodeId==Selected?4:2);
            FString Label=FString::Printf(TEXT("%d %s\n%s"),R.NodeId,*R.Role,*TypeLabel(R));if(!R.EventDefinitionId.IsNone())Label+=TEXT("\n")+R.EventDefinitionId.ToString();
            Text(Project(Centers[R.NodeId])+FVector2D(-45,-20),Label,RoomColor(R));
        }
        return Layer+2;
    }
};
class SGraphPreview : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SGraphPreview){} SLATE_END_ARGS()
    TWeakObjectPtr<AActor> Source;FRogueliteDungeonMapSnapshot Data;int32 Seed=2,Selected=INDEX_NONE;bool bLive=false,bTopology=false,bLocal=false;double NextRead=0;
    FDelegateHandle BeginHandle,EndHandle,MapHandle;
    ~SGraphPreview(){FEditorDelegates::BeginPIE.Remove(BeginHandle);FEditorDelegates::EndPIE.Remove(EndHandle);FEditorDelegates::OnMapOpened.Remove(MapHandle);}
    void InvalidateLive(){if(!bLive)return;bLive=false;bLocal=false;Source.Reset();Data={};Data.Reason=TEXT("World/source changed or PIE stopped. Previous map invalidated; read the current map again.");RefreshWidgets();}
    TSharedPtr<FDesignerWorkbenchSession> Session;TSharedPtr<SWidgetSwitcher> Pages;TSharedPtr<STextBlock> Diagnostics;
    TSharedPtr<STextBlock> Header,Detail;TSharedPtr<SVerticalBox> NodeList;TSharedPtr<SRoomMapCanvas> Canvas;
    void Construct(const FArguments&)
    {
        Session=MakeShared<FDesignerWorkbenchSession>();
        ChildSlot[SNew(SVerticalBox)
        +SVerticalBox::Slot().AutoHeight()[SNew(SHorizontalBox)
          +SHorizontalBox::Slot().AutoWidth()[SNew(SButton).Text(FText::FromString(TEXT("Dungeon Generator"))).OnClicked_Lambda([this](){Pages->SetActiveWidgetIndex(0);return FReply::Handled();})]
          +SHorizontalBox::Slot().AutoWidth()[SNew(SButton).Text(FText::FromString(TEXT("Dungeon Graph"))).OnClicked_Lambda([this](){Pages->SetActiveWidgetIndex(1);return FReply::Handled();})]]
        +SVerticalBox::Slot().FillHeight(1)[SAssignNew(Pages,SWidgetSwitcher)
        +SWidgetSwitcher::Slot()[SNew(SDesignerWorkbenchPanel,Session.ToSharedRef(),[this](AActor* A){bLive=A!=nullptr;bLocal=false;Source=A;Data=A?URogueliteDungeonMapQuery::Capture(A):FRogueliteDungeonMapSnapshot();RefreshWidgets();})]
        +SWidgetSwitcher::Slot()[SNew(SVerticalBox)
        +SVerticalBox::Slot().AutoHeight().Padding(6)[SNew(SHorizontalBox)
          +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(STextBlock).Text(FText::FromString(TEXT("Seed: ")))]
          +SHorizontalBox::Slot().AutoWidth()[SNew(SNumericEntryBox<int32>).AllowSpin(false).Value_Lambda([this](){return TOptional<int32>(Session->PreviewSeed);}).OnValueChanged_Lambda([this](int32 V){Session->PreviewSeed=V;})]
          +SHorizontalBox::Slot().AutoWidth().Padding(8,0)[SNew(SButton).Text(FText::FromString(TEXT("Logical Preview / 逻辑示意"))).OnClicked_Lambda([this](){bLive=false;bLocal=false;Source.Reset();if(auto* P=Session->Profile.Get()){const auto C=P->ResolvePreview(Session->PreviewSeed);Data=C.bValid?URogueliteDungeonMapQuery::PreviewGraph(C.UsedSeed,C.Source.GraphSettings()):FRogueliteDungeonMapSnapshot();if(C.bValid){auto G=FRogueliteRoomGraphGenerator::Generate(C.UsedSeed,C.Source.GraphSettings());auto T=FRogueliteRoomTypeAssigner::Assign(G.Graph,C.Source.TypeAssignment);for(auto& Room:Data.Rooms)if(T.bSucceeded&&T.Types.IsValidIndex(Room.NodeId)){Room.bRoomTypeAssigned=true;Room.RoomType=T.Types[Room.NodeId];Room.EventDefinitionId=T.EventDefinitionIds[Room.NodeId];}}else Data.Reason=C.Message;}else{Data={};Data.Reason=TEXT("Select an installed Floor Profile first.");}RefreshWidgets();return FReply::Handled();})]
          +SHorizontalBox::Slot().AutoWidth()[SNew(SButton).Text(FText::FromString(TEXT("Read Active Map / 读取当前地图"))).OnClicked_Lambda([this](){ReadSelected();return FReply::Handled();})]
          +SHorizontalBox::Slot().AutoWidth().Padding(8,0)[SNew(SButton).Text(FText::FromString(TEXT("Clear View / 清空显示"))).OnClicked_Lambda([this](){bLive=false;bLocal=false;Source.Reset();Data={};Data.Reason=TEXT("View cleared; dungeon unchanged.");RefreshWidgets();return FReply::Handled();})]]
        +SVerticalBox::Slot().AutoHeight().Padding(6)[SNew(SHorizontalBox)
          +SHorizontalBox::Slot().AutoWidth()[SNew(SButton).Text_Lambda([this](){return FText::FromString(bTopology?TEXT("Show Spatial / 显示真实平面"):TEXT("Show Topology / 显示拓扑示意"));}).OnClicked_Lambda([this](){bTopology=!bTopology;RefreshWidgets();return FReply::Handled();})]
          +SHorizontalBox::Slot().AutoWidth().Padding(8,0)[SNew(SButton).Text_Lambda([this](){return FText::FromString(bLocal?TEXT("Show All / 显示全部"):TEXT("Selected One Hop / 所选节点一跳"));}).OnClicked_Lambda([this](){bLocal=!bLocal;RefreshWidgets();return FReply::Handled();})]]
        +SVerticalBox::Slot().AutoHeight().Padding(6)[SAssignNew(Header,STextBlock).AutoWrapText(true)]
        +SVerticalBox::Slot().FillHeight(1)[SNew(SHorizontalBox)
          +SHorizontalBox::Slot().FillWidth(1)[SNew(SBorder).Padding(6)[SAssignNew(Canvas,SRoomMapCanvas)]]
          +SHorizontalBox::Slot().AutoWidth().Padding(8)[SNew(SScrollBox)+SScrollBox::Slot()[SAssignNew(NodeList,SVerticalBox)]]]
        +SVerticalBox::Slot().AutoHeight().Padding(8)[SAssignNew(Detail,STextBlock).AutoWrapText(true)]
        +SVerticalBox::Slot().AutoHeight()[SNew(SExpandableArea).InitiallyCollapsed(true).HeaderContent()[SNew(STextBlock).Text(FText::FromString(TEXT("Developer Diagnostics")))].BodyContent()[SAssignNew(Diagnostics,STextBlock).AutoWrapText(true)]]]]];
        Data.Reason=TEXT("No current map. Select a generator or read the unique active batch. Logical Preview is schematic only.");RefreshWidgets();
        BeginHandle=FEditorDelegates::BeginPIE.AddSPLambda(this,[this](bool){InvalidateLive();});
        EndHandle=FEditorDelegates::EndPIE.AddSPLambda(this,[this](bool){InvalidateLive();});
        MapHandle=FEditorDelegates::OnMapOpened.AddSPLambda(this,[this](const FString&,bool){InvalidateLive();});
    }
    void ReadSelected()
    {
        UWorld* W=ActiveWorld();AActor* Chosen=nullptr;
        if(W&&GEditor)for(FSelectionIterator It(*GEditor->GetSelectedActors());It;++It)
        {
            AActor* A=Cast<AActor>(*It);if(!URogueliteDungeonMapQuery::IsSupportedSource(A))continue;
            if(A->GetWorld()==W){Chosen=A;break;}
            if(W->IsGameWorld())for(TActorIterator<AActor> Live(W);Live;++Live)if(Live->GetFName()==A->GetFName()&&URogueliteDungeonMapQuery::IsSupportedSource(*Live)){Chosen=*Live;break;}
            if(Chosen)break;
        }
        if(W&&!Chosen)
        {
            TArray<AActor*> Valid;
            for(TActorIterator<AActor> It(W);It;++It)if(URogueliteDungeonMapQuery::IsSupportedSource(*It)&&URogueliteDungeonMapQuery::Capture(*It).bValid)Valid.Add(*It);
            if(Valid.Num()==1)Chosen=Valid[0];
            else{bLive=false;Source.Reset();Data={};Data.Reason=Valid.IsEmpty()?TEXT("No valid live batch in active World. Generate using the existing test rig; this view is read-only."):TEXT("Multiple live batches: select one generator explicitly.");RefreshWidgets();return;}
        }
        bLive=true;bLocal=false;Source=Chosen;Data=URogueliteDungeonMapQuery::Capture(Chosen);Seed=Data.Seed;RefreshWidgets();
        UE_LOG(LogTemp,Display,TEXT("E1_UI_CAPTURE Valid=%d World=%s Rooms=%d Edges=%d Loops=%d Batch=%s"),Data.bValid,*Data.WorldKind,Data.Rooms.Num(),Data.Connections.Num(),Data.LoopCount,*Data.BatchId.ToString());
    }
    virtual void Tick(const FGeometry& G,double Now,float Delta) override
    {
        SCompoundWidget::Tick(G,Now,Delta);if(!bLive||Now<NextRead)return;NextRead=Now+1;
        FRogueliteDungeonMapSnapshot Fresh;
        if(Source.IsValid()&&Source->GetWorld()==ActiveWorld())Fresh=URogueliteDungeonMapQuery::Capture(Source.Get());
        else{Fresh.Reason=TEXT("World/source changed or PIE stopped. Previous map invalidated; read the current map again.");bLive=false;Source.Reset();}
        if(Fresh.Revision!=Data.Revision||Fresh.bValid!=Data.bValid||Fresh.Reason!=Data.Reason){Data=Fresh;RefreshWidgets();}
    }
    void RefreshWidgets()
    {
        auto Visible=Data;
        if(bLocal){const auto Local=URogueliteDungeonMapQuery::FilterOneHop(Data,Selected);Visible.Rooms=Local.Rooms;Visible.Connections=Local.Connections;Visible.bValid=Data.bValid&&Local.bLocated;if(!Local.bLocated)Visible.Reason=Local.Reason;}
        int32 VisibleLoops=0;for(const auto& E:Visible.Connections)VisibleLoops+=E.bCommittedLoop;
        Canvas->Data=Visible;Canvas->Selected=Selected;Canvas->bTopology=bTopology;
        Header->SetText(FText::FromString(FString::Printf(TEXT("%s | Seed %d | Rooms %d | Corridors %d | Committed Loops %d\n%s | Geometry %s | Navigation %s | %s\n%s"),Data.bSpatial&&!bTopology?TEXT("Actual world XY / 真实平面"):TEXT("Topology schematic / 非世界坐标"),Data.Seed,Visible.Rooms.Num(),Visible.Connections.Num(),VisibleLoops,*Data.WorldKind,Data.bGeometryValidated?TEXT("Validated"):TEXT("Not validated"),*Data.Navigation,Data.bValid?TEXT("Current"):TEXT("Unavailable / 已失效"),*Visible.Reason)));
        Diagnostics->SetText(FText::FromString(FString::Printf(TEXT("Batch %s | A/C %d/%d | Graph/Geometry %d/%d | %s | DungeonReady=%d"),*Data.BatchId.ToString(),Data.AlgorithmVersion,Data.ConfigVersion,Data.GraphRevision,Data.GeometryRevision,*Data.Stage,Data.bDungeonReady)));
        NodeList->ClearChildren();for(const auto& R:Visible.Rooms){const int32 Id=R.NodeId;NodeList->AddSlot().AutoHeight().Padding(2)[SNew(SButton).Text(FText::FromString(FString::Printf(TEXT("Node %d | %s | %s"),Id,*R.Role,*TypeLabel(R)))).OnClicked_Lambda([this,Id](){Selected=Id;RefreshWidgets();return FReply::Handled();})];}
        ShowNode();
    }
    void ShowNode()
    {
        Canvas->Selected=Selected;const auto* R=Data.Rooms.FindByPredicate([this](const auto& V){return V.NodeId==Selected;});
        FString Neighbors;if(R)for(int32 N:R->Neighbors)Neighbors+=FString::Printf(TEXT(" %d"),N);
        Detail->SetText(FText::FromString(R?FString::Printf(TEXT("NodeId %d | Degree %d | %s | RoomType %s | TemplateId %s | Event %s\nNeighbors:%s | Dimensions %s\n%s"),R->NodeId,R->Degree,*R->Role,*TypeLabel(*R),*R->TemplateId.ToString(),*R->EventDefinitionId.ToString(),*Neighbors,*R->Dimensions.ToString(),*R->WorldTransform.ToString()):TEXT("Select a node. Tree: white/main, orange/branch; committed loop: cyan; Event: pink. Start/Final are topology roles. View performs no generation or navigation tests.")));
    }
};
TSharedPtr<SWindow> OpenPreview()
{
    auto Existing=PreviewWindow.Pin();if(Existing.IsValid()&&FSlateApplication::Get().GetTopLevelWindows().Contains(Existing.ToSharedRef())){Existing->ShowWindow();Existing->BringToFront();return Existing;}
    auto Window=SNew(SWindow).Title(FText::FromString(TEXT("Roguelite Room Graph / 房间图预览"))).ClientSize(FVector2D(1200,780)).SupportsMaximize(true).SupportsMinimize(true)[SNew(SGraphPreview)];
    Window->SetOnWindowClosed(FOnWindowClosed::CreateLambda([](const TSharedRef<SWindow>& Closed){if(PreviewWindow.Pin()==Closed)PreviewWindow.Reset();}));
    PreviewWindow=Window;FSlateApplication::Get().AddWindow(Window,true);Window->ShowWindow();Window->BringToFront();return Window;
}
}
class FRoguelitePrototypeEditorModule : public IModuleInterface
{
public:
    virtual void StartupModule() override
    {if(!IsRunningCommandlet())UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this,&FRoguelitePrototypeEditorModule::RegisterMenu));}
    void RegisterMenu()
    {
        FToolMenuOwnerScoped Owner(this);auto* Menu=UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Window");
        Menu->FindOrAddSection("Roguelite").AddMenuEntry("RogueliteGraphPreview",FText::FromString(TEXT("Roguelite Room Graph / 房间图预览")),FText::FromString(TEXT("Designer Workbench and read-only topology / actual batch XY map.")),FSlateIcon(),FToolUIAction(FToolMenuExecuteAction::CreateLambda([](const FToolMenuContext&){auto Window=OpenPreview();UE_LOG(LogTemp,Display,TEXT("E1_UI_MENU Visible=%d"),Window.IsValid()&&Window->IsVisible());})));
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
        if(Step==0){auto* Menu=UToolMenus::Get()->FindMenu(TEXT("LevelEditor.MainMenu.Window"));auto* Section=Menu?Menu->FindSection(TEXT("Roguelite")):nullptr;auto* Entry=Section?Section->FindEntry(TEXT("RogueliteGraphPreview")):nullptr;Test->TestNotNull(TEXT("Registered Window menu entry exists"),Entry);if(!Entry)return true;Test->TestTrue(TEXT("Actual registered menu action executed"),Entry->TryExecuteToolUIAction(FToolMenuContext()));Window=PreviewWindow.Pin();Test->TestTrue(TEXT("Window actually visible"),Window.IsValid()&&Window->IsVisible());auto View=StaticCastSharedRef<SGraphPreview>(Window->GetContent());View->Data=URogueliteDungeonMapQuery::PreviewGraph(2,FRogueliteGraphGenerationSettings());View->RefreshWidgets();Step=1;return false;}
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
