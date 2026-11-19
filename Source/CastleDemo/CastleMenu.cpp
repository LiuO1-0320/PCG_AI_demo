#include "CastleDemo.h"
#include "CastleLanguage.h"
#include "Blueprint/WidgetTree.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/EditableTextBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"

namespace
{
    struct FEntranceView
    {
        FVector Viewpoint;
        FRotator Rotation;
    };

    FEntranceView GetEntranceView(const FCastleRoom& Entrance, bool bMenu)
    {
        static const FVector Sides[]={FVector::ForwardVector,FVector::RightVector,
            -FVector::ForwardVector,-FVector::RightVector};
        int32 Wall=0;
        for(int32 Side=0;Side<4;++Side)
        {
            if(!(Entrance.Doors&(1<<Side))) {Wall=Side;break;}
        }
        int32 Door=INDEX_NONE;
        for(int32 Distance : {1,3,2})
        {
            const int32 Candidate=(Wall+Distance)%4;
            if(Entrance.Doors&(1<<Candidate)) {Door=Candidate;break;}
        }
        const FVector AlongDoor=Door!=INDEX_NONE?Sides[Door]:FVector::ZeroVector;
        const FVector AlongWall=Sides[Wall];
        const FVector Viewpoint=Entrance.Center-AlongWall*(bMenu?600.:530.)-AlongDoor*(bMenu?600.:510.);
        const FVector Aim=Entrance.Center+AlongWall*(bMenu?435.:350.)+AlongDoor*(bMenu?385.:335.);
        FRotator Rotation=(Aim-Viewpoint).Rotation();
        Rotation.Pitch=bMenu?10.:4.;
        return {Viewpoint,Rotation};
    }
}

void ACastleDirector::BuildStartPreview(APlayerController* Controller)
{
    // The menu uses the actual generated entrance, with the gameplay gates kept closed.
    bStartPreview=true;
    ClearGenerated();
    TGuardValue<int32> PreviewSeed(RunSeed,StartPreviewSeed);
    GenerateLayout(nullptr);
    if(!Rooms.IsValidIndex(0))return;

    const FEntranceView View=GetEntranceView(Rooms[0],true);
    StartPreviewCamera=GetWorld()->SpawnActor<ACameraActor>(View.Viewpoint+FVector(0,0,174),View.Rotation);
    if(!StartPreviewCamera)return;
    StartPreviewCamera->GetCameraComponent()->SetFieldOfView(104.f);
    Controller->SetViewTarget(StartPreviewCamera);
    if(auto* Player=Cast<ACastlePlayer>(Controller->GetPawn()))
    {
        Player->GetCharacterMovement()->DisableMovement();
        Player->SetActorHiddenInGame(true);
    }
    UE_LOG(LogTemp,Display,TEXT("CASTLE_START_PREVIEW Seed=%d Room=Entrance Doors=%d"),StartPreviewSeed,Rooms[0].Doors);
}

void ACastleDirector::EndStartPreview()
{
    if(!bStartPreview)return;
    bStartPreview=false;
    if(auto* Controller=UGameplayStatics::GetPlayerController(this,0))
    {
        if(APawn* Player=Controller->GetPawn())
        {
            Player->SetActorHiddenInGame(false);
            Controller->SetViewTarget(Player);
        }
    }
    if(IsValid(StartPreviewCamera))StartPreviewCamera->Destroy();
    StartPreviewCamera=nullptr;
}

void ACastleDirector::PositionPlayerAtEntrance(ACastlePlayer* Player)
{
    AController* Controller=Player->GetController();
    if(!Controller || !Rooms.IsValidIndex(0))
    {
        Travel(Player,FVector(-400,0,110));
        return;
    }

    const FEntranceView Entrance=GetEntranceView(Rooms[0],false);
    Travel(Player,Entrance.Viewpoint+FVector(0,0,110),Entrance.Rotation.Yaw);
    Controller->SetControlRotation(Entrance.Rotation);
    Player->bHelp=false;
}

bool UCastleStartMenu::ParseSeed(const FString& Text, int32& OutSeed)
{
    const FString Trimmed=Text.TrimStartAndEnd();
    if(Trimmed.IsEmpty()) return false;
    for(TCHAR Digit:Trimmed) if(Digit<TEXT('0') || Digit>TEXT('9')) return false;
    if(Trimmed.Len()>10) return false;
    const int64 Value=FCString::Atoi64(*Trimmed);
    if(Value<1 || Value>MAX_int32) return false;
    OutSeed=static_cast<int32>(Value);
    return true;
}

