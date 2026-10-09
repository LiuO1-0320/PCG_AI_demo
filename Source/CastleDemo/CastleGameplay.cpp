#include "CastleDemo.h"
#include "CastleLanguage.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "EngineUtils.h"
#include "DrawDebugHelpers.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"
#include "Engine/DamageEvents.h"
#include "GameFramework/PlayerInput.h"

static UMaterialInterface* CastleMaterial(const TCHAR* Name)
{
    return LoadObject<UMaterialInterface>(nullptr, *FString::Printf(TEXT("/Game/Demo/Materials/%s.%s"), Name, Name));
}

ACastlePlayer::ACastlePlayer()
{
    PrimaryActorTick.bCanEverTick = true;
    OverrideInputComponentClass = UInputComponent::StaticClass();
    GetCapsuleComponent()->InitCapsuleSize(32, 88);
    GetCharacterMovement()->MaxWalkSpeed = 620;
    GetCharacterMovement()->JumpZVelocity = 460;
    GetCharacterMovement()->AirControl = 0.3f;
    Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("FirstPersonCamera"));
    Camera->SetupAttachment(GetRootComponent());
    Camera->SetRelativeLocation(FVector(0, 0, 64));
    Camera->bUsePawnControlRotation = true;
    Camera->FieldOfView = 95;
    Gun = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Carbine"));
    Gun->SetupAttachment(Camera);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
    Gun->SetStaticMesh(Cube.Object);
    Gun->SetRelativeLocation(FVector(48, 19, -18));
    Gun->SetRelativeScale3D(FVector(.55f, .10f, .13f));
    Gun->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Gun->SetCastShadow(false);
    Muzzle = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MuzzleFlash"));
    Muzzle->SetupAttachment(Camera);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    Muzzle->SetStaticMesh(Sphere.Object);
    Muzzle->SetRelativeLocation(FVector(80, 19, -17));
    Muzzle->SetRelativeScale3D(FVector(.12f));
    Muzzle->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Muzzle->SetCastShadow(false);
    Muzzle->SetVisibility(false);
}

ACastlePlayerController::ACastlePlayerController()
{
    // EnhancedInputEditor can upgrade global defaults. These legacy bindings must
    // keep matching PlayerInput/InputComponent classes regardless of those defaults.
    OverridePlayerInputClass = UPlayerInput::StaticClass();
}

void ACastlePlayerController::BeginPlay()
{
    Super::BeginPlay();
    if (IsLocalController())
    {
        SetInputMode(FInputModeGameOnly());
        bShowMouseCursor = false;
    }
}

void ACastlePlayer::BeginPlay()
{
    Super::BeginPlay();
    Gun->SetMaterial(0,CastleMaterial(TEXT("M_Metal")));
    Muzzle->SetMaterial(0,CastleMaterial(TEXT("M_Gold")));
}

void ACastlePlayer::SetupPlayerInputComponent(UInputComponent* Input)
{
    Super::SetupPlayerInputComponent(Input);
    Input->BindAxis(TEXT("MoveForward"), this, &ACastlePlayer::MoveForward);
    Input->BindAxis(TEXT("MoveRight"), this, &ACastlePlayer::MoveRight);
    Input->BindAxis(TEXT("Turn"), this, &ACastlePlayer::Turn);
    Input->BindAxis(TEXT("LookUp"), this, &ACastlePlayer::Look);
    Input->BindAction(TEXT("Fire"), IE_Pressed, this, &ACastlePlayer::FirePressed);
    Input->BindAction(TEXT("Fire"), IE_Released, this, &ACastlePlayer::FireReleased);
    Input->BindAction(TEXT("Reload"), IE_Pressed, this, &ACastlePlayer::Reload);
    Input->BindAction(TEXT("Interact"), IE_Pressed, this, &ACastlePlayer::Interact);
    Input->BindAction(TEXT("Jump"), IE_Pressed, this, &ACharacter::Jump);
    Input->BindAction(TEXT("Jump"), IE_Released, this, &ACharacter::StopJumping);
    Input->BindAction(TEXT("Restart"), IE_Pressed, this, &ACastlePlayer::Restart);
    Input->BindAction(TEXT("RestartSame"), IE_Pressed, this, &ACastlePlayer::RestartSame);
    Input->BindAction(TEXT("Help"), IE_Pressed, this, &ACastlePlayer::Help);
    Input->BindKey(EKeys::F2,IE_Pressed,this,&ACastlePlayer::ToggleLanguage).bExecuteWhenPaused=true;
    Input->BindAction(TEXT("Pause"), IE_Pressed, this, &ACastlePlayer::Pause).bExecuteWhenPaused = true;
}

