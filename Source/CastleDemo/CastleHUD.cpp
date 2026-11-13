#include "CastleDemo.h"
#include "CastleMapProjection.h"
#include "CastleLanguage.h"
#include "CastleCollectibles.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Misc/Paths.h"
#include "Kismet/GameplayStatics.h"
#include "EngineUtils.h"
#include "CanvasTypes.h"
#include "CanvasItem.h"

void ACastleHUD::Text(const FString& V,float X,float Y,FLinearColor C,float Scale)
{
    if(!LanguageFont)
    {
        LanguageFont=NewObject<UFont>(this);
        LanguageFont->FontCacheType=EFontCacheType::Runtime;
        LanguageFont->GetMutableInternalCompositeFont().DefaultTypeface.Fonts.Emplace(
            FName(TEXT("Regular")),FPaths::EngineContentDir()/TEXT("Slate/Fonts/DroidSansFallback.ttf"),EFontHinting::Default,EFontLoadingPolicy::LazyLoad);
    }
    FCanvasTextItem Item(FVector2D(X,Y),FText::FromString(CastleText(V)),FSlateFontInfo(LanguageFont,12*Scale,FName(TEXT("Regular"))),C);
    Item.EnableShadow(FLinearColor::Black);
    Canvas->DrawItem(Item);
}
void ACastleHUD::Bar(float X,float Y,float W,float H,float V,float Max,FLinearColor C)
{
    DrawRect(FLinearColor(.025f,.03f,.045f,.9f),X,Y,W,H);
    DrawRect(C,X,Y,W*FMath::Clamp(V/FMath::Max(1.f,Max),0.f,1.f),H);
}

