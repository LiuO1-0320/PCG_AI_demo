#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "../CastleDemo.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/IConsoleManager.h"
#include "Misc/App.h"
#include "NavMesh/RecastNavMesh.h"
#include "Navigation/PathFollowingComponent.h"
#include "NavigationSystem.h"
#include "TimerManager.h"

namespace
{
constexpr double OpenAt = 16.0;
const FVector PlayerStart(530.0, -510.0, 90.15);
const FVector GuardStarts[] = {
    FVector(2047.420, -254.878, 90.15),
    FVector(2383.825, -268.878, 90.15),
    FVector(2170.0, -435.0, 90.15)
};

enum class ETrial { Reopen, Move, Combat };

struct FObservedGuard
{
    TWeakObjectPtr<ACastleEnemy> Enemy;
    TWeakObjectPtr<AAIController> AI;
    TWeakObjectPtr<UPathFollowingComponent> Following;
    FDelegateHandle FinishHandle;
    TSet<uint32> ObservedRequests;
    uint32 LastRequest = 0;
    int32 BeforeRequests = 0;
    int32 BeforePartialFinishes = 0;
    bool bLastWasPartial = false;
    bool bFirstFullLogged = false;
    bool bSawFiringStop = false;
    bool bPursuedAfterFiring = false;
    bool bSawOccludedPursuit = false;
    bool bWasOccluded = false;
    bool bSawLostSight = false;
    bool bSawVisible = false;
    double FirstRecovery = -1.0;
    double FirstFullRoute = -1.0;
    double LastBoltTime = -1.0;
    double MinBoltInterval = TNumericLimits<double>::Max();
    int32 Bolts = 0;
};

struct FAmbientGuard
{
    TWeakObjectPtr<ACastleEnemy> Enemy;
    bool bWasActive;
    bool bHadCollision;
};

class FPursuitFixture : public TSharedFromThis<FPursuitFixture>
{
public:
    FPursuitFixture(UWorld& InWorld, ACastleDirector& InDirector, ACastlePlayer& InPlayer,
        UClass& InGuardClass, ETrial InTrial, int32 InCount)
        : World(&InWorld), Director(&InDirector), Player(&InPlayer), GuardClass(&InGuardClass),
          InitialClass(InDirector.OrdinaryEnemyClass.Get()), Trial(InTrial), Count(InCount), Start(InWorld.GetTimeSeconds()),
          InitialHealth(InPlayer.Health), InitialShield(InPlayer.Shield) {}

    void StartTrial()
    {
        UWorld& TestWorld = *World.Get();
        ACastlePlayer& TestPlayer = *Player.Get();
        TestPlayer.GetCharacterMovement()->StopMovementImmediately();
        TestPlayer.SetActorLocation(PlayerStart, false, nullptr, ETeleportType::TeleportPhysics);

        for (TActorIterator<ACastleEnemy> It(&TestWorld); It; ++It)
        {
            ACastleEnemy* Enemy = *It;
            if (Enemy->bBoss || Enemy->bElite) continue;
            Ambient.Add({Enemy, Enemy->bActive, Enemy->GetActorEnableCollision()});
            Enemy->bActive = false;
            Enemy->SetActorEnableCollision(false);
            if (AAIController* AI = Cast<AAIController>(Enemy->GetController())) AI->StopMovement();
        }

        UE_LOG(LogTemp, Display, TEXT("CASTLE_FIX05_TEST START t=%.3f seed=%d map=%s scenario=%s class=%s count=%d player=%s"),
            Start, Director->RunSeed, *TestWorld.GetMapName(), ScenarioName(), *GuardClass->GetPathName(),
            Count, *TestPlayer.GetActorLocation().ToCompactString());

        if (Trial != ETrial::Combat && !BuildPen(TestWorld))
        {
            Finish(false, TEXT("fence creation failed"));
            return;
        }

        TWeakPtr<FPursuitFixture> WeakFixture = AsShared();
        SpawnHandle = TestWorld.AddOnActorSpawnedHandler(FOnActorSpawned::FDelegate::CreateLambda([WeakFixture](AActor* Actor)
        {
            if (const TSharedPtr<FPursuitFixture> Fixture = WeakFixture.Pin()) Fixture->ActorSpawned(Actor);
        }));
        FTimerManagerTimerParameters TimerParameters;
        TimerParameters.bLoop = true;
        TimerParameters.bMaxOncePerFrame = Trial != ETrial::Reopen;
        // Movement input is consumed each frame. Keep the original Reopen sampling cadence.
        const float Interval = Trial == ETrial::Reopen ? 0.05f : 0.001f;
        TestWorld.GetTimerManager().SetTimer(TickHandle, FTimerDelegate::CreateLambda([Fixture = AsShared()] { Fixture->Tick(); }),
            Interval, TimerParameters);
    }

private:
    bool BuildPen(UWorld& TestWorld)
    {
        UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
        if (!Cube) return false;

        // Exact positions and scales of the FIX04 temporary fixture. Index 0 is the west wall.
        const FVector Locations[] = {{338, -520, 160}, {722, -520, 160}, {530, -742, 160}, {411, -300, 160}, {649, -300, 160}};
        const FVector Scales[] = {{.26, 4.66, 3.20}, {.26, 4.66, 3.20}, {3.82, .26, 3.20}, {1.68, .40, 3.20}, {1.68, .40, 3.20}};
        FActorSpawnParameters Parameters;
        Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        Parameters.ObjectFlags |= RF_Transient;
        for (int32 Index = 0; Index < UE_ARRAY_COUNT(Locations); ++Index)
        {
            const FTransform Transform(FQuat::Identity, Locations[Index], Scales[Index]);
            AStaticMeshActor* Wall = TestWorld.SpawnActor<AStaticMeshActor>(AStaticMeshActor::StaticClass(), Transform, Parameters);
            if (!Wall) return false;
            Walls.Add(Wall);
            UStaticMeshComponent* Mesh = Wall->GetStaticMeshComponent();
            // Native StaticMeshActor registers its static component during spawn.
            Mesh->SetMobility(EComponentMobility::Movable);
            if (!Mesh->SetStaticMesh(Cube)) return false;
            Mesh->SetCollisionProfileName(TEXT("BlockAll"));
            Mesh->SetCanEverAffectNavigation(true);
            Mesh->SetMobility(EComponentMobility::Static);
        }
        return true;
    }