ACastleDirector* ACastlePlayer::Director() const
{
    for (TActorIterator<ACastleDirector> It(GetWorld()); It; ++It) return *It;
    return nullptr;
}

void ACastlePlayer::MoveForward(float V) { if (!bDead) AddMovementInput(GetActorForwardVector(), V); }
void ACastlePlayer::MoveRight(float V) { if (!bDead) AddMovementInput(GetActorRightVector(), V); }
void ACastlePlayer::Turn(float V) { if (!bDead) AddControllerYawInput(V); }
void ACastlePlayer::Look(float V) { if (!bDead) AddControllerPitchInput(V); }
void ACastlePlayer::FirePressed() { bFiring = true; bHelp = false; Fire(); }
void ACastlePlayer::FireReleased() { bFiring = false; }
void ACastlePlayer::Help() { bHelp = !bHelp; }
void ACastlePlayer::ToggleLanguage() { CastleSetChinese(!CastleIsChinese()); }

void ACastlePlayer::Tick(float Dt)
{
    Super::Tick(Dt);
    const float Now = GetWorld()->GetTimeSeconds();
    if (bReloading && Now >= ReloadEnd)
    {
        const int32 Taken = FMath::Min(MagazineSize - Ammo, Reserve);
        Ammo += Taken; Reserve -= Taken; bReloading = false;
    }
    if (bFiring) Fire();
    if (!bDead && Now - LastDamage > 5) Shield = FMath::Min(MaxShield, Shield + Dt * 12);
    const float Kick = FMath::Clamp((NextShot - Now) / .16f, 0.f, 1.f);
    Gun->SetRelativeLocation(FVector(48 - Kick * 6, 19, -18 + Kick * 2));
    Muzzle->SetVisibility(NextShot - Now > .12f && !bReloading);
    if (!bDead && GetActorLocation().Z < -600) TakeDamage(9999, FDamageEvent(), nullptr, nullptr);
}

void ACastlePlayer::Fire()
{
    auto* D = Director();
    const float Now = GetWorld()->GetTimeSeconds();
    if (bDead || bReloading || Now < NextShot || !D || !D->bReady || D->bWon) return;
    if (Ammo <= 0) { Reload(); return; }
    --Ammo; NextShot = Now + .16f;
    const FVector Start = Camera->GetComponentLocation();
    const FVector End = Start + Camera->GetForwardVector() * 14000;
    FHitResult Hit;
    FCollisionQueryParams Query(SCENE_QUERY_STAT(CastleShot), false, this);
    GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Query);
    const FVector Target = Hit.bBlockingHit ? Hit.ImpactPoint : End;
    DrawDebugLine(GetWorld(), Muzzle->GetComponentLocation(), Target, FColor(255, 210, 85), false, .065f, 0, 1.5f);
    if (auto* Enemy = Cast<ACastleEnemy>(Hit.GetActor()))
    {
        UGameplayStatics::ApplyDamage(Enemy, BulletDamage, GetController(), this, nullptr);
        HitMarkerUntil = Now + .15f;
    }
    if (Hit.bBlockingHit) DrawDebugPoint(GetWorld(), Target, 8, FColor::Orange, false, .2f);
}

void ACastlePlayer::Reload()
{
    if (bDead || bReloading || Ammo >= MagazineSize) return;
    if (Reserve <= 0) { Tell(TEXT("No reserve ammo. Buy supplies at the shop or collect dropped ammo.")); return; }
    bReloading = true; ReloadEnd = GetWorld()->GetTimeSeconds() + 1.25f;
}

