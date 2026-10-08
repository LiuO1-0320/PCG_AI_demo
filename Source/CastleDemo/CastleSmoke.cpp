#include "CastleDemo.h"
#include "Camera/CameraComponent.h"
#include "Kismet/GameplayStatics.h"
#include "NavigationSystem.h"
#include "NavigationPath.h"
#include "EngineUtils.h"
#include "Engine/DamageEvents.h"
#include "HAL/PlatformMisc.h"
#include "Engine/GameViewportClient.h"
#include "Misc/Paths.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerInput.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/InputComponent.h"
#include "InputKeyEventArgs.h"
#include "HAL/PlatformTime.h"

void ACastleDirector::TickSmokeTest()
{
    const float Now=GetWorld()->GetTimeSeconds();
    auto Fail=[](const TCHAR* Reason) { UE_LOG(LogTemp,Error,TEXT("CASTLE_SMOKE_FAILED: %s"),Reason); FPlatformMisc::RequestExitWithStatus(true,1); };
    if(Now>90) { Fail(TEXT("Timed out waiting for PCG or navigation"));return; }
    if(!bReady || SmokeFloor==Floor)return;
    auto* P=Cast<ACastlePlayer>(UGameplayStatics::GetPlayerPawn(this,0));
    if(!P) { Fail(TEXT("Default first person pawn missing"));return; }
    auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
    if(!Nav || Nav->IsNavigationBuildInProgress())return;
    // Give component registration, navmesh tasks and physics a few frames to settle.
    if(SmokeWaitStart==0) {SmokeWaitStart=Now; return;}
    if(Now-SmokeWaitStart<1)return;
    if(Floor==1 && SmokeMovementStep<4)
    {
        auto* PC=Cast<APlayerController>(P->GetController());
        const FKey Keys[]={EKeys::W,EKeys::S,EKeys::D,EKeys::A};
        const FVector Directions[]={FVector(1,0,0),FVector(-1,0,0),FVector(0,1,0),FVector(0,-1,0)};
        if(!PC || !PC->PlayerInput || !P->InputComponent) {Fail(TEXT("Player input chain missing"));return;}
        if(SmokeMovementAt==0)
        {
            Travel(P,FVector(-400,0,110)); SmokeMovementStart=P->GetActorLocation(); SmokeMovementAt=Now;
            PC->InputKey(FInputKeyEventArgs(nullptr,FInputDeviceId::CreateFromInternalId(0),Keys[SmokeMovementStep],IE_Pressed,1.f,false,FPlatformTime::Cycles64())); return;
        }
        if(Now-SmokeMovementAt<.35f)return;
        const bool KeyDown=PC->IsInputKeyDown(Keys[SmokeMovementStep]);
        const float Axis=P->InputComponent->GetAxisValue(SmokeMovementStep<2?TEXT("MoveForward"):TEXT("MoveRight"));
        PC->InputKey(FInputKeyEventArgs(nullptr,FInputDeviceId::CreateFromInternalId(0),Keys[SmokeMovementStep],IE_Released,0.f,false,FPlatformTime::Cycles64()));
        const FVector Delta=P->GetActorLocation()-SmokeMovementStart;
        if(FVector::DotProduct(Delta,Directions[SmokeMovementStep])<30)
        {
            UE_LOG(LogTemp,Error,TEXT("Key=%s Delta=%s PlayerInput=%s Component=%s Down=%d Axis=%.2f Mode=%d Ignore=%d LastInput=%s"),*Keys[SmokeMovementStep].ToString(),*Delta.ToString(),*PC->PlayerInput->GetClass()->GetName(),*P->InputComponent->GetClass()->GetName(),KeyDown,Axis,int32(P->GetCharacterMovement()->MovementMode),PC->IsMoveInputIgnored(),*P->GetLastMovementInputVector().ToString());
            Fail(TEXT("WASD keyboard input did not move the pawn"));return;
        }
        UE_LOG(LogTemp,Display,TEXT("CASTLE_KEYBOARD_OK Key=%s Distance=%.1f"),*Keys[SmokeMovementStep].ToString(),FVector::DotProduct(Delta,Directions[SmokeMovementStep]));
        ++SmokeMovementStep; SmokeMovementAt=0; return;
    }
    if(Rooms.Num()!=9 || !IsValid(Boss)) {Fail(TEXT("Generated room count or boss missing"));return;}
    int32 TypeCount[6]={0}; TSet<int32> Seen{0}; TArray<int32> Q{0};
    const int32 DX[]={1,0,-1,0},DY[]={0,1,0,-1};
    for(int32 Head=0;Head<Q.Num();++Head)
    {
        int32 I=Q[Head];
        for(int32 S=0;S<4;++S)if(Rooms[I].Doors&(1<<S))
        {
            int32 X=I%3+DX[S],Y=I/3+DY[S],N=Y*3+X;
            if(X<0 || X>=3 || Y<0 || Y>=3 || !(Rooms[N].Doors&(1<<((S+2)%4)))) {Fail(TEXT("Invalid or asymmetric corridor"));return;}
            if(!Seen.Contains(N)) {Seen.Add(N);Q.Add(N);}
        }
    }
    if(Seen.Num()!=9) {Fail(TEXT("Unreachable exploration room"));return;}
    for(const auto& Room:Rooms)
    {
        ++TypeCount[static_cast<int32>(Room.Type)];
        auto* Path=UNavigationSystemV1::FindPathToLocationSynchronously(this,FVector(-400,0,100),Room.Center+FVector(0,0,100));
        if(!Path || !Path->IsValid() || Path->IsPartial())
        {
            for(const auto& Bounds:Nav->GetNavigationBounds()) UE_LOG(LogTemp,Error,TEXT("NavBounds %s"),*Bounds.AreaBox.ToString());
            UE_LOG(LogTemp,Error,TEXT("Failed nav room %s PathValid=%d Partial=%d"),*Room.Center.ToString(),Path?Path->IsValid():0,Path?Path->IsPartial():0);
            Fail(TEXT("Navigation does not reach every room"));return;
        }
    }
    if(TypeCount[0]!=1 || TypeCount[1]!=1 || TypeCount[2]!=3 || TypeCount[3]!=2 || TypeCount[4]!=1 || TypeCount[5]!=1) {Fail(TEXT("Missing guaranteed room functions"));return;}
    auto FindItem=[&](ECastleInteract Kind)
    {
        for(TActorIterator<ACastleInteractable> It(GetWorld());It;++It)if(It->Kind==Kind && !It->bUsed)return *It;
        return static_cast<ACastleInteractable*>(nullptr);
    };
    for(TActorIterator<ACastleEnemy> It(GetWorld());It;++It)
    {
        if(!It->GetController() || !Cast<ACastleAIController>(It->GetController())) {Fail(TEXT("AI controller missing"));return;}
        It->bActive=false;
    }
    if(Floor==1)
    {
        P->ResetRun(); P->TakeDamage(80,FDamageEvent(),nullptr,nullptr);
        if(P->Shield!=0 || P->Health!=80) {Fail(TEXT("Shield absorption is incorrect"));return;}
        P->ResetRun();
        auto* Shop=FindItem(ECastleInteract::Shop); if(!Shop){Fail(TEXT("Shop missing"));return;}
        P->Coins=29; Shop->Use(P); if(P->WeaponLevel!=1 || P->Coins!=29) {Fail(TEXT("Shop affordability check"));return;}
        P->Coins=30; Shop->Use(P); if(P->WeaponLevel!=2 || P->Coins!=0 || P->BulletDamage!=30) {Fail(TEXT("Weapon upgrade transaction"));return;}
        auto* Chest=FindItem(ECastleInteract::Chest); Chest->Use(P); int32 Relics=P->Relics,Coins=P->Coins;
        Chest->Use(P); if(P->Relics!=Relics || P->Coins!=Coins) {Fail(TEXT("Chest reward can be duplicated"));return;}
        auto* Event=FindItem(ECastleInteract::Event); P->Health=15; Event->Use(P);
        if(Event->bUsed) {Fail(TEXT("Altar allowed lethal health cost"));return;}
        P->Health=P->MaxHealth; Event->Use(P); if(!Event->bUsed || P->Relics!=Relics+1){Fail(TEXT("Altar reward"));return;}
        auto* Cave=FindItem(ECastleInteract::CaveEnter); Cave->Use(P);
        if(Region!=TEXT("TREASURE CAVE") || FVector::Dist2D(P->GetActorLocation(),CaveCenter)>1800) {Fail(TEXT("Cave teleport"));return;}
        FindItem(ECastleInteract::CaveReturn)->Use(P);
        if(Region!=TEXT("EXPLORATION") || FVector::Dist2D(P->GetActorLocation(),Cave->GetActorLocation())>400) {Fail(TEXT("Cave return"));return;}
        // Exercise the actual camera trace / ammunition / damage path, not only ApplyDamage.
        auto* Target=AddEnemy(FVector(200,0,100),false); Target->bActive=true;
        Travel(P,FVector(-400,0,100));
        P->GetController()->SetControlRotation((Target->GetActorLocation()-P->Camera->GetComponentLocation()).Rotation());
        P->Camera->SetWorldRotation(P->GetController()->GetControlRotation());
        P->Camera->UpdateComponentToWorld(); P->NextShot=0; int32 AmmoBefore=P->Ammo; float HPBefore=Target->Health;
        P->Fire(); if(P->Ammo!=AmmoBefore-1 || Target->Health>=HPBefore)
        {
            FHitResult Shot; FCollisionQueryParams Params(SCENE_QUERY_STAT(CastleSmokeShot),false,P);
            GetWorld()->LineTraceSingleByChannel(Shot,P->Camera->GetComponentLocation(),P->Camera->GetComponentLocation()+P->Camera->GetForwardVector()*14000,ECC_Visibility,Params);
            UE_LOG(LogTemp,Error,TEXT("Shot ammo=%d/%d hp=%.0f/%.0f hit=%s camera=%s direction=%s target=%s"),P->Ammo,AmmoBefore,Target->Health,HPBefore,*GetNameSafe(Shot.GetActor()),*P->Camera->GetComponentLocation().ToString(),*P->Camera->GetForwardVector().ToString(),*Target->GetActorLocation().ToString());
            Fail(TEXT("First person shooting trace"));return;
        }
        P->Ammo=0; P->Reload(); if(!P->bReloading){Fail(TEXT("Reload did not start"));return;}
        P->ReloadEnd=Now; P->Tick(.01f); if(P->bReloading || P->Ammo!=P->MagazineSize){Fail(TEXT("Reload transfer"));return;}
        auto* Weapon=FindItem(ECastleInteract::Weapon); if(!Weapon){Fail(TEXT("Exploration weapon pickup missing"));return;}
        const float DamageBefore=P->BulletDamage; Weapon->Use(P); Weapon->Use(P);
        if(P->BulletDamage!=DamageBefore+10){Fail(TEXT("Weapon pickup or duplicate reward"));return;}
        P->TakeDamage(99999,FDamageEvent(),nullptr,nullptr); if(!P->bDead){Fail(TEXT("Death state"));return;}
        P->ResetRun(); P->GrantRelic(0); SmokeDamageBeforeTravel=P->BulletDamage;
    }
    else if(P->BulletDamage!=SmokeDamageBeforeTravel) {Fail(TEXT("Attributes not preserved across floors"));return;}
    auto* Gate=FindItem(ECastleInteract::BossEnter); if(!Gate){Fail(TEXT("Boss gate missing"));return;}
    Gate->Use(P); if(!Boss->bActive || Region!=TEXT("BOSS ARENA")){Fail(TEXT("Boss activation"));return;}
    // Verify the boss has a navigation path around randomized cover.
    auto* BossPath=UNavigationSystemV1::FindPathToLocationSynchronously(this,Boss->GetActorLocation(),P->GetActorLocation());
    if(!BossPath || !BossPath->IsValid() || BossPath->IsPartial()){Fail(TEXT("Boss arena navigation"));return;}
    Boss->TakeDamage(99999,FDamageEvent(),P->GetController(),P);
    if(!bBossDefeated || IsValid(Boss)){Fail(TEXT("Boss victory state"));return;}
    auto* Exit=FindItem(ECastleInteract::NextFloor); if(!Exit){Fail(TEXT("Boss did not unlock exit"));return;}
    SmokeFloor=Floor; SmokeWaitStart=0;
    UE_LOG(LogTemp,Display,TEXT("CASTLE_SMOKE_FLOOR_OK Floor=%d Seed=%d ConnectedRooms=9 Navigation=OK"),Floor,RunSeed);
    Exit->Use(P);
    if(Floor==3)
    {
        if(!bWon){Fail(TEXT("Three floor victory"));return;}
        UE_LOG(LogTemp,Display,TEXT("CASTLE_SMOKE_OK: PCG, topology, navigation, AI possession, shooting, shield, shop, relics, cave, reload, death and three-floor victory verified."));
        FPlatformMisc::RequestExitWithStatus(false,0);
    }
}

