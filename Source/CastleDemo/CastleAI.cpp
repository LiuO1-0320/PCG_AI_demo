#include "CastleAI.h"
#include "CastleDemo.h"
#include "Camera/CameraComponent.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "BehaviorTree/BehaviorTreeTypes.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "BehaviorTree/BlackboardData.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"

ACastleAIController::ACastleAIController()
{
    bWantsPlayerState = false;
    GuardBehavior = FSoftObjectPath(TEXT("/Game/Demo/AI/BT_CastleGuard.BT_CastleGuard"));
    BossBehavior = FSoftObjectPath(TEXT("/Game/Demo/AI/BT_CastleBoss.BT_CastleBoss"));
}

void ACastleAIController::OnPossess(APawn* InPawn)
{
    Super::OnPossess(InPawn);
    // SpawnActor possession can precede Configure, which supplies the director and boss flag.
    if (const auto* Enemy = Cast<ACastleEnemy>(InPawn); Enemy && Enemy->Director) StartCastleBehavior();
}

void ACastleAIController::OnUnPossess()
{
    if (auto* BT = Cast<UBehaviorTreeComponent>(BrainComponent)) BT->StopTree(EBTStopMode::Forced);
    Super::OnUnPossess();
}

bool ACastleAIController::StartCastleBehavior()
{
    auto* Enemy = Cast<ACastleEnemy>(GetPawn());
    if (!Enemy || !Enemy->Director) return false;
    auto* Tree = (Enemy->bBoss ? BossBehavior : GuardBehavior).LoadSynchronous();
    if (!Tree || !Tree->BlackboardAsset || !Tree->RootNode)
    {
        UE_LOG(LogTemp, Error, TEXT("CASTLE_AI_MISSING: Generate /Game/Demo/AI assets with -run=CastleAISetup"));
        return false;
    }
    if (auto* BT = Cast<UBehaviorTreeComponent>(BrainComponent)) BT->StopTree(EBTStopMode::Forced);
    const bool bStarted = RunBehaviorTree(Tree);
    UpdateCastleBlackboard();
    if (!bStarted) UE_LOG(LogTemp, Error, TEXT("CASTLE_AI_FAILED: %s"), *Tree->GetName());
    return bStarted;
}

void ACastleAIController::UpdateCastleBlackboard()
{
    auto* BB = GetBlackboardComponent();
    auto* Enemy = Cast<ACastleEnemy>(GetPawn());
    auto* Player = Cast<ACastlePlayer>(UGameplayStatics::GetPlayerPawn(this, 0));
    if (!BB || !Enemy) return;
    const bool bOperational = Enemy->CanRunAI(Player);
    if (bOperational) Enemy->ResolvePendingBlast(Player);
    const bool bInRange = bOperational && Enemy->IsInDecisionRange(Player);
    bool bCanSee = false;
    if (bInRange)
    {
        FHitResult Hit;
        FCollisionQueryParams Query(SCENE_QUERY_STAT(CastleSight), false, Enemy);
        GetWorld()->LineTraceSingleByChannel(Hit, Enemy->GetActorLocation()+FVector(0,0,60),
            Player->Camera->GetComponentLocation(), ECC_Visibility, Query);
        bCanSee = !Hit.bBlockingHit || Hit.GetActor() == Player;
    }
    BB->SetValueAsObject(TEXT("TargetActor"), Player);
    BB->SetValueAsVector(TEXT("HomeLocation"), Enemy->Home);
    BB->SetValueAsFloat(TEXT("TargetDistance"), Player ? FVector::Dist(Player->GetActorLocation(),Enemy->GetActorLocation()) : MAX_flt);
    BB->SetValueAsBool(TEXT("IsOperational"), bOperational);
    BB->SetValueAsBool(TEXT("InDecisionRange"), bInRange);
    BB->SetValueAsBool(TEXT("CanSeeTarget"), bCanSee);
    BB->SetValueAsBool(TEXT("IsBoss"), Enemy->bBoss);
    BB->SetValueAsBool(TEXT("IsEnraged"), Enemy->bBoss && Enemy->Health < Enemy->MaxHealth*.5f);
    BB->SetValueAsBool(TEXT("IsChargingBlast"), Enemy->IsChargingBlast());
}

