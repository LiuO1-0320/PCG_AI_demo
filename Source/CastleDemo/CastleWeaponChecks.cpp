#include "CastleDemo.h"
#include "CastleWeaponProjectile.h"
#include "CastleGuardLoot.h"
#include "Camera/CameraComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMeshActor.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerInput.h"
#include "Components/InputComponent.h"
#include "HAL/PlatformTime.h"
#include "InputKeyEventArgs.h"
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
    ACastleInteractable* Gatling = nullptr;
    for (TActorIterator<ACastleInteractable> It(World); It; ++It)
        if (It->Kind == ECastleInteract::Gatling)
        {
            if (Gatling) return TEXT("More than one Gatling in the vault");
            Gatling = *It;
        }
    if (!Gatling || !Gatling->ActorHasTag(TEXT("CastleTreasureCave")) ||
        FVector::Dist2D(Gatling->GetActorLocation(), CaveExit) > 900.)
        return TEXT("Gatling missing from the final room of the second-floor vault");
    if (P->HasWeapon(ECastleWeapon::Gatling)) return TEXT("Gatling unlocked before reaching the vault reward");
    Travel(P, Gatling->GetActorLocation() + FVector(0,-170,55));
    AimAt(Gatling->GetActorLocation());
    if (P->Focused() != Gatling) return TEXT("Vault Gatling cannot be focused");
    P->Interact();
    if (!Gatling->bUsed || P->EquippedWeapon != ECastleWeapon::Gatling)
        return TEXT("Vault pickup did not equip the Gatling");

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
    if (!FMath::IsNearlyEqual(P->NextShot - World->GetTimeSeconds(), .16f, .001f))
        return TEXT("Unmodified carbine firing interval");
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
    Gatling->Use(P);
    if (P->EquippedWeapon != ECastleWeapon::Carbine) return TEXT("Vault Gatling was collectible a second time");
    UE_LOG(LogTemp, Display, TEXT("CASTLE_WEAPONS_OK: second-floor vault pickup, F mapping, delayed damage, thin/close cover, Gatling spin and independent reload, slash and reflected hit."));
    return nullptr;
}

