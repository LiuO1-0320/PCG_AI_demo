#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "../CastleDemo.h"
#include "../CastleOrdinaryEnemyMix.h"
#include "../CastleQueen.h"
#include "../CastleWeaponProjectile.h"
#include "../Melee/CastleMeleeCombatComponent.h"
#include "AIController.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "Camera/CameraComponent.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "CoreGlobals.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/DamageEvents.h"
#include "Engine/Engine.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "InputKeyEventArgs.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "NavigationPath.h"
#include "NavigationSystem.h"
#include "UObject/Package.h"
#include <limits>

namespace
{
UWorld* CreateMeleeTestWorld()
{
    const UWorld::InitializationValues Options = UWorld::InitializationValues()
        .CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false)
        .AllowAudioPlayback(false).CreateFXSystem(false).SetTransactional(false);
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Options);
    if (World)
    {
        GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    }
    return World;
}

void DestroyMeleeTestWorld(UWorld& World)
{
    World.DestroyWorld(false);
    GEngine->DestroyWorldContext(&World);
}

UClass* MakeTransientMeleeClass(FAutomationTestBase& Test)
{
    UPackage* Package = GetTransientPackage();
    const FName Name = MakeUniqueObjectName(Package, UBlueprint::StaticClass(), FName(TEXT("BP_CastleMeleeSelectionFixture")));
    UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprint(
        ACastleEnemy::StaticClass(), Package, Name, BPTYPE_Normal,
        UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass());
    UClass* Class = Blueprint ? Blueprint->GeneratedClass.Get() : nullptr;
    if (!Test.TestNotNull(TEXT("An unsaved Blueprint class was generated for the selection fixture"), Class))
    {
        return nullptr;
    }
    // A test class for selection only. It has no attack assets.
    Class->GetDefaultObject<ACastleEnemy>()->bUseMeleeV1 = true;
    return Class;
}

struct FSpawnRecord
{
    FTransform Transform;
    UClass* Class;
    bool bElite;
};

struct FItemRecord
{
    FTransform Transform;
    ECastleInteract Kind;
    int32 RewardKind;
};

struct FGenerationCapture
{
    TArray<FSpawnRecord> Exploration;
    TArray<FSpawnRecord> Vault;
    TArray<FItemRecord> Items;
    TArray<FCastleRoom> Rooms;
    TArray<FCastleRoom> CaveRooms;
    TArray<int32> Route;
    TArray<int32> CaveRoute;
};

void CaptureSpawns(const ACastleDirector& Director, int32 First, TArray<FSpawnRecord>& Spawns)
{
    for (int32 Index = First; Index < Director.GeneratedActors.Num(); ++Index)
    {
        const ACastleEnemy* Enemy = Cast<ACastleEnemy>(Director.GeneratedActors[Index].Get());
        if (IsValid(Enemy))
        {
            Spawns.Add({Enemy->GetActorTransform(), Enemy->GetClass(), Enemy->bElite});
        }
    }
}

bool CaptureGeneratedFloor(FAutomationTestBase& Test, UClass* MeleeClass, float Chance,
    FGenerationCapture& Capture, bool bRejectOneSpawn = false)
{
    UWorld* World = CreateMeleeTestWorld();
    if (!Test.TestNotNull(TEXT("Transient game world created"), World))
    {
        return false;
    }
    ON_SCOPE_EXIT { DestroyMeleeTestWorld(*World); };

    ACastleDirector* Director = World->SpawnActor<ACastleDirector>();
    if (!Test.TestNotNull(TEXT("Native Director spawned"), Director))
    {
        return false;
    }
    Director->RunSeed = 24680;
    Director->Floor = 2;
    Director->OrdinaryMeleeEnemyClass = MeleeClass;
    Director->OrdinaryMeleeChance = Chance;

    bool bRejected = false;
    const FDelegateHandle Handler = bRejectOneSpawn
        ? World->AddOnActorSpawnedHandler(FOnActorSpawned::FDelegate::CreateLambda(
            [MeleeClass, &bRejected](AActor* Actor)
            {
                if (!bRejected && Actor->GetClass() == MeleeClass)
                {
                    bRejected = Actor->Destroy();
                }
            }))
        : FDelegateHandle();
    ON_SCOPE_EXIT { if (Handler.IsValid()) World->RemoveOnActorSpawnedHandler(Handler); };

    Director->GenerateLayout(nullptr);
    CaptureSpawns(*Director, 0, Capture.Exploration);
    const int32 BeforeVault = Director->GeneratedActors.Num();
    Director->GenerateCave(nullptr);
    CaptureSpawns(*Director, BeforeVault, Capture.Vault);

    for (const TObjectPtr<AActor>& Actor : Director->GeneratedActors)
    {
        const ACastleInteractable* Item = Cast<ACastleInteractable>(Actor.Get());
        if (IsValid(Item))
        {
            Capture.Items.Add({Item->GetActorTransform(), Item->Kind, Item->RewardKind});
        }
    }
    Capture.Rooms = Director->Rooms;
    Capture.CaveRooms = Director->CaveRooms;
    Capture.Route = Director->FirstFloorRoute;
    Capture.CaveRoute = Director->CaveRoute;
    return !bRejectOneSpawn || Test.TestTrue(TEXT("The spawn-failure probe rejected a real selected actor"), bRejected);
}

void CompareSpawnPositions(FAutomationTestBase& Test, const TCHAR* Region,
    const TArray<FSpawnRecord>& Baseline, const TArray<FSpawnRecord>& Other)
{
    if (!Test.TestEqual(FString::Printf(TEXT("%s spawn count"), Region), Other.Num(), Baseline.Num()))
    {
        return;
    }
    for (int32 Index = 0; Index < Baseline.Num(); ++Index)
    {
        Test.TestTrue(FString::Printf(TEXT("%s spawn transform and role %d"), Region, Index),
            Baseline[Index].Transform.Equals(Other[Index].Transform, 0.f)
            && Baseline[Index].bElite == Other[Index].bElite);
    }
}

void CompareRoomLayout(FAutomationTestBase& Test, const TCHAR* Region,
    const TArray<FCastleRoom>& Baseline, const TArray<FCastleRoom>& Other)
{
    if (!Test.TestEqual(FString::Printf(TEXT("%s room count"), Region), Other.Num(), Baseline.Num()))
    {
        return;
    }
    for (int32 Index = 0; Index < Baseline.Num(); ++Index)
    {
        const FCastleRoom& A = Baseline[Index];
        const FCastleRoom& B = Other[Index];
        Test.TestTrue(FString::Printf(TEXT("%s room layout %d"), Region, Index),
            A.Center == B.Center && A.Type == B.Type && A.bActive == B.bActive && A.Doors == B.Doors);
    }
}

