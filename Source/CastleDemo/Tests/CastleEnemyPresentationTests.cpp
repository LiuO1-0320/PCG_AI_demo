#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "../CastleDemo.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequenceBase.h"
#include "Blueprint/BlueprintExceptionInfo.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/DamageEvents.h"
#include "Engine/Engine.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/WorldSettings.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "TimerManager.h"
#include "UObject/Script.h"
#include "UObject/UnrealType.h"

namespace
{
UWorld* CreateCastlePresentationTestWorld()
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

void DestroyCastlePresentationTestWorld(UWorld& World)
{
    World.DestroyWorld(false);
    GEngine->DestroyWorldContext(&World);
}

class FWaitForCastleDeathProxy final : public IAutomationLatentCommand
{
public:
    FWaitForCastleDeathProxy(FAutomationTestBase& InTest, UWorld& InWorld, AActor& InProxy, float InLifeSpan)
        : Test(InTest), World(InWorld), Proxy(&InProxy), ScheduledLifeSpan(InLifeSpan)
    {
    }

    ~FWaitForCastleDeathProxy() override
    {
        DestroyCastlePresentationTestWorld(World);
    }

    bool Update() override
    {
        constexpr float Step = 0.1f;
        // Actor lifespan is driven by this world's timer, independently of rendering or level travel.
        World.GetTimerManager().Tick(Step);
        SecondsElapsed += Step;
        if (!Proxy.IsValid())
        {
            Test.TestTrue(TEXT("Death proxy did not expire before the configured playback lifetime"), SecondsElapsed + Step >= ScheduledLifeSpan);
            Test.AddInfo(FString::Printf(TEXT("FIX01 proxy scheduled=%.3fs expired=%.3fs without explicit test destroy"), ScheduledLifeSpan, SecondsElapsed));
            return true;
        }
        if (SecondsElapsed > ScheduledLifeSpan + 1.f)
        {
            Test.AddError(TEXT("The death proxy outlived its scheduled lifespan."));
            return true;
        }
        return false;
    }

private:
    FAutomationTestBase& Test;
    UWorld& World;
    TWeakObjectPtr<AActor> Proxy;
    const float ScheduledLifeSpan;
    float SecondsElapsed = 0.f;
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleEnemyPresentationSpawnAndDamageTest,
    "CastleDemo.EnemyPresentation.SpawnAndDamage",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCastleEnemyPresentationSpawnAndDamageTest::RunTest(const FString& Parameters)
{
    UClass* GruntClass = LoadClass<ACastleEnemy>(nullptr,
        TEXT("/Game/CastleDemo/Enemies/Presentation/BP_CastleEnemy_Grunt.BP_CastleEnemy_Grunt_C"));
    UClass* ProxyClass = LoadClass<AActor>(nullptr,
        TEXT("/Game/CastleDemo/Enemies/Presentation/BP_CastleEnemy_DeathVisualProxy.BP_CastleEnemy_DeathVisualProxy_C"));
    if (!TestNotNull(TEXT("Saved Grunt Blueprint class loads"), GruntClass) || !TestNotNull(TEXT("Saved death proxy class loads"), ProxyClass))
    {
        return false;
    }
    ACastleEnemy* GruntDefaults = GruntClass->GetDefaultObject<ACastleEnemy>();
    USkeletalMeshComponent* DefaultMesh = GruntDefaults->GetMesh();
    if (!TestNotNull(TEXT("Saved Grunt uses the inherited character mesh"), DefaultMesh))
    {
        return false;
    }
    TestEqual(TEXT("Saved Grunt mesh"), GetNameSafe(DefaultMesh->GetSkeletalMeshAsset()), FString(TEXT("SKM_Manny_Simple")));
    TestEqual(TEXT("Saved Grunt animation class"), GetNameSafe(DefaultMesh->GetAnimClass()), FString(TEXT("ABP_Unarmed_C")));
    TestTrue(TEXT("Saved Grunt mesh has no collision"), DefaultMesh->GetCollisionEnabled() == ECollisionEnabled::NoCollision);
    for (const FName ComponentName : {FName(TEXT("SentinelBody")), FName(TEXT("Helmet")), FName(TEXT("Visor"))})
    {
        const UStaticMeshComponent* Component = Cast<UStaticMeshComponent>(GruntDefaults->GetDefaultSubobjectByName(ComponentName));
        TestTrue(FString::Printf(TEXT("Original %s is hidden on the Grunt"), *ComponentName.ToString()), Component && Component->bHiddenInGame);
    }

    struct FSpawnCounts
    {
        int32 ExplorationGuards = 0;
        int32 CaveGuards = 0;
        int32 Elites = 0;
    };

    const auto RunScenario = [this, GruntClass, ProxyClass](UClass* ConfiguredGuardClass, FSpawnCounts& Counts)
    {
        UWorld* World = CreateCastlePresentationTestWorld();
        if (!TestNotNull(TEXT("Transient game world was created"), World))
        {
            return false;
        }
        ON_SCOPE_EXIT { DestroyCastlePresentationTestWorld(*World); };

        ACastleDirector* Director = World->SpawnActor<ACastleDirector>();
        if (!TestNotNull(TEXT("Native Director was spawned"), Director))
        {
            return false;
        }

        // This first-floor seed exercises both ordinary combat rooms and an elite room.
        Director->RunSeed = 24680;
        TestNull(TEXT("Native Director starts with no presentation override"), Director->OrdinaryEnemyClass.Get());
        if (ConfiguredGuardClass)
        {
            Director->OrdinaryEnemyClass = ConfiguredGuardClass;
        }
        UClass* SelectedClass = Director->OrdinaryEnemyClass.Get();
        if (ConfiguredGuardClass && !ConfiguredGuardClass->IsChildOf(ACastleEnemy::StaticClass()))
        {
            TestNull(TEXT("The typed class field rejects an unrelated Actor class"), SelectedClass);
        }
        UClass* ExpectedGuardClass = SelectedClass ? SelectedClass : ACastleEnemy::StaticClass();

        Director->GenerateLayout(nullptr);
        TSet<const ACastleEnemy*> ExplorationEnemies;
        ACastleEnemy* GuardToDamage = nullptr;
        TArray<FString> GuardClasses, CaveClasses, EliteClasses, Owners;
        for (const TObjectPtr<AActor>& Actor : Director->GeneratedActors)
        {
            ACastleEnemy* Enemy = Cast<ACastleEnemy>(Actor.Get());
            if (!IsValid(Enemy))
            {
                continue;
            }
            ExplorationEnemies.Add(Enemy);
            Owners.AddUnique(GetPathNameSafe(Enemy->GetOwner()));
            TestTrue(TEXT("Exploration enemy is configured by its Director with no Actor Owner"), Enemy->Director == Director && Enemy->GetOwner() == nullptr);
            TestFalse(TEXT("Exploration generated a guard or elite, not the boss"), Enemy->bBoss);
            if (Enemy->bElite)
            {
                ++Counts.Elites;
                EliteClasses.AddUnique(Enemy->GetClass()->GetPathName());
                TestEqual(TEXT("Elite uses the configured rifle Grunt class or its native fallback"), Enemy->GetClass(), ExpectedGuardClass);
                TestEqual(TEXT("Elite is twice the size"), Enemy->GetActorScale3D(), FVector(2.));
                TestEqual(TEXT("Elite has 500 maximum and current health"), Enemy->MaxHealth, 500.f);
                TestEqual(TEXT("Elite spawns at full health"), Enemy->Health, 500.f);
                continue;
            }
            ++Counts.ExplorationGuards;
            GuardClasses.AddUnique(Enemy->GetClass()->GetPathName());
            TestEqual(TEXT("Exploration used the expected ordinary class"), Enemy->GetClass(), ExpectedGuardClass);
            if (!GuardToDamage)
            {
                GuardToDamage = Enemy;
            }
        }
        TestTrue(TEXT("Exploration generated the elite fixture"), Counts.Elites > 0);
        TestTrue(TEXT("Exploration generated ordinary guards"), Counts.ExplorationGuards > 0);

        Director->GenerateCave(nullptr);
        for (const TObjectPtr<AActor>& Actor : Director->GeneratedActors)
        {
            ACastleEnemy* Enemy = Cast<ACastleEnemy>(Actor.Get());
            if (!IsValid(Enemy) || ExplorationEnemies.Contains(Enemy))
            {
                continue;
            }
            ++Counts.CaveGuards;
            CaveClasses.AddUnique(Enemy->GetClass()->GetPathName());
            Owners.AddUnique(GetPathNameSafe(Enemy->GetOwner()));
            TestTrue(TEXT("Cave guard is configured by its Director with no Actor Owner"), Enemy->Director == Director && Enemy->GetOwner() == nullptr);
            TestFalse(TEXT("New cave enemy is an ordinary guard"), Enemy->bElite || Enemy->bBoss);
            TestEqual(TEXT("Cave used the expected ordinary class"), Enemy->GetClass(), ExpectedGuardClass);
        }
        TestTrue(TEXT("Cave generated ordinary guards"), Counts.CaveGuards > 0);
        AddInfo(FString::Printf(TEXT("FIX01 spawn seed=%d requested=%s accepted=%s exploration=%d [%s] cave=%d [%s] elite=%d [%s] actorOwners=[%s] director=%s"),
            Director->RunSeed, *GetPathNameSafe(ConfiguredGuardClass), *GetPathNameSafe(SelectedClass),
            Counts.ExplorationGuards, *FString::Join(GuardClasses, TEXT(",")), Counts.CaveGuards, *FString::Join(CaveClasses, TEXT(",")),
            Counts.Elites, *FString::Join(EliteClasses, TEXT(",")), *FString::Join(Owners, TEXT(",")), *Director->GetPathName()));

        if (ConfiguredGuardClass != GruntClass)
        {
            return true;
        }
        if (!TestTrue(TEXT("Generated Grunt survives until the event test"), IsValid(GuardToDamage)) ||
            !TestFalse(TEXT("Global actor event probe is available"), AActor::ProcessEventDelegate.IsBound()))
        {
            return false;
        }
        for (const FName Slot : {FName(TEXT("AttackMontage")), FName(TEXT("HitMontage")), FName(TEXT("DeathMontage")),
            FName(TEXT("AttackSound")), FName(TEXT("HitSound")), FName(TEXT("DeathSound")), FName(TEXT("SoundAttenuation"))})
        {
            const FObjectPropertyBase* Property = FindFProperty<FObjectPropertyBase>(GruntClass, Slot);
            if (!TestNotNull(FString::Printf(TEXT("Grunt exposes %s"), *Slot.ToString()), Property))
            {
                return false;
            }
            Property->SetObjectPropertyValue_InContainer(GuardToDamage, nullptr);
        }

        FActorSpawnParameters Spawn;
        Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        ACastlePlayer* Player = World->SpawnActor<ACastlePlayer>(GuardToDamage->GetActorLocation() + FVector(500, 0, 0), FRotator::ZeroRotator, Spawn);
        if (!TestNotNull(TEXT("Preview target was spawned"), Player))
        {
            return false;
        }

        const int32 InitialKills = Director->Kills;
        const auto CountAmmo = [World]
        {
            int32 Count = 0;
            for (TActorIterator<ACastleInteractable> Pickup(World); Pickup; ++Pickup)
            {
                Count += Pickup->Kind == ECastleInteract::Ammo ? 1 : 0;
            }
            return Count;
        };
        const int32 InitialAmmo = CountAmmo();
        int32 Attacks = 0;
        int32 Hits = 0;
        int32 Deaths = 0;
        // The transient world has no GameMode, but these calls must execute the saved Blueprint script.
        const FEditorScriptExecutionGuard AllowScripts;
        AActor::ProcessEventDelegate.BindLambda([&](AActor* Actor, UFunction* Function, void* Params)
        {
            if (Actor != GuardToDamage)
            {
                return false;
            }
            const FName Event = Function->GetFName();
            if (Event == FName(TEXT("BP_OnAttackVisual")))
            {
                ++Attacks;
                const FBoolProperty* Melee = FindFProperty<FBoolProperty>(Function, TEXT("bMelee"));
                TestTrue(TEXT("The live attack handler received a ranged attack"), Function->Script.Num() > 0 && Melee && !Melee->GetPropertyValue_InContainer(Params));
            }
            else if (Event == FName(TEXT("BP_OnHitVisual")))
            {
                ++Hits;
                const FFloatProperty* HealthLost = FindFProperty<FFloatProperty>(Function, TEXT("HealthLost"));
                TestTrue(TEXT("The live hit handler received actual health loss"), Function->Script.Num() > 0 && HealthLost && HealthLost->GetPropertyValue_InContainer(Params) == 1.f);
            }
            else if (Event == FName(TEXT("BP_OnDeathVisual")))
            {
                ++Deaths;
                TestTrue(TEXT("The live death handler runs once after gameplay settlement and before Destroy"),
                    Function->Script.Num() > 0 && GuardToDamage->bDead && Director->Kills == InitialKills + 1 && IsValid(GuardToDamage));
            }
            return false;
        });
        ON_SCOPE_EXIT { AActor::ProcessEventDelegate.Unbind(); };

        const FDelegateHandle Exceptions = FBlueprintCoreDelegates::OnScriptException.AddLambda(
            [this, World](const UObject* Object, const FFrame&, const FBlueprintExceptionInfo& Info)
            {
                if (Object && Object->GetWorld() == World && Info.GetType() >= EBlueprintExceptionType::AccessViolation)
                {
                    AddError(FString::Printf(TEXT("Empty presentation raised a Blueprint exception: %s"), *Info.GetDescription().ToString()));
                }
            });
        ON_SCOPE_EXIT { FBlueprintCoreDelegates::OnScriptException.Remove(Exceptions); };

        const FVector GuardPosition = GuardToDamage->GetActorLocation();
        Player->SetActorLocation(GuardPosition + FVector(4300, 0, 0));
        TestFalse(TEXT("Ranged guard remains idle beyond its extended range"), GuardToDamage->IsInDecisionRange(Player));
        GuardToDamage->UpdateAttack(Player, true);
        TestEqual(TEXT("Clear sight beyond 4200 cm does not fire"), Attacks, 0);
        Player->SetActorLocation(GuardPosition + FVector(4100, 0, 0));
        TestTrue(TEXT("Ranged guard sees a target within its extended range"), GuardToDamage->IsInDecisionRange(Player));
        GuardToDamage->UpdateAttack(Player, false);
        TestEqual(TEXT("Blocked sight sends no attack notification"), Attacks, 0);
        GuardToDamage->UpdateAttack(Player, true);
        GuardToDamage->UpdateAttack(Player, true);
        TestEqual(TEXT("One committed attack sends one notification despite the immediate retry"), Attacks, 1);
        int32 Projectiles = 0;
        for (TActorIterator<ACastleBolt> Bolt(World); Bolt; ++Bolt)
        {
            if (Bolt->GetOwner() == GuardToDamage && Bolt->Shooter == GuardToDamage)
            {
                ++Projectiles;
            }
        }
        TestEqual(TEXT("The ranged attack still emits exactly one gameplay projectile"), Projectiles, 1);

        const FDamageEvent DamageEvent;
        const float StartingHealth = GuardToDamage->Health;
        GuardToDamage->bActive = false;
        TestEqual(TEXT("Inactive guard ignores damage"), GuardToDamage->TakeDamage(1.f, DamageEvent, nullptr, nullptr), 0.f);
        GuardToDamage->bActive = true;
        GuardToDamage->TakeDamage(0.f, DamageEvent, nullptr, nullptr);
        TestEqual(TEXT("Ignored and zero damage send no hit notification"), Hits, 0);
        TestEqual(TEXT("Nonlethal damage is accepted"), GuardToDamage->TakeDamage(1.f, DamageEvent, nullptr, nullptr), 1.f);
        TestEqual(TEXT("Nonlethal damage reduces health"), GuardToDamage->Health, StartingHealth - 1.f);
        TestEqual(TEXT("Nonlethal damage sends exactly one hit notification"), Hits, 1);
        TestEqual(TEXT("Nonlethal damage does not count as a kill"), Director->Kills, InitialKills);
        Player->SetActorLocation(GuardPosition + FVector(6000, 0, 0));
        Director->bReady = true;
        TestFalse(TEXT("Damage with no player source does not alert the ranged guard"), GuardToDamage->IsInDecisionRange(Player));
        GuardToDamage->TakeDamage(1.f, DamageEvent, nullptr, Player);
        TestTrue(TEXT("Player damage immediately wakes a distant ranged guard"), GuardToDamage->IsInDecisionRange(Player));
        Director->Region = TEXT("TREASURE CAVE");
        TestFalse(TEXT("Alerted guard does not follow the player through a region portal"), GuardToDamage->IsInDecisionRange(Player));
        Director->Region = TEXT("EXPLORATION");
        GuardToDamage->TakeDamage(StartingHealth, DamageEvent, nullptr, nullptr);
        TestFalse(TEXT("Lethal damage destroys the generated Grunt immediately"), IsValid(GuardToDamage));
        TestEqual(TEXT("Lethal damage counts exactly one kill"), Director->Kills, InitialKills + 1);
        TestEqual(TEXT("Lethal damage sends one death notification"), Deaths, 1);
        TestEqual(TEXT("Lethal damage does not also send a hit notification"), Hits, 2);
        const int32 AmmoDelta = CountAmmo() - InitialAmmo;
        TestEqual(TEXT("Defeated guards drop no ammunition"), AmmoDelta, 0);
        const bool bEmptyProxySpawned = bool(TActorIterator<AActor>(World, ProxyClass));
        TestFalse(TEXT("Empty death resources spawn no death proxy"), bEmptyProxySpawned);
        AddInfo(FString::Printf(TEXT("FIX01 empty-slot attack=%d hit=%d death=%d projectile=%d killDelta=%d ammoDropDelta=%d proxy=%d"),
            Attacks, Hits, Deaths, Projectiles, Director->Kills - InitialKills, AmmoDelta, bEmptyProxySpawned));
        return true;
    };

    FSpawnCounts Native;
    FSpawnCounts ExplicitNative;
    FSpawnCounts Configured;
    FSpawnCounts Incompatible;
    if (!RunScenario(nullptr, Native) || !RunScenario(ACastleEnemy::StaticClass(), ExplicitNative) ||
        !RunScenario(AActor::StaticClass(), Incompatible) || !RunScenario(GruntClass, Configured))
    {
        return false;
    }

    TestEqual(TEXT("Blueprint selection preserves the exploration guard count"), Configured.ExplorationGuards, Native.ExplorationGuards);
    TestEqual(TEXT("Blueprint selection preserves the cave guard count"), Configured.CaveGuards, Native.CaveGuards);
    TestEqual(TEXT("Blueprint selection preserves the elite count"), Configured.Elites, Native.Elites);
    TestEqual(TEXT("Explicit native class agrees with the unset exploration fallback"), ExplicitNative.ExplorationGuards, Native.ExplorationGuards);
    TestEqual(TEXT("Explicit native class agrees with the unset cave fallback"), ExplicitNative.CaveGuards, Native.CaveGuards);
    TestEqual(TEXT("Explicit native class preserves the elite count"), ExplicitNative.Elites, Native.Elites);
    TestEqual(TEXT("Rejected class falls back on the exploration entry"), Incompatible.ExplorationGuards, Native.ExplorationGuards);
    TestEqual(TEXT("Rejected class falls back on the cave entry"), Incompatible.CaveGuards, Native.CaveGuards);
    TestEqual(TEXT("Rejected class preserves the elite count"), Incompatible.Elites, Native.Elites);

    UAnimSequenceBase* Idle = LoadObject<UAnimSequenceBase>(nullptr,
        TEXT("/Game/Characters/Mannequins/Anims/Unarmed/MM_Idle.MM_Idle"));
    if (!TestNotNull(TEXT("Shared mannequin Idle loads"), Idle))
    {
        return false;
    }
    UWorld* ProxyWorld = CreateCastlePresentationTestWorld();
    if (!TestNotNull(TEXT("Transient proxy world was created"), ProxyWorld))
    {
        return false;
    }
    bool bWorldHandedOff = false;
    ON_SCOPE_EXIT { if (!bWorldHandedOff) DestroyCastlePresentationTestWorld(*ProxyWorld); };
    ProxyWorld->InitializeActorsForPlay(FURL());
    ProxyWorld->GetWorldSettings()->NotifyBeginPlay();

    FActorSpawnParameters Spawn;
    Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    ACastleEnemy* Enemy = ProxyWorld->SpawnActor<ACastleEnemy>(GruntClass, FVector(0, 0, 100), FRotator(0, 35, 0), Spawn);
    UAnimMontage* TestMontage = UAnimMontage::CreateSlotAnimationAsDynamicMontage(Idle, FName(TEXT("DefaultSlot")), 0.f, 0.f);
    const FObjectPropertyBase* DeathMontage = FindFProperty<FObjectPropertyBase>(GruntClass, TEXT("DeathMontage"));
    if (!TestNotNull(TEXT("Proxy fixture Grunt spawned"), Enemy) || !TestNotNull(TEXT("Transient Idle montage created"), TestMontage) ||
        !TestNotNull(TEXT("Death Montage field was found"), DeathMontage))
    {
        return false;
    }
    DeathMontage->SetObjectPropertyValue_InContainer(Enemy, TestMontage);
    const FTransform SourceMesh = Enemy->GetMesh()->GetComponentTransform();
    const FDamageEvent DamageEvent;
    Enemy->TakeDamage(Enemy->Health, DamageEvent, nullptr, nullptr);
    TestFalse(TEXT("The filled montage does not delay lethal destruction"), IsValid(Enemy));

    AActor* Proxy = nullptr;
    int32 ProxyCount = 0;
    for (TActorIterator<AActor> Actor(ProxyWorld, ProxyClass); Actor; ++Actor)
    {
        Proxy = *Actor;
        ++ProxyCount;
    }
    if (!TestEqual(TEXT("Exactly one death proxy spawned"), ProxyCount, 1))
    {
        return false;
    }
    TestTrue(TEXT("Death proxy has no Pawn, Owner or network identity"), !Proxy->IsA<APawn>() && !Proxy->GetOwner() && !Proxy->GetIsReplicated());
    USkeletalMeshComponent* ProxyMesh = Proxy->FindComponentByClass<USkeletalMeshComponent>();
    if (!TestNotNull(TEXT("Death proxy contains its visual skeletal mesh"), ProxyMesh))
    {
        return false;
    }
    TestTrue(TEXT("Death proxy mesh has no collision, overlaps or navigation effect"),
        ProxyMesh->GetCollisionEnabled() == ECollisionEnabled::NoCollision && !ProxyMesh->GetGenerateOverlapEvents() && !ProxyMesh->CanEverAffectNavigation());
    TestTrue(TEXT("Death proxy uses the dying mesh world transform"), ProxyMesh->GetComponentTransform().Equals(SourceMesh, 0.01f));
    UAnimInstance* ProxyAnim = ProxyMesh->GetAnimInstance();
    TestTrue(TEXT("Death proxy started the transient montage"), ProxyAnim && ProxyAnim->Montage_IsPlaying(TestMontage));
    const float LifeSpan = Proxy->GetLifeSpan();
    if (!TestTrue(TEXT("Successful montage gets a finite lifetime beyond the quarter-second fallback"), FMath::IsFinite(LifeSpan) && LifeSpan > 0.25f && LifeSpan < 60.f))
    {
        return false;
    }
    FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FWaitForCastleDeathProxy>(*this, *ProxyWorld, *Proxy, LifeSpan));
    bWorldHandedOff = true;
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCastleEnemyPresentationSpawnFailureTest,
    "CastleDemo.EnemyPresentation.SpawnFailure",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCastleEnemyPresentationSpawnFailureTest::RunTest(const FString& Parameters)
{
    UClass* GruntClass = LoadClass<ACastleEnemy>(nullptr,
        TEXT("/Game/CastleDemo/Enemies/Presentation/BP_CastleEnemy_Grunt.BP_CastleEnemy_Grunt_C"));
    if (!TestNotNull(TEXT("Saved Grunt Blueprint class loads"), GruntClass))
    {
        return false;
    }
    UWorld* World = CreateCastlePresentationTestWorld();
    if (!TestNotNull(TEXT("Transient failure fixture world was created"), World))
    {
        return false;
    }
    ON_SCOPE_EXIT { DestroyCastlePresentationTestWorld(*World); };

    ACastleDirector* Director = World->SpawnActor<ACastleDirector>();
    if (!TestNotNull(TEXT("Fixture Director was spawned"), Director))
    {
        return false;
    }
    Director->RunSeed = 24680;
    Director->OrdinaryEnemyClass = GruntClass;
    bool bRejected = false;
    const FDelegateHandle Spawned = World->AddOnActorSpawnedHandler(FOnActorSpawned::FDelegate::CreateLambda(
        [GruntClass, &bRejected](AActor* Actor)
        {
            if (!bRejected && Actor->GetClass() == GruntClass)
            {
                // Invalidate one genuine spawn before the Director accepts it.
                bRejected = Actor->Destroy();
            }
        }));
    ON_SCOPE_EXIT { World->RemoveOnActorSpawnedHandler(Spawned); };

    AddExpectedError(TEXT("CASTLE_ENEMY_SPAWN_FAILED Class=BP_CastleEnemy_Grunt_C"),
        EAutomationExpectedErrorFlags::Contains, 1, false);
    Director->GenerateLayout(nullptr);
    TestTrue(TEXT("One returned Grunt was invalidated before tracking"), bRejected);

    int32 Requested = 0;
    for (const FCastleRoom& Room : Director->Rooms)
    {
        Requested += Room.bActive && Room.Type == ECastleRoom::Combat ? 2 + Director->Floor : 0;
    }
    int32 Tracked = 0;
    for (const TObjectPtr<AActor>& Actor : Director->GeneratedActors)
    {
        const ACastleEnemy* Enemy = Cast<ACastleEnemy>(Actor.Get());
        if (!IsValid(Enemy) || Enemy->bElite || Enemy->bBoss)
        {
            continue;
        }
        ++Tracked;
        TestEqual(TEXT("Remaining slots still use the configured Grunt class"), Enemy->GetClass(), GruntClass);
    }
    TestTrue(TEXT("Fixture has ordinary combat slots"), Requested > 0);
    TestEqual(TEXT("One invalid spawn is skipped without retrying as native"), Tracked, Requested - 1);
    AddInfo(FString::Printf(TEXT("FIX01 rejected spawn requested=%d tracked=%d rejected=%d"), Requested, Tracked, bRejected));
    return true;
}

#endif