ACastleInteractable* ACastlePlayer::Focused() const
{
    ACastleInteractable* Best = nullptr;
    float BestScore = -1;
    for (TActorIterator<ACastleInteractable> It(GetWorld()); It; ++It)
    {
        if (It->bUsed) continue;
        const FVector Delta = It->GetActorLocation() - Camera->GetComponentLocation();
        const float Dist = Delta.Size();
        const float Dot = FVector::DotProduct(Delta.GetSafeNormal(), Camera->GetForwardVector());
        if (Dist > 320 || Dot < .55f) continue;
        FHitResult Hit;
        FCollisionQueryParams Q(SCENE_QUERY_STAT(CastleInteract), false, this);
        GetWorld()->LineTraceSingleByChannel(Hit, Camera->GetComponentLocation(), It->GetActorLocation(), ECC_Visibility, Q);
        if (Hit.bBlockingHit && Hit.GetActor() != *It) continue;
        const float Score = Dot - Dist / 1500;
        if (Score > BestScore) { BestScore = Score; Best = *It; }
    }
    return Best;
}

void ACastlePlayer::Interact() { if (!bDead) if (auto* Item = Focused()) Item->Use(this); }
void ACastlePlayer::Tell(const FString& Message) { Notice = Message; NoticeUntil = GetWorld()->GetTimeSeconds() + 4.5f; }

void ACastlePlayer::GrantRelic(int32 Kind)
{
    ++Relics;
    switch (Kind % 3)
    {
    case 0: BulletDamage += 6; Tell(TEXT("RELIC: Ember Fang | bullet damage +6")); break;
    case 1: MaxShield += 25; Shield = MaxShield; Tell(TEXT("RELIC: Azure Ward | shield capacity +25")); break;
    default: MaxHealth += 25; Health = MaxHealth; Tell(TEXT("RELIC: Heart of Stone | maximum health +25")); break;
    }
}

float ACastlePlayer::TakeDamage(float Damage, FDamageEvent const& Event, AController* DamageController, AActor* Causer)
{
    if (bDead || Damage <= 0 || (Director() && Director()->bWon)) return 0;
    const float Absorbed = FMath::Min(Shield, Damage);
    Shield -= Absorbed; Health = FMath::Max(0.f, Health - (Damage - Absorbed));
    LastDamage = GetWorld()->GetTimeSeconds(); HurtUntil = LastDamage + .25f;
    if (Health <= 0)
    {
        bDead = true; bFiring = false; bReloading = false;
        GetCharacterMovement()->StopMovementImmediately();
        Tell(TEXT("You fell in the castle. Press ENTER to begin a new run."));
    }
    return Damage;
}

void ACastlePlayer::ResetRun()
{
    Health = MaxHealth = 100; Shield = MaxShield = 60; BulletDamage = 22;
    Ammo = MagazineSize = 24; Reserve = 144; Coins = 35; Relics = 0; WeaponLevel = 1;
    bDead = bReloading = bFiring = false; NextShot = 0; LastDamage = -100;
}
void ACastlePlayer::Restart() { if (auto* D = Director()) if (bDead || D->bWon) D->NewRun(); }
void ACastlePlayer::RestartSame() { if (auto* D = Director()) if (bDead || D->bWon) D->RetrySameSeed(); }
void ACastlePlayer::Pause()
{
    auto* PC = Cast<APlayerController>(GetController());
    if (PC) { const bool Paused = !UGameplayStatics::IsGamePaused(this); UGameplayStatics::SetGamePaused(this, Paused); PC->bShowMouseCursor = Paused; }
}

