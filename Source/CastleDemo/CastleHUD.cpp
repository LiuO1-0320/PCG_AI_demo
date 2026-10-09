#include "CastleDemo.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Kismet/GameplayStatics.h"
#include "EngineUtils.h"
#include "CanvasTypes.h"

void ACastleHUD::Text(const FString& V,float X,float Y,FLinearColor C,float Scale)
{
    DrawText(V,C,X,Y,GEngine->GetSmallFont(),Scale,false);
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
    Text(FString::Printf(TEXT("FLOOR  %d / 3     %s"),D->Floor,*D->Region),36,58,White,1.05f);
    Text(FString::Printf(TEXT("SEED %d   |   KILLS %d   |   RELICS %d"),D->RunSeed,D->Kills,P->Relics),36,80,Muted,.85f);

    DrawRect(FLinearColor(.018f,.025f,.04f,.9f),20,H-126,370,105);
    Text(FString::Printf(TEXT("SHIELD  %.0f / %.0f"),P->Shield,P->MaxShield),35,H-117,Teal);
    Bar(35,H-94,338,10,P->Shield,P->MaxShield,Teal);
    Text(FString::Printf(TEXT("HEALTH  %.0f / %.0f"),P->Health,P->MaxHealth),35,H-77,White);
    Bar(35,H-54,338,10,P->Health,P->MaxHealth,FLinearColor(.87f,.25f,.26f));
    DrawRect(FLinearColor(.018f,.025f,.04f,.9f),W-310,H-126,290,105);
    Text(P->bReloading?TEXT("RELOADING..."):FString::Printf(TEXT("%02d  /  %d"),P->Ammo,P->Reserve),W-291,H-114,White,1.9f);
    Text(FString::Printf(TEXT("CARBINE Lv.%d    DAMAGE %.0f"),P->WeaponLevel,P->BulletDamage),W-291,H-73,Teal);
    Text(FString::Printf(TEXT("GOLD  %d     R: reload"),P->Coins),W-291,H-48,Gold);
    if(P->bReloading)Bar(W-291,H-26,245,4,1.25f-(P->ReloadEnd-Now),1.25f,Gold);

    // Use the same grid and connections as the generated rooms.
    const float MX=W-218,MY=28,Step=D->GridWidth==4?43:55,Tile=D->GridWidth==4?31:39;
    const float LegendOffset=28+(D->GridWidth-1)*Step+Tile+12;
    DrawRect(FLinearColor(.018f,.025f,.04f,.88f),MX-12,MY-8,210,LegendOffset+44);
    Text(TEXT("FLOOR MAP"),MX,MY,Gold);
    const TCHAR* Initials[]={TEXT("IN"),TEXT("S"),TEXT("F"),TEXT("C"),TEXT("E"),TEXT("V"),TEXT("R"),TEXT("EL"),TEXT("T"),TEXT("OUT")};
    for(int32 I=0;I<D->Rooms.Num();++I)
    {
        const auto& R=D->Rooms[I]; if(!R.bActive)continue;
        float X=MX+(I%D->GridWidth)*Step,Y=MY+28+(D->GridWidth-1-I/D->GridWidth)*Step;
        if(R.Doors&1)DrawRect(Muted,X+Tile,Y+Tile*.45f,Step-Tile,4);
        if(R.Doors&2)DrawRect(Muted,X+Tile*.45f,Y-(Step-Tile),4,Step-Tile);
        const FLinearColor Color=D->Floor==1 && !R.bRevealed?FLinearColor(.08f,.11f,.16f)
            :R.Type==ECastleRoom::Combat?FLinearColor(.45f,.18f,.18f)
            :R.Type==ECastleRoom::Shop?FLinearColor(.1f,.36f,.34f):FLinearColor(.18f,.23f,.3f);
        DrawRect(Color,X,Y,Tile,Tile);
        if(R.bVisited)DrawRect(Teal,X+Tile-4,Y,4,Tile);
        Text(D->Floor==1 && !R.bRevealed?TEXT("?"):Initials[static_cast<int32>(R.Type)],X+(D->GridWidth==4?5:12),Y+8,White,D->GridWidth==4?.72f:.9f);
    }
    if(D->Region==TEXT("EXPLORATION"))
    {
        const FVector Pos=P->GetActorLocation();
        const float X=MX+Tile*.5f+Pos.X/2400*Step,Y=MY+28+Tile*.5f+(D->GridWidth-1-Pos.Y/2400)*Step;
        DrawRect(Teal,X-3,Y-3,6,6);
        for(TActorIterator<ACastleInteractable> It(GetWorld());It;++It)if(It->Kind==ECastleInteract::BossEnter)
        {
            const auto L=It->GetActorLocation(); Text(TEXT("B"),MX+Tile*.5f+L.X/2400*Step,MY+28+Tile*.5f+(D->GridWidth-1-L.Y/2400)*Step,Gold);
        }
    }
    Text(TEXT("S shop / F foes / C chest"),MX,MY+LegendOffset,Muted,.72f);
    Text(D->Floor==1?TEXT("? unknown / R rest / EL elite / T trap"):TEXT("E altar / V vault / B boss"),MX,MY+LegendOffset+14,Muted,.72f);
    Text(D->Floor==1?TEXT("Objective: explore rooms; the exit is to the northeast"):D->bBossDefeated?TEXT("Objective: use the ASCEND portal"):D->Region==TEXT("BOSS ARENA")?TEXT("Objective: defeat the guardian"):TEXT("Objective: gather relics, find B gate"),26,116,White);

    if(IsValid(D->Boss) && D->Boss->bActive)
    {
        float BW=FMath::Min(W*.42f,620.f),BX=(W-BW)*.5f;
        DrawRect(FLinearColor(.025f,.018f,.045f,.9f),BX-12,25,BW+24,66);
        Text(D->BossName()+(D->Boss->Health<D->Boss->MaxHealth*.5f?TEXT("  /  ENRAGED"):TEXT("")),BX,31,FLinearColor(.8f,.6f,1),1.15f);
        Bar(BX,60,BW,14,D->Boss->Health,D->Boss->MaxHealth,FLinearColor(.65f,.3f,.9f));
        Text(FString::Printf(TEXT("%.0f / %.0f"),D->Boss->Health,D->Boss->MaxHealth),BX+BW*.43f,77,White,.7f);
    }

    if(!P->bDead && !D->bWon)
    {
        const float CX=W*.5f,CY=H*.5f;
        DrawLine(CX-12,CY,CX-5,CY,White,1.4f);DrawLine(CX+5,CY,CX+12,CY,White,1.4f);
        DrawLine(CX,CY-12,CX,CY-5,White,1.4f);DrawLine(CX,CY+5,CX,CY+12,White,1.4f);
        if(Now<P->HitMarkerUntil) {DrawLine(CX-6,CY-6,CX+6,CY+6,Gold,2);DrawLine(CX+6,CY-6,CX-6,CY+6,Gold,2);}
        if(auto* Item=P->Focused())
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
        Text(TEXT("ENTER THE CASTLE"),X+24,Y+17,Gold,1.5f);
        Text(TEXT("Explore connected rooms. Chests grant gold, ammo and relics."),X+24,Y+53,White);
        Text(TEXT("Spend gold at S rooms. E altars trade health for attributes."),X+24,Y+78,White);
        Text(D->Floor==1?TEXT("R rests once. OUT leads to floor 2. EL / T guard side loot."):TEXT("V portals lead to vaults. B gates lead to the boss."),X+24,Y+103,White);
        Text(TEXT("Shield recharges after 5 seconds without damage."),X+24,Y+128,Teal);
        Text(TEXT("Clear all 3 floors to win. Fire or press F1 to close."),X+24,Y+155,Gold);
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
