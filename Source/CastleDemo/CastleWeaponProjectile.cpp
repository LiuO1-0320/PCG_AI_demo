#include "CastleWeaponProjectile.h"
#include "CastleQueen.h"
#include "QueenBlinkEffect.h"
#include "Camera/CameraComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInterface.h"
#include "Sound/SoundBase.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
    constexpr float ShotRange = 14000.f;
    constexpr float ShotSpeed = 7000.f;
    constexpr float ShotRadius = 2.5f;

    FVector CrosshairTarget(const ACastlePlayer& Player)
    {
        const FVector Eye = Player.Camera->GetComponentLocation();
        const FVector End = Eye + Player.Camera->GetForwardVector() * ShotRange;
        FHitResult Hit;
        FCollisionQueryParams Query(SCENE_QUERY_STAT(CastleBulletAim), false, &Player);
        if (Player.GetWorld()->LineTraceSingleByChannel(Hit, Eye, End, ECC_Visibility, Query))
            return Hit.ImpactPoint;
        return End;
    }
}

ACastleBolt::ACastleBolt()
{
    PrimaryActorTick.bCanEverTick = true;
    InitialLifeSpan = 6.f;
    Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Bolt"));
    SetRootComponent(Mesh);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Danger(TEXT("/Game/Demo/Materials/M_Danger.M_Danger"));
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Teal(TEXT("/Game/Demo/Materials/M_Teal.M_Teal"));
    static ConstructorHelpers::FObjectFinder<USoundBase> Ping(TEXT("/Engine/EngineSounds/1kSineTonePing.1kSineTonePing"));
    ReflectedMaterial = Teal.Object;
    ParrySound = Ping.Object;
    Mesh->SetStaticMesh(Sphere.Object);
    Mesh->SetMaterial(0, Danger.Object);
    Mesh->SetRelativeScale3D(FVector(.16f));
    Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Mesh->SetCanEverAffectNavigation(false);
    Mesh->SetCastShadow(false);
}

void ACastleBolt::HitPlayer(ACastlePlayer& Player)
{
    Player.ReceiveBoltHit(Damage, Shooter);
}

void ACastleBolt::ResolveImpact(const FHitResult& Hit)
{
    if (auto* Player = Cast<ACastlePlayer>(Shooter))
    {
        if (auto* Enemy = Cast<ACastleEnemy>(Hit.GetActor()))
        {
            bool bRegistered = false;
            if (auto* Queen = Cast<ACastleQueen>(Enemy); Queen && Queen->Action == 3)
                bRegistered = ShieldBreakLevel != INDEX_NONE && Queen->HandleShieldShot(Hit, ShieldBreakLevel);
            else
                bRegistered = UGameplayStatics::ApplyDamage(Enemy, Damage, Player->GetController(), Player, nullptr) > 0.f;
            if (bRegistered) Player->HitMarkerUntil = GetWorld()->GetTimeSeconds() + .15f;
        }
    }
    else if (auto* Target = Cast<ACastlePlayer>(Hit.GetActor()))
    {
        HitPlayer(*Target);
    }
    Destroy();
}

bool ACastleBolt::TryParry(ACastlePlayer& Player, FVector Start, FVector End, FVector& Contact)
{
    if (!Player.IsParrying()) return false;
    const FVector Forward = Player.Camera->GetForwardVector();
    const FVector Incoming = Velocity.GetSafeNormal();
    if (FVector::DotProduct(Forward, Incoming) >= 0.) return false;

    // Intersect the incoming segment with the guard volume in front of the blade.
    // End is already clipped at the first blocking surface, including the player.
    const FVector Eye = Player.Camera->GetComponentLocation();
    const FVector Center = Eye + Forward * 74. - Player.Camera->GetUpVector() * 28.;
    const FVector Offset = Start - Center;
    constexpr double Reach = 62.;
    const double Along = FVector::DotProduct(Offset, Incoming);
    const double Discriminant = Along * Along - (Offset.SizeSquared() - Reach * Reach);
    if (Discriminant < 0.) return false;
    const double Root = FMath::Sqrt(Discriminant);
    if (-Along + Root < 0.) return false;
    const double Distance = FMath::Max(0., -Along - Root);
    if (Distance > FVector::Distance(Start, End)) return false;
    Contact = Start + Incoming * Distance;
    FCollisionQueryParams Query(SCENE_QUERY_STAT(CastleParrySight), false, &Player);
    if (GetWorld()->LineTraceTestByChannel(Eye, Contact, ECC_Visibility, Query)) return false;

    const FVector Aim = (CrosshairTarget(Player) - Contact).GetSafeNormal();
    if (Aim.IsNearlyZero()) return false;
    Velocity = Aim * Velocity.Size() * 1.7f;
    Shooter = &Player;
    SetOwner(&Player);
    SetInstigator(&Player);
    ShieldBreakLevel = 0; // A reflected bolt can strike a weak point like an unupgraded shot.
    Mesh->SetMaterial(0, ReflectedMaterial);
    SetActorRotation(Velocity.Rotation());
    SetLifeSpan(6.f);
    if (auto* Flash = GetWorld()->SpawnActor<AQueenBlinkEffect>(Contact, FRotator::ZeroRotator))
    {
        Flash->SetActorScale3D(FVector(.22));
        Flash->Start(true);
    }
    UGameplayStatics::PlaySoundAtLocation(this, ParrySound, Contact, .45f, 1.6f);
    return true;
}