void ACastleHUD::DrawHUD()
{
    Super::DrawHUD();
    auto* P=Cast<ACastlePlayer>(GetOwningPawn()); if(!P || !Canvas)return;
    auto* D=P->Director(); if(!D)return;
    const float UIScale=FMath::Max(.1f,FMath::Min(Canvas->ClipX/1440.f,Canvas->ClipY/900.f));
    Canvas->Canvas->PushRelativeTransform(FScaleMatrix(FVector(UIScale,UIScale,1)));
    const float W=Canvas->ClipX/UIScale,H=Canvas->ClipY/UIScale,Now=GetWorld()->GetTimeSeconds();
    const FLinearColor White(.88f,.91f,.94f),Teal(.2f,.85f,.84f),Gold(1,.76f,.3f),Muted(.5f,.57f,.65f);
    DrawRect(FLinearColor(.018f,.025f,.04f,.85f),20,20,410,82);
    Text(TEXT("THE HOLLOW CITADEL"),36,29,Gold,1.5f);
    Text((CastleIsChinese()?FString::Printf(TEXT("楼层 %d / 3     %s"),D->Floor,*CastleText(D->Region)):FString::Printf(TEXT("FLOOR  %d / 3     %s"),D->Floor,*CastleText(D->Region))),36,58,White,1.05f);
    Text((CastleIsChinese()?FString::Printf(TEXT("种子 %d | 击杀 %d | 藏品 %d"),D->RunSeed,D->Kills,P->Relics):FString::Printf(TEXT("SEED %d   |   KILLS %d   |   RELICS %d"),D->RunSeed,D->Kills,P->Relics)),36,80,Muted,.85f);

    DrawRect(FLinearColor(.018f,.025f,.04f,.9f),20,H-126,370,105);
    Text((CastleIsChinese()?FString::Printf(TEXT("护盾 %.0f / %.0f"),P->Shield,P->MaxShield):FString::Printf(TEXT("SHIELD  %.0f / %.0f"),P->Shield,P->MaxShield)),35,H-117,Teal);
    Bar(35,H-94,338,10,P->Shield,P->MaxShield,Teal);
    Text((CastleIsChinese()?FString::Printf(TEXT("生命 %.0f / %.0f"),P->Health,P->MaxHealth):FString::Printf(TEXT("HEALTH  %.0f / %.0f"),P->Health,P->MaxHealth)),35,H-77,White);
    Bar(35,H-54,338,10,P->Health,P->MaxHealth,FLinearColor(.87f,.25f,.26f));
    DrawRect(FLinearColor(.018f,.025f,.04f,.9f),W-310,H-126,290,105);
    Text(P->bReloading?TEXT("RELOADING..."):(CastleIsChinese()?FString::Printf(TEXT("%02d  /  %d"),P->Ammo,P->Reserve):FString::Printf(TEXT("%02d  /  %d"),P->Ammo,P->Reserve)),W-291,H-114,White,1.9f);
    Text((CastleIsChinese()?FString::Printf(TEXT("卡宾枪 等级 %d    伤害 %.0f"),P->WeaponLevel,P->BulletDamage):FString::Printf(TEXT("CARBINE Lv.%d    DAMAGE %.0f"),P->WeaponLevel,P->BulletDamage)),W-291,H-73,Teal);
    Text((CastleIsChinese()?FString::Printf(TEXT("记忆碎片 %d     R：换弹"),P->Coins):FString::Printf(TEXT("FRAGMENTS %d     R: reload"),P->Coins)),W-291,H-48,Gold,.95f);
    if(P->bReloading)Bar(W-291,H-26,245,4,1.25f-(P->ReloadEnd-Now),1.25f,Gold);

    const bool InCave=D->Region==TEXT("TREASURE CAVE"),InBoss=D->Region==TEXT("BOSS ARENA");
    const auto& MapRooms=InCave?D->CaveRooms:D->Rooms;
    const int32 MapWidth=InCave?2:D->GridWidth;
    const FVector MapOrigin=InCave?D->CaveCenter:FVector::ZeroVector;
    const float MapSpacing=InCave?2100.f:2400.f;
    if(!InBoss)
    {
    const float MX=W-218,MY=28,Step=MapWidth==4?43:55,Tile=MapWidth==4?31:39;
    const float LegendOffset=28+(MapWidth-1)*Step+Tile+12;
    DrawRect(FLinearColor(.018f,.025f,.04f,.88f),MX-12,MY-8,210,LegendOffset+44);
    Text(InCave?TEXT("VAULT MAP"):TEXT("FLOOR MAP"),MX,MY,Gold);
    const FVector2D MapBase(MX,MY+28+(MapWidth-1)*Step);
    auto MapPoint=[&](const FVector& WorldPosition)
    {
        return MapBase+FVector2D(Tile*.5f,Tile*.5f)+CastleMapOffset(WorldPosition-MapOrigin,Step/MapSpacing);
    };
    const TCHAR* Initials[]={TEXT("IN"),TEXT("S"),TEXT("F"),TEXT("C"),TEXT("E"),TEXT("V"),TEXT("R"),TEXT("EL"),TEXT("T"),TEXT("OUT")};
    for(int32 I=0;I<MapRooms.Num();++I)
    {
        const auto& R=MapRooms[I]; if(!R.bActive)continue;
        const auto Center=MapPoint(R.Center);
        const float X=Center.X-Tile*.5f,Y=Center.Y-Tile*.5f;
        if(R.Doors&1)DrawRect(Muted,X+Tile*.45f,Y-(Step-Tile),4,Step-Tile);
        if(R.Doors&2)DrawRect(Muted,X+Tile,Y+Tile*.45f,Step-Tile,4);
        const FLinearColor Color=!R.bRevealed?FLinearColor(.08f,.11f,.16f)
            :R.Type==ECastleRoom::Combat?FLinearColor(.45f,.18f,.18f)
            :R.Type==ECastleRoom::Shop?FLinearColor(.1f,.36f,.34f):FLinearColor(.18f,.23f,.3f);
        DrawRect(Color,X,Y,Tile,Tile);
        if(R.bVisited)DrawRect(Teal,X+Tile-4,Y,4,Tile);
        Text(!R.bRevealed?TEXT("?"):Initials[static_cast<int32>(R.Type)],X+(MapWidth==4?5:12),Y+8,White,MapWidth==4?.72f:.9f);
    }
    if(D->Region==TEXT("EXPLORATION") || InCave)
    {
        const auto PlayerPoint=MapPoint(P->GetActorLocation());
        DrawRect(Teal,PlayerPoint.X-3,PlayerPoint.Y-3,6,6);
        for(TActorIterator<ACastleInteractable> It(GetWorld());It;++It)if(!InCave && It->Kind==ECastleInteract::BossEnter)
        {
            const auto L=MapPoint(It->GetActorLocation()); Text(TEXT("B"),L.X,L.Y,Gold);
        }
    }
    Text(InCave?TEXT("IN entrance / C relic / OUT exit"):TEXT("S shop / F foes / C chest"),MX,MY+LegendOffset,Muted,.72f);
    Text(InCave?TEXT("Four rooms; follow the path to OUT"):TEXT("? fog / R rest / EL elite / T trap"),MX,MY+LegendOffset+14,Muted,.72f);
    }
    else
    {
        const float X=W-218,Y=28,Side=180,Scale=Side/4500.f;
        DrawRect(FLinearColor(.018f,.025f,.04f,.88f),X-12,Y-8,210,232);
        Text(TEXT("ARENA MAP"),X,Y,Gold);
        DrawRect(FLinearColor(.14f,.12f,.2f),X,Y+28,Side,Side);
        auto ArenaPoint=[&](FVector Pos) {return FVector2D(X+Side*.5f,Y+28+Side*.5f)+CastleMapOffset(Pos-D->BossCenter,Scale);};
        for(const auto& Top:D->BossPlatforms)
        {
            const auto V=ArenaPoint(Top); DrawRect(FLinearColor(.6f,.3f,.8f),V.X-17,V.Y-14,34,28);
        }
        for(const auto& Cover:D->BossCoverPoints)
        {
            const auto V=ArenaPoint(Cover);DrawRect(Muted,V.X-4,V.Y-4,8,8);
        }
        const auto PlayerPos=ArenaPoint(P->GetActorLocation());DrawRect(Teal,PlayerPos.X-3,PlayerPos.Y-3,6,6);
        if(IsValid(D->Boss)) {const auto B=ArenaPoint(D->Boss->GetActorLocation());Text(TEXT("B"),B.X,B.Y,Gold);}
        Text(TEXT("Purple: raised decks / Grey: cover"),X,Y+214,Muted,.72f);
    }
    Text(InCave?TEXT("Objective: collect relics and reach the vault exit"):D->Floor==1?TEXT("Objective: explore rooms; the exit is to the northeast"):D->bBossDefeated?TEXT("Objective: use the ASCEND portal"):D->Region==TEXT("BOSS ARENA")?TEXT("Objective: defeat the guardian"):TEXT("Objective: gather relics, find B gate"),26,116,White);

    Text(CastleIsChinese()?TEXT("F2：语言设置 · 简体中文 / English"):TEXT("F2: Language settings · English / 简体中文"),26,140,Muted,.85f);

    if(IsValid(D->Boss) && D->Boss->bActive)
    {
        float BW=FMath::Min(W*.42f,620.f),BX=(W-BW)*.5f;
        DrawRect(FLinearColor(.025f,.018f,.045f,.9f),BX-12,25,BW+24,66);
        Text(CastleText(D->BossName())+(D->Boss->Health<D->Boss->MaxHealth*.5f?CastleText(TEXT("  /  ENRAGED")):FString()),BX,31,FLinearColor(.8f,.6f,1),1.15f);
        Bar(BX,60,BW,14,D->Boss->Health,D->Boss->MaxHealth,FLinearColor(.65f,.3f,.9f));
        Text((CastleIsChinese()?FString::Printf(TEXT("%.0f / %.0f"),D->Boss->Health,D->Boss->MaxHealth):FString::Printf(TEXT("%.0f / %.0f"),D->Boss->Health,D->Boss->MaxHealth)),BX+BW*.43f,77,White,.7f);
    }

    if(!P->bDead && !D->bWon)
    {
        const float CX=W*.5f,CY=H*.5f;
        DrawLine(CX-12,CY,CX-5,CY,White,1.4f);DrawLine(CX+5,CY,CX+12,CY,White,1.4f);
        DrawLine(CX,CY-12,CX,CY-5,White,1.4f);DrawLine(CX,CY+5,CX,CY+12,White,1.4f);
        if(Now<P->HitMarkerUntil) {DrawLine(CX-6,CY-6,CX+6,CY+6,Gold,2);DrawLine(CX+6,CY-6,CX-6,CY+6,Gold,2);}
        if(auto* Item=P->bCollectionOpen?nullptr:P->Focused())
        {
            DrawRect(FLinearColor(.018f,.025f,.04f,.92f),CX-300,H*.68f,600,38);
            Text(Item->Prompt(P),CX-280,H*.68f+10,Gold,1.05f);
        }
    }
    if(Now<P->HurtUntil)DrawRect(FLinearColor(.8f,0,0,.15f),0,0,W,H);
    if(Now<P->NoticeUntil)
    {
        DrawRect(FLinearColor(.018f,.025f,.04f,.88f),W*.5f-360,H-181,720,38);
        Text(P->Notice,W*.5f-343,H-171,Gold,.95f);
    }
    Text(TEXT("WASD move  |  Mouse aim / fire  |  E interact  |  SPACE jump  |  F1 help  |  ESC pause"),W*.5f-340,H-19,Muted,.82f);

    if(P->bHelp && !P->bDead && !D->bWon)
    {
        float X=W*.5f-290,Y=H*.5f-115;
        DrawRect(FLinearColor(.018f,.025f,.04f,.94f),X,Y,580,190);
        Text(TEXT("THE PAINTER RETURNS"),X+24,Y+17,Gold,1.5f);
        Text(TEXT("Recover court keepsakes. Chests grant fragments, ammo and collectibles."),X+24,Y+53,White);
        Text(TEXT("Spend fragments at S rooms. E altars trade health for keepsakes."),X+24,Y+78,White);
        Text(D->Floor==1?TEXT("R rests once. OUT leads to floor 2. EL / T guard side loot."):TEXT("V portals lead to vaults. B gates lead to the boss."),X+24,Y+103,White);
        Text(TEXT("Shield recharges after 5 seconds without damage."),X+24,Y+128,Teal);
        Text(TEXT("Clear all 3 floors to win. Fire or press F1 to close."),X+24,Y+155,Gold);
    }
    if(D->bReady && !P->bDead && !D->bWon && !P->bHelp && !UGameplayStatics::IsGamePaused(this))
    {
        const bool Chinese=CastleIsChinese();
        if(P->bCollectionOpen)
        {
            const float Left=W*.5f-500,Top=H*.5f-245;
            DrawRect(FLinearColor(.025f,.018f,.038f,.97f),Left,Top,1000,490);
            Text(Chinese?TEXT("宫廷旧物 / 画师的记忆"):TEXT("COURT COLLECTIBLES / THE PAINTER'S MEMORIES"),Left+30,Top+22,Gold,1.5f);
            Text(Chinese?TEXT("Tab 关闭。效果可叠加，跨层保留，新局重置。游戏继续运行。"):
                TEXT("Tab to close. Stacks persist across floors and reset each run. The game continues."),Left+30,Top+62,Muted,.95f);
            for(int32 Kind=0;Kind<3;++Kind)
            {
                const auto& Def=CastleCollectible(Kind);
                const int32 Count=P->CollectibleStacks.IsValidIndex(Kind)?P->CollectibleStacks[Kind]:0;
                const float Y=Top+108+Kind*112;
                Text(FString::Printf(TEXT("%s  x%d"),*Def.GetName(Chinese),Count),Left+30,Y,Count?Teal:Muted,1.25f);
                Text(Def.GetEffect(Chinese),Left+30,Y+32,White,1.05f);
                Text(Count?Def.GetMemory(Chinese):(Chinese?FString(TEXT("尚未找回这段记忆。")):FString(TEXT("This memory has not been recovered."))),Left+30,Y+63,Muted,1.f);
            }
            Text(Chinese?FString::Printf(TEXT("本局累计：伤害 +%d / 护盾上限 +%d / 生命上限 +%d"),P->CollectibleStacks[0]*6,P->CollectibleStacks[1]*25,P->CollectibleStacks[2]*25):
                FString::Printf(TEXT("Collectible bonuses: damage +%d / max shield +%d / max health +%d"),P->CollectibleStacks[0]*6,P->CollectibleStacks[1]*25,P->CollectibleStacks[2]*25),Left+30,Top+450,Gold);
        }
        else if(P->LastCollectibleKind!=INDEX_NONE && Now<P->MemoryNoticeUntil)
        {
            const auto& Def=CastleCollectible(P->LastCollectibleKind);
            const float Left=W*.5f-450,Top=170;
            DrawRect(FLinearColor(.045f,.025f,.075f,.96f),Left,Top,900,130);
            Text((Chinese?FString(TEXT("找回宫廷旧物：")):FString(TEXT("COLLECTIBLE RECOVERED: ")))+Def.GetName(Chinese),Left+24,Top+14,Gold,1.3f);
            Text(Def.GetEffect(Chinese),Left+24,Top+49,Teal,1.05f);
            Text(Def.GetMemory(Chinese),Left+24,Top+78,White,1.05f);
            Text(Chinese?TEXT("Tab 查看全部旧物与叠加数量"):TEXT("Tab: collection and stack counts"),Left+24,Top+107,Muted,.8f);
        }
        Text(Chinese?TEXT("Tab：宫廷旧物"):TEXT("Tab: collectibles"),26,164,Muted,.85f);
    }
    const bool Paused=UGameplayStatics::IsGamePaused(this);
    if(!D->bReady || P->bDead || D->bWon || Paused)
    {
        DrawRect(FLinearColor(.005f,.01f,.02f,.78f),0,0,W,H);
        const FString Title=P->bDead?TEXT("YOU FELL"):D->bWon?TEXT("THE CITADEL IS YOURS"):Paused?TEXT("PAUSED"):TEXT("GENERATING CASTLE...");
        Text(Title,W*.5f-200,H*.5f-45,D->bWon?Gold:White,2.2f);
        Text(P->bDead||D->bWon?TEXT("ENTER: random run   |   K: same seed"):Paused?TEXT("ESC: resume"):D->Floor==1?TEXT("PCG is building the first-floor node map."):TEXT("PCG is building connected rooms, cover and the vault."),W*.5f-195,H*.5f+5,Teal,1.1f);
    }
    Canvas->Canvas->PopTransform();
}