void ACastleDirector::TickCapture()
{
    if(!bReady)return;
    auto* P=Cast<ACastlePlayer>(UGameplayStatics::GetPlayerPawn(this,0)); if(!P)return;
    const float Now=GetWorld()->GetTimeSeconds();
    if(NextCaptureAt==0)NextCaptureAt=Now+20;
    if(Now<NextCaptureAt)return;
    if(!PendingCaptureName.IsEmpty())
    {
        FString Filename=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("Screenshots/Demo")/PendingCaptureName+TEXT(".png"));
        FScreenshotRequest::RequestScreenshot(Filename,false,false);
        UE_LOG(LogTemp,Display,TEXT("CASTLE_CAPTURE %s"),*Filename);
        PendingCaptureName.Empty(); ++CaptureStage; NextCaptureAt=Now+3; return;
    }
    // Screenshots come from the UE renderer; no desktop automation is required.
    for(TActorIterator<ACastleEnemy> It(GetWorld());It;++It) { It->SetActorTickEnabled(false); if(auto* AI=Cast<AAIController>(It->GetController()))AI->StopMovement(); }
    P->bDead=false; P->bHelp=false; P->Health=P->MaxHealth; P->Shield=P->MaxShield;
    FString Name;
    if(CaptureStage==0)
    {
        Travel(P,FVector(-620,-430,110),20);
        Name=TEXT("Exploration");
    }
    else if(CaptureStage==1)
    {
        EnterCave(P,Rooms[0].Center); Name=TEXT("TreasureCave");
    }
    else if(CaptureStage==2)
    {
        EnterBoss(P); Travel(P,BossCenter+FVector(-1750,-1500,110),35); Name=TEXT("BossArena");
    }
    else if(CaptureStage==3)
    {
        FPlatformMisc::RequestExit(false); return;
    }
    PendingCaptureName=Name; NextCaptureAt=Now+2;
}
