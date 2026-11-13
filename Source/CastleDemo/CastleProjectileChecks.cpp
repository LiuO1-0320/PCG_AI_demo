#include "CastleProjectileChecks.h"
#include "CastleDemo.h"
#include "AIController.h"
#include "BrainComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/ScopeExit.h"

bool CastleCheckEnemyProjectiles(ACastleDirector* Director, ACastlePlayer* Player, FString& FailureReason)
{
    FailureReason.Empty();
    if(!IsInGameThread() || !IsValid(Director) || !IsValid(Player) || !Director->GetWorld() ||
        Director->GetWorld()!=Player->GetWorld() || !Director->GetWorld()->IsGameWorld() || Director->bWon)
    {
        FailureReason=TEXT("Projectile check requires the active director and player in the same game world.");
        return false;
    }
    UWorld* World=Director->GetWorld();
    auto* Capsule=Player->GetCapsuleComponent();
    auto* Movement=Player->GetCharacterMovement();
    if(!Capsule || !Movement || Capsule->GetCollisionResponseToChannel(ECC_Visibility)!=ECR_Block)
    {
        FailureReason=TEXT("Player capsule must block Visibility, the enemy projectile sweep channel.");
        return false;
    }

    const FTransform SavedTransform=Player->GetActorTransform();
    const float SavedHealth=Player->Health,SavedMaxHealth=Player->MaxHealth;
    const float SavedShield=Player->Shield,SavedMaxShield=Player->MaxShield,SavedHurtUntil=Player->HurtUntil;
    const bool SavedDead=Player->bDead,SavedCollision=Player->GetActorEnableCollision();
    const EMovementMode SavedMovementMode=Movement->MovementMode;
    const uint8 SavedCustomMode=Movement->CustomMovementMode;
    const FVector SavedVelocity=Movement->Velocity;
    TArray<TWeakObjectPtr<AActor>> OwnedActors;
    auto ClearOwned=[&]()
    {
        // Bolts are added last, then controllers, so cleanup precedes the shooter.
        for(int32 I=OwnedActors.Num()-1;I>=0;--I)
            if(auto* Actor=OwnedActors[I].Get(); IsValid(Actor) && !Actor->IsActorBeingDestroyed())Actor->Destroy();
        OwnedActors.Reset();
    };
    ON_SCOPE_EXIT
    {
        ClearOwned();
        if(IsValid(Player))
        {
            Player->SetActorTransform(SavedTransform,false,nullptr,ETeleportType::TeleportPhysics);
            Player->SetActorEnableCollision(SavedCollision);
            Player->Health=SavedHealth;Player->MaxHealth=SavedMaxHealth;
            Player->Shield=SavedShield;Player->MaxShield=SavedMaxShield;
            Player->bDead=SavedDead;Player->HurtUntil=SavedHurtUntil;
            Movement->SetMovementMode(SavedMovementMode,SavedCustomMode);
            Movement->Velocity=SavedVelocity;
        }
    };

    const FVector TestOrigin(0,0,5000);
    Movement->StopMovementImmediately();Movement->DisableMovement();
    Player->SetActorEnableCollision(true);
    FActorSpawnParameters Spawn;
    Spawn.Owner=Director;
    Spawn.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

    auto CheckVolley=[&](const TCHAR* Label,bool Boss,float InitialShield,bool WithWall,bool Dodge)
    {
        ClearOwned();
        Player->SetActorLocation(TestOrigin,false,nullptr,ETeleportType::TeleportPhysics);
        Player->Health=Player->MaxHealth=100;Player->MaxShield=60;Player->Shield=InitialShield;Player->bDead=false;
        auto* Enemy=World->SpawnActor<ACastleEnemy>(TestOrigin+FVector(-1200,0,0),FRotator::ZeroRotator,Spawn);
        if(!Enemy){FailureReason=FString(Label)+TEXT(": could not spawn the real enemy.");return false;}
        OwnedActors.Add(Enemy);
        Enemy->Configure(Boss,Boss?2:1,Director);
        Enemy->SetActorTickEnabled(false);Enemy->GetCharacterMovement()->DisableMovement();
        if(auto* AI=Cast<AAIController>(Enemy->GetController()))
        {
            OwnedActors.Add(AI);AI->StopMovement();
            if(AI->GetBrainComponent())AI->GetBrainComponent()->StopLogic(TEXT("Isolated projectile collision check"));
        }
        Enemy->bActive=true;

        if(WithWall)
        {
            auto* Wall=World->SpawnActor<AStaticMeshActor>(TestOrigin+FVector(-600,0,0),FRotator::ZeroRotator,Spawn);
            if(!Wall){FailureReason=FString(Label)+TEXT(": could not spawn blocking wall.");return false;}
            OwnedActors.Add(Wall);
            auto* Mesh=Wall->GetStaticMeshComponent();Mesh->SetMobility(EComponentMobility::Movable);
            Mesh->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));
            Mesh->SetWorldScale3D(FVector(1,8,8));Mesh->SetCollisionProfileName(TEXT("BlockAll"));
            Mesh->SetCanEverAffectNavigation(false);
        }

        // Exercise the actual distance branch and ShootAt spawn/aim/velocity setup.
        // 1200 cm is well outside the 140 cm direct-melee branch.
        Enemy->UpdateAttack(Player,true);
        TArray<TWeakObjectPtr<ACastleBolt>> Bolts;
        for(TActorIterator<ACastleBolt> It(World);It;++It)
            if(It->Shooter==Enemy && !It->IsActorBeingDestroyed())
            {
                It->SetActorTickEnabled(false);Bolts.Add(*It);OwnedActors.Add(*It);
            }
        const int32 ExpectedCount=Boss?3:1;
        if(Bolts.Num()!=ExpectedCount || Enemy->AILastVolley!=ExpectedCount)
        {
            FailureReason=FString::Printf(TEXT("%s: expected %d spawned projectiles, got %d (volley %d)."),
                Label,ExpectedCount,Bolts.Num(),Enemy->AILastVolley);
            return false;
        }
        if(Dodge)Player->SetActorLocation(TestOrigin+FVector(0,400,0),false,nullptr,ETeleportType::TeleportPhysics);
        // One long movement step crosses the target. Tick performs the real world
        // sphere sweep, testing collision at range as well as low-frame-rate travel.
        for(const auto& WeakBolt:Bolts)
            if(auto* Bolt=WeakBolt.Get(); IsValid(Bolt) && !Bolt->IsActorBeingDestroyed())Bolt->Tick(2.f);

        const float ExpectedDamage=(WithWall||Dodge)?0.f:(Boss?22.f:10.f);
        const float ExpectedShield=FMath::Max(0.f,InitialShield-ExpectedDamage);
        const float ExpectedHealth=100-FMath::Max(0.f,ExpectedDamage-InitialShield);
        if(!FMath::IsNearlyEqual(Player->Shield,ExpectedShield,.01f) ||
            !FMath::IsNearlyEqual(Player->Health,ExpectedHealth,.01f))
        {
            FailureReason=FString::Printf(TEXT("%s: shield %.2f / expected %.2f, health %.2f / expected %.2f."),
                Label,Player->Shield,ExpectedShield,Player->Health,ExpectedHealth);
            return false;
        }
        if(WithWall && Bolts.ContainsByPredicate([](const auto& Bolt){return Bolt.IsValid()&&!Bolt->IsActorBeingDestroyed();}))
        {FailureReason=FString(Label)+TEXT(": projectile did not stop at the blocking wall.");return false;}
        if(Dodge && !Bolts.ContainsByPredicate([](const auto& Bolt){return Bolt.IsValid()&&!Bolt->IsActorBeingDestroyed();}))
        {FailureReason=FString(Label)+TEXT(": unobstructed missed projectile was unexpectedly destroyed.");return false;}
        return true;
    };

    if(!CheckVolley(TEXT("Guard ranged shield hit"),false,60,false,false) ||
        !CheckVolley(TEXT("Guard ranged health hit"),false,0,false,false) ||
        !CheckVolley(TEXT("Boss central fan hit"),true,60,false,false) ||
        !CheckVolley(TEXT("Wall blocks projectile"),false,60,true,false) ||
        !CheckVolley(TEXT("Sidestep avoids projectile"),false,60,false,true))return false;

    UE_LOG(LogTemp,Display,TEXT("CASTLE_PROJECTILE_COLLISION_OK: real ranged guard/boss shots at 1200cm, shield/health damage, blocking wall, sidestep and long-step sweeps verified."));
    return true;
}
