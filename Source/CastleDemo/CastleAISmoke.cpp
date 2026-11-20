#include "CastleDemo.h"
#include "CastleQueen.h"
#include "Camera/CameraComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "Engine/DamageEvents.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/StaticMeshActor.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "EngineUtils.h"
#include "NavigationSystem.h"
#include "HAL/PlatformMisc.h"

// Opt-in real-world BT test. Runs in the existing level; never modifies saved content.
void ACastleDirector::TickAISmokeTest()
{
    struct FTestState
    {
        int32 Stage=0, SavedAttackEvents=0;
        float StageAt=0, HealthBefore=0, PeakCoverSpeed=0;
        FVector Start;
        TWeakObjectPtr<ACastleEnemy> Enemy;
        TWeakObjectPtr<AStaticMeshActor> Wall;
    };
    static FTestState S;
    const float Now=GetWorld()->GetTimeSeconds();
    auto Fail=[](const TCHAR* Reason)
    {
        UE_LOG(LogTemp,Error,TEXT("CASTLE_AI_SMOKE_FAILED: %s"),Reason);
        FPlatformMisc::RequestExitWithStatus(true,1);
    };
    if(Now>65) {Fail(TEXT("Timed out"));return;}
    if(!bReady)return;
    auto* P=Cast<ACastlePlayer>(UGameplayStatics::GetPlayerPawn(this,0));
    if(!P)return;
    auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
    if(!Nav || Nav->IsNavigationBuildInProgress())return;
    auto Next=[&](int32 Stage) {S.Stage=Stage;S.StageAt=Now;};
    auto ClearBolts=[&]() {for(TActorIterator<ACastleBolt> It(GetWorld());It;++It)It->Destroy();};
    auto MakeWall=[&]()
    {
        auto* Wall=GetWorld()->SpawnActor<AStaticMeshActor>(FVector(-100,0,160),FRotator::ZeroRotator);
        auto* Mesh=Wall->GetStaticMeshComponent(); Mesh->SetMobility(EComponentMobility::Movable);
        Mesh->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));
        Mesh->SetWorldScale3D(FVector(.6f,5,4));Mesh->SetCollisionProfileName(TEXT("BlockAll"));
        Mesh->SetCanEverAffectNavigation(false);S.Wall=Wall;
    };
    auto MakeCover = [&]()
    {
        MakeWall();
        S.Wall->SetActorLocation(FVector(60,230,160));
        S.Wall->GetStaticMeshComponent()->SetWorldScale3D(FVector(1.3f,1.3f,3.2f));
        BossCoverPoints.Add(FVector(60,230,320));
    };
    if(S.Stage==0)
    {
        for(TActorIterator<ACastleEnemy> It(GetWorld());It;++It)It->bActive=false;
        P->MaxHealth=P->Health=10000; P->MaxShield=P->Shield=0;
        Travel(P,FVector(-400,0,110));
        S.Enemy=AddEnemy(FVector(300,0,100),false);S.Start=S.Enemy->GetActorLocation();
        Next(1);return;
    }
    auto* E=S.Enemy.Get();
    if(!E) {Fail(TEXT("Test enemy missing"));return;}
    auto* AI=Cast<ACastleAIController>(E->GetController());
    auto* BT=AI?Cast<UBehaviorTreeComponent>(AI->GetBrainComponent()):nullptr;
    auto* BB=AI?AI->GetBlackboardComponent():nullptr;
    if(!BT || !BT->IsRunning() || !BB) {Fail(TEXT("Real behavior tree/blackboard not running"));return;}
    const float Elapsed=Now-S.StageAt;
    switch(S.Stage)
    {
    case 1:
        if(Elapsed<.8f)return;
        if(BT->GetCurrentTree()->GetFName()!=TEXT("BT_CastleGuard") || E->AIPursuitTicks<5 || E->AIAttackTicks<5 ||
            E->AILastVolley!=1 || FVector::Dist2D(E->GetActorLocation(),S.Start)<20)
        {
            UE_LOG(LogTemp,Error,TEXT("Guard: Tree=%s Pursuit=%d Attack=%d Events=%d Volley=%d Move=%.1f BB Operational=%d Range=%d Sight=%d"),
                *BT->GetCurrentTree()->GetName(),E->AIPursuitTicks,E->AIAttackTicks,E->AIAttackEvents,E->AILastVolley,
                FVector::Dist2D(E->GetActorLocation(),S.Start),BB->GetValueAsBool(TEXT("IsOperational")),BB->GetValueAsBool(TEXT("InDecisionRange")),BB->GetValueAsBool(TEXT("CanSeeTarget")));
            Fail(TEXT("Guard tree did not chase and shoot concurrently"));return;
        }
        UE_LOG(LogTemp,Display,TEXT("CASTLE_AI_GUARD_OK: real BT, concurrent pursuit/attack, one bolt, movement %.1f"),FVector::Dist2D(E->GetActorLocation(),S.Start));
        E->GetCharacterMovement()->DisableMovement();AI->StopMovement();
        E->SetActorLocation(FVector(200,0,100)); Travel(P,FVector(100,0,100));
        ClearBolts();S.HealthBefore=P->Health;Next(2);break;
    case 2:
        if(Elapsed<2.1f)return;
        if(!FMath::IsNearlyEqual(S.HealthBefore-P->Health,10.f,.1f)) {Fail(TEXT("Guard melee damage/cooldown changed"));return;}
        UE_LOG(LogTemp,Display,TEXT("CASTLE_AI_MELEE_OK: 10 damage, original cooldown"));
        Travel(P,FVector(-400,0,110));MakeWall();ClearBolts();S.SavedAttackEvents=E->AIAttackEvents;Next(3);break;
    case 3:
        if(Elapsed<2.2f)return;
        if(BB->GetValueAsBool(TEXT("CanSeeTarget")) || E->AIAttackEvents!=S.SavedAttackEvents)
        {Fail(TEXT("Occluded guard attacked"));return;}
        UE_LOG(LogTemp,Display,TEXT("CASTLE_AI_SIGHT_OK: blocked sight prevents attacks"));
        S.Wall->Destroy();S.Wall.Reset();
        // Player stays close to enemy, but is outside the original home radius.
        E->SetActorLocation(FVector(2800,0,100));Travel(P,FVector(3000,0,100));Next(4);break;
    case 4:
        if(Elapsed<.3f)return;
        if(BB->GetValueAsBool(TEXT("InDecisionRange"))) {Fail(TEXT("Guard home radius changed"));return;}
        E->bActive=false;Next(5);break;
    case 5:
        if(Elapsed<.3f)return;
        if(BB->GetValueAsBool(TEXT("IsOperational"))) {Fail(TEXT("Inactive guard remains operational"));return;}
        UE_LOG(LogTemp,Display,TEXT("CASTLE_AI_GATES_OK: home radius and inactive stop"));
        E->Destroy();ClearBolts();Travel(P,FVector(-400,0,110));
        S.Enemy=AddEnemy(FVector(200,0,100),true);S.Enemy->Configure(true,2,this);
        S.Enemy->GetCharacterMovement()->DisableMovement();Next(6);break;
    case 6:
        if(Elapsed<.4f)return;
        if(BT->GetCurrentTree()->GetFName()!=TEXT("BT_CastleBoss") || E->AIAttackEvents!=0 || E->AIBlastEvents!=0)
        {Fail(TEXT("Dormant boss attacked or wrong tree"));return;}
        E->bActive=true;Next(7);break;
    case 7:
        if(Elapsed<1.2f)return;
        if(E->AILastVolley!=6 || E->AIPursuitTicks<5 || E->AIAttackTicks<5 || E->AISlamTicks<5)
        {Fail(TEXT("Boss parallel tasks/normal volley not running"));return;}
        if(auto* Queen=Cast<ACastleQueen>(E);!Queen || !Queen->ShootAnimation || !Queen->ChargeAnimation || !Queen->FrameGlow)
        {Fail(TEXT("Queen rig, animations or frame material missing"));return;}
        if(Cast<ACastleQueen>(E)->QueenMesh->GetMaterial(0)!=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/BossPrototype/QueenDemo/M_QueenDemo.M_QueenDemo")))
        {Fail(TEXT("Queen body is using the default material instead of its texture"));return;}
        {
            FHitResult UpperBody;FCollisionQueryParams ShotQuery(NAME_None,false,P);
            const FVector Target=E->GetActorLocation()+FVector(0,0,180);
            if(!GetWorld()->LineTraceSingleByChannel(UpperBody,Target-FVector(500,0,0),Target,ECC_Visibility,ShotQuery) || UpperBody.GetActor()!=E)
            {Fail(TEXT("Queen upper body is not targetable above movement capsule"));return;}
        }
        UE_LOG(LogTemp,Display,TEXT("CASTLE_AI_BOSS_NORMAL_OK: queen abilities use demo BT; 6 bolts from two muzzles"));
        E->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
        E->SetActorLocation(FVector(300,80,100));
        MakeCover();
        S.SavedAttackEvents=E->AIAttackEvents;
        Next(15);break;
    case 15:
    {
        if (E->GetCharacterMovement()->MaxWalkSpeed > 320.1f) {Fail(TEXT("Normal queen received cover sprint bonus"));return;}
        const FVector Behind=(BossCoverPoints.Last()-P->Camera->GetComponentLocation()).GetSafeNormal2D();
        const FVector Goal=BossCoverPoints.Last()+Behind*165.f;
        if (FVector::Dist2D(E->GetActorLocation(),Goal)>40.f)
        {
            if (E->IsChargingBlast() || Elapsed>6.f) Fail(TEXT("Queen must reach cover before charging"));
            return;
        }
        if(!E->IsChargingBlast())return;
        P->Camera->SetWorldRotation((E->GetActorLocation()+FVector(0,0,75)-P->Camera->GetComponentLocation()).Rotation());
        FHitResult Shot;
        FCollisionQueryParams Query(NAME_None,false,P);
        GetWorld()->LineTraceSingleByChannel(Shot,P->Camera->GetComponentLocation(),E->GetActorLocation()+FVector(0,0,75),ECC_Visibility,Query);
        S.HealthBefore=E->Health;
        const int32 Ammo=P->Ammo;
        P->Fire();
        if (Shot.GetActor()!=S.Wall.Get() || P->Ammo!=Ammo-1 || E->Health!=S.HealthBefore)
        {Fail(TEXT("Column did not block follow-up fire"));return;}
        UE_LOG(LogTemp,Display,TEXT("CASTLE_AI_COVER_OK: queen reached cover and the column blocked the follow-up shot"));
        S.Start=E->GetActorLocation(); Next(16);break;
    }
    case 16:
    {
        if(FVector::Dist(E->GetActorLocation(),S.Start)>1.f){Fail(TEXT("Queen moved during ground charge"));return;}
        if(E->IsChargingBlast())return;
        UE_LOG(LogTemp,Display,TEXT("CASTLE_AI_COVER_CHARGE_OK: reached cover without player fire and remained stationary through the blast"));
        BossCoverPoints.Pop();S.Wall->Destroy();S.Wall.Reset();
        E->SetActorLocation(FVector(200,0,100));Travel(P,FVector(-400,0,110));
    }
        E->GetCharacterMovement()->DisableMovement();
        E->Health=E->MaxHealth*.49f;Next(8);break;
    case 8:
        if(Elapsed<.2f)return;
        if(Elapsed<1.8f)
        {
            auto* Queen=Cast<ACastleQueen>(E);
            if(!Queen || Queen->Action!=3 || Queen->TakeDamage(1,FDamageEvent(),P->GetController(),P)!=0)
            {Fail(TEXT("Queen transition did not block damage and attacks"));return;}
            for(TActorIterator<ACastleQueenBolt> It(GetWorld());It;++It) if(It->Shooter==E)
            {Fail(TEXT("Queen transition retained old projectiles"));return;}
            return;
        }
        if(Elapsed<4.2f)return;
        if(!BB->GetValueAsBool(TEXT("IsEnraged")) || E->AILastVolley!=10)
        {Fail(TEXT("Queen phase two did not fire 10-bolt volley"));return;}
        UE_LOG(LogTemp,Display,TEXT("CASTLE_AI_ENRAGE_OK: invulnerable transition, clean projectiles, 10 bolts"));
        E->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
        E->SetActorLocation(FVector(300,80,100));MakeCover();
        S.Start=E->GetActorLocation();
        Travel(P,FVector(-400,0,110));
        Next(13);break;
    case 13:
        S.PeakCoverSpeed=FMath::Max(S.PeakCoverSpeed,float(E->GetVelocity().Size2D()));
        if(Elapsed<.55f || (S.PeakCoverSpeed<375.f && Elapsed<2.5f))return;
        if(S.PeakCoverSpeed<375.f || S.PeakCoverSpeed>385.f)
        {UE_LOG(LogTemp,Error,TEXT("Cover peak %.1f"),S.PeakCoverSpeed);Fail(TEXT("Enraged cover sprint speed"));return;}
        UE_LOG(LogTemp,Display,TEXT("CASTLE_AI_COVER_SPRINT_OK Peak=%.1f cm/s"),S.PeakCoverSpeed);
        ClearBolts();Next(9);break;
    case 9:
        if(!E->IsChargingBlast())return;
        if(BB->GetValueAsBool(TEXT("CanSeeTarget"))) {Fail(TEXT("Slam occlusion setup failed"));return;}
        if(FMath::Abs((P->GetActorLocation().Z-P->GetCapsuleComponent()->GetScaledCapsuleHalfHeight())-E->BlastCenter.Z)>10)
        {Fail(TEXT("Slam telegraph not on player's floor"));return;}
        UE_LOG(LogTemp,Display,TEXT("CASTLE_AI_SLAM_TRIGGER_OK: arms without sight, telegraph on floor"));
        S.HealthBefore=P->Health;E->SetActorLocation(FVector(4000,0,100));Next(10);break;
    case 10:
        if(Elapsed<1.6f)return;
        if(BB->GetValueAsBool(TEXT("InDecisionRange")) || E->IsChargingBlast() ||
            !FMath::IsNearlyEqual(S.HealthBefore-P->Health,52.8f,.15f))
        {Fail(TEXT("Armed slam did not resolve outside decision range with original damage"));return;}
        UE_LOG(LogTemp,Display,TEXT("CASTLE_AI_SLAM_RESOLVE_OK: 1.4s telegraph, 52.8 damage, resolves outside 3600cm"));
        S.Wall->Destroy();S.Wall.Reset();ClearBolts();
        E->SetActorLocation(FVector(200,0,100));P->bDead=true;S.SavedAttackEvents=E->AIAttackEvents;Next(11);break;
    case 11:
        if(Elapsed<1.1f)return;
        if(BB->GetValueAsBool(TEXT("IsOperational")) || E->AIAttackEvents!=S.SavedAttackEvents)
        {Fail(TEXT("Dead player still attacked"));return;}
        E->bActive=false;P->ResetRun();
        {
            auto* Bolt=GetWorld()->SpawnActor<ACastleBolt>(P->GetActorLocation()+FVector(100,0,0),FRotator::ZeroRotator);
            if(!Bolt){Fail(TEXT("Could not spawn test bolt"));return;}
            Bolt->Shooter=E;Bolt->Velocity=FVector(-1000,0,0);Bolt->Tick(.15f);
        }
        if(P->GetCharacterMovement()->MaxWalkSpeed!=465.f || P->Shield>=P->MaxShield)
        {Fail(TEXT("Shield hit did not apply projectile slow"));return;}
        Next(17);break;
    case 17:
        if(Elapsed<.4f)return;
        {
            auto* Bolt=GetWorld()->SpawnActor<ACastleQueenBolt>(P->GetActorLocation()+FVector(100,0,0),FRotator::ZeroRotator);
            if(!Bolt){Fail(TEXT("Could not spawn queen test bolt"));return;}
            Bolt->Shooter=E;Bolt->Velocity=FVector(-1000,0,0);Bolt->Tick(.15f);
        }
        Next(18);break;
    case 18:
        if(Elapsed<1.3f)return;
        if(P->GetCharacterMovement()->MaxWalkSpeed!=465.f){Fail(TEXT("Queen hit did not refresh slow without stacking"));return;}
        Next(19);break;
    case 19:
        if(Elapsed<.35f)return;
        if(P->GetCharacterMovement()->MaxWalkSpeed!=620.f){Fail(TEXT("Projectile slow did not expire"));return;}
        UE_LOG(LogTemp,Display,TEXT("CASTLE_BOLT_SLOW_OK guard and queen hits, shield, refresh, expiry"));
        UE_LOG(LogTemp,Display,TEXT("CASTLE_AI_SMOKE_OK: real guard/boss BTs, shared BB, movement/attack concurrency, sight, home gate, dormancy, enrage, ground slam, death gate."));
        FPlatformMisc::RequestExitWithStatus(true,0);Next(12);break;
    default:break;
    }
}
