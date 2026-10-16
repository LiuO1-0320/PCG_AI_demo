#include "CastleDemo.h"
#include "Camera/CameraComponent.h"
#include "Camera/CameraActor.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "NavigationSystem.h"
#include "Engine/GameViewportClient.h"
#include "EngineUtils.h"
#include "HAL/PlatformMisc.h"
#include "Misc/Paths.h"

// Opt-in presentation capture: stages positions/camera, but all attacks are executed
// by the real behavior trees. No saved level, asset or default gameplay is changed.
void ACastleDirector::TickAICapture()
{
    struct FState
    {
        int32 Stage=0,Events=0,NextStage=0;
        float WaitUntil=0,ObservedAt=0,ShieldBefore=0;
        FVector Center;
        TWeakObjectPtr<ACastleEnemy> Guard;
        TWeakObjectPtr<ACameraActor> Observer;
    };
    static FState S;
    const float Now=GetWorld()->GetTimeSeconds();
    if(Now>140) {UE_LOG(LogTemp,Error,TEXT("CASTLE_AI_CAPTURE_FAILED: timed out"));FPlatformMisc::RequestExitWithStatus(true,1);return;}
    if(!bReady || Now<S.WaitUntil)return;
    auto* P=Cast<ACastlePlayer>(UGameplayStatics::GetPlayerPawn(this,0));if(!P)return;
    auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
    if(!Nav || Nav->IsNavigationBuildInProgress())return;
    auto ClearBolts=[&]() {for(TActorIterator<ACastleBolt> It(GetWorld());It;++It)It->Destroy();};
    auto Restore=[&]() {P->bHelp=false;P->bDead=false;P->Health=P->MaxHealth;P->Shield=P->MaxShield;P->Ammo=P->MagazineSize;};
    auto PlayerView=[&]()
    {
        if(auto* PC=Cast<APlayerController>(P->GetController()))PC->SetViewTarget(P);
        if(S.Observer.IsValid())S.Observer->Destroy();S.Observer.Reset();
    };
    auto MeleeView=[&](ACastleEnemy* Enemy,FVector Offset)
    {
        const FVector Position=Enemy->GetActorLocation()+Offset;
        S.Observer=GetWorld()->SpawnActor<ACameraActor>(Position,(Enemy->GetActorLocation()+FVector(0,0,30)-Position).Rotation());
        S.Observer->GetCameraComponent()->SetFieldOfView(65);
        if(auto* PC=Cast<APlayerController>(P->GetController()))PC->SetViewTarget(S.Observer.Get());
    };
    auto Aim=[&](FVector Target)
    {
        if(auto* PC=Cast<APlayerController>(P->GetController())) PC->SetControlRotation((Target-P->Camera->GetComponentLocation()).Rotation());
    };
    auto Capture=[&](const TCHAR* Name,int32 NextStage)
    {
        const FString File=FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()/TEXT("output/ai_tasks_en_2026-10-14/screenshots")/FString(Name)+TEXT(".png"));
        FScreenshotRequest::RequestScreenshot(File,false,false);
        UE_LOG(LogTemp,Display,TEXT("CASTLE_AI_TASK_CAPTURE: %s Shield=%.1f Health=%.1f"),Name,P->Shield,P->Health);
        S.Stage=NextStage;S.WaitUntil=Now+.8f;S.ObservedAt=0;
    };
    switch(S.Stage)
    {
    case 0:
        if(Now<12)return;
        if(auto* PC=Cast<APlayerController>(P->GetController()))P->DisableInput(PC);
        for(TActorIterator<ACastleEnemy> It(GetWorld());It;++It)It->bActive=false;
        S.Center=Rooms[FirstFloorRoute[1]].Center;
        S.Guard=AddEnemy(S.Center+FVector(420,0,100),false);
        S.Guard->GetCharacterMovement()->DisableMovement();
        Restore();Travel(P,S.Center+FVector(-600,-180,110));P->Camera->SetFieldOfView(65);
        Aim(S.Guard->GetActorLocation()+FVector(0,0,30));S.Events=S.Guard->AIAttackEvents;
        S.Stage=1;break;
    case 1:
        if(!S.Guard.IsValid())return;
        if(S.Guard->AIAttackEvents<=S.Events || S.Guard->AILastVolley!=1)return;
        if(S.ObservedAt==0){S.ObservedAt=Now;return;}
        if(Now-S.ObservedAt<.18f)return;
        Capture(TEXT("01_guard_ranged"),2);break;
    case 2:
        ClearBolts();Restore();P->Camera->SetFieldOfView(90);
        S.Guard->SetActorLocation(S.Center+FVector(200,0,100));
        Travel(P,S.Center+FVector(90,0,100));Aim(S.Guard->GetActorLocation()+FVector(0,0,45));
        MeleeView(S.Guard.Get(),FVector(-430,-270,200));
        S.Events=S.Guard->AIAttackEvents;S.ShieldBefore=P->Shield;S.Stage=3;break;
    case 3:
        if(S.Guard->AIAttackEvents<=S.Events || P->Shield>=S.ShieldBefore)return;
        UE_LOG(LogTemp,Display,TEXT("CASTLE_AI_CAPTURE_MELEE: Guard Shield %.1f -> %.1f Damage=%.1f"),S.ShieldBefore,P->Shield,S.Guard->AttackDamage);
        Capture(TEXT("02_guard_melee"),4);break;
    case 4:
        PlayerView();
        if(S.Guard.IsValid())S.Guard->bActive=false;
        ClearBolts();
        if(Floor==1){RequestFloor(2);S.WaitUntil=Now+2;return;}
        S.WaitUntil=Now+3;S.Stage=5;break;
    case 5:
        for(TActorIterator<ACastleEnemy> It(GetWorld());It;++It)It->bActive=false;
        Restore();EnterBoss(P);Boss->GetCharacterMovement()->DisableMovement();
        Travel(P,BossCenter+FVector(-700,-420,110));P->Camera->SetFieldOfView(65);
        Aim(Boss->GetActorLocation()+FVector(0,0,65));S.Events=Boss->AIAttackEvents;S.Stage=6;break;
    case 6:
        if(Boss->AIAttackEvents<=S.Events || Boss->AILastVolley!=3)return;
        if(S.ObservedAt==0){S.ObservedAt=Now;return;}
        if(Now-S.ObservedAt<.18f)return;
        Capture(TEXT("03_boss_ranged"),7);break;
    case 7:
        ClearBolts();Restore();Boss->Health=Boss->MaxHealth*.49f;
        S.Events=Boss->AIAttackEvents;S.Stage=8;break;
    case 8:
        if(Boss->AIAttackEvents<=S.Events || Boss->AILastVolley!=5)return;
        if(S.ObservedAt==0){S.ObservedAt=Now;return;}
        if(Now-S.ObservedAt<.18f)return;
        Capture(TEXT("04_boss_enraged_ranged"),9);break;
    case 9:
        ClearBolts();Restore();P->Camera->SetFieldOfView(100);
        Boss->Health=Boss->MaxHealth;
        Travel(P,Boss->GetActorLocation()+FVector(-100,0,-80));
        Aim(Boss->GetActorLocation()+FVector(0,0,50));
        MeleeView(Boss,FVector(-650,-350,300));
        S.Events=Boss->AIAttackEvents;S.ShieldBefore=P->Shield;S.Stage=10;break;
    case 10:
        if(Boss->AIAttackEvents<=S.Events || P->Shield>=S.ShieldBefore)return;
        UE_LOG(LogTemp,Display,TEXT("CASTLE_AI_CAPTURE_MELEE: Boss Shield %.1f -> %.1f Damage=%.1f"),S.ShieldBefore,P->Shield,Boss->AttackDamage);
        Capture(TEXT("05_boss_melee"),11);break;
    case 11:
        PlayerView();
        ClearBolts();Restore();Travel(P,BossCenter+FVector(-550,-100,110));
        P->Camera->SetFieldOfView(95);
        if(auto* PC=Cast<APlayerController>(P->GetController()))
        {
            auto Look=(Boss->GetActorLocation()-P->GetActorLocation()).Rotation();Look.Pitch=-22;Look.Roll=0;PC->SetControlRotation(Look);
        }
        S.Stage=12;S.WaitUntil=Now+.1f;break;
    case 12:
        if(!Boss->IsChargingBlast())return;
        // If this is a previous telegraph at the melee position, wait for the next one.
        if(FVector::Dist2D(P->GetActorLocation(),Boss->BlastCenter)>100)return;
        Capture(TEXT("06_boss_slam_charge"),13);break;
    case 13:
        UE_LOG(LogTemp,Display,TEXT("CASTLE_AI_TASK_CAPTURE_OK: six real rendered task screenshots captured."));
        FPlatformMisc::RequestExitWithStatus(false,0);S.Stage=14;break;
    default:break;
    }
}
