#include "CastleDemo.h"
#include "CastleWeaponProjectile.h"
#include "Camera/CameraComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMeshActor.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerInput.h"
#include "Misc/ScopeExit.h"

const TCHAR* ACastleDirector::CheckPlayerWeapons(ACastlePlayer* P)
{
    UWorld* World = GetWorld();
    const auto AimAt = [&](FVector Position)
    {
        const FRotator Aim = (Position - P->Camera->GetComponentLocation()).Rotation();
        P->GetController()->SetControlRotation(Aim);
        P->Camera->SetWorldRotation(Aim);
    };
    const auto FindBullet = [&]() -> ACastleWeaponProjectile*
    {
        for (TActorIterator<ACastleWeaponProjectile> It(World); It; ++It)
            if (It->Shooter == P && !It->IsActorBeingDestroyed()) return *It;
        return nullptr;
    };

    const auto* PC = CastChecked<APlayerController>(P->GetController());
    const auto& Bindings = PC->PlayerInput->GetKeysForAction(TEXT("Interact"));
    if (!Bindings.ContainsByPredicate([](const FInputActionKeyMapping& Mapping) { return Mapping.Key == EKeys::F; }))
        return TEXT("F is missing from the active interact mapping");
    for (const auto Kind : {ECastleInteract::Gatling, ECastleInteract::Knife})
    {
        ACastleInteractable* Pickup = nullptr;
        for (TActorIterator<ACastleInteractable> It(World); It; ++It)
            if (It->Kind == Kind) { Pickup = *It; break; }
        if (!Pickup || FVector::Dist2D(Pickup->GetActorLocation(), Rooms[0].Center) > 500.)
            return TEXT("Weapon pickup missing from entrance");
        Travel(P, Pickup->GetActorLocation() + FVector(0,-170,55));
        AimAt(Pickup->GetActorLocation());
        if (P->Focused() != Pickup) return TEXT("Entrance weapon cannot be focused");
        P->Interact();
        if (!Pickup->bUsed || P->EquippedWeapon != (Kind == ECastleInteract::Knife ? ECastleWeapon::Knife : ECastleWeapon::Gatling))
            return TEXT("Entrance pickup did not equip the weapon");
    }

    // Keep the shot path independent of decorations and randomly spawned guards.
    const FVector Origin(0,0,6500);
    Travel(P, Origin);
    const FVector Eye = P->Camera->GetComponentLocation();
    auto* Enemy = World->SpawnActor<ACastleEnemy>(Origin + FVector(750,0,0), FRotator::ZeroRotator);
    if (!Enemy) return TEXT("Weapon test guard spawn failed");
    auto* GuardController = Enemy->GetController();
    ON_SCOPE_EXIT { Enemy->Destroy(); if (IsValid(GuardController)) GuardController->Destroy(); };
    Enemy->bActive = true;
    Enemy->Health = Enemy->MaxHealth = 1000.f;
    AimAt(Enemy->GetActorLocation());
    P->EquipWeapon(ECastleWeapon::Carbine);
    const int32 StartingAmmo = P->Ammo;
    const float StartingHealth = Enemy->Health;
    P->NextShot = 0;
    P->Fire();
    auto* Shot = FindBullet();
    if (!Shot || P->Ammo != StartingAmmo - 1 || Enemy->Health != StartingHealth)
        return TEXT("Expected a flying carbine round before impact");
    Shot->Tick(.01f);
    if (Shot->IsActorBeingDestroyed() || Enemy->Health != StartingHealth) return TEXT("Carbine round hit before arrival");
    Shot->Tick(.2f);
    if (!Shot->IsActorBeingDestroyed() || Enemy->Health != StartingHealth - P->BulletDamage)
        return TEXT("Carbine sweep did not deliver one impact");

    auto* Wall = World->SpawnActor<AStaticMeshActor>(Origin + FVector(320,0,40), FRotator::ZeroRotator);
    if (!Wall) return TEXT("Weapon test cover spawn failed");
    auto* Mesh = Wall->GetStaticMeshComponent();
    Mesh->SetMobility(EComponentMobility::Movable);
    Mesh->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));
    Mesh->SetWorldScale3D(FVector(.05,3,3));
    Mesh->SetCollisionProfileName(TEXT("BlockAll"));
    Mesh->SetCanEverAffectNavigation(false);
    const float BehindCover = Enemy->Health;
    P->NextShot = 0;
    P->Fire();
    Shot = FindBullet();
    if (!Shot) return TEXT("Expected a round between shooter and wall");
    Shot->Tick(.4f);
    if (!Shot->IsActorBeingDestroyed() || Enemy->Health != BehindCover) return TEXT("Bullet crossed thin cover");
    Wall->SetActorLocation(Origin + FVector(42,0,40));
    P->NextShot = 0;
    P->Fire();
    if (FindBullet() || Enemy->Health != BehindCover) return TEXT("Muzzle clipped through close cover");
    Wall->Destroy();

    P->EquipWeapon(ECastleWeapon::Gatling);
    const int32 CarbineRounds = P->Ammo;
    P->NextShot = 0;
    P->FirePressed();
    if (P->GetEquippedAmmo() != 100 || FindBullet()) return TEXT("Gatling fired before spin-up");
    P->SpinStartedAt -= .31f;
    P->Fire();
    P->FireReleased();
    Shot = FindBullet();
    if (!Shot || P->GetEquippedAmmo() != 99 || P->Ammo != CarbineRounds ||
        !FMath::IsNearlyEqual(Shot->Damage, P->BulletDamage * .5f)) return TEXT("Gatling shot or independent ammo");
    Shot->Tick(.3f);
    P->Reload();
    if (!P->bReloading) return TEXT("Gatling reload did not start");
    P->EquipWeapon(ECastleWeapon::Knife);
    P->EquipWeapon(ECastleWeapon::Gatling);
    if (P->bReloading || P->GetEquippedAmmo() != 99) return TEXT("Switching completed an unfinished reload");
    P->Reload();
    const int32 GatlingStock = P->GetEquippedReserve();
    P->ReloadEnd = World->GetTimeSeconds();
    P->Tick(.001f);
    if (P->GetEquippedAmmo() != 100 || P->GetEquippedReserve() != GatlingStock - 1 || P->Ammo != CarbineRounds)
        return TEXT("Gatling reload transferred the wrong ammunition");

    P->EquipWeapon(ECastleWeapon::Knife);
    Enemy->SetActorLocation(Origin + FVector(125,0,0));
    AimAt(Enemy->GetActorLocation());
    const float BeforeSlash = Enemy->Health;
    P->NextShot = 0;
    P->Fire();
    if (Enemy->Health >= BeforeSlash) return TEXT("Knife missed guard within reach");
    Enemy->SetActorLocation(Origin + FVector(750,0,0));
    AimAt(Enemy->GetActorLocation() + FVector(0,0,64));
    const float BeforeReturn = Enemy->Health;
    const float Shield = P->Shield, Health = P->Health;
    P->SwingStartedAt -= .21f;
    P->NextShot = P->ParryReadyAt = 0;
    P->Parry();
    if (!P->IsParrying()) return TEXT("Knife guard window did not open");
    auto* Bolt = World->SpawnActor<ACastleBolt>(Eye + FVector(250,0,-28), FRotator::ZeroRotator);
    if (!Bolt) return TEXT("Incoming bolt spawn failed");
    Bolt->Shooter = Enemy;
    Bolt->Velocity = FVector(-1000,0,0);
    Bolt->Damage = 12;
    Bolt->Tick(.20f);
    if (Bolt->Shooter != P || Bolt->GetOwner() != P || Bolt->Velocity.X <= 0) return TEXT("Blade did not return incoming bolt");
    Bolt->Tick(.75f);
    if (Enemy->Health != BeforeReturn - 12 || P->Health != Health || P->Shield != Shield)
        return TEXT("Returned bolt failed to damage its original shooter");
    P->ParryEnd = 0;
    P->Parry();
    if (P->IsParrying()) return TEXT("Parry ignored its cooldown");
    P->EquipWeapon(ECastleWeapon::Carbine);
    UE_LOG(LogTemp, Display, TEXT("CASTLE_WEAPONS_OK: entrance pickups, F mapping, delayed damage, thin/close cover, Gatling spin and independent reload, slash and reflected hit."));
    return nullptr;
}