    bool SpawnGuards(UWorld& TestWorld)
    {
        TWeakPtr<FPursuitFixture> WeakFixture = AsShared();
        FActorSpawnParameters Parameters;
        Parameters.Owner = Director.Get();
        Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        Parameters.ObjectFlags |= RF_Transient;
        for (int32 Index = 0; Index < Count; ++Index)
        {
            ACastleEnemy* Enemy = TestWorld.SpawnActor<ACastleEnemy>(GuardClass.Get(), GuardStarts[Index], FRotator::ZeroRotator, Parameters);
            if (!Enemy) return false;
            FObservedGuard& Guard = Guards.AddDefaulted_GetRef();
            Guard.Enemy = Enemy;
            if (!Enemy->GetController()) Enemy->SpawnDefaultController();
            AAIController* AI = Cast<AAIController>(Enemy->GetController());
            if (!AI || !AI->GetPathFollowingComponent()) return false;
            Guard.AI = AI;
            Guard.Following = AI->GetPathFollowingComponent();
            Guard.FinishHandle = Guard.Following->OnRequestFinished.AddLambda([WeakFixture, Index](FAIRequestID Id, const FPathFollowingResult& Result)
            {
                if (const TSharedPtr<FPursuitFixture> Fixture = WeakFixture.Pin()) Fixture->RequestFinished(Index, Id, Result);
            });
            Enemy->Configure(false, Director->Floor, Director.Get());
            UE_LOG(LogTemp, Display, TEXT("CASTLE_FIX05_TEST GUARD t=%.3f enemy=%s class=%s origin=%s"),
                Elapsed(), *Enemy->GetName(), *Enemy->GetClass()->GetPathName(), *Enemy->Home.ToCompactString());
            if (Index == 0)
            {
                UNavigationSystemV1* Navigation = FNavigationSystem::GetCurrent<UNavigationSystemV1>(&TestWorld);
                ARecastNavMesh* Recast = Navigation ? Cast<ARecastNavMesh>(Navigation->GetNavDataForProps(
                    AI->GetNavAgentPropertiesRef(), AI->GetNavAgentLocation())) : nullptr;
                if (!Recast) return false;
                NavMesh = Recast;
                UE_LOG(LogTemp, Display, TEXT("CASTLE_FIX05_TEST NAVDATA t=%.3f nav=%s remainingJobs=%d"),
                    Elapsed(), *Recast->GetName(), Navigation->GetNumRemainingBuildTasks());
            }
        }
        return true;
    }

