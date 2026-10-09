#include "CastleDemo.h"
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
        Label->SetText(FText::FromString(Value));
        Label->SetColorAndOpacity(FSlateColor(Color));
        FSlateFontInfo Font=Label->GetFont(); Font.Size=Size; Label->SetFont(Font);
        Label->SetJustification(ETextJustify::Center);
        auto* Slot=Column->AddChildToVerticalBox(Label); Slot->SetPadding(FMargin(4,8));
        return Label;
    };
    AddText(TEXT("THE HOLLOW CITADEL"),32,FLinearColor(.96f,.72f,.36f));
    AddText(TEXT("FIRST FLOOR  |  NODE EXPEDITION"),18,FLinearColor(.36f,.88f,.85f));
    AddText(TEXT("Enter a seed to repeat a map, or leave it blank for a random run."),14,FLinearColor(.84f,.88f,.91f));
    SeedInput=WidgetTree->ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass());
    FEditableTextBoxStyle InputStyle=SeedInput->GetWidgetStyle();
    InputStyle.SetBackgroundColor(FSlateColor(FLinearColor(.04f,.07f,.11f,1.f)));
    InputStyle.SetForegroundColor(FSlateColor(FLinearColor(.94f,.96f,.97f)));
    InputStyle.SetFocusedForegroundColor(FSlateColor(FLinearColor(.94f,.96f,.97f)));
    InputStyle.TextStyle.SetColorAndOpacity(FSlateColor(FLinearColor(.94f,.96f,.97f)));
    InputStyle.TextStyle.Font.Size=20;
    InputStyle.Padding=FMargin(12,8);
    SeedInput->SetWidgetStyle(InputStyle);
    SeedInput->SetHintText(FText::FromString(TEXT("Seed: 1 - 2147483647")));
    Column->AddChildToVerticalBox(SeedInput)->SetPadding(FMargin(8,14));

    auto AddButton=[&](const FString& Caption)
    {
        auto* Button=WidgetTree->ConstructWidget<UButton>(UButton::StaticClass());
        Button->SetBackgroundColor(FLinearColor(.08f,.31f,.35f,1.f));
        auto* Text=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
        Text->SetText(FText::FromString(Caption));
        Text->SetJustification(ETextJustify::Center);
        Text->SetColorAndOpacity(FSlateColor(FLinearColor(.94f,.96f,.97f)));
        FSlateFontInfo Font=Text->GetFont(); Font.Size=19; Text->SetFont(Font);
        Button->AddChild(Text);
        Column->AddChildToVerticalBox(Button)->SetPadding(FMargin(8,6));
        return Button;
    };
    RandomButton=AddButton(TEXT("RANDOM SEED"));
    RandomButton->OnClicked.AddDynamic(this,&UCastleStartMenu::ChooseRandom);
    StartButton=AddButton(TEXT("START GAME"));
    StartButton->OnClicked.AddDynamic(this,&UCastleStartMenu::StartGame);
    Message=AddText(TEXT("Explore connected rooms. Reveal nearby nodes and find the exit."),14,FLinearColor(.7f,.78f,.83f));
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
    Message->SetText(FText::FromString(TEXT("Random seed selected. Press START GAME.")));
}

void UCastleStartMenu::StartGame()
{
    int32 Seed=0;
    const FString Typed=SeedInput->GetText().ToString().TrimStartAndEnd();
    if(Typed.IsEmpty()) Seed=FMath::RandRange(1,1000000000);
    else if(!ParseSeed(Typed,Seed))
    {
        Message->SetText(FText::FromString(TEXT("Enter a whole number from 1 to 2147483647.")));
        return;
    }
    if(!IsValid(Director)) return;
    RemoveFromParent();
    if(auto* PC=GetOwningPlayer()) {PC->SetInputMode(FInputModeGameOnly()); PC->bShowMouseCursor=false;}
    Director->StartRun(Seed);
}