UBTService_CastlePerception::UBTService_CastlePerception()
{
    NodeName = TEXT("Update target, sight and pending slam");
    Interval = 0.f; RandomDeviation = 0.f;
    bNotifyTick = true; bNotifyOnSearch = true; bCallTickOnSearchStart = false;
}

void UBTService_CastlePerception::OnSearchStart(FBehaviorTreeSearchData& SearchData)
{
    if (auto* AI = Cast<ACastleAIController>(SearchData.OwnerComp.GetAIOwner())) AI->UpdateCastleBlackboard();
}

void UBTService_CastlePerception::TickNode(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
    Super::TickNode(OwnerComp, NodeMemory, DeltaSeconds);
    if (auto* AI = Cast<ACastleAIController>(OwnerComp.GetAIOwner())) AI->UpdateCastleBlackboard();
}

UBTTask_CastleAction::UBTTask_CastleAction()
{
    NodeName = TEXT("Castle action");
    bNotifyTick = true;
}

FString UBTTask_CastleAction::GetStaticDescription() const
{
    switch (Action)
    {
    case ECastleAIAction::Pursuit: return TEXT("Face target each frame; MoveToActor every 0.45s. Guard: 450/320 cm; Boss: 650/500 cm.");
    case ECastleAIAction::Attack: return TEXT("Requires sight. Melee <140 cm, otherwise projectile. Guard: 1.9s/1 bolt. Boss: 1.35s/3 bolts, below half HP: 0.85s/5 bolts.");
    case ECastleAIAction::GroundSlam: return TEXT("Independent of sight. Cooldown 6s, telegraph 1.4s, radius 400 cm, height tolerance 160 cm, damage x2.4. Root service resolves pending hits even outside decision range.");
    default: return TEXT("Keep existing path outside range; stop only when inactive, player dead or victorious. No patrol or return-home behavior.");
    }
}

EBTNodeResult::Type UBTTask_CastleAction::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
    return EBTNodeResult::InProgress;
}

void UBTTask_CastleAction::TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
    auto* AI = Cast<ACastleAIController>(OwnerComp.GetAIOwner());
    auto* Enemy = AI ? Cast<ACastleEnemy>(AI->GetPawn()) : nullptr;
    auto* BB = OwnerComp.GetBlackboardComponent();
    auto* Player = BB ? Cast<ACastlePlayer>(BB->GetValueAsObject(TEXT("TargetActor"))) : nullptr;
    if (!Enemy) return;
    if (Action == ECastleAIAction::Hold)
    {
        if (Player && !Enemy->bDead && Enemy->Director && Enemy->Director->bReady &&
            (!Enemy->bActive || Player->bDead || Enemy->Director->bWon)) AI->StopMovement();
        return;
    }
    // Recheck live state during branch aborts, so no task can attack a paused/dead player.
    if (!Enemy->CanRunAI(Player) || !Enemy->IsInDecisionRange(Player)) return;
    const bool bCanSee = BB && BB->GetValueAsBool(TEXT("CanSeeTarget"));
    switch (Action)
    {
    case ECastleAIAction::Pursuit: Enemy->UpdatePursuit(Player,bCanSee); break;
    case ECastleAIAction::Attack: Enemy->UpdateAttack(Player,bCanSee); break;
    case ECastleAIAction::GroundSlam: Enemy->UpdateGroundSlam(Player); break;
    default: break;
    }
}

void UBTDecorator_CastleBool::ConfigureKey(FName Key)
{
    BlackboardKey.SelectedKeyName = Key;
    OperationType = static_cast<uint8>(EBasicKeyOperation::Set);
#if WITH_EDITORONLY_DATA
    BasicOperation = EBasicKeyOperation::Set;
#endif
    FlowAbortMode = EBTFlowAbortMode::Both;
    NodeName = FString::Printf(TEXT("%s is true"), *Key.ToString());
}