const TCHAR* ACastleDirector::CheckGuardLoot(ACastlePlayer* P)
{
    const FVector Origin(0,0,6500), Target = Origin + FVector(125,0,0);
    Travel(P, Origin);
    const FRotator Aim = (Target - P->Camera->GetComponentLocation()).Rotation();
    P->GetController()->SetControlRotation(Aim);
    P->Camera->SetWorldRotation(Aim);
    P->EquipWeapon(ECastleWeapon::Carbine);
    const int32 Loaded = P->Ammo, GatlingStock = P->GatlingReserve;
    P->Ammo = P->Reserve = 0;
    P->NextShot = 0;
    P->Fire();
    if (P->bReloading || P->Ammo != 0) return TEXT("An empty carbine reloaded without reserves");
    P->EquipWeapon(ECastleWeapon::Knife);

    // Pick one deterministic kill ordinal per outcome. All four cases still use
    // the real melee hit, guard death, generated pickup, and interaction logic.
    bool Seen[4] = {};
    int32 Outcomes = 0;
    const int32 FirstOrdinal = GuardKillOrdinal + 1;
    for (int32 Ordinal = FirstOrdinal; Ordinal < FirstOrdinal + 512 && Outcomes < 4; ++Ordinal)
    {
        const auto Roll = CastleRollGuardMod(RunSeed, Floor, Ordinal);
        const int32 Slot = Roll.IsSet() ? static_cast<int32>(Roll.GetValue()) + 1 : 0;
        if (Slot < 0 || Slot >= 4) return TEXT("Invalid guard loot roll");
        if (Seen[Slot]) continue;
        Seen[Slot] = true;
        ++Outcomes;
        GuardKillOrdinal = Ordinal - 1;
        auto* Enemy = AddEnemy(Target, false);
        if (!Enemy) return TEXT("Loot check guard spawn failed");
        Enemy->Health = 1;
        Enemy->bActive = true;
        const int32 StartingCoins = P->Coins;
        P->NextShot = 0;
        P->Fire();
        if (!Enemy->bDead || P->Coins != StartingCoins + 12 || GuardKillOrdinal != Ordinal)
            return TEXT("Melee fallback or guard kill accounting failed");

        ACastleInteractable* Drop = nullptr;
        for (TActorIterator<ACastleInteractable> It(GetWorld()); It; ++It)
        {
            if (It->bUsed || FVector::Dist2D(It->GetActorLocation(), Target) > 1.) continue;
            if (It->Kind == ECastleInteract::Ammo) return TEXT("A guard still dropped ammunition");
            if (It->Kind != ECastleInteract::WeaponMod) continue;
            if (Drop) return TEXT("Guard generated multiple weapon mods");
            Drop = *It;
        }
        if (Roll.IsSet() != (Drop != nullptr)) return TEXT("Guard did not honor its loot roll");
        if (Drop)
        {
            if (Drop->RewardKind != static_cast<int32>(Roll.GetValue())) return TEXT("Wrong mod spawned on guard death");
            P->Notice.Reset();
            const int32 OldRate = P->RapidFireStacks, OldSpread = P->SpreadStacks;
            const FRotator PickupAim = (Drop->GetActorLocation() - P->Camera->GetComponentLocation()).Rotation();
            P->GetController()->SetControlRotation(PickupAim);
            P->Camera->SetWorldRotation(PickupAim);
            if (P->Focused() != Drop) return TEXT("Dropped weapon mod cannot be focused from pickup range");
            auto* PC = CastChecked<APlayerController>(P->GetController());
            const TArray<UInputComponent*> InputStack{P->InputComponent.Get()};
            PC->InputKey(FInputKeyEventArgs(nullptr,FInputDeviceId::CreateFromInternalId(0),EKeys::F,IE_Pressed,1.f,false,FPlatformTime::Cycles64()));
            PC->PlayerInput->ProcessInputStack(InputStack,1.f/60.f,false);
            PC->InputKey(FInputKeyEventArgs(nullptr,FInputDeviceId::CreateFromInternalId(0),EKeys::F,IE_Released,0.f,false,FPlatformTime::Cycles64()));
            PC->PlayerInput->ProcessInputStack(InputStack,1.f/60.f,false);
            if (!Drop->bUsed) return TEXT("F did not collect a focused weapon mod");
            Drop->Use(P);
            P->GetController()->SetControlRotation(Aim);
            P->Camera->SetWorldRotation(Aim);
            if (!Drop->bUsed || P->Notice.IsEmpty() ||
                P->RapidFireStacks != OldRate + (Roll.GetValue() == ECastleWeaponMod::RapidFire ? 1 : 0) ||
                P->SpreadStacks != OldSpread + (Roll.GetValue() == ECastleWeaponMod::Spread ? 1 : 0))
                return TEXT("Weapon mod pickup feedback or single-use stacking failed");
            if (Roll.GetValue() == ECastleWeaponMod::InfiniteAmmo && !FMath::IsNearlyEqual(P->GetInfiniteAmmoRemaining(),60.f,.01f))
                return TEXT("Infinite ammo pickup did not start a sixty-second timer");
        }
    }
    if (Outcomes != 4 || P->Reserve != 0 || P->GatlingReserve != GatlingStock)
        return TEXT("Expected all three mod drops and an empty drop, without gun ammunition");

    ACastleInteractable* Supplies = nullptr;
    for (TActorIterator<ACastleInteractable> It(GetWorld()); It; ++It)
        if (!It->bUsed && It->Kind == ECastleInteract::Ammo && It->Title == TEXT("STARTER SUPPLIES")) { Supplies = *It; break; }
    if (!Supplies) return TEXT("Fixed entrance supplies missing");
    Supplies->Use(P);
    if (P->Reserve != 36 || P->GatlingReserve != GatlingStock + (P->HasWeapon(ECastleWeapon::Gatling) ? 72 : 0))
        return TEXT("Fixed supplies granted the wrong reserves");
    P->EquipWeapon(ECastleWeapon::Carbine);
    P->Ammo = Loaded;
    UE_LOG(LogTemp, Display, TEXT("CASTLE_GUARD_LOOT_OK Floor=%d KnifeKill=1 Mods=3 EmptyDrop=1 GuardAmmo=0"), Floor);
    return nullptr;
}