ACastleInteractable::ACastleInteractable()
{
    PrimaryActorTick.bCanEverTick = true;
    Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Pedestal")); SetRootComponent(Mesh);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
    Mesh->SetStaticMesh(Cube.Object); Mesh->SetCollisionProfileName(TEXT("BlockAllDynamic"));
    Mesh->SetRelativeScale3D(FVector(.9f, .9f, .8f));
    Label = CreateDefaultSubobject<UTextRenderComponent>(TEXT("Label")); Label->SetupAttachment(Mesh);
    Label->SetRelativeLocation(FVector(0, 0, 100)); Label->SetWorldSize(25);
    Label->SetHorizontalAlignment(EHTA_Center);
    Label->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

void ACastleInteractable::Configure(ECastleInteract InKind, const FString& InTitle, ACastleDirector* InDirector, int32 InReward)
{
    Kind = InKind; Title = InTitle; Director = InDirector; RewardKind = InReward;
    const bool Portal = Kind == ECastleInteract::CaveEnter || Kind == ECastleInteract::CaveReturn || Kind == ECastleInteract::BossEnter || Kind == ECastleInteract::NextFloor;
    Mesh->SetWorldScale3D(Portal ? FVector(.22f, 1.8f, 2.6f) : FVector(.9f, .9f, .8f));
    const TCHAR* MatName = Portal ? TEXT("M_Portal") : Kind == ECastleInteract::Shop ? TEXT("M_Teal") : Kind == ECastleInteract::Event ? TEXT("M_Purple") : TEXT("M_Gold");
    Mesh->SetMaterial(0, CastleMaterial(MatName));
    Label->SetText(FText::FromString(Title)); Label->SetTextRenderColor(Portal ? FColor(150, 210, 255) : FColor(255, 215, 120));
    Label->SetAbsolute(false, true, true); Label->SetWorldScale3D(FVector(1));
    Label->SetWorldLocation(GetActorLocation() + FVector(0, 0, Portal ? 175 : 95));
}

void ACastleInteractable::Tick(float Dt)
{
    Super::Tick(Dt);
    if (const auto* P = UGameplayStatics::GetPlayerPawn(this, 0))
        Label->SetWorldRotation((P->GetActorLocation() - Label->GetComponentLocation()).Rotation());
}

FString ACastleInteractable::Prompt(const ACastlePlayer* P) const
{
    switch (Kind)
    {
    case ECastleInteract::Chest: return TEXT("E  Open chest: coins, ammo and a relic");
    case ECastleInteract::Shop: return (CastleIsChinese()?FString::Printf(TEXT("E 升级武器并补给 | %d 金币"), 30 + (P->WeaponLevel - 1) * 20):FString::Printf(TEXT("E  Upgrade weapon + supplies | %d coins"), 30 + (P->WeaponLevel - 1) * 20));
    case ECastleInteract::Event: return TEXT("E  Make an offering: 15 health for a relic");
    case ECastleInteract::CaveEnter: return TEXT("E  Enter the treasure cave");
    case ECastleInteract::CaveReturn: return TEXT("E  Return to exploration");
    case ECastleInteract::BossEnter: return TEXT("E  Enter boss arena (no return until victory)");
    case ECastleInteract::NextFloor: return Director && Director->Floor == 3 ? TEXT("E  Claim victory") : TEXT("E  Ascend to the next floor");
    case ECastleInteract::Weapon: return TEXT("E  Acquire reinforced carbine: +10 damage, +4 magazine");
    case ECastleInteract::Rest: return TEXT("E  Rest once: health, shield and reserve ammo");
    default: return TEXT("E  Collect ammo and coins");
    }
}

void ACastleInteractable::Use(ACastlePlayer* P)
{
    if (!P || bUsed || !Director || !Director->bReady) return;
    switch (Kind)
    {
    case ECastleInteract::Chest:
        P->Coins += 25 + 5 * Director->Floor; P->Reserve += 48; P->GrantRelic(RewardKind); bUsed = true; break;
    case ECastleInteract::Shop:
    {
        const int32 Cost = 30 + (P->WeaponLevel - 1) * 20;
        if (P->Coins < Cost) { P->Tell(TEXT("Not enough coins. Search chests or defeat sentinels.")); return; }
        P->Coins -= Cost; ++P->WeaponLevel; P->BulletDamage += 8; P->MagazineSize += 4;
        P->Ammo = P->MagazineSize; P->Reserve += 96; P->Health = P->MaxHealth; P->Shield = P->MaxShield;
        P->Tell(TEXT("Carbine upgraded: +8 damage, +4 magazine. Health, shield and ammo restored.")); break;
    }
    case ECastleInteract::Event:
        if (P->Health <= 15) { P->Tell(TEXT("The altar needs more than 15 health.")); return; }
        P->Health -= 15; P->GrantRelic(RewardKind); bUsed = true; break;
    case ECastleInteract::CaveEnter: Director->EnterCave(P, GetActorLocation()); break;
    case ECastleInteract::CaveReturn: Director->LeaveCave(P); break;
    case ECastleInteract::BossEnter: Director->EnterBoss(P); break;
    case ECastleInteract::NextFloor:
        if (Director->Floor != 1 && !Director->bBossDefeated) return;
        if (Director->Floor == 3) { Director->bWon = true; P->Tell(TEXT("CASTLE CLEARED | Press ENTER for a new seed.")); }
        else Director->RequestFloor(Director->Floor + 1);
        break;
    case ECastleInteract::Ammo: P->Reserve += 36; P->Coins += 8; P->Tell(TEXT("Supplies collected: +36 ammo, +8 coins")); bUsed = true; break;
    case ECastleInteract::Weapon:
        ++P->WeaponLevel; P->BulletDamage+=10; P->MagazineSize+=4; P->Ammo=P->MagazineSize;
        P->Tell(TEXT("WEAPON ACQUIRED: reinforced carbine | +10 damage, +4 magazine")); bUsed=true; break;
    case ECastleInteract::Rest:
        P->Health=FMath::Min(P->MaxHealth,P->Health+35);
        P->Shield=FMath::Min(P->MaxShield,P->Shield+30);
        P->Reserve+=48;
        P->Tell(TEXT("RESTED: +35 health, +30 shield, +48 reserve ammo")); bUsed=true; break;
    }
    if (bUsed) { Mesh->SetVisibility(false); Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision); Label->SetVisibility(false); }
}