void ACastleBolt::Tick(float Dt)
{
    Super::Tick(Dt);
    if (Dt <= 0.f || Velocity.IsNearlyZero() || IsActorBeingDestroyed()) return;
    FVector Start = GetActorLocation();
    FVector End = Start + Velocity * Dt;
    FCollisionQueryParams Query(SCENE_QUERY_STAT(CastleProjectileFlight), false, this);
    Query.AddIgnoredActor(Shooter);
    const FCollisionShape Shape = FCollisionShape::MakeSphere(SweepRadius);
    FHitResult Hit;
    GetWorld()->SweepSingleByChannel(Hit, Start, End, FQuat::Identity, ECC_Visibility, Shape, Query);
    if (!Cast<ACastlePlayer>(Shooter))
    {
        auto* Player = Cast<ACastlePlayer>(UGameplayStatics::GetPlayerPawn(this, 0));
        FVector Contact;
        const double IncomingSpeed = Velocity.Size();
        if (Player && TryParry(*Player, Start, Hit.bBlockingHit ? Hit.Location : End, Contact))
        {
            const double TimeLeft = FMath::Max(0., Dt - FVector::Distance(Start, Contact) / IncomingSpeed);
            Start = Contact;
            End = Start + Velocity * TimeLeft;
            // Ownership has changed. The original shooter is now a valid target.
            FCollisionQueryParams ReturnQuery(SCENE_QUERY_STAT(CastleReflectedFlight), false, Player);
            GetWorld()->SweepSingleByChannel(Hit, Start, End, FQuat::Identity, ECC_Visibility, Shape, ReturnQuery);
        }
    }
    if (Hit.bBlockingHit)
    {
        SetActorLocation(Hit.Location);
        ResolveImpact(Hit);
    }
    else
    {
        SetActorLocationAndRotation(End, Velocity.Rotation());
    }
}

ACastleWeaponProjectile::ACastleWeaponProjectile()
{
    SweepRadius = ShotRadius;
    InitialLifeSpan = ShotRange / ShotSpeed;
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Gold(TEXT("/Game/Demo/Materials/M_Gold.M_Gold"));
    Mesh->SetMaterial(0, Gold.Object);
    Mesh->SetRelativeScale3D(FVector(.19f, .05f, .05f));
}

bool ACastleWeaponProjectile::Fire(ACastlePlayer& Player, FVector MuzzleLocation, float ShotDamage, float InaccuracyDegrees, float FanAngle)
{
    UWorld* World = Player.GetWorld();
    const FVector Eye = Player.Camera->GetComponentLocation();
    const FVector Direction = (CrosshairTarget(Player) - MuzzleLocation)
        .GetSafeNormal(UE_SMALL_NUMBER, Player.Camera->GetForwardVector());
    const FVector Aim = InaccuracyDegrees > 0.f ? FMath::VRandCone(Direction, FMath::DegreesToRadians(InaccuracyDegrees)) : Direction;

    // Detect a barrel already clipping through cover before spawning on its far side.
    FHitResult Obstruction;
    FCollisionQueryParams Query(SCENE_QUERY_STAT(CastleMuzzleClearance), false, &Player);
    World->SweepSingleByChannel(Obstruction, Eye, MuzzleLocation, FQuat::Identity, ECC_Visibility,
        FCollisionShape::MakeSphere(ShotRadius), Query);
    const FVector Origin = Obstruction.bBlockingHit ? Obstruction.Location : MuzzleLocation;
    FActorSpawnParameters Params;
    Params.Owner = &Player;
    Params.Instigator = &Player;
    Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    const auto SpawnRound = [&](const FVector& ShotDirection, float DamageAmount, int32 ShieldLevel)
    {
        auto* Bullet = World->SpawnActor<ACastleWeaponProjectile>(Origin, ShotDirection.Rotation(), Params);
        if (!Bullet) return false;
        Bullet->Shooter = &Player;
        Bullet->Damage = DamageAmount;
        Bullet->ShieldBreakLevel = ShieldLevel;
        Bullet->Velocity = ShotDirection * ShotSpeed;
        if (Obstruction.bBlockingHit) Bullet->ResolveImpact(Obstruction);
        return true;
    };
    if (!SpawnRound(Aim, ShotDamage, Player.WeaponLevel)) return false;
    if (FanAngle > 0.f)
    {
        // The center round retains precision against the queen's shield and weak points.
        // 原名 Up，与 CastleCourtScenery.cpp 匿名命名空间里的全局 Up 撞名；
        // unity 构建把两者编进同一单元时会触发 C4459（本工程按错误处理）→ 改名规避。
        const FVector UpAxis = Player.Camera->GetUpVector();
        SpawnRound(Aim.RotateAngleAxis(-FanAngle, UpAxis), ShotDamage*.25f, INDEX_NONE);
        SpawnRound(Aim.RotateAngleAxis( FanAngle, UpAxis), ShotDamage*.25f, INDEX_NONE);
    }
    return true;
}