const TCHAR* ACastleDirector::CheckWeaponMods(ACastlePlayer* P)
{
    const float Now = GetWorld()->GetTimeSeconds();
    P->Camera->SetWorldRotation(FRotator::ZeroRotator);
    // Target open space and examine the actual rounds from one trigger pull.
    auto CheckSalvo = [&](int32 Levels, float Interval, bool Infinite)
    {
        const int32 Loaded = P->Ammo, Stock = P->Reserve;
        P->NextShot = 0;
        P->Fire();
        TArray<float> WingYaw;
        float CentralYaw = 0;
        int32 Centers = 0;
        for (TActorIterator<ACastleWeaponProjectile> It(GetWorld()); It; ++It)
        {
            if (It->Shooter != P || It->IsActorBeingDestroyed()) continue;
            const float Yaw = It->Velocity.Rotation().Yaw;
            if (FMath::IsNearlyEqual(It->Damage,P->BulletDamage)) { ++Centers; CentralYaw = Yaw; }
            else if (FMath::IsNearlyEqual(It->Damage,.25f*P->BulletDamage)) WingYaw.Add(Yaw);
            else return false;
            It->Destroy();
        }
        if (Centers != 1 || WingYaw.Num() != 2 || P->Ammo != Loaded - (Infinite ? 0 : 1) || P->Reserve != Stock ||
            !FMath::IsNearlyEqual(P->NextShot - Now,Interval,.001f)) return false;
        for (float& Yaw : WingYaw) Yaw = FMath::FindDeltaAngleDegrees(CentralYaw,Yaw);
        WingYaw.Sort();
        return FMath::IsNearlyEqual(WingYaw[0],-6.f*Levels,.1f) && FMath::IsNearlyEqual(WingYaw[1],6.f*Levels,.1f);
    };
    if (P->RapidFireStacks != 1 || P->SpreadStacks != 1) return TEXT("Missing mods from guard pickups");
    P->InfiniteAmmoUntil = Now;
    P->Ammo = 0;
    P->Reload();
    if (!P->bReloading) return TEXT("Empty magazine should start reloading before unlimited ammo");
    const int32 BeforePickup = P->Reserve;
    P->ApplyWeaponMod(ECastleWeaponMod::InfiniteAmmo);
    if (P->bReloading || P->Ammo != 0 || P->Reserve != BeforePickup)
        return TEXT("Infinite ammo pickup should cancel reload without moving ammunition");
    P->InfiniteAmmoUntil = Now + 1.f;
    P->ApplyWeaponMod(ECastleWeaponMod::InfiniteAmmo);
    if (!FMath::IsNearlyEqual(P->GetInfiniteAmmoRemaining(),60.f,.01f)) return TEXT("Another charge should refresh infinite ammo to 60 seconds");
    if (!CheckSalvo(1,.16f/1.15f,true)) return TEXT("Infinite ammo must fire a three-round fan from an empty magazine");
    for (int32 I = 0; I < 3; ++I)
    {
        P->ApplyWeaponMod(ECastleWeaponMod::RapidFire);
        P->ApplyWeaponMod(ECastleWeaponMod::Spread);
    }
    if (P->RapidFireStacks != 3 || P->SpreadStacks != 2) return TEXT("Weapon mod stacking limits");
    P->InfiniteAmmoUntil = Now-.01f;
    P->Ammo = 2;
    if (P->GetInfiniteAmmoRemaining() > 0 || !CheckSalvo(2,.16f/1.45f,false))
        return TEXT("Expired infinite ammo should spend one round for a wider three-round fan");
    UE_LOG(LogTemp,Display,TEXT("CASTLE_WEAPON_MODS_OK RapidFireMax=3 SpreadMax=2 InfiniteRefresh=60 ExpiryConsumesAmmo=1"));
    return nullptr;
}