    void ActorSpawned(AActor* Actor)
    {
        const ACastleBolt* Bolt = Cast<ACastleBolt>(Actor);
        if (!Bolt) return;
        for (FObservedGuard& Guard : Guards)
        {
            if (Bolt->GetOwner() != Guard.Enemy.Get()) continue;
            const double Now = Elapsed();
            const double Interval = Guard.LastBoltTime < 0.0 ? -1.0 : Now - Guard.LastBoltTime;
            if (Interval >= 0.0) Guard.MinBoltInterval = FMath::Min(Guard.MinBoltInterval, Interval);
            Guard.LastBoltTime = Now;
            ++Guard.Bolts;
            UE_LOG(LogTemp, Display, TEXT("CASTLE_FIX05_TEST BOLT t=%.3f enemy=%s bolt=%s interval=%.3f"),
                Now, *GetNameSafe(Guard.Enemy.Get()), *Bolt->GetName(), Interval);
        }
    }

    void RequestFinished(int32 Index, FAIRequestID Id, const FPathFollowingResult& Result)
    {
        if (!Guards.IsValidIndex(Index)) return;
        FObservedGuard& Guard = Guards[Index];
        const bool bKnownPartial = Guard.LastRequest == Id.GetID() && Guard.bLastWasPartial;
        if (Elapsed() < OpenAt && bKnownPartial && Result.IsSuccess()) ++Guard.BeforePartialFinishes;
        UE_LOG(LogTemp, Display, TEXT("CASTLE_FIX05_TEST FINISH t=%.3f enemy=%s request=%u partial=%d result=%s"),
            Elapsed(), *GetNameSafe(Guard.Enemy.Get()), Id.GetID(), bKnownPartial, *Result.ToString());
    }