void CompareGeneration(FAutomationTestBase& Test, const FGenerationCapture& Baseline, const FGenerationCapture& Other)
{
    CompareSpawnPositions(Test, TEXT("Exploration"), Baseline.Exploration, Other.Exploration);
    CompareSpawnPositions(Test, TEXT("Vault"), Baseline.Vault, Other.Vault);
    CompareRoomLayout(Test, TEXT("Exploration"), Baseline.Rooms, Other.Rooms);
    CompareRoomLayout(Test, TEXT("Vault"), Baseline.CaveRooms, Other.CaveRooms);
    Test.TestTrue(TEXT("Exploration and vault routes are unchanged"),
        Baseline.Route == Other.Route && Baseline.CaveRoute == Other.CaveRoute);
    if (Test.TestEqual(TEXT("Generated interactable count"), Other.Items.Num(), Baseline.Items.Num()))
    {
        for (int32 Index = 0; Index < Baseline.Items.Num(); ++Index)
        {
            const FItemRecord& A = Baseline.Items[Index];
            const FItemRecord& B = Other.Items[Index];
            Test.TestTrue(FString::Printf(TEXT("Interactable position, kind and reward %d"), Index),
                A.Transform.Equals(B.Transform, 0.f) && A.Kind == B.Kind && A.RewardKind == B.RewardKind);
        }
    }
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleMeleeSelectionTest, "CastleDemo.MeleeV1.Selection",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCastleMeleeSelectionTest::RunTest(const FString& Parameters)
{
    using EChoice = ECastleOrdinaryEnemyMixChoice;
    UClass* Melee = MakeTransientMeleeClass(*this);
    if (!Melee)
    {
        return false;
    }
    const FCastleOrdinaryEnemyMixSlot Slot{24680, 2, ECastlePCGRegion::Exploration, 4, 0};
    TestTrue(TEXT("Zero keeps the original route even with an ineligible configured class"),
        CastleOrdinaryEnemyMix::Select(0.f, ACastleQueen::StaticClass(), Slot) == EChoice::Original);
    TestTrue(TEXT("Positive chance without a class is diagnosable"),
        CastleOrdinaryEnemyMix::Select(1.f, nullptr, Slot) == EChoice::MissingMeleeClass);
    TestTrue(TEXT("An explicit non-opt-in guard is rejected"),
        CastleOrdinaryEnemyMix::Select(1.f, ACastleEnemy::StaticClass(), Slot) == EChoice::InvalidMeleeClass);
    TestTrue(TEXT("The existing Queen is rejected even though it derives from a guard"),
        CastleOrdinaryEnemyMix::Select(1.f, ACastleQueen::StaticClass(), Slot) == EChoice::InvalidMeleeClass);
    TestTrue(TEXT("Out-of-range and nonfinite chances are rejected"),
        CastleOrdinaryEnemyMix::Select(-0.1f, Melee, Slot) == EChoice::InvalidChance
        && CastleOrdinaryEnemyMix::Select(1.01f, Melee, Slot) == EChoice::InvalidChance
        && CastleOrdinaryEnemyMix::Select(std::numeric_limits<float>::quiet_NaN(), Melee, Slot) == EChoice::InvalidChance);
    TestTrue(TEXT("Boss arena is not an ordinary mix slot"),
        CastleOrdinaryEnemyMix::Select(1.f, Melee, {24680, 2, ECastlePCGRegion::BossArena, 4, 0}) == EChoice::InvalidSlot);
    TestTrue(TEXT("A chance of one always chooses the eligible melee class"),
        CastleOrdinaryEnemyMix::Select(1.f, Melee, Slot) == EChoice::Melee);

    int32 MeleeCount = 0;
    int32 OriginalCount = 0;
    constexpr float Chance = 1.f / 3.f;
    for (int32 Index = 0; Index < 64; ++Index)
    {
        const FCastleOrdinaryEnemyMixSlot Current{24680 + Index / 8, 2,
            Index % 2 ? ECastlePCGRegion::TreasureCave : ECastlePCGRegion::Exploration, Index % 8, Index % 4};
        const double Sample = CastleOrdinaryEnemyMix::GetSlotSample(Current);
        const EChoice Selected = CastleOrdinaryEnemyMix::Select(Chance, Melee, Current);
        TestTrue(TEXT("A slot sample is finite in [0, 1)"), FMath::IsFinite(Sample) && Sample >= 0.0 && Sample < 1.0);
        TestTrue(TEXT("A mixed selection matches the slot threshold"),
            Selected == (Sample < static_cast<double>(Chance) ? EChoice::Melee : EChoice::Original));
        CastleOrdinaryEnemyMix::GetSlotSample(Slot);
        TestTrue(TEXT("Interleaving other samples cannot disturb a slot"),
            Sample == CastleOrdinaryEnemyMix::GetSlotSample(Current)
            && Selected == CastleOrdinaryEnemyMix::Select(Chance, Melee, Current));
        MeleeCount += Selected == EChoice::Melee;
        OriginalCount += Selected == EChoice::Original;
    }
    TestTrue(TEXT("The fixed sample cohort contains each ordinary type"), MeleeCount > 0 && OriginalCount > 0);
    AddInfo(FString::Printf(TEXT("SelectionVersion=%d ordinary=%d melee=%d"),
        CastleOrdinaryEnemyMix::SelectionVersion, OriginalCount, MeleeCount));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleMeleeDirectorRoutesTest, "CastleDemo.MeleeV1.DirectorRoutes",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCastleMeleeDirectorRoutesTest::RunTest(const FString& Parameters)
{
    UClass* Melee = MakeTransientMeleeClass(*this);
    if (!Melee)
    {
        return false;
    }
    FGenerationCapture Baseline, AllMelee, Mixed, Repeat;
    if (!CaptureGeneratedFloor(*this, nullptr, 0.f, Baseline)
        || !CaptureGeneratedFloor(*this, Melee, 1.f, AllMelee)
        || !CaptureGeneratedFloor(*this, Melee, 0.5f, Mixed)
        || !CaptureGeneratedFloor(*this, Melee, 0.5f, Repeat))
    {
        return false;
    }
    CompareGeneration(*this, Baseline, AllMelee);
    CompareGeneration(*this, Baseline, Mixed);
    CompareGeneration(*this, Mixed, Repeat);
    int32 Elites = 0;
    const auto CheckClasses = [this, Melee, &Elites](const TCHAR* RegionName,
        const TArray<FSpawnRecord>& Full, const TArray<FSpawnRecord>& Mix, const TArray<FSpawnRecord>& Replay)
    {
        for (const FSpawnRecord& Record : Full)
        {
            TestEqual(TEXT("All ordinary slots honor the melee override and elites remain native"),
                Record.Class, Record.bElite ? ACastleEnemy::StaticClass() : Melee);
            Elites += Record.bElite;
        }
        if (Mix.Num() != Replay.Num())
        {
            return;
        }
        int32 MixedMelee = 0;
        int32 MixedRanged = 0;
        for (int32 Index = 0; Index < Mix.Num(); ++Index)
        {
            const FSpawnRecord& Record = Mix[Index];
            TestEqual(TEXT("Same seed repeats assigned class in the original slot order"), Record.Class, Replay[Index].Class);
            TestTrue(TEXT("The only mixed choices are the two admitted ordinary types"),
                Record.Class == ACastleEnemy::StaticClass() || (!Record.bElite && Record.Class == Melee));
            MixedMelee += !Record.bElite && Record.Class == Melee;
            MixedRanged += !Record.bElite && Record.Class == ACastleEnemy::StaticClass();
        }
        TestTrue(FString::Printf(TEXT("%s spawns both ordinary types at seed 24680"), RegionName),
            MixedMelee > 0 && MixedRanged > 0);
        AddInfo(FString::Printf(TEXT("%s mix seed=24680 floor=2 ranged=%d melee=%d"),
            RegionName, MixedRanged, MixedMelee));
    };
    CheckClasses(TEXT("Exploration"), AllMelee.Exploration, Mixed.Exploration, Repeat.Exploration);
    CheckClasses(TEXT("Vault"), AllMelee.Vault, Mixed.Vault, Repeat.Vault);
    TestTrue(TEXT("The fixture has exploration combat and an elite control"), Baseline.Exploration.Num() > Elites && Elites > 0);
    TestEqual(TEXT("The current floor-two vault keeps five actual ordinary slots"), Baseline.Vault.Num(), 5);
    AddInfo(FString::Printf(TEXT("Director routes seed=24680 floor=2 exploration=%d vault=%d elites=%d"),
        Baseline.Exploration.Num(), Baseline.Vault.Num(), Elites));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleMeleeSpawnFailureTest, "CastleDemo.MeleeV1.SpawnFailure",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCastleMeleeSpawnFailureTest::RunTest(const FString& Parameters)
{
    UClass* Melee = MakeTransientMeleeClass(*this);
    if (!Melee)
    {
        return false;
    }
    FGenerationCapture Baseline, Failed;
    if (!CaptureGeneratedFloor(*this, Melee, 1.f, Baseline))
    {
        return false;
    }
    AddExpectedError(TEXT("CASTLE_ENEMY_SPAWN_FAILED"), EAutomationExpectedErrorFlags::Contains, 1, false);
    AddExpectedError(TEXT("CASTLE_ORDINARY_MELEE_SPAWN_FAILED"), EAutomationExpectedErrorFlags::Contains, 1, false);
    if (!CaptureGeneratedFloor(*this, Melee, 1.f, Failed, true))
    {
        return false;
    }
    const int32 RemovedIndex = Baseline.Exploration.IndexOfByPredicate(
        [Melee](const FSpawnRecord& Record) { return Record.Class == Melee; });
    if (!TestTrue(TEXT("The first route has an eligible melee slot to remove"), RemovedIndex != INDEX_NONE))
    {
        return false;
    }
    Baseline.Exploration.RemoveAt(RemovedIndex);
    CompareGeneration(*this, Baseline, Failed);
    for (const FSpawnRecord& Record : Failed.Exploration)
    {
        TestTrue(TEXT("A failed spawn does not insert another ranged guard"), Record.bElite || Record.Class == Melee);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleMeleeFailClosedTest, "CastleDemo.MeleeV1.FailClosed",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCastleMeleeFailClosedTest::RunTest(const FString& Parameters)
{
    UWorld* World = CreateMeleeTestWorld();
    if (!TestNotNull(TEXT("Transient attack fixture world created"), World))
    {
        return false;
    }
    ON_SCOPE_EXIT { DestroyMeleeTestWorld(*World); };

    FActorSpawnParameters Spawn;
    Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    ACastleDirector* Director = World->SpawnActor<ACastleDirector>();
    ACastleEnemy* Enemy = World->SpawnActor<ACastleEnemy>(FVector(0, 0, 100), FRotator::ZeroRotator, Spawn);
    ACastlePlayer* Player = World->SpawnActor<ACastlePlayer>(FVector(110, 0, 100), FRotator::ZeroRotator, Spawn);
    AAIController* AI = World->SpawnActor<AAIController>();
    ACastlePlayerController* PC = World->SpawnActor<ACastlePlayerController>();
    if (!TestNotNull(TEXT("Attack Director"), Director) || !TestNotNull(TEXT("Attack owner"), Enemy)
        || !TestNotNull(TEXT("Attack target"), Player) || !TestNotNull(TEXT("Attack owner controller"), AI)
        || !TestNotNull(TEXT("Attack target controller"), PC))
    {
        return false;
    }
    AI->Possess(Enemy);
    PC->Possess(Player);
    Director->bReady = true;
    Enemy->Configure(false, 1, Director);
    Enemy->bUseMeleeV1 = true;
    if (!TestTrue(TEXT("Both bodies are possessed and the legacy operational gate is open"),
        AI->GetPawn() == Enemy && PC->GetPawn() == Player && Enemy->CanRunAI(Player)))
    {
        return false;
    }

    const float OriginalShield = Player->Shield;
    const float OriginalHealth = Player->Health;
    Enemy->UpdateAttack(Player, true); // Opt-in, missing component, in contact range.
    auto* Melee = NewObject<UCastleMeleeCombatComponent>(Enemy);
    Enemy->AddInstanceComponent(Melee);
    Melee->RegisterComponent();
    AddExpectedMessage(TEXT("unusable StrikeMontage or CastleMeleeHitWindow"), ELogVerbosity::Warning,
        EAutomationExpectedMessageFlags::Contains, 1, false);
    Enemy->UpdateAttack(Player, true); // No montage.
    Melee->StrikeMontage = NewObject<UAnimMontage>(Melee); // An asset pointer alone is insufficient.
    TestFalse(TEXT("An empty Montage cannot start detection"), Melee->TryStart(Player, true));
    Melee->Advance(Player, 1.f);
    Melee->Cancel();
    Melee->Cancel();
    TestEqual(TEXT("An invalid strike never left idle"), static_cast<uint8>(Melee->GetPhase()),
        static_cast<uint8>(UCastleMeleeCombatComponent::EPhase::Idle));
    TestEqual(TEXT("No accepted attack was started"), Melee->GetStartedCount(), 0);
    TestEqual(TEXT("No window began"), Melee->GetWindowCount(), 0);
    TestEqual(TEXT("No hit was committed"), Melee->GetHitCount(), 0);
    TestFalse(TEXT("An invalid strike does not leave movement held"), Melee->ShouldHoldMovement());
    TestEqual(TEXT("A failed preparation cannot send old contact damage to shield"), Player->Shield, OriginalShield);
    TestEqual(TEXT("A failed preparation cannot send old contact damage to health"), Player->Health, OriginalHealth);
    Player->SetActorLocation(FVector(400, 0, 100));
    Enemy->UpdateAttack(Player, true);
    TestFalse(TEXT("An opted-in enemy cannot fall back to spawning a bolt"), bool(TActorIterator<ACastleBolt>(World)));
    AI->UnPossess();

    // Positive control: a legacy guard in the same world can damage this exact player.
    ACastleEnemy* Legacy = World->SpawnActor<ACastleEnemy>(FVector(290, 0, 100), FRotator::ZeroRotator, Spawn);
    if (!TestNotNull(TEXT("Legacy damage control spawned"), Legacy))
    {
        return false;
    }
    Legacy->Configure(false, 1, Director);
    Legacy->UpdateAttack(Player, true);
    TestTrue(TEXT("Legacy close-range damage is still live in this fixture"), Player->Shield < OriginalShield);
    return true;
}

namespace
{
const TCHAR* CastleMeleeCohortMap = TEXT("/Game/Art/Grunt01/MeleeV1/Tests/L_CastleMelee_V1_Dev");
const TCHAR* CastleMeleeCohortClass = TEXT("/Game/Art/Grunt01/MeleeV1/BP_CastleEnemy_Melee.BP_CastleEnemy_Melee_C");

struct FCastleMeleeCohortRun
{
    TWeakObjectPtr<UWorld> OwnWorld;
    TArray<TWeakObjectPtr<ACastleEnemy>> Guards;
    FDelegateHandle WorldCleanup;

    void Forget()
    {
        if (WorldCleanup.IsValid())
        {
            FWorldDelegates::OnWorldCleanup.Remove(WorldCleanup);
            WorldCleanup.Reset();
        }
        OwnWorld.Reset();
        Guards.Reset();
    }

    void Stop()
    {
        UWorld* RunWorld = OwnWorld.Get();
        TArray<TWeakObjectPtr<ACastleEnemy>> Owned = MoveTemp(Guards);
        Forget();
        if (!RunWorld)
        {
            return; // PIE already removed the entire test world.
        }

        TArray<TWeakObjectPtr<ACastleBolt>> OwnBolts;
        for (TActorIterator<ACastleBolt> Bolt(RunWorld); Bolt; ++Bolt)
        {
            const ACastleEnemy* BoltOwner = Cast<ACastleEnemy>(Bolt->GetOwner());
            if (BoltOwner && Owned.ContainsByPredicate([BoltOwner](const auto& Guard) { return Guard.Get() == BoltOwner; }))
            {
                OwnBolts.Add(*Bolt);
            }
        }
        for (const TWeakObjectPtr<ACastleBolt>& Bolt : OwnBolts)
        {
            if (Bolt.IsValid()) Bolt->Destroy();
        }
        for (const TWeakObjectPtr<ACastleEnemy>& Guard : Owned)
        {
            if (ACastleEnemy* Spawned = Guard.Get())
            {
                if (AAIController* AI = Cast<AAIController>(Spawned->GetController()))
                {
                    AI->StopMovement();
                    AI->UnPossess();
                    AI->Destroy();
                }
                Spawned->Destroy();
            }
        }
    }
};

FCastleMeleeCohortRun& CastleMeleeCohortRun()
{
    static FCastleMeleeCohortRun Run;
    return Run;
}

const UCastleMeleeCombatComponent* FindCastleMeleeCohortTemplate(UClass& Class)
{
    auto* Generated = Cast<UBlueprintGeneratedClass>(&Class);
    const UCastleMeleeCombatComponent* Result = nullptr;
    for (auto* Base = Generated; Base; Base = Cast<UBlueprintGeneratedClass>(Base->GetSuperClass()))
    {
        if (!Base->SimpleConstructionScript) continue;
        for (const USCS_Node* Node : Base->SimpleConstructionScript->GetAllNodes())
        {
            const auto* Candidate = Node ? Cast<UCastleMeleeCombatComponent>(Node->GetActualComponentTemplate(Generated)) : nullptr;
            if (Candidate)
            {
                if (Result) return nullptr;
                Result = Candidate;
            }
        }
    }
    return Result;
}

void CastleMeleeRunCohort(const TArray<FString>& Args, UWorld* World)
{
    const auto Reject = [](const TCHAR* Reason)
    {
        UE_LOG(LogTemp, Warning, TEXT("CASTLE_MELEE_COHORT_REJECT %s"), Reason);
    };
    if (!World || World->WorldType != EWorldType::PIE || World->GetNetMode() != NM_Standalone ||
        UWorld::RemovePIEPrefix(World->GetOutermost()->GetName()) != CastleMeleeCohortMap)
    {
        Reject(TEXT("Expected standalone PIE on the exact CastleMeleeV1 acceptance map."));
        return;
    }
    if (Args.Num() != 1)
    {
        Reject(TEXT("Usage: CastleMeleeV1.Cohort Ranged6|Mixed42|Single|Pair|Stop"));
        return;
    }

    FCastleMeleeCohortRun& Run = CastleMeleeCohortRun();
    if (Args[0].Equals(TEXT("Stop"), ESearchCase::IgnoreCase))
    {
        if (Run.OwnWorld.Get() == World)
        {
            Run.Stop();
            UE_LOG(LogTemp, Display, TEXT("CASTLE_MELEE_COHORT_STOP: restart PIE before measuring another case; gameplay rewards and proxies retain their original lifecycle."));
        }
        return;
    }
    const bool bMixed = Args[0].Equals(TEXT("Mixed42"), ESearchCase::IgnoreCase);
    const bool bRanged = Args[0].Equals(TEXT("Ranged6"), ESearchCase::IgnoreCase);
    const bool bSingle = Args[0].Equals(TEXT("Single"), ESearchCase::IgnoreCase);
    const bool bPair = Args[0].Equals(TEXT("Pair"), ESearchCase::IgnoreCase);
    if (!bMixed && !bRanged && !bSingle && !bPair)
    {
        Reject(TEXT("Unknown cohort. Use Ranged6, Mixed42, Single or Pair."));
        return;
    }
    if (Run.OwnWorld.IsValid())
    {
        Reject(TEXT("Stop the previous cohort or restart PIE before selecting another."));
        return;
    }

    TActorIterator<ACastleDirector> Directors(World);
    ACastleDirector* Director = Directors ? *Directors : nullptr;
    if (Director) ++Directors;
    APlayerController* PC = World->GetFirstPlayerController();
    ACastlePlayer* Player = PC ? Cast<ACastlePlayer>(PC->GetPawn()) : nullptr;
    if (!Director || Directors || !Director->bReady || Director->bWon || !Player || Player->bDead ||
        Director->Region != TEXT("EXPLORATION") || !Director->Rooms.IsValidIndex(Director->CurrentRoomIndex))
    {
        Reject(TEXT("Expected one ready Director and a living player in an exploration room."));
        return;
    }

    UClass* RangedClass = Director->OrdinaryEnemyClass.Get();
    UClass* MeleeClass = Director->OrdinaryMeleeEnemyClass.Get();
    const ACastleEnemy* RangedDefaults = RangedClass ? RangedClass->GetDefaultObject<ACastleEnemy>() : nullptr;
    const ACastleEnemy* MeleeDefaults = MeleeClass ? MeleeClass->GetDefaultObject<ACastleEnemy>() : nullptr;
    const auto* Template = MeleeClass ? FindCastleMeleeCohortTemplate(*MeleeClass) : nullptr;
    constexpr EClassFlags RejectedFlags = CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists;
    if (!RangedDefaults || RangedDefaults->bUseMeleeV1 || RangedDefaults->bElite || RangedDefaults->bBoss ||
        RangedClass->IsChildOf(ACastleQueen::StaticClass()) || RangedClass->HasAnyClassFlags(RejectedFlags) ||
        !MeleeDefaults || !MeleeDefaults->bUseMeleeV1 || MeleeDefaults->bElite || MeleeDefaults->bBoss ||
        MeleeClass->GetPathName() != CastleMeleeCohortClass || MeleeClass->HasAnyClassFlags(RejectedFlags) ||
        !Template || !IsValid(Template->StrikeMontage.Get()))
    {
        Reject(TEXT("The Director needs a valid ranged class and the saved melee Blueprint with one strike Montage."));
        return;
    }

    const FCastleRoom& Room = Director->Rooms[Director->CurrentRoomIndex];
    if (!Room.bActive || FVector::Dist2D(Room.Center, Player->GetActorLocation()) > 600.f)
    {
        Reject(TEXT("Stand within 600 cm of the intended room center before starting either case."));
        return;
    }
    int32 AmbientCount = 0;
    for (TActorIterator<ACastleEnemy> Enemy(World); Enemy; ++Enemy)
    {
        if (Enemy->bActive && !Enemy->bDead)
        {
            ++AmbientCount;
            if (Enemy->CanRunAI(Player) && Enemy->IsInDecisionRange(Player))
            {
                Reject(TEXT("A live ambient enemy can engage this player. Choose a clear room; ambient actors remain unchanged."));
                return;
            }
        }
    }

    UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
    if (!Nav || UNavigationSystemV1::IsNavigationBeingBuiltOrLocked(World))
    {
        Reject(TEXT("Navigation is unavailable or rebuilding."));
        return;
    }
    const float Radius = FMath::Max(RangedDefaults->GetCapsuleComponent()->GetScaledCapsuleRadius(),
        MeleeDefaults->GetCapsuleComponent()->GetScaledCapsuleRadius());
    const float HalfHeight = FMath::Max(RangedDefaults->GetCapsuleComponent()->GetScaledCapsuleHalfHeight(),
        MeleeDefaults->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
    // Same room, floor, seed and slot list for either six-enemy case; player input is not overridden.
    const FVector Offsets[] = {FVector(350, -180, 0), FVector(350, 180, 0), FVector(585, -340, 0),
        FVector(585, -100, 0), FVector(585, 100, 0), FVector(585, 340, 0)};
    const int32 Count = bSingle ? 1 : bPair ? 2 : UE_ARRAY_COUNT(Offsets);
    TArray<FVector> Positions;
    for (int32 Index = 0; Index < Count; ++Index)
    {
        const FVector Desired = Room.Center + Offsets[Index];
        FNavLocation NavSlot;
        if (!Nav->ProjectPointToNavigation(Desired, NavSlot, FVector(40, 40, 175)) ||
            FVector::Dist2D(NavSlot.Location, Desired) > 30.f)
        {
            Reject(TEXT("A fixed slot has no nearby navigation support. Use another clear room."));
            return;
        }
        const auto* Path = UNavigationSystemV1::FindPathToLocationSynchronously(World, NavSlot.Location, Player->GetActorLocation(), Player);
        const FVector At = NavSlot.Location + FVector(0, 0, HalfHeight + 4.f);
        if (!Path || !Path->IsValid() || Path->IsPartial() ||
            World->OverlapBlockingTestByChannel(At, FQuat::Identity, ECC_Pawn, FCollisionShape::MakeCapsule(Radius, HalfHeight)) ||
            Positions.ContainsByPredicate([&](const FVector& Position) { return FVector::Dist2D(Position, At) <= Radius * 2.f; }))
        {
            Reject(TEXT("A fixed slot lacks a full path or its capsule is occupied."));
            return;
        }
        Positions.Add(At);
    }

    Run.OwnWorld = World;
    // Full PIE teardown clears the original rewards and death proxies as usual.
    // This delegate only releases probe ownership so the next PIE can run.
    Run.WorldCleanup = FWorldDelegates::OnWorldCleanup.AddLambda(
        [World](UWorld* CleaningWorld, bool, bool)
        {
            if (CleaningWorld == World)
            {
                CastleMeleeCohortRun().Forget();
            }
        });
    for (int32 Index = 0; Index < Count; ++Index)
    {
        const bool bMeleeSlot = bSingle || bPair || (bMixed && Index < 2);
        UClass* SpawnClass = bMeleeSlot ? MeleeClass : RangedClass;
        FActorSpawnParameters Spawn;
        Spawn.ObjectFlags |= RF_Transient;
        Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::DontSpawnIfColliding;
        ACastleEnemy* Guard = World->SpawnActor<ACastleEnemy>(SpawnClass, Positions[Index], FRotator(0, 180, 0), Spawn);
        if (IsValid(Guard))
        {
            Run.Guards.Add(Guard);
            Guard->Configure(false, Director->Floor, Director);
        }
        if (!IsValid(Guard) || !Guard->GetController() || Guard->GetClass() != SpawnClass ||
            (bMeleeSlot && !IsValid(Guard->FindComponentByClass<UCastleMeleeCombatComponent>())))
        {
            Run.Stop();
            Reject(TEXT("Could not create the exact fixed cohort. Partial fixture cleaned up."));
            return;
        }
        UE_LOG(LogTemp, Display, TEXT("CASTLE_MELEE_COHORT_SLOT Slot=%d Class=%s Position=%s"),
            Index, *Guard->GetClass()->GetPathName(), *Guard->GetActorLocation().ToCompactString());
    }
    UE_LOG(LogTemp, Display, TEXT("CASTLE_MELEE_COHORT_READY Mode=%s Seed=%d Floor=%d Room=%d Count=%d Ambient=%d Player=%s"),
        *Args[0], Director->RunSeed, Director->Floor, Director->CurrentRoomIndex, Count, AmbientCount,
        *Player->GetActorLocation().ToCompactString());
}

FAutoConsoleCommandWithWorldAndArgs CastleMeleeCohortCommand(TEXT("CastleMeleeV1.Cohort"),
    TEXT("Fixed combat cohort for the CastleMeleeV1 test map. Ranged6|Mixed42|Single|Pair|Stop. Start fresh PIE for each measurement."),
    FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&CastleMeleeRunCohort));
}

namespace
{
enum class ECastleMeleeInputTrial { Cancel, Death, Hitch, Blocked, Outside, HitchBlocked, HitchOutside, ActiveDeath, Sustain, Recovery };
enum class ECastleMeleeInputStep { Aim, Pressed, Released, FinalObservation };

struct FCastleMeleeInputTrial
{
    TWeakObjectPtr<UWorld> TrialWorld;
    TWeakObjectPtr<ACastleDirector> Director;
    TWeakObjectPtr<ACastlePlayer> Player;
    TWeakObjectPtr<APlayerController> Controller;
    TWeakObjectPtr<ACastleEnemy> Enemy;
    TWeakObjectPtr<UCastleMeleeCombatComponent> Melee;
    TWeakObjectPtr<AActor> TestObstacle;
    TWeakObjectPtr<AController> DyingGuardController;
    TWeakObjectPtr<AActor> ActiveDeathProxy;
    FDelegateHandle TickHandle;
    FDelegateHandle SpawnHandle;
    FDelegateHandle CleanupHandle;
    FRotator OriginalAim;
    FVector InitialPlayerPosition;
    FVector WindupPlayerPosition;
    FVector WindupDirection;
    FString InitialRegion;
    double StartTime = 0.0;
    double StepTime = 0.0;
    double NextInputTime = 0.0;
    double OriginalWindowDeadline = 0.0;
    int32 InitialSeed = 0;
    int32 InitialFloor = 0;
    int32 Rounds = 0;
    int32 ShotRoundsBefore = 0;
    int32 AmmoBefore = 0;
    int32 KillsBefore = 0;
    int32 CanceledBefore = 0;
    int32 MeleeHitsBefore = 0;
    int32 NotifySamplesBefore = 0;
    int32 NotifyHitsBefore = 0;
    int32 SameFrameWindowsBefore = 0;
    int32 WindowsBefore = 0;
    int32 TickSamplesBefore = 0;
    int32 SweepsBefore = 0;
    int32 OutsideBefore = 0;
    int32 BlockedBefore = 0;
    int32 MissedBefore = 0;
    int32 CompletedBefore = 0;
    int32 ActiveDeathProxyCount = 0;
    float TargetHealthBefore = 0.f;
    float PlayerEffectiveHealthBefore = 0.f;
    uint64 PressFrame = 0;
    uint64 ReleaseFrame = 0;
    uint64 ExpectedAttackId = 0;
    uint64 AttackIdAtShot = 0;
    bool bOwnPressedKey = false;
    bool bLethalShot = false;
    bool bStallIssued = false;
    bool bActiveDeathInjection = false;
    UCastleMeleeCombatComponent::EPhase PhaseAtShot = UCastleMeleeCombatComponent::EPhase::Idle;
    ECastleMeleeInputTrial Trial = ECastleMeleeInputTrial::Cancel;
    ECastleMeleeInputStep Step = ECastleMeleeInputStep::Aim;

    bool IsGeometryTrial() const
    {
        return Trial == ECastleMeleeInputTrial::Blocked || Trial == ECastleMeleeInputTrial::HitchBlocked ||
            Trial == ECastleMeleeInputTrial::ActiveDeath || Trial == ECastleMeleeInputTrial::Recovery || IsOutsideTrial();
    }

    bool IsOutsideTrial() const
    {
        return Trial == ECastleMeleeInputTrial::Outside || Trial == ECastleMeleeInputTrial::HitchOutside;
    }

    bool IsGeometryHitchTrial() const
    {
        return Trial == ECastleMeleeInputTrial::HitchBlocked || Trial == ECastleMeleeInputTrial::HitchOutside;
    }

    int32 ExpectedCarbineRounds() const
    {
        return Trial == ECastleMeleeInputTrial::Sustain ? 2 : 1;
    }

    FKey GetTrialKey() const
    {
        return IsOutsideTrial() ? EKeys::S : EKeys::LeftMouseButton;
    }

    float GetPlayerEffectiveHealth() const
    {
        const ACastlePlayer* Pawn = Player.Get();
        return Pawn ? Pawn->Health + Pawn->Shield : 0.f;
    }

    void ReleaseKey()
    {
        if (!bOwnPressedKey) return;
        bOwnPressedKey = false;
        ReleaseFrame = GFrameCounter;
        if (APlayerController* PC = Controller.Get())
        {
            const FKey Key = GetTrialKey();
            const bool bRouted = PC->InputKey(FInputKeyEventArgs::CreateSimulated(Key, IE_Released, 0.f));
            UE_LOG(LogTemp, Display, TEXT("CASTLE_MELEE_INPUT_RELEASE Frame=%llu Key=%s Routed=%d"), ReleaseFrame, *Key.ToString(), bRouted);
        }
    }

    void Finish(bool bPass, const TCHAR* Reason, bool bWorldEnding = false)
    {
        const TCHAR* TrialName = Trial == ECastleMeleeInputTrial::Cancel ? TEXT("Cancel")
            : Trial == ECastleMeleeInputTrial::Death ? TEXT("Death")
            : Trial == ECastleMeleeInputTrial::Hitch ? TEXT("Hitch")
            : Trial == ECastleMeleeInputTrial::Blocked ? TEXT("Blocked")
            : Trial == ECastleMeleeInputTrial::HitchBlocked ? TEXT("HitchBlocked")
            : Trial == ECastleMeleeInputTrial::HitchOutside ? TEXT("HitchOutside")
            : Trial == ECastleMeleeInputTrial::ActiveDeath ? TEXT("ActiveDeath")
            : Trial == ECastleMeleeInputTrial::Sustain ? TEXT("Sustain")
            : Trial == ECastleMeleeInputTrial::Recovery ? TEXT("Recovery") : TEXT("Outside");
        const UCastleMeleeCombatComponent* Component = Melee.Get();
        const ACastlePlayer* Pawn = Player.Get();
        UE_LOG(LogTemp, Display, TEXT("CASTLE_MELEE_INPUT_%s Mode=%s Reason=%s Frame=%llu Rounds=%d Ammo=%d Attack=%llu EffectiveHP=%.1f MeleeHits=%d NotifySamples=%d NotifyHits=%d SameFrame=%d"),
            bPass ? TEXT("PASS") : TEXT("FAIL"), TrialName, Reason, GFrameCounter, Rounds,
            Pawn ? Pawn->GetEquippedAmmo() : -1, ExpectedAttackId, GetPlayerEffectiveHealth(),
            Component ? Component->GetHitCount() : -1, Component ? Component->GetNotifyBeginSampleCount() : -1,
            Component ? Component->GetNotifyBeginHitCount() : -1, Component ? Component->GetSameFrameWindowCount() : -1);
        FWorldDelegates::OnWorldPostActorTick.Remove(TickHandle);
        FWorldDelegates::OnWorldCleanup.Remove(CleanupHandle);
        if (UWorld* World = TrialWorld.Get())
        {
            World->RemoveOnActorSpawnedHandler(SpawnHandle);
        }
        if (!bWorldEnding)
        {
            ReleaseKey();
            if (AActor* Obstacle = TestObstacle.Get())
            {
                Obstacle->Destroy();
            }
            if (Trial != ECastleMeleeInputTrial::Hitch && (!IsGeometryTrial() || IsOutsideTrial()) &&
                Controller.IsValid() && Controller->GetPawn() == Pawn)
            {
                Controller->SetControlRotation(OriginalAim);
            }
        }
        *this = FCastleMeleeInputTrial();
    }

    void ObserveRound(AActor* Actor)
    {
        if (bActiveDeathInjection && Actor &&
            Actor->GetClass()->GetPathName() == TEXT("/Game/Art/Grunt01/IntegrationV2/Ragdoll/BP_CastleEnemy_DeathVisualProxy_Ragdoll.BP_CastleEnemy_DeathVisualProxy_Ragdoll_C"))
        {
            ++ActiveDeathProxyCount;
            ActiveDeathProxy = Actor;
            UE_LOG(LogTemp, Display, TEXT("CASTLE_MELEE_INPUT_ACTIVE_DEATH_PROXY_SPAWN Frame=%llu Attack=%llu Class=%s Position=%s"),
                GFrameCounter, ExpectedAttackId, *Actor->GetClass()->GetPathName(), *Actor->GetActorLocation().ToCompactString());
            return;
        }
        if (!Cast<ACastleWeaponProjectile>(Actor) || Actor->GetOwner() != Player.Get())
        {
            return;
        }
        ++Rounds;
        const UCastleMeleeCombatComponent* Component = Melee.Get();
        AttackIdAtShot = Component ? Component->GetAttackId() : 0;
        PhaseAtShot = Component ? Component->GetPhase() : UCastleMeleeCombatComponent::EPhase::Idle;
        UE_LOG(LogTemp, Display, TEXT("CASTLE_MELEE_INPUT_PROJECTILE Round=%d Class=%s Frame=%llu Attack=%llu Phase=%d"),
            Rounds, *Actor->GetClass()->GetPathName(), GFrameCounter, AttackIdAtShot, static_cast<int32>(PhaseAtShot));
    }

    bool AimHasClearTarget(ACastlePlayer& FiringPawn, APlayerController& PC, ACastleEnemy& Guard, UWorld& World)
    {
        const FVector Eye = FiringPawn.Camera->GetComponentLocation();
        const FVector Offset = Guard.GetCapsuleComponent()->GetComponentLocation() - Eye;
        if (Offset.IsNearlyZero())
        {
            return false;
        }
        // Exact facing for the fixture. The shot still goes through PlayerInput -> Fire -> projectile.
        PC.SetControlRotation(Offset.Rotation());
        FCollisionQueryParams Query(SCENE_QUERY_STAT(CastleMeleeInputFixtureAim), false, &FiringPawn);
        FHitResult Hit;
        return World.LineTraceSingleByChannel(Hit, Eye, Eye + FiringPawn.Camera->GetForwardVector() * 14000.f,
            ECC_Visibility, Query) && Hit.GetActor() == &Guard && Hit.GetComponent() == Guard.GetCapsuleComponent();
    }

    bool PlaceVisibilityBlocker(UWorld& World, const ACastleEnemy& Guard, const ACastlePlayer& Pawn,
        const UCastleMeleeCombatComponent& Component)
    {
        const FVector Origin = Guard.GetActorLocation();
        const FVector TargetPosition = Pawn.GetCapsuleComponent()->GetComponentLocation();
        const double TargetDistance = FVector::DotProduct(TargetPosition - Origin, WindupDirection);
        if (TargetDistance <= 2.0 * (Pawn.GetCapsuleComponent()->GetScaledCapsuleRadius() + 4.f))
        {
            return false;
        }
        FActorSpawnParameters Params;
        Params.ObjectFlags |= RF_Transient;
        AActor* Obstacle = World.SpawnActor<AActor>(Params);
        if (!Obstacle) return false;
        TestObstacle = Obstacle;
        auto* Box = NewObject<UBoxComponent>(Obstacle, FName(TEXT("CastleMeleeTestVisibilityBlocker")), RF_Transient);
        Obstacle->AddInstanceComponent(Box);
        Obstacle->SetRootComponent(Box);
        Box->SetMobility(EComponentMobility::Movable);
        Box->SetBoxExtent(FVector(4.0, Component.StrikeRadius * 3.0, 110.0));
        Box->SetCanEverAffectNavigation(false);
        Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
        Box->SetCollisionResponseToAllChannels(ECR_Ignore);
        Box->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
        Box->SetGenerateOverlapEvents(false);
        Box->SetWorldLocationAndRotation(Origin + WindupDirection * (TargetDistance * 0.5), WindupDirection.Rotation());
        Box->RegisterComponent();

        // Independent readback of the same geometric sweep. The component's counters
        // below count only its own real attempt during the authored strike window.
        FHitResult Probe;
        FCollisionQueryParams Query(SCENE_QUERY_STAT(CastleMeleeInputFixtureBlocker), false, &Guard);
        const bool bActualFirstBlock = World.SweepSingleByChannel(Probe, Origin,
            Origin + WindupDirection * Component.StrikeReach, FQuat::Identity, ECC_Visibility,
            FCollisionShape::MakeSphere(Component.StrikeRadius), Query)
            && Probe.GetActor() == Obstacle && Probe.GetComponent() == Box;
        UE_LOG(LogTemp, Display, TEXT("CASTLE_MELEE_INPUT_BLOCKER Position=%s PawnResponse=%d FirstBlock=%s Confirmed=%d"),
            *Box->GetComponentLocation().ToCompactString(), static_cast<int32>(Box->GetCollisionResponseToChannel(ECC_Pawn)),
            *GetNameSafe(Probe.GetComponent()), bActualFirstBlock);
        return bActualFirstBlock;
    }

    void UpdateGeometryTrial(UWorld& World, ACastlePlayer& Pawn, APlayerController& PC, ACastleEnemy& Guard,
        UCastleMeleeCombatComponent& Component)
    {
        using EPhase = UCastleMeleeCombatComponent::EPhase;
        const bool bOutsideTrial = IsOutsideTrial();
        const FVector PlayerPosition = Pawn.GetActorLocation();
        if (Rounds != 0 || Component.GetHitCount() != MeleeHitsBefore ||
            !FMath::IsNearlyEqual(GetPlayerEffectiveHealth(), PlayerEffectiveHealthBefore))
        {
            Finish(false, TEXT("unexpected shot or damage in the geometry fixture"));
            return;
        }
        if (Trial == ECastleMeleeInputTrial::Recovery && Step == ECastleMeleeInputStep::FinalObservation)
        {
            if (Component.GetWindowCount() != WindowsBefore + 1)
            {
                Finish(false, TEXT("extra active window before the same guard was observed starting over"));
            }
            else if (Component.GetPhase() == EPhase::Windup && Component.GetAttackId() != ExpectedAttackId &&
                Component.GetAttackId() != 0)
            {
                UE_LOG(LogTemp, Display, TEXT("CASTLE_MELEE_INPUT_RECOVERY_REPLAY Old=%llu New=%llu NaturalCompletions=%d"),
                    ExpectedAttackId, Component.GetAttackId(), Component.GetCompletedCount() - CompletedBefore);
                Finish(Component.GetCompletedCount() - CompletedBefore == 1 && Component.GetCanceledCount() == CanceledBefore,
                    TEXT("old strike stayed closed after barrier removal, finished naturally, and a new windup was observed"));
            }
            return;
        }
        if (!ExpectedAttackId)
        {
            if (bOutsideTrial)
            {
                const FVector Inward = (Guard.GetActorLocation() - PlayerPosition).GetSafeNormal2D();
                if (Inward.IsNearlyZero()) return;
                PC.SetControlRotation(Inward.Rotation()); // Align regular S movement away from the incoming strike.
                if (FVector::DotProduct(Pawn.GetActorForwardVector().GetSafeNormal2D(), Inward) < 0.97)
                {
                    return;
                }
            }
            if (Component.GetPhase() != EPhase::Windup || Component.GetAttackId() == 0)
            {
                return;
            }
            ExpectedAttackId = Component.GetAttackId();
            WindupPlayerPosition = PlayerPosition;
            WindupDirection = Guard.GetActorForwardVector().GetSafeNormal2D();
            if (bOutsideTrial)
            {
                // Only the normal reverse-walk binding may move the player.
                PressFrame = GFrameCounter;
                bOwnPressedKey = true;
                const bool bRouted = PC.InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::S, IE_Pressed, 1.f));
                UE_LOG(LogTemp, Display, TEXT("CASTLE_MELEE_INPUT_BACKSTEP Frame=%llu Attack=%llu Routed=%d Pose=%s"),
                    PressFrame, ExpectedAttackId, bRouted, *WindupPlayerPosition.ToCompactString());
            }
            else if (!PlaceVisibilityBlocker(World, Guard, Pawn, Component))
            {
                Finish(false, TEXT("could not put the owned visibility blocker first on the physical sweep"));
            }
            return;
        }
        if (Component.GetAttackId() != ExpectedAttackId)
        {
            Finish(false, TEXT("original windup was replaced before geometry evidence"));
            return;
        }

        const FVector PlayerOffset = PlayerPosition - Guard.GetActorLocation();
        const double ExitDistance = Component.StrikeReach + Component.StrikeRadius +
            Pawn.GetCapsuleComponent()->GetScaledCapsuleRadius() + 8.0;
        const double Along = FVector::DotProduct(PlayerOffset, WindupDirection);
        const double Retreated = FVector::DotProduct(PlayerPosition - WindupPlayerPosition, WindupDirection);
        if (bOutsideTrial && bOwnPressedKey && GFrameCounter > PressFrame && Along > ExitDistance)
        {
            ReleaseKey();
        }
        if (IsGeometryHitchTrial() && !bStallIssued && Component.GetPhase() == EPhase::Windup &&
            (!bOutsideTrial || (!bOwnPressedKey && ReleaseFrame > PressFrame && GFrameCounter > ReleaseFrame &&
                !PC.IsInputKeyDown(EKeys::S) && Retreated > 45.0 && Along > ExitDistance)))
        {
            StallBeforeHitchWindow(Guard, Component, PlayerPosition);
        }
        if (Trial == ECastleMeleeInputTrial::ActiveDeath && Component.GetPhase() == EPhase::Active &&
            Component.GetWindowCount() - WindowsBefore == 1 && Component.GetBlockedSweepCount() > BlockedBefore)
        {
            if (!RecordOriginalWindowDeadline(Guard, Component, World.GetTimeSeconds(), true))
            {
                Finish(false, TEXT("could not capture the live Active instance deadline"));
                return;
            }
            ACastleDirector* D = Director.Get();
            if (!D)
            {
                Finish(false, TEXT("missing Director for isolated Active death fixture"));
                return;
            }
            KillsBefore = D->Kills;
            DyingGuardController = Guard.GetController();
            const float LethalDamage = Guard.Health;
            UE_LOG(LogTemp, Display, TEXT("CASTLE_MELEE_INPUT_ACTIVE_DEATH_INJECTION Frame=%llu Attack=%llu Phase=Active WindowCount=%d Damage=%.1f Sweeps=%d Blocked=%d"),
                GFrameCounter, ExpectedAttackId, Component.GetWindowCount() - WindowsBefore, LethalDamage,
                Component.GetSweepCount() - SweepsBefore, Component.GetBlockedSweepCount() - BlockedBefore);
            // Purposefully exercise only the original enemy lifecycle. The separate Death
            // case is the source of real carbine/projectile evidence.
            TGuardValue<bool> Injecting(bActiveDeathInjection, true);
            const float Applied = Guard.TakeDamage(LethalDamage, FDamageEvent(), &PC, &Pawn);
            if (!FMath::IsNearlyEqual(Applied, LethalDamage) || D->Kills != KillsBefore + 1)
            {
                Finish(false, TEXT("scoped native damage did not invoke exactly one real kill"));
                return;
            }
            Step = ECastleMeleeInputStep::FinalObservation;
            StepTime = World.GetTimeSeconds();
            return;
        }
        if (Component.GetPhase() != EPhase::Recovery)
        {
            return;
        }
        if (Trial == ECastleMeleeInputTrial::ActiveDeath)
        {
            Finish(false, TEXT("real strike window ended without an observable Active death boundary"));
            return;
        }
        if (bOutsideTrial)
        {
            ReleaseKey();
            if (GFrameCounter <= ReleaseFrame || PC.IsInputKeyDown(EKeys::S)) return;
        }

        const int32 Sweeps = Component.GetSweepCount() - SweepsBefore;
        const int32 Outside = Component.GetOutsideVolumeSampleCount() - OutsideBefore;
        const int32 Blocked = Component.GetBlockedSweepCount() - BlockedBefore;
        const int32 Missed = Component.GetMissedSweepCount() - MissedBefore;
        const int32 Samples = Component.GetNotifyBeginSampleCount() - NotifySamplesBefore +
            Component.GetTickSampleCount() - TickSamplesBefore;
        const int32 SameFrame = Component.GetSameFrameWindowCount() - SameFrameWindowsBefore;
        const bool bClassified = Component.GetWindowCount() - WindowsBefore == 1 &&
            Samples > 0 && Samples == Outside + Sweeps && Sweeps == Missed + Blocked &&
            (!IsGeometryHitchTrial() || (bStallIssued && SameFrame == 1));
        const bool bGeometryPass = bOutsideTrial
            ? Retreated > 45.0 && Along > ExitDistance && Outside + Missed > 0 && Blocked == 0
            : TestObstacle.IsValid() && FVector::DistSquared(PlayerPosition, WindupPlayerPosition) <= FMath::Square(5.0)
                && Blocked > 0 && Sweeps == Blocked;
        UE_LOG(LogTemp, Display, TEXT("CASTLE_MELEE_INPUT_GEOMETRY Attack=%llu Sweeps=%d OutsideVolume=%d Blocked=%d Missed=%d Samples=%d SameFrame=%d Along=%.1f Retreated=%.1f WindupPose=%s FinalPose=%s"),
            ExpectedAttackId, Sweeps, Outside, Blocked, Missed, Samples, SameFrame, Along, Retreated,
            *WindupPlayerPosition.ToCompactString(), *PlayerPosition.ToCompactString());
        if (Trial == ECastleMeleeInputTrial::Recovery && bClassified && bGeometryPass)
        {
            TestObstacle->Destroy();
            TestObstacle.Reset();
            Step = ECastleMeleeInputStep::FinalObservation;
            UE_LOG(LogTemp, Display, TEXT("CASTLE_MELEE_INPUT_RECOVERY_OPEN Frame=%llu Attack=%llu"), GFrameCounter, ExpectedAttackId);
            return;
        }
        Finish(bClassified && bGeometryPass, bOutsideTrial ? TEXT("actual backstep trajectory and no-damage strike outcomes")
            : TEXT("stationary player and physical visibility-blocked strike outcomes"));
    }

    void StallBeforeHitchWindow(ACastleEnemy& Guard, const UCastleMeleeCombatComponent& Component, const FVector& PlayerPose)
    {
        if (bStallIssued || Component.GetPhase() != UCastleMeleeCombatComponent::EPhase::Windup) return;
        UAnimInstance* Anim = Guard.GetMesh()->GetAnimInstance();
        const UAnimMontage* Montage = Component.StrikeMontage.Get();
        const FAnimMontageInstance* Instance = Anim && Montage ? Anim->GetActiveInstanceForMontage(Montage) : nullptr;
        if (!Instance || !Instance->IsPlaying()) return;
        for (const FAnimNotifyEvent& Notify : Montage->Notifies)
        {
            const UAnimNotifyState* State = Notify.NotifyStateClass.Get();
            if (!State || State->GetNotifyName() != TEXT("CastleMeleeHitWindow")) continue;
            const float TrueRate = Instance->GetPlayRate() * Montage->RateScale;
            if (TrueRate <= 0.f) return;
            const float Position = Instance->GetPosition();
            const float BeforeBegin = (Notify.GetTriggerTime() - Position) / TrueRate;
            const float UntilEnd = (Notify.GetEndTriggerTime() - Position) / TrueRate;
            if (BeforeBegin >= 0.025f && BeforeBegin <= 0.11f && UntilEnd <= 0.20f)
            {
                constexpr float StallSeconds = 0.22f;
                bStallIssued = true;
                ExpectedAttackId = Component.GetAttackId();
                const double Before = FPlatformTime::Seconds();
                FPlatformProcess::SleepNoStats(StallSeconds);
                UE_LOG(LogTemp, Display, TEXT("CASTLE_MELEE_HITCH_STALL Frame=%llu Attack=%llu Position=%.6f Begin=%.6f End=%.6f Pose=%s WallMs=%.3f"),
                    GFrameCounter, ExpectedAttackId, Position, Notify.GetTriggerTime(), Notify.GetEndTriggerTime(),
                    *PlayerPose.ToCompactString(),
                    (FPlatformTime::Seconds() - Before) * 1000.0);
            }
            return;
        }
    }

    bool RecordOriginalWindowDeadline(ACastleEnemy& Guard, const UCastleMeleeCombatComponent& Component, double Now,
        bool bAlreadyActive = false)
    {
        UAnimInstance* Anim = Guard.GetMesh()->GetAnimInstance();
        const UAnimMontage* Montage = Component.StrikeMontage.Get();
        const FAnimMontageInstance* Instance = Anim && Montage ? Anim->GetActiveInstanceForMontage(Montage) : nullptr;
        const float TrueRate = Instance ? Instance->GetPlayRate() * Montage->RateScale : 0.f;
        if (TrueRate <= 0.f || !FMath::IsNearlyEqual(Guard.CustomTimeDilation, 1.f))
        {
            return false;
        }
        for (const FAnimNotifyEvent& Notify : Montage->Notifies)
        {
            const UAnimNotifyState* State = Notify.NotifyStateClass.Get();
            if (!State || State->GetNotifyName() != TEXT("CastleMeleeHitWindow")) continue;
            const float StartAt = Notify.GetTriggerTime();
            const float EndAt = Notify.GetEndTriggerTime();
            const float At = Instance->GetPosition();
            // Cancel holds a 0.35s native retry interval. An original window under 0.30s
            // leaves a margin before a legitimate replay's opening. Reject other timings.
            if (bAlreadyActive ? !(At >= StartAt && At < EndAt)
                : At >= StartAt || (EndAt - StartAt) / TrueRate >= 0.30f)
            {
                return false;
            }
            OriginalWindowDeadline = Now + (EndAt - At) / TrueRate + 0.03;
            UE_LOG(LogTemp, Display, TEXT("CASTLE_MELEE_INPUT_OLD_WINDOW Attack=%llu Instance=%d Position=%.6f Rate=%.4f OriginalDeadline=%.6f"),
                Component.GetAttackId(), Instance->GetInstanceID(), At, TrueRate, OriginalWindowDeadline);
            return true;
        }
        return false;
    }

    void OnTick(UWorld* World, ELevelTick, float DeltaSeconds)
    {
        if (World != TrialWorld.Get()) return;
        APlayerController* PC = Controller.Get();
        ACastlePlayer* Pawn = Player.Get();
        ACastleDirector* D = Director.Get();
        ACastleEnemy* Guard = Enemy.Get();
        auto* Component = Melee.Get();
        using EPhase = UCastleMeleeCombatComponent::EPhase;
        const double Now = World->GetTimeSeconds();
        if (!PC || !Pawn || PC->GetPawn() != Pawn || Pawn->bDead || !D || !D->bReady || D->bWon ||
            D->RunSeed != InitialSeed || D->Floor != InitialFloor || D->Region != InitialRegion ||
            CastleMeleeCohortRun().OwnWorld.Get() != World)
        {
            Finish(false, TEXT("fixture context changed"));
            return;
        }
        if (Now - StartTime > 15.0)
        {
            Finish(false, Trial == ECastleMeleeInputTrial::Hitch ? TEXT("no qualifying same-frame notify evidence")
                : TEXT("input or target scenario did not complete"));
            return;
        }
        if (Trial == ECastleMeleeInputTrial::Hitch)
        {
            if (!Component || !Guard || FVector::DistSquared(Pawn->GetActorLocation(), InitialPlayerPosition) > FMath::Square(5.f))
            {
                Finish(false, TEXT("the stationary hitch subject moved or changed"));
                return;
            }
            if (Component->GetSameFrameWindowCount() > SameFrameWindowsBefore)
            {
                const bool bHitOnce = bStallIssued && Component->GetAttackId() == ExpectedAttackId
                    && Component->GetSameFrameWindowCount() - SameFrameWindowsBefore == 1
                    && Component->GetHitCount() - MeleeHitsBefore == 1
                    && Component->GetNotifyBeginSampleCount() - NotifySamplesBefore == 1
                    && Component->GetNotifyBeginHitCount() - NotifyHitsBefore == 1
                    && Component->GetSweepCount() - SweepsBefore == 1
                    && FMath::IsNearlyEqual(PlayerEffectiveHealthBefore - GetPlayerEffectiveHealth(), Guard->AttackDamage);
                UE_LOG(LogTemp, Display, TEXT("CASTLE_MELEE_HITCH_SAMPLE Frame=%llu GameDeltaMs=%.3f TickSamples=%d"),
                    GFrameCounter, DeltaSeconds * 1000.f, Component->GetTickSampleCount());
                Finish(bHitOnce, bHitOnce ? TEXT("one real opening hit across one same-frame Montage window")
                    : TEXT("crossed window did not yield exactly one observed hit"));
            }
            else if (Component->GetHitCount() != MeleeHitsBefore)
            {
                Finish(false, TEXT("first hit occurred in a regular multi-frame window"));
            }
            else
            {
                StallBeforeHitchWindow(*Guard, *Component, Pawn->GetActorLocation());
            }
            return;
        }
        if (IsGeometryTrial())
        {
            if (Trial == ECastleMeleeInputTrial::ActiveDeath && Step == ECastleMeleeInputStep::FinalObservation)
            {
                if (!FMath::IsNearlyEqual(GetPlayerEffectiveHealth(), PlayerEffectiveHealthBefore) || Rounds != 0)
                {
                    Finish(false, TEXT("unexpected player damage or projectile after scoped Active death"));
                }
                else if (Now >= OriginalWindowDeadline && Now - StepTime >= 0.21)
                {
                    AActor* Proxy = ActiveDeathProxy.Get();
                    const USkeletalMeshComponent* Visual = Proxy ? Proxy->FindComponentByClass<USkeletalMeshComponent>() : nullptr;
                    if (ActiveDeathProxyCount != 1 || !Proxy || !Visual || Guard || DyingGuardController.IsValid() || D->Kills != KillsBefore + 1)
                    {
                        Finish(false, TEXT("original guard/controller cleanup or single living proxy did not match"));
                    }
                    else if (Visual->IsAnySimulatingPhysics())
                    {
                        const float Remaining = Proxy->GetLifeSpan();
                        const bool bPhysics = Visual->GetCollisionEnabled() == ECollisionEnabled::PhysicsOnly &&
                            Visual->GetCollisionResponseToChannel(ECC_Pawn) == ECR_Ignore && Remaining > 0.f && Remaining <= 10.5f;
                        UE_LOG(LogTemp, Display, TEXT("CASTLE_MELEE_INPUT_ACTIVE_DEATH_PROXY_LIVE Class=%s Mesh=%s Remaining=%.3f Collision=%d PawnResponse=%d"),
                            *Proxy->GetClass()->GetPathName(), *Visual->GetName(), Remaining, static_cast<int32>(Visual->GetCollisionEnabled()),
                            static_cast<int32>(Visual->GetCollisionResponseToChannel(ECC_Pawn)));
                        Finish(bPhysics, TEXT("native Active death removed the guard/controller; one live ragdoll in tested physics state, no stale hit"));
                    }
                    else if (Now - StepTime > 2.5)
                    {
                        Finish(false, TEXT("one living death proxy existed but its physical state transition was not observed"));
                    }
                }
            }
            else if (Component && Guard) UpdateGeometryTrial(*World, *Pawn, *PC, *Guard, *Component);
            else Finish(false, TEXT("geometry fixture lost its live guard"));
            return;
        }
        if (Pawn->bCollectionOpen || Pawn->bReloading || Pawn->EquippedWeapon != ECastleWeapon::Carbine)
        {
            Finish(false, TEXT("carbine input stopped being eligible"));
            return;
        }

        if (Step == ECastleMeleeInputStep::Aim)
        {
            if (!Guard || !Component || Pawn->GetEquippedAmmo() < ExpectedCarbineRounds() || !Pawn->Camera ||
                PC->IsInputKeyDown(EKeys::LeftMouseButton))
            {
                Finish(false, TEXT("expected a live melee subject, loaded carbine and unused fire input"));
                return;
            }
            const bool bLethal = Guard->Health <= Pawn->GetEquippedDamage();
            const bool bNeedsWindup = Trial == ECastleMeleeInputTrial::Cancel || bLethal;
            if (Trial == ECastleMeleeInputTrial::Cancel && bLethal)
            {
                Finish(false, TEXT("cancel case needs a target that survives one ordinary shot"));
                return;
            }
            if (Trial == ECastleMeleeInputTrial::Sustain && Guard->Health <= 2 * Pawn->GetEquippedDamage())
            {
                Finish(false, TEXT("sustained fire needs a live guard that can survive two actual rounds"));
                return;
            }
            const bool bClear = AimHasClearTarget(*Pawn, *PC, *Guard, *World);
            if (Now < NextInputTime || !bClear ||
                (bNeedsWindup && (Component->GetPhase() != EPhase::Windup || Component->GetAttackId() == 0)))
            {
                return;
            }
            bLethalShot = bLethal;
            ExpectedAttackId = bNeedsWindup ? Component->GetAttackId() : 0;
            if (bNeedsWindup && !RecordOriginalWindowDeadline(*Guard, *Component, Now))
            {
                Finish(false, TEXT("could not isolate the original live window from a legitimate replay"));
                return;
            }
            AmmoBefore = Pawn->GetEquippedAmmo();
            TargetHealthBefore = Guard->Health;
            KillsBefore = D->Kills;
            CanceledBefore = Component->GetCanceledCount();
            MeleeHitsBefore = Component->GetHitCount();
            WindowsBefore = Component->GetWindowCount();
            PlayerEffectiveHealthBefore = GetPlayerEffectiveHealth();
            ShotRoundsBefore = Rounds;
            Step = ECastleMeleeInputStep::Pressed;
            StepTime = Now;
            PressFrame = GFrameCounter;
            bOwnPressedKey = true;
            const bool bRouted = PC->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::LeftMouseButton, IE_Pressed, 1.f));
            UE_LOG(LogTemp, Display, TEXT("CASTLE_MELEE_INPUT_PRESS Frame=%llu Routed=%d Ammo=%d TargetHP=%.1f Attack=%llu"),
                PressFrame, bRouted, AmmoBefore, TargetHealthBefore, ExpectedAttackId);
            return;
        }
        if (Step == ECastleMeleeInputStep::Pressed)
        {
            if (GFrameCounter <= PressFrame) return; // Never press and release before the next PlayerInput phase.
            if (Trial == ECastleMeleeInputTrial::Sustain && Guard && Pawn->Camera)
            {
                AimHasClearTarget(*Pawn, *PC, *Guard, *World);
            }
            if (Rounds - ShotRoundsBefore >= ExpectedCarbineRounds() ||
                AmmoBefore - Pawn->GetEquippedAmmo() >= ExpectedCarbineRounds())
            {
                ReleaseKey();
                Step = ECastleMeleeInputStep::Released;
                StepTime = Now;
            }
            else if (Now - StepTime > 0.55)
            {
                Finish(false, TEXT("controller input produced no actual projectile or ammo consumption"));
            }
            return;
        }

        if (Rounds != ShotRoundsBefore + ExpectedCarbineRounds() ||
            Pawn->GetEquippedAmmo() != AmmoBefore - ExpectedCarbineRounds() ||
            (ExpectedAttackId && (AttackIdAtShot != ExpectedAttackId || PhaseAtShot != EPhase::Windup)))
        {
            Finish(false, TEXT("exact round, ammo or live windup identity did not match"));
            return;
        }
        if (GFrameCounter <= ReleaseFrame || PC->IsInputKeyDown(EKeys::LeftMouseButton))
        {
            if (Now - StepTime > 0.55) Finish(false, TEXT("the MouseButton release was not processed"));
            return;
        }
        if (Trial == ECastleMeleeInputTrial::Sustain)
        {
            const bool bExpectedDamage = Guard &&
                FMath::IsNearlyEqual(TargetHealthBefore - Guard->Health, 2 * Pawn->GetEquippedDamage());
            if (bExpectedDamage && Now - StepTime >= 0.21)
            {
                UE_LOG(LogTemp, Display, TEXT("CASTLE_MELEE_INPUT_SUSTAIN Press=%llu Release=%llu Rounds=2 Damage=%.1f TargetHP=%.1f"),
                    PressFrame, ReleaseFrame, TargetHealthBefore - Guard->Health, Guard->Health);
                Finish(true, TEXT("one held carbine input, two actual rounds and impacts, released without a third"));
            }
            else if (Now - StepTime > 0.55)
            {
                Finish(false, TEXT("two real sustained rounds did not produce the expected damage on the living guard"));
            }
            return;
        }
        if (Step == ECastleMeleeInputStep::FinalObservation)
        {
            const bool bNoDelayedHit = FMath::IsNearlyEqual(GetPlayerEffectiveHealth(), PlayerEffectiveHealthBefore);
            if (!bNoDelayedHit || (Component && Component->GetHitCount() != MeleeHitsBefore))
            {
                Finish(false, TEXT("unexpected hit or player damage before the original window deadline"));
                return;
            }
            if (Now < OriginalWindowDeadline || Now - StepTime < 0.21) return;
            if (Trial == ECastleMeleeInputTrial::Cancel)
            {
                const bool bStillCanceled = Guard && Component && D->Kills == KillsBefore &&
                    Component->GetAttackId() != ExpectedAttackId && Component->GetWindowCount() == WindowsBefore;
                Finish(bStillCanceled, bStillCanceled
                    ? TEXT("one real shot hit a living guard before its window, canceled the windup and released fire")
                    : TEXT("the live target or canceled identity changed after fire release"));
                return;
            }
            Finish(!Guard && D->Kills == KillsBefore + 1,
                TEXT("one lethal carbine round in current windup; no stale hit through the original window deadline"));
            return;
        }
        if (bLethalShot)
        {
            if (!Guard && D->Kills == KillsBefore + 1)
            {
                Step = ECastleMeleeInputStep::FinalObservation;
                StepTime = Now;
            }
            else if (Now - StepTime > 0.55)
            {
                Finish(false, TEXT("a purported lethal projectile did not remove the guard and award one kill"));
            }
            return;
        }

        if (Guard && Component && Guard->Health < TargetHealthBefore)
        {
            const bool bExpectedDamage = FMath::IsNearlyEqual(TargetHealthBefore - Guard->Health, Pawn->GetEquippedDamage());
            const bool bCanceledOldWindup = !ExpectedAttackId ||
                (Component->GetCanceledCount() == CanceledBefore + 1 && Component->GetAttackId() != ExpectedAttackId &&
                    Component->GetHitCount() == MeleeHitsBefore && Component->GetWindowCount() == WindowsBefore);
            UE_LOG(LogTemp, Display, TEXT("CASTLE_MELEE_INPUT_IMPACT Frame=%llu HealthBefore=%.1f HealthAfter=%.1f ExpectedAttack=%llu CanceledBefore=%d CanceledNow=%d"),
                GFrameCounter, TargetHealthBefore, Guard->Health, ExpectedAttackId, CanceledBefore, Component->GetCanceledCount());
            if (!bExpectedDamage || !bCanceledOldWindup)
            {
                Finish(false, TEXT("actual projectile damage or windup cancel evidence did not match"));
            }
            else if (Trial == ECastleMeleeInputTrial::Cancel)
            {
                Step = ECastleMeleeInputStep::FinalObservation;
                StepTime = Now;
            }
            else
            {
                Step = ECastleMeleeInputStep::Aim;
                NextInputTime = Now + 0.21;
            }
        }
        else if (Now - StepTime > 0.55)
        {
            Finish(false, TEXT("a real projectile spawned but produced no expected target damage"));
        }
    }

    void OnWorldEnding(UWorld* World, bool, bool)
    {
        if (World == TrialWorld.Get()) Finish(false, TEXT("PIE ended"), true);
    }
};

FCastleMeleeInputTrial& CastleMeleeInputTrial()
{
    static FCastleMeleeInputTrial Run;
    return Run;
}

void CastleMeleeRunInputTrial(const TArray<FString>& Args, UWorld* World)
{
    if (!IsInGameThread() || !World || World->WorldType != EWorldType::PIE || World->GetNetMode() != NM_Standalone ||
        UWorld::RemovePIEPrefix(World->GetOutermost()->GetName()) != CastleMeleeCohortMap || Args.Num() != 1)
    {
        UE_LOG(LogTemp, Warning, TEXT("CASTLE_MELEE_INPUT_REJECT expected exact standalone test map PIE and one supported mode"));
        return;
    }
    auto& Run = CastleMeleeInputTrial();
    if (Args[0].Equals(TEXT("Stop"), ESearchCase::IgnoreCase))
    {
        if (Run.TrialWorld.Get() == World) Run.Finish(false, TEXT("stopped by test operator"));
        return;
    }
    const bool bHitch = Args[0].Equals(TEXT("Hitch"), ESearchCase::IgnoreCase);
    const bool bDeath = Args[0].Equals(TEXT("Death"), ESearchCase::IgnoreCase);
    const bool bCancel = Args[0].Equals(TEXT("Cancel"), ESearchCase::IgnoreCase);
    const bool bBlocked = Args[0].Equals(TEXT("Blocked"), ESearchCase::IgnoreCase);
    const bool bOutside = Args[0].Equals(TEXT("Outside"), ESearchCase::IgnoreCase);
    const bool bHitchBlocked = Args[0].Equals(TEXT("HitchBlocked"), ESearchCase::IgnoreCase);
    const bool bHitchOutside = Args[0].Equals(TEXT("HitchOutside"), ESearchCase::IgnoreCase);
    const bool bActiveDeath = Args[0].Equals(TEXT("ActiveDeath"), ESearchCase::IgnoreCase);
    const bool bSustain = Args[0].Equals(TEXT("Sustain"), ESearchCase::IgnoreCase);
    const bool bRecovery = Args[0].Equals(TEXT("Recovery"), ESearchCase::IgnoreCase);
    const auto& Cohort = CastleMeleeCohortRun();
    APlayerController* PC = World->GetFirstPlayerController();
    auto* Pawn = PC ? Cast<ACastlePlayer>(PC->GetPawn()) : nullptr;
    auto* Guard = Cohort.OwnWorld.Get() == World && Cohort.Guards.Num() == 1 ? Cohort.Guards[0].Get() : nullptr;
    auto* Component = Guard ? Guard->FindComponentByClass<UCastleMeleeCombatComponent>() : nullptr;
    auto* D = Pawn ? Pawn->Director() : nullptr;
    if ((!bHitch && !bDeath && !bCancel && !bBlocked && !bOutside && !bHitchBlocked && !bHitchOutside && !bActiveDeath &&
        !bSustain && !bRecovery) ||
        Run.TickHandle.IsValid() ||
        World->IsPaused() || !Pawn || Pawn->bDead || !PC->IsLocalController() || !D || !D->bReady || D->bWon ||
        !IsValid(Guard) || Guard->bDead || !Guard->bUseMeleeV1 || !Component || !IsValid(Component->StrikeMontage.Get()) ||
        PC->IsInputKeyDown(EKeys::LeftMouseButton) || ((bOutside || bHitchOutside) && PC->IsInputKeyDown(EKeys::S)) ||
        ((bCancel || bDeath || bSustain) && (Pawn->EquippedWeapon != ECastleWeapon::Carbine || Pawn->GetEquippedAmmo() <= 0 ||
            Pawn->GetInfiniteAmmoRemaining() > 0.f || Pawn->SpreadStacks > 0 || Pawn->bReloading || Pawn->bCollectionOpen)))
    {
        UE_LOG(LogTemp, Warning, TEXT("CASTLE_MELEE_INPUT_REJECT requires a live Single melee cohort and unclaimed input; Cancel/Death need a loaded unmodified carbine"));
        return;
    }

    Run.Trial = bHitch ? ECastleMeleeInputTrial::Hitch : bDeath ? ECastleMeleeInputTrial::Death
        : bBlocked ? ECastleMeleeInputTrial::Blocked : bOutside ? ECastleMeleeInputTrial::Outside
        : bHitchBlocked ? ECastleMeleeInputTrial::HitchBlocked : bHitchOutside ? ECastleMeleeInputTrial::HitchOutside
        : bActiveDeath ? ECastleMeleeInputTrial::ActiveDeath : bSustain ? ECastleMeleeInputTrial::Sustain
        : bRecovery ? ECastleMeleeInputTrial::Recovery : ECastleMeleeInputTrial::Cancel;
    Run.TrialWorld = World;
    Run.Controller = PC;
    Run.Player = Pawn;
    Run.Enemy = Guard;
    Run.Melee = Component;
    Run.Director = D;
    Run.InitialSeed = D->RunSeed;
    Run.InitialFloor = D->Floor;
    Run.InitialRegion = D->Region;
    Run.InitialPlayerPosition = Pawn->GetActorLocation();
    Run.OriginalAim = PC->GetControlRotation();
    Run.StartTime = World->GetTimeSeconds();
    Run.NextInputTime = Run.StartTime + 0.21;
    Run.PlayerEffectiveHealthBefore = Pawn->Health + Pawn->Shield;
    Run.MeleeHitsBefore = Component->GetHitCount();
    Run.NotifySamplesBefore = Component->GetNotifyBeginSampleCount();
    Run.NotifyHitsBefore = Component->GetNotifyBeginHitCount();
    Run.SameFrameWindowsBefore = Component->GetSameFrameWindowCount();
    Run.WindowsBefore = Component->GetWindowCount();
    Run.TickSamplesBefore = Component->GetTickSampleCount();
    Run.SweepsBefore = Component->GetSweepCount();
    Run.OutsideBefore = Component->GetOutsideVolumeSampleCount();
    Run.BlockedBefore = Component->GetBlockedSweepCount();
    Run.MissedBefore = Component->GetMissedSweepCount();
    Run.CanceledBefore = Component->GetCanceledCount();
    Run.CompletedBefore = Component->GetCompletedCount();
    Run.SpawnHandle = World->AddOnActorSpawnedHandler(FOnActorSpawned::FDelegate::CreateRaw(&Run, &FCastleMeleeInputTrial::ObserveRound));
    Run.TickHandle = FWorldDelegates::OnWorldPostActorTick.AddRaw(&Run, &FCastleMeleeInputTrial::OnTick);
    Run.CleanupHandle = FWorldDelegates::OnWorldCleanup.AddRaw(&Run, &FCastleMeleeInputTrial::OnWorldEnding);
    UE_LOG(LogTemp, Display, TEXT("CASTLE_MELEE_INPUT_START Mode=%s Seed=%d Floor=%d Guard=%s Position=%s"),
        *Args[0], D->RunSeed, D->Floor, *Guard->GetPathName(), *Pawn->GetActorLocation().ToCompactString());
}

FAutoConsoleCommandWithWorldAndArgs CastleMeleeInputCommand(TEXT("CastleMeleeV1.Input"),
    TEXT("Run an isolated Single cohort in fresh acceptance-map PIE, then Cancel|Death|Hitch|Blocked|Outside|HitchBlocked|HitchOutside|ActiveDeath|Sustain|Recovery|Stop."),
    FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&CastleMeleeRunInputTrial));
}

#endif