ACastleTrap::ACastleTrap()
{
    Trigger=CreateDefaultSubobject<UBoxComponent>(TEXT("DamageZone"));
    SetRootComponent(Trigger);
    Trigger->SetBoxExtent(FVector(70,500,130));
    Trigger->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    Trigger->SetCollisionResponseToAllChannels(ECR_Ignore);
    Trigger->SetCollisionResponseToChannel(ECC_Pawn,ECR_Overlap);
    Trigger->SetGenerateOverlapEvents(true);
    Trigger->SetCanEverAffectNavigation(false);
}

void ACastleTrap::NotifyActorBeginOverlap(AActor* OtherActor)
{
    Super::NotifyActorBeginOverlap(OtherActor);
    auto* Player=Cast<ACastlePlayer>(OtherActor);
    if (!Player || bTriggered) return;
    bTriggered=true;
    Player->TakeDamage(15,FDamageEvent(),nullptr,this);
    Player->Tell(TEXT("TRAP TRIGGERED: 15 damage"));
    Trigger->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

ACastleAIController::ACastleAIController() { bWantsPlayerState = false; }

ACastleEnemy::ACastleEnemy()
{
    PrimaryActorTick.bCanEverTick = true;
    AIControllerClass = ACastleAIController::StaticClass(); AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;
    GetCapsuleComponent()->InitCapsuleSize(36, 88);
    GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
    GetCharacterMovement()->MaxWalkSpeed = 270;
    bUseControllerRotationYaw = false;
    Body = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("SentinelBody")); Body->SetupAttachment(GetRootComponent());
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
    Body->SetStaticMesh(Cube.Object); Body->SetRelativeScale3D(FVector(.6f, .8f, 1.2f)); Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    auto* Head = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Helmet")); Head->SetupAttachment(GetRootComponent());
    Head->SetStaticMesh(Sphere.Object); Head->SetRelativeLocation(FVector(0, 0, 65)); Head->SetRelativeScale3D(FVector(.55f)); Head->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    auto* Eye = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Visor")); Eye->SetupAttachment(GetRootComponent());
    Eye->SetStaticMesh(Cube.Object); Eye->SetRelativeLocation(FVector(28, 0, 65)); Eye->SetRelativeScale3D(FVector(.07f, .45f, .10f)); Eye->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Label = CreateDefaultSubobject<UTextRenderComponent>(TEXT("HealthLabel")); Label->SetupAttachment(GetRootComponent());
    Label->SetRelativeLocation(FVector(0, 0, 130)); Label->SetWorldSize(22); Label->SetHorizontalAlignment(EHTA_Center); Label->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Telegraph = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BlastTelegraph")); Telegraph->SetupAttachment(GetRootComponent());
    Telegraph->SetStaticMesh(Cylinder.Object); Telegraph->SetAbsolute(true, true, true); Telegraph->SetWorldScale3D(FVector(8, 8, .025f));
    Telegraph->SetCollisionEnabled(ECollisionEnabled::NoCollision); Telegraph->SetCanEverAffectNavigation(false); Telegraph->SetVisibility(false); Telegraph->SetCastShadow(false);
}