    void ObserveGuards(ACastlePlayer& TestPlayer, double Now)
    {
        const bool bSample = Now >= NextSample;
        if (bSample) NextSample = Now + .25;
        for (FObservedGuard& Guard : Guards)
        {
            ACastleEnemy* Enemy = Guard.Enemy.Get();
            const AAIController* AI = Guard.AI.Get();
            const UPathFollowingComponent* Following = Guard.Following.Get();
            if (!Enemy || !AI || !Following) continue;
            const EPathFollowingStatus::Type Status = Following->GetStatus();
            const FAIRequestID Id = Following->GetCurrentRequestId();
            const bool bPartial = AI->HasPartialPath();
            if (Id.IsValid() && Status != EPathFollowingStatus::Idle)
            {
                Guard.LastRequest = Id.GetID();
                Guard.bLastWasPartial = bPartial;
                if (!Guard.ObservedRequests.Contains(Id.GetID()))
                {
                    Guard.ObservedRequests.Add(Id.GetID());
                    if (Now < OpenAt) ++Guard.BeforeRequests;
                    else if (Guard.FirstRecovery < 0.0) Guard.FirstRecovery = Now;
                    UE_LOG(LogTemp, Display, TEXT("CASTLE_FIX05_TEST REQUEST t=%.3f enemy=%s request=%u partial=%d"),
                        Now, *Enemy->GetName(), Id.GetID(), bPartial);
                }
            }
            if (!bSample) continue;

            const UBlackboardComponent* Blackboard = AI->GetBlackboardComponent();
            const bool bCanSee = Blackboard && Blackboard->GetValueAsBool(TEXT("CanSeeTarget"));
            const bool bDecision = Blackboard && Blackboard->GetValueAsBool(TEXT("InDecisionRange"));
            const FVector Velocity = Enemy->GetVelocity();
            const float Speed = Velocity.Size2D();
            const float Distance = FVector::Dist(Enemy->GetActorLocation(), TestPlayer.GetActorLocation());
            Guard.bSawLostSight |= Guard.bSawVisible && !bCanSee && bDecision;
            Guard.bSawVisible |= bCanSee;
            Guard.bSawOccludedPursuit |= !bCanSee && bDecision && Speed > 35.f;
            Guard.bPursuedAfterFiring |= Guard.bSawFiringStop && (!bCanSee || Distance > 450.f) && Speed > 35.f;
            Guard.bSawFiringStop |= bCanSee && Distance <= 450.f && Speed < 10.f && Status == EPathFollowingStatus::Idle;
            const double Angle = Speed > 60.f ? FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(
                FVector::DotProduct(Velocity.GetSafeNormal2D(), Enemy->GetActorForwardVector().GetSafeNormal2D()), -1.0, 1.0))) : -1.0;
            UE_LOG(LogTemp, Display, TEXT("CASTLE_FIX05_TEST SAMPLE t=%.3f enemy=%s player=%s loc=%s speed=%.2f angle=%.2f dist=%.2f los=%d decision=%d path=%d partial=%d moveMode=%d attacks=%d frameMs=%.3f"),
                Now, *Enemy->GetName(), *TestPlayer.GetActorLocation().ToCompactString(), *Enemy->GetActorLocation().ToCompactString(),
                Speed, Angle, Distance, bCanSee, bDecision, int32(Status), bPartial, int32(Enemy->GetCharacterMovement()->MovementMode),
                Enemy->AIAttackEvents, FApp::GetDeltaTime() * 1000.0);
            if (Trial == ETrial::Reopen && Now >= 13.0 && Now <= 24.0) CheckPath(Guard, *Enemy, *AI, TestPlayer, Now);
        }
    }

    void CheckPath(FObservedGuard& Guard, ACastleEnemy& Enemy, const AAIController& AI, ACastlePlayer& TestPlayer, double Now)
    {
        UNavigationSystemV1* Navigation = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World.Get());
        if (!Navigation) return;
        FAIMoveRequest MoveRequest(&TestPlayer);
        MoveRequest.SetAllowPartialPath(true);
        MoveRequest.SetNavigationFilter(AI.GetDefaultNavigationFilterClass());
        FPathFindingQuery Query;
        if (!AI.BuildPathfindingQuery(MoveRequest, Query)) return;
        const double ProbeStart = FPlatformTime::Seconds();
        const FPathFindingResult Path = Navigation->FindPathSync(Query);
        const double ProbeMs = (FPlatformTime::Seconds() - ProbeStart) * 1000.0;
        const bool bValid = Path.IsSuccessful() && Path.Path.IsValid() && Path.Path->IsValid();
        const bool bPartial = bValid && Path.Path->IsPartial();
        if (Now >= OpenAt && bValid && !bPartial && !Guard.bFirstFullLogged)
        {
            Guard.bFirstFullLogged = true;
            Guard.FirstFullRoute = Now;
        }
        UE_LOG(LogTemp, Display, TEXT("CASTLE_FIX05_TEST NAV t=%.3f enemy=%s nav=%s valid=%d partial=%d remainingJobs=%d queryMs=%.4f"),
            Now, *Enemy.GetName(), *GetNameSafe(NavMesh.Get()), bValid, bPartial, Navigation->GetNumRemainingBuildTasks(), ProbeMs);
    }

    void Tick()
    {
        UWorld* TestWorld = World.Get();
        ACastlePlayer* TestPlayer = Player.Get();
        if (!TestWorld || !TestPlayer || !Director.IsValid()) return;
        const double Now = Elapsed();
        if (!bSpawned && Now >= 1.25)
        {
            bSpawned = true;
            if (!SpawnGuards(*TestWorld)) { Finish(false, TEXT("native guard/controller/NavData setup failed")); return; }
        }
        if (Trial == ETrial::Reopen && !bOpened && Now >= OpenAt)
        {
            bOpened = Walls[0].IsValid() && Walls[0]->Destroy();
            if (!bOpened) { Finish(false, TEXT("west wall could not be removed")); return; }
            UE_LOG(LogTemp, Display, TEXT("CASTLE_FIX05_TEST OPEN t=%.3f player=%s displacement=%.3f"),
                Now, *TestPlayer->GetActorLocation().ToCompactString(), FVector::Dist2D(PlayerStart, TestPlayer->GetActorLocation()));
        }
        if (Trial == ETrial::Move && Now >= OpenAt)
        {
            const double Displacement = FVector::Dist2D(PlayerStart, TestPlayer->GetActorLocation());
            if (!bMovementThresholdLogged && Displacement >= 100.0)
            {
                bMovementThresholdLogged = true;
                UE_LOG(LogTemp, Display, TEXT("CASTLE_FIX05_TEST MOVED t=%.3f displacement=%.3f"), Now, Displacement);
            }
            if (TestPlayer->GetActorLocation().Y < -354.0) TestPlayer->AddMovementInput(FVector::RightVector, .75f);
        }
        if (Trial == ETrial::Combat && Now >= 6.8) MoveCombatPlayer(*TestPlayer, Now);
        ObserveGuards(*TestPlayer, Now);
        if (Now >= (Trial == ETrial::Combat ? 35.0 : 27.0) || TestPlayer->bDead)
        {
            Finish(!TestPlayer->bDead, TestPlayer->bDead ? TEXT("player died before completion") : TEXT("finished"));
        }
    }

    void MoveCombatPlayer(ACastlePlayer& TestPlayer, double Now)
    {
        const FVector Waypoints[] = {{530, 0, 90.15}, {1160, 0, 90.15}, {1810, 0, 90.15}, {1160, 0, 90.15}, {530, 0, 90.15}, {530, -510, 90.15}};
        if (CombatWaypoint >= UE_ARRAY_COUNT(Waypoints) || Now < ResumeCombatAt) return;
        const FVector Delta = Waypoints[CombatWaypoint] - TestPlayer.GetActorLocation();
        if (Delta.Size2D() < 35.f)
        {
            UE_LOG(LogTemp, Display, TEXT("CASTLE_FIX05_TEST WAYPOINT t=%.3f index=%d"), Now, CombatWaypoint);
            if (CombatWaypoint == 2) ResumeCombatAt = Now + 4.0;
            ++CombatWaypoint;
            return;
        }
        TestPlayer.AddMovementInput(Delta.GetSafeNormal2D(), Delta.Size2D() < 110.f ? .55f : 1.f);
    }

    void Finish(bool bCompleted, const TCHAR* Reason)
    {
        const TSharedRef<FPursuitFixture> KeepAlive = AsShared();
        UWorld* TestWorld = World.Get();
        ACastlePlayer* TestPlayer = Player.Get();
        if (!TestWorld) return;
        if (SpawnHandle.IsValid()) TestWorld->RemoveOnActorSpawnedHandler(SpawnHandle);
        for (FObservedGuard& Guard : Guards)
        {
            UE_LOG(LogTemp, Display, TEXT("CASTLE_FIX05_TEST GUARD_SUMMARY enemy=%s beforeRequests=%d beforePartialFinishes=%d allRequests=%d firstFull=%.3f firstRecovery=%.3f firingStop=%d pursueAfterFiring=%d occludedPursuit=%d lostSight=%d bolts=%d minBoltInterval=%.3f"),
                *GetNameSafe(Guard.Enemy.Get()), Guard.BeforeRequests, Guard.BeforePartialFinishes, Guard.ObservedRequests.Num(),
                Guard.FirstFullRoute, Guard.FirstRecovery, Guard.bSawFiringStop, Guard.bPursuedAfterFiring, Guard.bSawOccludedPursuit,
                Guard.bSawLostSight, Guard.Bolts, Guard.Bolts > 1 ? Guard.MinBoltInterval : -1.0);
            if (Guard.Following.IsValid()) Guard.Following->OnRequestFinished.Remove(Guard.FinishHandle);
            if (Guard.AI.IsValid()) { Guard.AI->UnPossess(); Guard.AI->Destroy(); }
            if (Guard.Enemy.IsValid()) Guard.Enemy->Destroy();
        }
        for (const TWeakObjectPtr<AStaticMeshActor>& Wall : Walls) if (Wall.IsValid()) Wall->Destroy();
        for (const FAmbientGuard& Restore : Ambient)
        {
            if (ACastleEnemy* Enemy = Restore.Enemy.Get())
            {
                Enemy->SetActorEnableCollision(Restore.bHadCollision);
                Enemy->bActive = Restore.bWasActive;
            }
        }
        UE_LOG(LogTemp, Display, TEXT("CASTLE_FIX05_TEST END t=%.3f completed=%d reason=%s playerDisplacement=%.3f shieldDelta=%.2f healthDelta=%.2f classUnchanged=%d"),
            Elapsed(), bCompleted, Reason, TestPlayer ? FVector::Dist2D(PlayerStart, TestPlayer->GetActorLocation()) : -1.0,
            TestPlayer ? TestPlayer->Shield - InitialShield : 0.f, TestPlayer ? TestPlayer->Health - InitialHealth : 0.f,
            Director.IsValid() && Director->OrdinaryEnemyClass.Get() == InitialClass.Get());
        TestWorld->GetTimerManager().ClearTimer(TickHandle);
    }

    double Elapsed() const { return World.IsValid() ? World->GetTimeSeconds() - Start : 0.0; }
    const TCHAR* ScenarioName() const { return Trial == ETrial::Reopen ? TEXT("Reopen") : Trial == ETrial::Move ? TEXT("Move") : TEXT("Combat"); }

    TWeakObjectPtr<UWorld> World;
    TWeakObjectPtr<ACastleDirector> Director;
    TWeakObjectPtr<ACastlePlayer> Player;
    TWeakObjectPtr<UClass> GuardClass;
    TWeakObjectPtr<UClass> InitialClass;
    TWeakObjectPtr<ARecastNavMesh> NavMesh;
    TArray<TWeakObjectPtr<AStaticMeshActor>> Walls;
    TArray<FAmbientGuard> Ambient;
    TArray<FObservedGuard> Guards;
    FTimerHandle TickHandle;
    FDelegateHandle SpawnHandle;
    ETrial Trial;
    int32 Count;
    double Start;
    float InitialHealth;
    float InitialShield;
    double NextSample = 0.0;
    double ResumeCombatAt = 0.0;
    int32 CombatWaypoint = 0;
    bool bSpawned = false;
    bool bOpened = false;
    bool bMovementThresholdLogged = false;
};

