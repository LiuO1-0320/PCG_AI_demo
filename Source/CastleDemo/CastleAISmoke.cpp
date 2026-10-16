#include "CastleDemo.h"
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
        float StageAt=0, HealthBefore=0;
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
        if(Elapsed<.4f)return;
        if(E->AILastVolley!=3 || E->AIPursuitTicks<5 || E->AIAttackTicks<5 || E->AISlamTicks<5)
        {Fail(TEXT("Boss parallel tasks/normal volley not running"));return;}
        UE_LOG(LogTemp,Display,TEXT("CASTLE_AI_BOSS_NORMAL_OK: pursuit, attack and slam tasks concurrent; 3 bolts"));
        E->Health=E->MaxHealth*.49f;Next(8);break;
    case 8:
        if(Elapsed<1.4f)return;
        if(!BB->GetValueAsBool(TEXT("IsEnraged")) || E->AILastVolley!=5)
        {Fail(TEXT("Boss enrage/5-bolt fan changed"));return;}
        UE_LOG(LogTemp,Display,TEXT("CASTLE_AI_ENRAGE_OK: half-health state, 5 bolts"));
        MakeWall();ClearBolts();Next(9);break;
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
        UE_LOG(LogTemp,Display,TEXT("CASTLE_AI_SMOKE_OK: real guard/boss BTs, shared BB, movement/attack concurrency, sight, home gate, dormancy, enrage, ground slam, death gate."));
        FPlatformMisc::RequestExitWithStatus(true,0);Next(12);break;
    default:break;
    }
}