void ACastleEnemy::Configure(bool InBoss, int32 InFloor, ACastleDirector* D)
{
    bBoss = InBoss; Director = D; Home = GetActorLocation();
    MaxHealth = Health = bBoss ? 500 + (InFloor - 1) * 275 : 65 + InFloor * 20;
    AttackDamage = bBoss ? 14 + InFloor * 4 : 7 + InFloor * 3;
    Body->SetMaterial(0, CastleMaterial(bBoss ? TEXT("M_Purple") : TEXT("M_Enemy")));
    TArray<UStaticMeshComponent*> Meshes; GetComponents(Meshes);
    for (auto* M : Meshes) if (M != Body && M != Telegraph) M->SetMaterial(0, CastleMaterial(TEXT("M_Gold")));
    Telegraph->SetMaterial(0, CastleMaterial(TEXT("M_Danger")));
    if (bBoss) { SetActorScale3D(FVector(1.8f)); bActive = false; GetCharacterMovement()->MaxWalkSpeed = 220; }
    Label->SetTextRenderColor(bBoss ? FColor(220, 170, 255) : FColor(255, 150, 120));
    NextBlast = GetWorld()->GetTimeSeconds() + 6;
}

void ACastleEnemy::ShootAt(ACastlePlayer* P, int32 Count)
{
    const FVector Start = GetActorLocation() + FVector(65, 0, 35);
    const FVector Aim = (P->GetActorLocation() + FVector(0, 0, 20) - Start).GetSafeNormal();
    for (int32 I = 0; I < Count; ++I)
    {
        FActorSpawnParameters Params; Params.Owner = this; Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        if (auto* Bolt = GetWorld()->SpawnActor<ACastleBolt>(Start, Aim.Rotation(), Params))
        {
            Bolt->Shooter = this; Bolt->Damage = AttackDamage;
            Bolt->Velocity = Aim.RotateAngleAxis((I - (Count - 1) * .5f) * 9, FVector::UpVector) * (bBoss ? 1000 : 850);
        }
    }
}

void ACastleEnemy::Tick(float Dt)
{
    Super::Tick(Dt);
    auto* P = Cast<ACastlePlayer>(UGameplayStatics::GetPlayerPawn(this, 0));
    if (!P || bDead || !Director || !Director->bReady) return;
    Label->SetWorldRotation((P->GetActorLocation() - Label->GetComponentLocation()).Rotation());
    Label->SetText(FText::FromString(FString::Printf(TEXT("%s  %.0f"), bBoss ? *Director->BossName() : TEXT("SENTINEL"), Health)));
    if (!bActive || P->bDead || Director->bWon) { if (auto* AI = Cast<AAIController>(GetController())) AI->StopMovement(); return; }
    const float Now = GetWorld()->GetTimeSeconds();
    const float Dist = FVector::Dist(P->GetActorLocation(), GetActorLocation());
    if (bCharging && Now >= BlastTime)
    {
        bCharging = false; Telegraph->SetVisibility(false);
        if (FVector::Dist2D(P->GetActorLocation(), BlastCenter) < 400 && FMath::Abs(P->GetActorLocation().Z-P->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()-BlastCenter.Z)<160)
            UGameplayStatics::ApplyDamage(P, AttackDamage * 2.4f, GetController(), this, nullptr);
        DrawDebugCircle(GetWorld(), BlastCenter + FVector(0,0,10), 400, 48, FColor::Orange, false, .35f, 0, 8, FVector::ForwardVector, FVector::RightVector, false);
    }
    if (Dist > (bBoss ? 3600 : 2100) || (!bBoss && FVector::Dist2D(P->GetActorLocation(), Home) > 2600)) return;
    FHitResult Hit; FCollisionQueryParams Q(SCENE_QUERY_STAT(CastleSight), false, this);
    GetWorld()->LineTraceSingleByChannel(Hit, GetActorLocation() + FVector(0,0,60), P->Camera->GetComponentLocation(), ECC_Visibility, Q);
    const bool CanSee = !Hit.bBlockingHit || Hit.GetActor() == P;
    if (Now >= NextThink)
    {
        NextThink = Now + .45f;
        if (auto* AI = Cast<AAIController>(GetController()))
        {
            if (Dist > (bBoss ? 650 : 450) || !CanSee) AI->MoveToActor(P, bBoss ? 500 : 320, true);
            else AI->StopMovement();
        }
    }
    FRotator Face = (P->GetActorLocation() - GetActorLocation()).Rotation(); Face.Pitch = Face.Roll = 0; SetActorRotation(Face);
    if (CanSee && Now >= NextAttack)
    {
        NextAttack = Now + (bBoss ? (Health < MaxHealth * .5f ? .85f : 1.35f) : 1.9f);
        if (Dist < 140) UGameplayStatics::ApplyDamage(P, AttackDamage, GetController(), this, nullptr);
        else ShootAt(P, bBoss ? (Health < MaxHealth * .5f ? 5 : 3) : 1);
    }
    if (bBoss && Now >= NextBlast && !bCharging)
    {
        NextBlast = Now + 6; BlastTime = Now + 1.4f; bCharging = true;
        FHitResult Ground; FCollisionQueryParams GroundQuery(SCENE_QUERY_STAT(CastleSlamGround),false,this); GroundQuery.AddIgnoredActor(P);
        const FVector GroundStart=P->GetActorLocation()+FVector(0,0,60);
        GetWorld()->LineTraceSingleByChannel(Ground,GroundStart,GroundStart-FVector(0,0,900),ECC_Visibility,GroundQuery);
        BlastCenter=Ground.bBlockingHit?Ground.ImpactPoint+FVector(0,0,3):P->GetActorLocation()-FVector(0,0,85);
        Telegraph->SetWorldLocation(BlastCenter); Telegraph->SetVisibility(true);
        P->Tell(TEXT("BOSS SLAM: leave the red circle!"));
    }
}