void RunPursuitFixture(const TArray<FString>& Arguments, UWorld* World)
{
    static TWeakPtr<FPursuitFixture> Running;
    // Reject editor worlds even when this command is run from a global console.
    if (!World || World->WorldType != EWorldType::PIE || !World->GetMapName().EndsWith(TEXT("L_CastleGruntArt_FIX01")))
    {
        UE_LOG(LogTemp, Warning, TEXT("CASTLE_FIX05_TEST REJECT expected an active PIE world for L_CastleGruntArt_FIX01"));
        return;
    }
    const auto Reject = [](const TCHAR* Reason) { UE_LOG(LogTemp, Warning, TEXT("CASTLE_FIX05_TEST REJECT %s"), Reason); };
    if (Running.IsValid()) { Reject(TEXT("fixture already running; use a fresh PIE for each scenario")); return; }
    if (Arguments.Num() < 2 || Arguments.Num() > 3) { Reject(TEXT("usage: CastlePursuitFix05.Run <Art|Grunt|Native> <Reopen|Move|Combat> [count 1-3]")); return; }

    UClass* Class = nullptr;
    if (Arguments[0].Equals(TEXT("Native"), ESearchCase::IgnoreCase)) Class = ACastleEnemy::StaticClass();
    else if (Arguments[0].Equals(TEXT("Art"), ESearchCase::IgnoreCase)) Class = LoadClass<ACastleEnemy>(nullptr,
        TEXT("/Game/Art/Grunt01/IntegrationV2/BP_CastleEnemy_Grunt_Art.BP_CastleEnemy_Grunt_Art_C"));
    else if (Arguments[0].Equals(TEXT("Grunt"), ESearchCase::IgnoreCase)) Class = LoadClass<ACastleEnemy>(nullptr,
        TEXT("/Game/CastleDemo/Enemies/Presentation/BP_CastleEnemy_Grunt.BP_CastleEnemy_Grunt_C"));
    if (!Class) { Reject(TEXT("enemy class could not be selected or loaded")); return; }

    ETrial Trial;
    if (Arguments[1].Equals(TEXT("Reopen"), ESearchCase::IgnoreCase)) Trial = ETrial::Reopen;
    else if (Arguments[1].Equals(TEXT("Move"), ESearchCase::IgnoreCase)) Trial = ETrial::Move;
    else if (Arguments[1].Equals(TEXT("Combat"), ESearchCase::IgnoreCase)) Trial = ETrial::Combat;
    else { Reject(TEXT("unknown scenario")); return; }
    const int32 Count = Arguments.Num() == 3 ? FCString::Atoi(*Arguments[2]) : 1;
    if (Count < 1 || Count > UE_ARRAY_COUNT(GuardStarts)) { Reject(TEXT("count must be 1–3")); return; }

    TActorIterator<ACastleDirector> Directors(World);
    ACastleDirector* Director = Directors ? *Directors : nullptr;
    APlayerController* Controller = World->GetFirstPlayerController();
    ACastlePlayer* Player = Controller ? Cast<ACastlePlayer>(Controller->GetPawn()) : nullptr;
    if (!Director || !Director->bReady || Director->RunSeed != 24680 || !Player || Player->bDead)
    {
        Reject(TEXT("expected ready Director on seed 24680 and a living player"));
        return;
    }
    const TSharedRef<FPursuitFixture> Fixture = MakeShared<FPursuitFixture>(*World, *Director, *Player, *Class, Trial, Count);
    Running = Fixture;
    Fixture->StartTrial();
}

FAutoConsoleCommandWithWorldAndArgs PursuitFixtureCommand(TEXT("CastlePursuitFix05.Run"),
    TEXT("Runs a development-only pursuit fixture in test-map PIE. Art|Grunt|Native Reopen|Move|Combat [1-3]."),
    FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunPursuitFixture));
}

#endif
