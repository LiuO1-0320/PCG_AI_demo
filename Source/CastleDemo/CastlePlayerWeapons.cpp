#include "CastleDemo.h"
#include "CastleLanguage.h"
#include "CastleWeaponProjectile.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/OverlapResult.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
    constexpr float CarbineInterval = .16f;
    constexpr float GatlingInterval = .07f;
    constexpr float SpinUpSeconds = .30f;
    constexpr float KnifeInterval = .46f;
    constexpr float ParryWindow = .25f;
    constexpr float KnifeReach = 185.f;
    constexpr int32 GatlingCapacity = 100;
}

// All parts are local presentation components. The camera determines aim and
// the projectile owns collision, independently of the moving barrel meshes.
void ACastlePlayer::CreateWeaponModels()
{
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Tube(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Block(TEXT("/Engine/BasicShapes/Cube.Cube"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Point(TEXT("/Engine/BasicShapes/Cone.Cone"));
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Iron(TEXT("/Game/Demo/CastleCarbine/SM_ArchiveCarbine/Materials/Carbine_Iron.Carbine_Iron"));
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Brass(TEXT("/Game/Demo/CastleCarbine/SM_ArchiveCarbine/Materials/Carbine_Brass.Carbine_Brass"));
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Steel(TEXT("/Game/Demo/CastleCarbine/SM_ArchiveCarbine/Materials/Carbine_Pewter.Carbine_Pewter"));
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Grip(TEXT("/Game/Demo/CastleCarbine/SM_ArchiveCarbine/Materials/Carbine_Oxblood.Carbine_Oxblood"));

    auto Part = [this](FName Name, USceneComponent* Parent, UStaticMesh* Shape, UMaterialInterface* Material,
                       FVector Position, FVector Size, FRotator Rotation = FRotator::ZeroRotator)
    {
        auto* PartMesh = CreateDefaultSubobject<UStaticMeshComponent>(Name);
        PartMesh->SetupAttachment(Parent);
        PartMesh->SetStaticMesh(Shape);
        PartMesh->SetMaterial(0, Material);
        PartMesh->SetRelativeLocationAndRotation(Position, Rotation);
        PartMesh->SetRelativeScale3D(Size / 100.f);
        PartMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        PartMesh->SetCanEverAffectNavigation(false);
        PartMesh->SetCastShadow(false);
    };
    const FRotator AlongX(-90, 0, 0);
    GatlingModel = CreateDefaultSubobject<USceneComponent>(TEXT("Gatling"));
    GatlingModel->SetupAttachment(Camera);
    GatlingModel->SetRelativeLocation(FVector(39, 19, -19));
    Part(TEXT("Breech"), GatlingModel, Tube.Object, Iron.Object, FVector(2,0,0), FVector(15,15,17), AlongX);
    Part(TEXT("FeedDrum"), GatlingModel, Tube.Object, Grip.Object, FVector(-2,6,-5), FVector(14,14,15), FRotator(0,0,90));
    Part(TEXT("BarrelCollar"), GatlingModel, Tube.Object, Brass.Object, FVector(10,0,0), FVector(14,14,3), AlongX);
    BarrelRotor = CreateDefaultSubobject<USceneComponent>(TEXT("GatlingRotor"));
    BarrelRotor->SetupAttachment(GatlingModel);
    for (int32 Barrel = 0; Barrel < 6; ++Barrel)
    {
        const float Angle = Barrel * UE_PI / 3.f;
        const FVector Position(26, 3.8f * FMath::Cos(Angle), 3.8f * FMath::Sin(Angle));
        Part(*FString::Printf(TEXT("GatlingTube%d"), Barrel), BarrelRotor, Tube.Object, Steel.Object,
             Position, FVector(2.7,2.7,32), AlongX);
    }
    Part(TEXT("BarrelBand"), BarrelRotor, Tube.Object, Brass.Object, FVector(34,0,0), FVector(11.2,11.2,2.2), AlongX);

    KnifeModel = CreateDefaultSubobject<USceneComponent>(TEXT("ParryingKnife"));
    KnifeModel->SetupAttachment(Camera);
    KnifeModel->SetRelativeLocationAndRotation(FVector(32,19,-20), FRotator(34, -8, -12));
    Part(TEXT("KnifeGrip"), KnifeModel, Tube.Object, Grip.Object, FVector(-4,0,0), FVector(3.5,3.5,11), AlongX);
    Part(TEXT("KnifePommel"), KnifeModel, Tube.Object, Brass.Object, FVector(-10,0,0), FVector(4,4,2), AlongX);
    Part(TEXT("KnifeGuard"), KnifeModel, Block.Object, Brass.Object, FVector(2,0,0), FVector(2,3,11));
    Part(TEXT("KnifeBlade"), KnifeModel, Point.Object, Steel.Object, FVector(16,0,0), FVector(6,1.4,28), AlongX);
    GatlingModel->SetVisibility(false, true);
    KnifeModel->SetVisibility(false, true);
}

bool ACastlePlayer::HasWeapon(ECastleWeapon Weapon) const
{
    return Weapon == ECastleWeapon::Carbine || Weapon == ECastleWeapon::Knife ||
        (Weapon == ECastleWeapon::Gatling && bOwnsGatling);
}

void ACastlePlayer::UnlockWeapon(ECastleWeapon Weapon)
{
    if (Weapon == ECastleWeapon::Gatling) bOwnsGatling = true;
    EquipWeapon(Weapon);
}

void ACastlePlayer::EquipWeapon(ECastleWeapon Weapon)
{
    if (!HasWeapon(Weapon) || bDead || bCollectionOpen || EquippedWeapon == Weapon) return;
    FireReleased();
    bReloading = false;
    ParryEnd = 0;
    EquippedWeapon = Weapon;
    RefreshWeaponModels();
}

void ACastlePlayer::EquipCarbine() { EquipWeapon(ECastleWeapon::Carbine); }
void ACastlePlayer::EquipGatling() { EquipWeapon(ECastleWeapon::Gatling); }
void ACastlePlayer::EquipKnife() { EquipWeapon(ECastleWeapon::Knife); }

void ACastlePlayer::RefreshWeaponModels()
{
    Gun->SetVisibility(EquippedWeapon == ECastleWeapon::Carbine);
    GatlingModel->SetVisibility(EquippedWeapon == ECastleWeapon::Gatling, true);
    KnifeModel->SetVisibility(EquippedWeapon == ECastleWeapon::Knife, true);
    Muzzle->SetRelativeLocation(EquippedWeapon == ECastleWeapon::Gatling ? FVector(81,19,-19) : FVector(80,19,-17));
    LastShotAt = -10;
    Muzzle->SetVisibility(false);
}

int32 ACastlePlayer::GetEquippedAmmo() const
{
    if (EquippedWeapon == ECastleWeapon::Knife) return 0;
    return EquippedWeapon == ECastleWeapon::Gatling ? GatlingAmmo : Ammo;
}

int32 ACastlePlayer::GetEquippedReserve() const
{
    if (EquippedWeapon == ECastleWeapon::Knife) return 0;
    return EquippedWeapon == ECastleWeapon::Gatling ? GatlingReserve : Reserve;
}

float ACastlePlayer::GetEquippedDamage() const
{
    if (EquippedWeapon == ECastleWeapon::Knife) return 38.f;
    return EquippedWeapon == ECastleWeapon::Gatling ? BulletDamage * .5f : BulletDamage;
}

void ACastlePlayer::AddReserveAmmo(int32 BaseAmount)
{
    Reserve += BaseAmount;
    if (bOwnsGatling) GatlingReserve += BaseAmount * 2;
}

void ACastlePlayer::RefillMagazines()
{
    Ammo = MagazineSize;
    if (bOwnsGatling) GatlingAmmo = GatlingCapacity;
    bReloading = false;
}

float ACastlePlayer::GetReloadDuration() const
{
    return EquippedWeapon == ECastleWeapon::Gatling ? 2.f : 1.25f;
}

float ACastlePlayer::GetInfiniteAmmoRemaining() const
{
    return FMath::Max(0.f, InfiniteAmmoUntil - GetWorld()->GetTimeSeconds());
}

void ACastlePlayer::ApplyWeaponMod(ECastleWeaponMod Mod)
{
    const bool Chinese = CastleIsChinese();
    switch (Mod)
    {
    case ECastleWeaponMod::RapidFire:
        if (RapidFireStacks >= 3)
        {
            Tell(Chinese ? TEXT("快速扳机已满级") : TEXT("Rapid fire is at maximum level"));
            break;
        }
        ++RapidFireStacks;
        Tell(Chinese ? FString::Printf(TEXT("快速扳机：枪械射速 +%d%%"), RapidFireStacks*15)
                     : FString::Printf(TEXT("Rapid fire: firing rate +%d%%"), RapidFireStacks*15));
        break;
    case ECastleWeaponMod::Spread:
        if (SpreadStacks >= 2)
        {
            Tell(Chinese ? TEXT("扇形散射已满级") : TEXT("Spread is at maximum level"));
            break;
        }
        ++SpreadStacks;
        if (SpreadStacks == 1)
            Tell(Chinese ? TEXT("扇形散射：每枪增加两颗侧弹") : TEXT("Spread: each shot now adds two side rounds"));
        else
            Tell(Chinese ? TEXT("扇形散射 II：侧弹角度扩大一倍") : TEXT("Spread II: side rounds now fan twice as wide"));
        break;
    case ECastleWeaponMod::InfiniteAmmo:
        InfiniteAmmoUntil = GetWorld()->GetTimeSeconds() + 60.f;
        if (bReloading)
        {
            bReloading = false;
            SpinStartedAt = GetWorld()->GetTimeSeconds();
        }
        Tell(Chinese ? TEXT("无限弹药：接下来 60 秒开火无需耗弹或换弹") : TEXT("Infinite ammo: fire without consuming ammo or reloading for 60 seconds"));
        break;
    }
}

void ACastlePlayer::Reload()
{
    if (bDead || bReloading || bCollectionOpen || EquippedWeapon == ECastleWeapon::Knife || GetInfiniteAmmoRemaining() > 0.f) return;
    const bool Gatling = EquippedWeapon == ECastleWeapon::Gatling;
    if (GetEquippedAmmo() >= (Gatling ? GatlingCapacity : MagazineSize)) return;
    if (GetEquippedReserve() <= 0)
    {
        Tell(TEXT("No reserve ammo. Press 3 for the knife or collect supplies."));
        FireReleased();
        return;
    }
    bReloading = true;
    ReloadEnd = GetWorld()->GetTimeSeconds() + GetReloadDuration();
    SpinStartedAt = ReloadEnd;
}

void ACastlePlayer::CompleteReload()
{
    const bool Gatling = EquippedWeapon == ECastleWeapon::Gatling;
    int32& Loaded = Gatling ? GatlingAmmo : Ammo;
    int32& Stock = Gatling ? GatlingReserve : Reserve;
    const int32 Capacity = Gatling ? GatlingCapacity : MagazineSize;
    const int32 Transferred = FMath::Min(Capacity - Loaded, Stock);
    Loaded += Transferred;
    Stock -= Transferred;
    bReloading = false;
}

void ACastlePlayer::FirePressed()
{
    if (bDead || bCollectionOpen || bFiring) return;
    bFiring = true;
    bHelp = false;
    SpinStartedAt = bReloading ? ReloadEnd : GetWorld()->GetTimeSeconds();
    Fire();
}

void ACastlePlayer::FireReleased()
{
    bFiring = false;
}

void ACastlePlayer::Fire()
{
    const float Now = GetWorld()->GetTimeSeconds();
    if (bDead || bReloading || bCollectionOpen || Now < NextShot || IsParrying()) return;
    const ACastleDirector* D = Director();
    if (!D || !D->bReady || D->bWon) return;
    if (EquippedWeapon == ECastleWeapon::Knife)
    {
        Slash();
        NextShot = Now + KnifeInterval;
        return;
    }
    const bool InfiniteAmmo = GetInfiniteAmmoRemaining() > 0.f;
    if (!InfiniteAmmo && GetEquippedAmmo() == 0)
    {
        Reload();
        return;
    }
    const bool Gatling = EquippedWeapon == ECastleWeapon::Gatling;
    if (Gatling && (!bFiring || Now < SpinStartedAt + SpinUpSeconds)) return;
    const float Inaccuracy = Gatling ? .6f + GatlingHeat * 3.4f : 0.f;
    const float FanAngle = SpreadStacks * 6.f;
    if (!ACastleWeaponProjectile::Fire(*this, Muzzle->GetComponentLocation(), GetEquippedDamage(), Inaccuracy, FanAngle)) return;
    if (!InfiniteAmmo)
    {
        int32& Loaded = Gatling ? GatlingAmmo : Ammo;
        --Loaded;
    }
    LastShotAt = Now;
    const float Interval = (Gatling ? GatlingInterval : CarbineInterval) / (1.f + .15f * RapidFireStacks);
    // Carry fractional frame time during continuous fire without banking shots after idle.
    NextShot = (bFiring && Now - NextShot < Interval ? NextShot : Now) + Interval;
    if (Gatling) GatlingHeat = FMath::Min(1.f, GatlingHeat + .065f);
}

void ACastlePlayer::Slash()
{
    const FVector Start = Camera->GetComponentLocation();
    const FVector Forward = Camera->GetForwardVector();
    const FCollisionQueryParams Query(SCENE_QUERY_STAT(CastleKnife), false, this);
    TArray<FOverlapResult> Targets;
    GetWorld()->OverlapMultiByObjectType(Targets, Start, FQuat::Identity, FCollisionObjectQueryParams(ECC_Pawn),
        FCollisionShape::MakeSphere(KnifeReach), Query);
    TSet<ACastleEnemy*> Struck;
    for (const FOverlapResult& Overlap : Targets)
    {
        auto* Enemy = Cast<ACastleEnemy>(Overlap.GetActor());
        if (!Enemy || Enemy->bDead || Struck.Contains(Enemy)) continue;
        Struck.Add(Enemy);
        // Aim toward the capsule axis at eye height, even if standing beside a tall enemy.
        FVector Target = Enemy->GetActorLocation();
        const auto* Capsule = Enemy->GetCapsuleComponent();
        const double AxisHeight = Capsule->GetScaledCapsuleHalfHeight() - Capsule->GetScaledCapsuleRadius();
        Target.Z += FMath::Clamp(Start.Z - Target.Z, -AxisHeight, AxisHeight);
        const FVector Direction = (Target - Start).GetSafeNormal();
        if (FVector::DotProduct(Direction, Forward) < .64f) continue;
        FHitResult Hit;
        if (GetWorld()->LineTraceSingleByChannel(Hit, Start, Target, ECC_Visibility, Query)
            && Hit.GetActor() == Enemy && Hit.Distance <= KnifeReach)
        {
            if (UGameplayStatics::ApplyDamage(Enemy, GetEquippedDamage(), GetController(), this, nullptr) > 0.f)
                HitMarkerUntil = GetWorld()->GetTimeSeconds() + .15f;
        }
    }
    SwingStartedAt = GetWorld()->GetTimeSeconds();
}

bool ACastlePlayer::IsParrying() const
{
    return !bDead && EquippedWeapon == ECastleWeapon::Knife && GetWorld()->GetTimeSeconds() < ParryEnd;
}

void ACastlePlayer::Parry()
{
    if (bDead || bCollectionOpen || EquippedWeapon != ECastleWeapon::Knife) return;
    const float Now = GetWorld()->GetTimeSeconds();
    if (Now < ParryReadyAt || Now - SwingStartedAt < .2f) return;
    const ACastleDirector* D = Director();
    if (!D || !D->bReady || D->bWon) return;
    bHelp = false;
    ParryEnd = Now + ParryWindow;
    ParryReadyAt = Now + .68f;
}

void ACastlePlayer::UpdateWeaponModels(float Now, float Dt)
{
    const bool Gatling = EquippedWeapon == ECastleWeapon::Gatling;
    const bool Spinning = Gatling && bFiring && !bReloading && !bDead;
    if (!Spinning) GatlingHeat = FMath::Max(0.f, GatlingHeat - Dt * 1.4f);
    if (Gatling)
    {
        const float Spin = Spinning ? FMath::Clamp((Now - SpinStartedAt) / SpinUpSeconds, 0.f, 1.f) : GatlingHeat;
        BarrelRotor->AddLocalRotation(FRotator(0, 0, Dt * Spin * 980.f));
    }
    const float Kick = FMath::Clamp(1.f - (Now - LastShotAt) / .13f, 0.f, 1.f);
    Gun->SetRelativeLocation(FVector(48 - Kick * 6,19,-18 + Kick * 2));
    GatlingModel->SetRelativeLocation(FVector(39 - Kick * 2,19,-19));
    Muzzle->SetVisibility(EquippedWeapon != ECastleWeapon::Knife && !bDead && Now - LastShotAt < .032f);
    if (EquippedWeapon == ECastleWeapon::Knife)
    {
        const float Swing = FMath::Sin(UE_PI * FMath::Clamp((Now - SwingStartedAt) / .32f, 0.f, 1.f));
        const float Guard = FMath::Clamp((ParryEnd + .12f - Now) / .12f, 0.f, 1.f)
            * FMath::Clamp((Now - (ParryEnd - ParryWindow)) / .045f, 0.f, 1.f);
        const float Raised = ParryEnd > 0.f ? Guard : 0.f;
        KnifeModel->SetRelativeLocationAndRotation(FVector(32 + 8*Swing,19 - 17*Swing - 9*Raised,-20 + 7*Raised),
            FRotator(34 - 45*Swing - 8*Raised, -8 - 60*Swing - 55*Raised, -12 - 20*Raised));
    }
}
