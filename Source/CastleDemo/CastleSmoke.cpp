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
#include "PCGComponent.h"
#include "PCGGraph.h"
#include "PCGNode.h"
#include "BehaviorTree/BehaviorTreeComponent.h"

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
    if(Rooms.Num()!=16 || (Floor!=1 && !IsValid(Boss)) || (Floor==1 && IsValid(Boss))) {Fail(TEXT("Generated room count or boss state"));return;}
    int32 TypeCount[10]={0},ActiveCount=0; TSet<int32> Seen{0}; TArray<int32> Q{0};
    for(const auto& Room:Rooms)if(Room.bActive)++ActiveCount;
    const int32 DX[]={1,0,-1,0},DY[]={0,1,0,-1};
    for(int32 Head=0;Head<Q.Num();++Head)
    {
        int32 I=Q[Head];
        for(int32 S=0;S<4;++S)if(Rooms[I].Doors&(1<<S))
        {
            int32 X=I%GridWidth+DX[S],Y=I/GridWidth+DY[S],N=Y*GridWidth+X;
            if(X<0 || X>=GridWidth || Y<0 || Y>=GridWidth || !Rooms[N].bActive || !(Rooms[N].Doors&(1<<((S+2)%4)))) {Fail(TEXT("Invalid or asymmetric corridor"));return;}
            if(!Seen.Contains(N)) {Seen.Add(N);Q.Add(N);}
        }
    }
    if(Seen.Num()!=ActiveCount) {Fail(TEXT("Unreachable exploration room"));return;}
    for(const auto& Room:Rooms)
    {
        if(!Room.bActive)continue;
        ++TypeCount[static_cast<int32>(Room.Type)];
        auto* Path=UNavigationSystemV1::FindPathToLocationSynchronously(this,FVector(-400,0,100),Room.Center+FVector(0,0,100));
        if(!Path || !Path->IsValid() || Path->IsPartial())
        {
            for(const auto& Bounds:Nav->GetNavigationBounds()) UE_LOG(LogTemp,Error,TEXT("NavBounds %s"),*Bounds.AreaBox.ToString());
            UE_LOG(LogTemp,Error,TEXT("Failed nav room %s PathValid=%d Partial=%d"),*Room.Center.ToString(),Path?Path->IsValid():0,Path?Path->IsPartial():0);
            for(int32 I=0;I<Rooms.Num();++I)
            {
                FNavLocation Projected;
                const bool bProjected=Nav->ProjectPointToNavigation(Rooms[I].Center+FVector(0,0,100),Projected,FVector(300,300,400));
                auto* FromStart=UNavigationSystemV1::FindPathToLocationSynchronously(this,FVector(-400,0,100),Rooms[I].Center+FVector(0,0,100));
                UE_LOG(LogTemp,Error,TEXT("RoomNav %d Center=%s Doors=%d Projected=%d NavPoint=%s Path=%d Partial=%d"),I,*Rooms[I].Center.ToString(),Rooms[I].Doors,bProjected,*Projected.Location.ToString(),FromStart&&FromStart->IsValid(),FromStart&&FromStart->IsPartial());
            }
            Fail(TEXT("Navigation does not reach every room"));return;
        }
    }
    {
        if(ActiveCount<10 || ActiveCount>12 || TypeCount[0]!=1 || TypeCount[1]!=1 || TypeCount[2]<4 || TypeCount[3]!=1 || TypeCount[5]!=0 || TypeCount[6]!=1 || TypeCount[9]!=1 || TypeCount[4]+TypeCount[7]+TypeCount[8]<1 || TypeCount[4]+TypeCount[7]+TypeCount[8]>2 || TypeCount[7]>1)
        {Fail(TEXT("First-floor room quota"));return;}
        static const ECastleRoom Expected[7]={ECastleRoom::Entrance,ECastleRoom::Combat,ECastleRoom::Combat,ECastleRoom::Shop,ECastleRoom::Combat,ECastleRoom::Rest,ECastleRoom::Exit};
        if(FirstFloorRoute.Num()!=7) {Fail(TEXT("First-floor main route length"));return;}
        int32 EdgeCount=0;
        for(const auto& Room:Rooms) for(int32 S=0;S<4;++S) if(Room.Doors&(1<<S)) ++EdgeCount;
        if(EdgeCount<ActiveCount*2) {Fail(TEXT("First-floor map has no alternate route"));return;}
        for(int32 Order=0;Order<7;++Order)
        {
            const int32 Index=FirstFloorRoute[Order];
            if(Index<0 || Index>=16 || Rooms[Index].Type!=Expected[Order] || Rooms[Index].RouteOrder!=Order)
            {Fail(TEXT("First-floor main route order"));return;}
            if(Order>0)
            {
                const int32 Before=FirstFloorRoute[Order-1];
                bool bAdjacent=false;
                for(int32 Side=0;Side<4;++Side)if(Rooms[Before].Doors&(1<<Side))
                {
                    const int32 X=Before%4+DX[Side],Y=Before/4+DY[Side];
                    if(Y*4+X==Index)bAdjacent=true;
                }
                if(!bAdjacent){Fail(TEXT("First-floor route not physically adjacent"));return;}
            }
        }
        bool bAlternate=false;
        for(int32 Edge=1;Edge<7;++Edge)
        {
            const int32 A=FirstFloorRoute[Edge-1],B=FirstFloorRoute[Edge];
            TSet<int32> Reach{0};TArray<int32> Pending{0};
            for(int32 Head=0;Head<Pending.Num();++Head)
            {
                const int32 I=Pending[Head];
                for(int32 Side=0;Side<4;++Side)if(Rooms[I].Doors&(1<<Side))
                {
                    const int32 X=I%4+DX[Side],Y=I/4+DY[Side],N=Y*4+X;
                    if((I==A && N==B)||(I==B && N==A)||Reach.Contains(N))continue;
                    Reach.Add(N);Pending.Add(N);
                }
            }
            if(Reach.Contains(15)){bAlternate=true;break;}
        }
        if(!bAlternate){Fail(TEXT("No alternate entrance-to-exit route"));return;}
        if(!Rooms[0].bVisited || !Rooms[0].bRevealed){Fail(TEXT("Entrance fog state"));return;}
        for(int32 I=1;I<16;++I)if(Rooms[I].bActive)
        {
            bool bExpected=false;
            for(int32 Side=0;Side<4;++Side)if(Rooms[0].Doors&(1<<Side))
            {
                const int32 X=DX[Side],Y=DY[Side];if(X>=0 && X<4 && Y>=0 && Y<4 && Y*4+X==I)bExpected=true;
            }
            if(Rooms[I].bRevealed!=bExpected || Rooms[I].bVisited){Fail(TEXT("Initial fog reveals distant node"));return;}
        }
        int32 Parsed=0;
        if(!UCastleStartMenu::ParseSeed(TEXT("2147483647"),Parsed) || Parsed!=MAX_int32 || UCastleStartMenu::ParseSeed(TEXT("0"),Parsed) || UCastleStartMenu::ParseSeed(TEXT("12x"),Parsed))
        {Fail(TEXT("Menu seed validation"));return;}
        RevealRoom(FirstFloorRoute[1]);
        if(!Rooms[FirstFloorRoute[1]].bVisited || !Rooms[FirstFloorRoute[2]].bRevealed){Fail(TEXT("Adjacent room reveal"));return;}
    }
    auto FindItem=[&](ECastleInteract Kind)
    {
        for(TActorIterator<ACastleInteractable> It(GetWorld());It;++It)if(It->Kind==Kind && !It->bUsed)return *It;
        return static_cast<ACastleInteractable*>(nullptr);
    };
    for(TActorIterator<ACastleEnemy> It(GetWorld());It;++It)
    {
        auto* AI=Cast<ACastleAIController>(It->GetController());
        auto* BT=AI?Cast<UBehaviorTreeComponent>(AI->GetBrainComponent()):nullptr;
        if(!AI || !BT || !BT->IsRunning() || !AI->GetBlackboardComponent()) {Fail(TEXT("AI controller, running behavior tree or blackboard missing"));return;}
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
        auto* Rest=FindItem(ECastleInteract::Rest); if(!Rest){Fail(TEXT("Rest room missing"));return;}
        P->Health=50; P->Shield=10; P->Reserve=0; Rest->Use(P);
        if(!Rest->bUsed || P->Health!=85 || P->Shield!=40 || P->Reserve!=48){Fail(TEXT("Rest room recovery"));return;}
        Rest->Use(P); if(P->Health!=85 || P->Reserve!=48){Fail(TEXT("Rest room repeat reward"));return;}
        if(auto* Event=FindItem(ECastleInteract::Event))
        {
            P->Health=15; Event->Use(P);
            if(Event->bUsed) {Fail(TEXT("Altar allowed lethal health cost"));return;}
            P->Health=P->MaxHealth; Event->Use(P); if(!Event->bUsed || P->Relics!=Relics+1){Fail(TEXT("Altar reward"));return;}
        }
        if(TypeCount[7])
        {
            ACastleEnemy* Elite=nullptr;
            for(TActorIterator<ACastleEnemy> It(GetWorld());It;++It) if(It->bElite) Elite=*It;
            if(!Elite || Elite->bBoss || Elite->MaxHealth<130){Fail(TEXT("Elite room enemy"));return;}
            const int32 Before=P->Coins;
            int32 ChestsBefore=0;
            for(TActorIterator<ACastleInteractable> It(GetWorld());It;++It) if(It->Kind==ECastleInteract::Chest) ++ChestsBefore;
            Elite->bActive=true;
            Elite->TakeDamage(99999,FDamageEvent(),P->GetController(),P);
            int32 ChestsAfter=0;
            for(TActorIterator<ACastleInteractable> It(GetWorld());It;++It) if(It->Kind==ECastleInteract::Chest) ++ChestsAfter;
            if(P->Coins!=Before+30 || ChestsAfter!=ChestsBefore+1){Fail(TEXT("Elite reward"));return;}
        }
        if(TypeCount[8])
        {
            ACastleTrap* Trap=nullptr;
            for(TActorIterator<ACastleTrap> It(GetWorld());It;++It) {Trap=*It;break;}
            if(!Trap || !FindItem(ECastleInteract::Chest)){Fail(TEXT("Trap room content"));return;}
            P->Shield=0; P->Health=100; Trap->NotifyActorBeginOverlap(P);
            if(!Trap->bTriggered || P->Health!=85){Fail(TEXT("Trap damage"));return;}
            Trap->NotifyActorBeginOverlap(P); if(P->Health!=85){Fail(TEXT("Trap repeat damage"));return;}
        }
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
    if(Floor==1)
    {
        if(FindItem(ECastleInteract::BossEnter) || FindItem(ECastleInteract::CaveEnter)) {Fail(TEXT("Boss or cave appeared on first floor"));return;}
        auto* Exit=FindItem(ECastleInteract::NextFloor); if(!Exit){Fail(TEXT("First-floor exit missing"));return;}
        SmokeFloor=Floor; SmokeWaitStart=0;
        UE_LOG(LogTemp,Display,TEXT("CASTLE_FIRST_FLOOR_OK Seed=%d ConnectedRooms=%d Route=7 Navigation=OK"),RunSeed,ActiveCount);
        Exit->Use(P); if(PendingFloor!=2){Fail(TEXT("First-floor exit did not request floor two"));return;}
        return;
    }
    UPCGComponent* RegionComponents[]={PCG,BossPCG,CavePCG};
    if(PCG->GetGraph()==BossPCG->GetGraph() || PCG->GetGraph()==CavePCG->GetGraph() || BossPCG->GetGraph()==CavePCG->GetGraph()) {Fail(TEXT("Region graphs are not independent"));return;}
    for(int32 Area=0;Area<3;++Area)
    {
        bool Found=false;
        for(auto* Node:RegionComponents[Area]->GetGraph()->GetNodes()) if(auto* Settings=Cast<UCastleLayoutSettings>(Node->GetSettings()))
            Found=Settings->GeneratorRegion==static_cast<ECastlePCGRegion>(Area);
        if(!Found){Fail(TEXT("Wrong region generator in PCG graph"));return;}
    }
    if(CaveRooms.Num()!=6 || CaveRoute.Num()!=4 || BossPlatforms.Num()<2 || BossPlatforms.Num()>4 || BossCoverPoints.Num()<4) {Fail(TEXT("Cave grid or arena features missing"));return;}
    TSet<int32> CaveSeen;
    int32 CaveActive=0,CaveLinks=0;
    for(const auto& Room:CaveRooms) if(Room.bActive) {++CaveActive;for(int32 Side=0;Side<4;++Side)if(Room.Doors&(1<<Side))++CaveLinks;}
    if(CaveActive!=4 || CaveLinks!=6){Fail(TEXT("Vault must be a four-room path"));return;}
    for(int32 Order=0;Order<4;++Order)
    {
        const int32 I=CaveRoute[Order];
        if(I<0 || I>=6 || CaveSeen.Contains(I) || !CaveRooms[I].bActive || CaveRooms[I].RouteOrder!=Order){Fail(TEXT("Vault route repeats a tile"));return;}
        CaveSeen.Add(I);
        int32 Degree=0;
        for(int32 Side=0;Side<4;++Side)if(CaveRooms[I].Doors&(1<<Side))
        {
            ++Degree; const int32 X=I%2+DX[Side],Y=I/2+DY[Side],N=Y*2+X;
            if(X<0 || X>=2 || Y<0 || Y>=3 || !CaveRooms[N].bActive || !(CaveRooms[N].Doors&(1<<((Side+2)%4)))){Fail(TEXT("Invalid cave corridor"));return;}
        }
        if(Degree!=((Order==0 || Order==3)?1:2)){Fail(TEXT("Vault has a dead-end reward branch"));return;}
        if(Order==0)
        {
            FNavLocation Entry;
            if(!Nav->ProjectPointToNavigation(CaveEntrance+FVector(0,0,100),Entry,FVector(200,200,300))) {Fail(TEXT("Vault entrance is outside navigation"));return;}
            continue;
        }
        auto* CavePath=UNavigationSystemV1::FindPathToLocationSynchronously(this,CaveEntrance+FVector(0,0,100),CaveRooms[I].Center+FVector(0,0,100));
        if(!CavePath || !CavePath->IsValid() || CavePath->IsPartial())
        {
            FNavLocation A,B;
            const bool PA=Nav->ProjectPointToNavigation(CaveEntrance+FVector(0,0,100),A,FVector(200,200,300));
            const bool PB=Nav->ProjectPointToNavigation(CaveRooms[I].Center+FVector(0,0,100),B,FVector(200,200,300));
            UE_LOG(LogTemp,Error,TEXT("CaveNav Room=%d Entry=%s Target=%s Project=%d/%d Points=%s/%s Valid=%d Partial=%d"),I,*CaveEntrance.ToString(),*CaveRooms[I].Center.ToString(),PA,PB,*A.Location.ToString(),*B.Location.ToString(),CavePath?CavePath->IsValid():0,CavePath?CavePath->IsPartial():0);
            for(const auto& Bounds:Nav->GetNavigationBounds())UE_LOG(LogTemp,Error,TEXT("CaveNavBounds %s"),*Bounds.AreaBox.ToString());
            Fail(TEXT("Vault route is not navigable"));return;
        }
    }
    for(const auto& Top:BossPlatforms)
    {
        auto* RaisedPath=UNavigationSystemV1::FindPathToLocationSynchronously(this,BossCenter+FVector(-1700,0,100),Top+FVector(0,0,100));
        if(!RaisedPath || !RaisedPath->IsValid() || RaisedPath->IsPartial() || RaisedPath->PathPoints.Last().Z<Top.Z-40){Fail(TEXT("Arena raised platform cannot be reached by ramp"));return;}
    }
    auto* Cave=FindItem(ECastleInteract::CaveEnter); if(!Cave){Fail(TEXT("Later-floor cave portal missing"));return;}
    const FVector BeforeCave=Cave->GetActorLocation();
    Cave->Use(P); if(Region!=TEXT("TREASURE CAVE")){Fail(TEXT("Cave entry"));return;}
    if(FVector::Dist2D(P->GetActorLocation(),CaveEntrance)>100){Fail(TEXT("Cave portal missed the entrance"));return;}
    auto* Return=FindItem(ECastleInteract::CaveReturn); if(!Return){Fail(TEXT("Cave return missing"));return;}
    if(CaveEntrance==CaveExit || FVector::Dist2D(Return->GetActorLocation(),CaveExit)>700){Fail(TEXT("Cave exit is not in the final room"));return;}
    Travel(P,CaveExit+FVector(0,0,110));
    Return->Use(P); if(Region!=TEXT("EXPLORATION") || FVector::Dist2D(P->GetActorLocation(),BeforeCave)>300){Fail(TEXT("Cave return"));return;}
    if(FindItem(ECastleInteract::NextFloor)){Fail(TEXT("Later-floor exit unlocked before boss"));return;}
    auto* ExistingBoss=Boss.Get();
    for(auto* Node:PCG->GetGraph()->GetNodes()) if(auto* Settings=Cast<UCastleLayoutSettings>(Node->GetSettings())) GenerateLayout(Settings);
    if(Boss!=ExistingBoss || !IsValid(Return) || CaveRoute.Num()!=4 || !bReady){Fail(TEXT("Exploration regeneration modified another region"));return;}
    for(TActorIterator<ACastleEnemy> It(GetWorld());It;++It)It->bActive=false;
    UE_LOG(LogTemp,Display,TEXT("CASTLE_REGIONS_OK Floor=%d Graphs=3 Cave=2x3/4Rooms/3Links RaisedPlatforms=%d Navigation=OK"),Floor,BossPlatforms.Num());
    auto* Gate=FindItem(ECastleInteract::BossEnter); if(!Gate){Fail(TEXT("Boss gate missing"));return;}
    Gate->Use(P); if(!Boss->bActive || Region!=TEXT("BOSS ARENA")){Fail(TEXT("Boss activation"));return;}
    // Verify the boss has a navigation path around randomized cover.
    auto* BossPath=UNavigationSystemV1::FindPathToLocationSynchronously(this,Boss->GetActorLocation(),P->GetActorLocation());
    if(!BossPath || !BossPath->IsValid() || BossPath->IsPartial()){Fail(TEXT("Boss arena navigation"));return;}
    Boss->TakeDamage(99999,FDamageEvent(),P->GetController(),P);
    if(!bBossDefeated || IsValid(Boss)){Fail(TEXT("Boss victory state"));return;}
    auto* Exit=FindItem(ECastleInteract::NextFloor); if(!Exit){Fail(TEXT("Boss did not unlock exit"));return;}
    SmokeFloor=Floor; SmokeWaitStart=0;
    UE_LOG(LogTemp,Display,TEXT("CASTLE_SMOKE_FLOOR_OK Floor=%d Seed=%d ConnectedRooms=%d Navigation=OK"),Floor,RunSeed,ActiveCount);
    Exit->Use(P);
    if(Floor==3)
    {
        if(!bWon){Fail(TEXT("Three floor victory"));return;}
        if(FParse::Param(FCommandLine::Get(),TEXT("CastleRetrySmokeTest")) && !bSmokeRetried)
        {
            const int32 Before=RunSeed;
            RetrySameSeed();
            if(RunSeed!=Before || PendingFloor!=1){Fail(TEXT("Same-seed retry"));return;}
            SmokeFloor=0;SmokeWaitStart=0;SmokeMovementStep=0;SmokeMovementAt=0;bSmokeRetried=true;
            UE_LOG(LogTemp,Display,TEXT("CASTLE_RETRY_SMOKE_OK Seed=%d"),RunSeed);
            return;
        }
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
        // The first floor has no cave or boss; capture those in the second floor.
        if(Floor==1){RequestFloor(2);NextCaptureAt=Now+3;return;}
        EnterCave(P,Rooms[0].Center); Name=TEXT("TreasureCave");
    }
    else if(CaptureStage==2)
    {
        EnterBoss(P);
        const FVector Viewpoint=BossPlatforms.IsEmpty()?BossCenter+FVector(-1900,-300,0):BossPlatforms[0];
        Travel(P,Viewpoint+FVector(0,0,110),(BossCenter-Viewpoint).Rotation().Yaw);
        Name=TEXT("BossArena");
    }
    else if(CaptureStage==3)
    {
        FPlatformMisc::RequestExit(false); return;
    }
    PendingCaptureName=Name; NextCaptureAt=Now+2;
}
