#include "CastleDemo.h"
#include "CastleLanguage.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/EditableTextBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Kismet/GameplayStatics.h"

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
    auto* Background=WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
    Background->SetBrushColor(FLinearColor(.014f,.024f,.038f,1.f));
    Background->SetHorizontalAlignment(HAlign_Center);
    Background->SetVerticalAlignment(VAlign_Center);
    WidgetTree->RootWidget=Background;

    auto* Width=WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
    Width->SetWidthOverride(520);
    Background->SetContent(Width);
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
    AddText(TEXT("THREE FLOORS  |  NODE EXPEDITION"),18,FLinearColor(.36f,.88f,.85f));
    AddText(TEXT("Enter a seed to repeat a map, or leave it blank for a random run."),14,FLinearColor(.84f,.88f,.91f));
    SeedInput=WidgetTree->ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass());
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
        Button->SetBackgroundColor(FLinearColor(.08f,.31f,.35f,1.f));
        auto* Text=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
        MenuLabels.Add(Text); MenuLabelSources.Add(Caption);
        Text->SetText(FText::FromString(CastleText(Caption)));
        Text->SetJustification(ETextJustify::Center);
        Text->SetColorAndOpacity(FSlateColor(FLinearColor(.94f,.96f,.97f)));
        Text->SetFont(CastleUIFont(19));
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