void UCastleStartMenu::NativeOnInitialized()
{
    Super::NativeOnInitialized();
    if(WidgetTree->RootWidget) return;
    auto* Layout=WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
    Layout->SetBrushColor(FLinearColor::Transparent);
    Layout->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    Layout->SetHorizontalAlignment(HAlign_Right);
    Layout->SetVerticalAlignment(VAlign_Center);
    Layout->SetPadding(FMargin(28.f));
    WidgetTree->RootWidget=Layout;

    auto* Panel=WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
    Panel->SetBrushColor(FLinearColor(.028f,.019f,.028f,.88f));
    Panel->SetPadding(FMargin(22,18));
    Layout->SetContent(Panel);
    auto* Width=WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
    Width->SetWidthOverride(340);
    Panel->SetContent(Width);
    auto* Column=WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
    Width->SetContent(Column);
    auto AddText=[&](const FString& Value, float Size, FLinearColor Color)
    {
        auto* Label=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
        MenuLabels.Add(Label); MenuLabelSources.Add(Value);
        Label->SetText(FText::FromString(CastleText(Value)));
        Label->SetColorAndOpacity(FSlateColor(Color));
        Label->SetFont(CastleUIFont(Size));
        Label->SetJustification(ETextJustify::Center);
        Label->SetAutoWrapText(true);
        auto* Slot=Column->AddChildToVerticalBox(Label); Slot->SetPadding(FMargin(4,8));
        return Label;
    };
    AddText(TEXT("THE HOLLOW CITADEL"),32,FLinearColor(.96f,.72f,.36f));
    AddText(TEXT("Enter a seed to repeat a map, or leave it blank for a random run."),14,FLinearColor(.84f,.88f,.91f));
    SeedInput=WidgetTree->ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass());
    SeedInput->SetSelectAllTextWhenFocused(true);
    FEditableTextBoxStyle InputStyle=SeedInput->GetWidgetStyle();
    InputStyle.SetBackgroundColor(FSlateColor(FLinearColor(.04f,.07f,.11f,1.f)));
    InputStyle.SetForegroundColor(FSlateColor(FLinearColor(.94f,.96f,.97f)));
    InputStyle.SetFocusedForegroundColor(FSlateColor(FLinearColor(.94f,.96f,.97f)));
    InputStyle.TextStyle.SetColorAndOpacity(FSlateColor(FLinearColor(.94f,.96f,.97f)));
    InputStyle.TextStyle.Font=CastleUIFont(20);
    InputStyle.Padding=FMargin(12,8);
    SeedInput->SetWidgetStyle(InputStyle);
    SeedInput->SetHintText(FText::FromString(TEXT("Seed: 1 - 2147483647")));
    Column->AddChildToVerticalBox(SeedInput)->SetPadding(FMargin(8,14));

    auto AddButton=[&](const FString& Caption)
    {
        auto* Button=WidgetTree->ConstructWidget<UButton>(UButton::StaticClass());
        Button->SetBackgroundColor(FLinearColor(.32f,.14f,.18f,1.f));
        auto* Text=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
        MenuLabels.Add(Text); MenuLabelSources.Add(Caption);
        Text->SetText(FText::FromString(CastleText(Caption)));
        Text->SetJustification(ETextJustify::Center);
        Text->SetAutoWrapText(true);
        Text->SetColorAndOpacity(FSlateColor(FLinearColor(.94f,.96f,.97f)));
        Text->SetFont(CastleUIFont(Caption.StartsWith(TEXT("Language:"))?15.f:19.f));
        Button->AddChild(Text);
        Column->AddChildToVerticalBox(Button)->SetPadding(FMargin(8,6));
        return Button;
    };
    LanguageButton=AddButton(TEXT("Language: English (switch to 简体中文)"));
    LanguageButton->OnClicked.AddDynamic(this,&UCastleStartMenu::ToggleLanguage);
    RandomButton=AddButton(TEXT("RANDOM SEED"));
    RandomButton->OnClicked.AddDynamic(this,&UCastleStartMenu::ChooseRandom);
    StartButton=AddButton(TEXT("START GAME"));
    StartButton->OnClicked.AddDynamic(this,&UCastleStartMenu::StartGame);
    Message=AddText(TEXT("Explore connected rooms. Reveal nearby nodes and find the exit."),14,FLinearColor(.7f,.78f,.83f));
    MessageSource=TEXT("Explore connected rooms. Reveal nearby nodes and find the exit.");
    RefreshLanguage();
}

void UCastleStartMenu::NativeConstruct()
{
    Super::NativeConstruct();
    if(auto* PC=GetOwningPlayer())
    {
        FInputModeUIOnly Mode; Mode.SetWidgetToFocus(SeedInput->TakeWidget()); Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
        PC->SetInputMode(Mode); PC->bShowMouseCursor=true;
    }
}

void UCastleStartMenu::ChooseRandom()
{
    const int32 Seed=FMath::RandRange(1,1000000000);
    SeedInput->SetText(FText::FromString(FString::FromInt(Seed)));
    MessageSource=TEXT("Random seed selected. Press START GAME."); RefreshLanguage();
}

void UCastleStartMenu::StartGame()
{
    int32 Seed=0;
    const FString Typed=SeedInput->GetText().ToString().TrimStartAndEnd();
    if(Typed.IsEmpty()) Seed=FMath::RandRange(1,1000000000);
    else if(!ParseSeed(Typed,Seed))
    {
        MessageSource=TEXT("Enter a whole number from 1 to 2147483647."); RefreshLanguage();
        return;
    }
    if(!IsValid(Director)) return;
    RemoveFromParent();
    if(auto* PC=GetOwningPlayer()) {PC->SetInputMode(FInputModeGameOnly()); PC->bShowMouseCursor=false;}
    Director->StartRun(Seed);
}

void UCastleStartMenu::RefreshLanguage()
{
    for(int32 I=0;I<MenuLabels.Num();++I)
        MenuLabels[I]->SetText(FText::FromString(CastleText(MenuLabelSources[I])));
    if(Message) Message->SetText(FText::FromString(CastleText(MessageSource)));
    if(SeedInput) SeedInput->SetHintText(FText::FromString(CastleText(TEXT("Seed: 1 - 2147483647"))));
}

void UCastleStartMenu::ToggleLanguage()
{
    CastleSetChinese(!CastleIsChinese());
    RefreshLanguage();
}