float ACastleEnemy::TakeDamage(float Damage, FDamageEvent const&, AController*, AActor*)
{
    if (bDead || !bActive) return 0;
    Health = FMath::Max(0.f, Health - Damage);
    if (Health <= 0)
    {
        bDead = true; Telegraph->SetVisibility(false);
        if (auto* AI = Cast<AAIController>(GetController())) { AI->StopMovement(); AI->UnPossess(); AI->Destroy(); }
        if (Director) Director->EnemyKilled(this);
        Destroy();
    }
    return Damage;
}

ACastleBolt::ACastleBolt()
{
    PrimaryActorTick.bCanEverTick = true;
    Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Bolt")); SetRootComponent(Mesh);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    Mesh->SetStaticMesh(Sphere.Object); Mesh->SetRelativeScale3D(FVector(.16f)); Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Mesh->SetCanEverAffectNavigation(false); Mesh->SetCastShadow(false); InitialLifeSpan = 6;
}

void ACastleBolt::Tick(float Dt)
{
    Super::Tick(Dt);
    if (auto* Mat = CastleMaterial(TEXT("M_Danger"))) Mesh->SetMaterial(0, Mat);
    const FVector Start = GetActorLocation(), End = Start + Velocity * Dt;
    FHitResult Hit; FCollisionQueryParams Q(SCENE_QUERY_STAT(CastleBolt), false, this); Q.AddIgnoredActor(Shooter);
    GetWorld()->SweepSingleByChannel(Hit, Start, End, FQuat::Identity, ECC_Visibility, FCollisionShape::MakeSphere(8), Q);
    if (Hit.bBlockingHit)
    {
        if (auto* P = Cast<ACastlePlayer>(Hit.GetActor())) UGameplayStatics::ApplyDamage(P, Damage, nullptr, Shooter, nullptr);
        Destroy();
    }
    else SetActorLocation(End);
}

ACastleGameMode::ACastleGameMode()
{
    DefaultPawnClass = ACastlePlayer::StaticClass(); HUDClass = ACastleHUD::StaticClass();
    PlayerControllerClass = ACastlePlayerController::StaticClass();
}
void ACastleGameMode::BeginPlay()
{
    Super::BeginPlay();
    for (TActorIterator<ACastleDirector> It(GetWorld()); It; ++It) return;
    GetWorld()->SpawnActor<ACastleDirector>();
}
