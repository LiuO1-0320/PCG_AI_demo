#include "CastleQueen.h"
#include "Animation/AnimSequence.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/BoxComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/TextRenderComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "NavigationSystem.h"
#include "NavigationPath.h"
#include "Navigation/PathFollowingComponent.h"
#include "Camera/CameraComponent.h"

namespace {
UMaterialInterface* QueenMaterial(const TCHAR* Name)
{
    return LoadObject<UMaterialInterface>(nullptr,*FString::Printf(TEXT("/Game/BossPrototype/Materials/%s.%s"),Name,Name));
}
}

ACastleQueen::ACastleQueen()
{
    QueenMesh=CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("QueenBody"));
    QueenMesh->SetupAttachment(GetRootComponent());
    QueenMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    QueenMesh->SetCanEverAffectNavigation(false);
    QueenMesh->VisibilityBasedAnimTickOption=EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
    // The taller visual needs a full-height shot target, independent of the movement/nav capsule.
    auto* Target=CreateDefaultSubobject<UBoxComponent>(TEXT("QueenShotTarget"));
    Target->SetupAttachment(GetRootComponent());Target->SetRelativeLocation(FVector(0,0,62));
    Target->SetBoxExtent(FVector(45,60,150));Target->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    Target->SetCollisionResponseToAllChannels(ECR_Ignore);Target->SetCollisionResponseToChannel(ECC_Visibility,ECR_Block);
    Target->SetCanEverAffectNavigation(false);
}

void ACastleQueen::Configure(bool InBoss,int32 Floor,ACastleDirector* InDirector)
{
    LeaveCover();
    ThreatUntil = -1; NextCover = 0;
    Super::Configure(InBoss,Floor,InDirector);
    SetActorScale3D(FVector(1));
    Label->SetVisibility(false);
    // Preserve demo's navigation agent; the visual is fitted to boss1's 300 cm height.
    TInlineComponentArray<UStaticMeshComponent*> Pieces(this);
    for(auto* Piece:Pieces) if(Piece!=Telegraph) Piece->SetVisibility(false);
    auto* Skin=LoadObject<USkeletalMesh>(nullptr,TEXT("/Game/BossPrototype/QueenDemo/SK_QueenDemo.SK_QueenDemo"));
    QueenMesh->SetSkeletalMesh(Skin);
    if(Skin)
    {
        const auto Bounds=Skin->GetBounds();const float Scale=300.f/FMath::Max(1.f,Bounds.BoxExtent.Z*2);
        QueenMesh->SetRelativeScale3D(FVector(Scale));
        QueenMesh->SetRelativeLocation(FVector(0,0,-GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight()-(Bounds.Origin.Z-Bounds.BoxExtent.Z)*Scale));
        auto* BodyMaterial=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/BossPrototype/QueenDemo/M_QueenDemo.M_QueenDemo"));
        auto* FrameMaterial=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/BossPrototype/QueenDemo/M_QueenFrameGlow.M_QueenFrameGlow"));
        for(int32 I=0;I<QueenMesh->GetNumMaterials();++I)
        {
            const bool Frame=QueenMesh->GetMaterialSlotNames()[I].ToString().Contains(TEXT("FrameGlow"));
            QueenMesh->SetMaterial(I,Frame?FrameMaterial:BodyMaterial);
            if(Frame) FrameGlow=QueenMesh->CreateDynamicMaterialInstance(I);
        }
    }
    ShootAnimation=LoadObject<UAnimSequence>(nullptr,TEXT("/Game/BossPrototype/QueenDemo/Animations/Queen_Shoot.Queen_Shoot"));
    ChargeAnimation=LoadObject<UAnimSequence>(nullptr,TEXT("/Game/BossPrototype/QueenDemo/Animations/Queen_Charge.Queen_Charge"));
    Telegraph->SetWorldScale3D(FVector(5.6f,5.6f,.008f));Telegraph->SetMaterial(0,QueenMaterial(TEXT("M_Warning")));
    if(Cores.IsEmpty()) for(int32 Side=0;Side<2;++Side)
    {
        auto Make=[&](UMaterialInterface* Material)
        {
            auto* Mesh=NewObject<UStaticMeshComponent>(this);AddInstanceComponent(Mesh);Mesh->SetupAttachment(GetRootComponent());
            Mesh->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Sphere.Sphere")));
            Mesh->SetMaterial(0,Material);Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);Mesh->SetCanEverAffectNavigation(false);
            Mesh->SetCastShadow(false);Mesh->SetVisibility(false);Mesh->RegisterComponent();return Mesh;
        };
        Cores.Add(Make(QueenMaterial(TEXT("M_QueenChargeCore"))));Halos.Add(Make(QueenMaterial(TEXT("M_QueenChargeHalo"))));
        auto* Light=NewObject<UPointLightComponent>(this);AddInstanceComponent(Light);Light->SetupAttachment(GetRootComponent());
        Light->SetLightColor(FLinearColor(.62f,.35f,1));Light->SetAttenuationRadius(160);Light->SetCastShadows(false);Light->SetVisibility(false);Light->RegisterComponent();Lights.Add(Light);
    }
    const float Now=GetWorld()->GetTimeSeconds();NextVolley=Now;NextGround=Now+5;Action=0;bPhaseTwo=false;
    UE_LOG(LogTemp,Display,TEXT("CASTLE_QUEEN_READY Rig=%d Animations=%d FrameGlow=%d Floor=%d Health=%.0f"),Skin!=nullptr,ShootAnimation&&ChargeAnimation,FrameGlow!=nullptr,Floor,Health);
}

FVector ACastleQueen::Muzzle(int32 Side) const
{
    return QueenMesh->GetBoneLocation(Side<0?TEXT("weapon_l"):TEXT("weapon_r"))+GetActorForwardVector()*38;
}

void ACastleQueen::ClearQueenBolts()
{
    for(TActorIterator<ACastleQueenBolt> It(GetWorld());It;++It) if(It->Shooter==this) It->Destroy();
}

void ACastleQueen::BeginAction(int32 NewAction,float Delay)
{
    Action=NewAction;Due=GetWorld()->GetTimeSeconds()+Delay;
    if(NewAction==2 || NewAction==3) {ClearQueenBolts();FlashUntil=0;}
    if(NewAction==3) {Telegraph->SetVisibility(false);QueenMesh->Stop();QueenMesh->SetAnimation(nullptr);}
    else if(auto* Animation=NewAction==2?ChargeAnimation.Get():ShootAnimation.Get()) QueenMesh->PlayAnimation(Animation,false);
    UE_LOG(LogTemp,Display,TEXT("CASTLE_QUEEN_ACTION State=%d PhaseTwo=%d"),Action,bPhaseTwo);
}

void ACastleQueen::Tick(float Dt)
{
    Super::Tick(Dt);
    auto* Player=Cast<ACastlePlayer>(UGameplayStatics::GetPlayerPawn(this,0));
    const bool Active=CanRunAI(Player);const float Now=GetWorld()->GetTimeSeconds();
    if (bSeekingCover && (!Active || !IsInDecisionRange(Player) || Action==3)) LeaveCover();
    if(!Active) {Action=0;FlashUntil=0;Telegraph->SetVisibility(false);QueenMesh->Stop();ClearQueenBolts();}
    constexpr float StrafeSpeed = 320.f;
    const bool bRunningToCover = bPhaseTwo && bSeekingCover && HoldUntil == 0.f;
    const float CombatSpeed = bRunningToCover ? StrafeSpeed * 1.2f : StrafeSpeed;
    GetCharacterMovement()->MaxWalkSpeed = !Active || Action == 3 ? 0.f : (Action == 2 ? 80.f : CombatSpeed);
    if(FrameGlow)
    {
        const float Progress=FMath::Clamp(1-(Due-Now)/1.4f,0.f,1.f);
        FrameGlow->SetScalarParameterValue(TEXT("ChargeGlow"),Active&&Action==2?(.25f+Progress*1.8f)*(1+.07f*FMath::Sin(Now*24)):0.f);
    }
    const float Progress=FMath::Clamp(1-(Due-Now)/.7f,0.f,1.f);
    const float Flash=FMath::Clamp((FlashUntil-Now)/.14f,0.f,1.f);
    for(int32 I=0;I<Cores.Num();++I)
    {
        const bool Show=Active&&(Action==1||Now<FlashUntil);
        Cores[I]->SetVisibility(Show);Halos[I]->SetVisibility(Show);Lights[I]->SetVisibility(Show);
        if(!Show)continue;
        const FVector Position=Muzzle(I==0?-1:1);const float Pulse=1+.06f*FMath::Sin(Now*22);
        Cores[I]->SetWorldLocation(Position);Halos[I]->SetWorldLocation(Position);Lights[I]->SetWorldLocation(Position);
        Cores[I]->SetWorldScale3D(FVector((.025f+.055f*Progress+.045f*Flash)*Pulse));
        Halos[I]->SetWorldScale3D(FVector((.12f+.16f*Progress+.12f*Flash)*Pulse));Lights[I]->SetIntensity(35+100*Progress+180*Flash);
    }
}

void ACastleQueen::ResolvePendingBlast(ACastlePlayer* Player)
{
    const float Now=GetWorld()->GetTimeSeconds();
    if(!bPhaseTwo && Health>0 && Health<=MaxHealth*.5f)
    {
        bPhaseTwo=true;BeginAction(3,2);return;
    }
    if(Action==0 || Now<Due)return;
    if(Action==1)
    {
        const int32 PerSide=bPhaseTwo?5:3;AILastVolley=PerSide*2;++AIAttackEvents;FlashUntil=Now+.14f;
        for(int32 Side:{-1,1})
        {
            const FVector Start=Muzzle(Side);const FVector Aim=(Player->GetActorLocation()+FVector(0,0,40)-Start).GetSafeNormal();
            for(int32 I=0;I<PerSide;++I)
            {
                FActorSpawnParameters Params;Params.Owner=this;Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
                if(auto* Bolt=GetWorld()->SpawnActor<ACastleQueenBolt>(Start,FRotator::ZeroRotator,Params))
                {Bolt->Shooter=this;Bolt->Damage=AttackDamage;Bolt->Velocity=Aim.RotateAngleAxis((I-(PerSide-1)*.5f)*12,FVector::UpVector)*850;}
            }
        }
        NextVolley=Now+(bPhaseTwo?1.65f:2.25f);Recovery=Now+.6f;
        UE_LOG(LogTemp,Display,TEXT("CASTLE_QUEEN_VOLLEY Count=%d"),AILastVolley);
    }
    else if(Action==2)
    {
        const float Feet=Player->GetActorLocation().Z-Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
        if(FVector::Dist2D(Player->GetActorLocation(),BlastCenter)<280 && FMath::Abs(Feet-BlastCenter.Z)<160)
            UGameplayStatics::ApplyDamage(Player,AttackDamage*2.4f,GetController(),this,nullptr);
        Telegraph->SetVisibility(false);NextGround=Now+7;NextVolley=FMath::Max(NextVolley,Now+.9f);Recovery=Now+.5f;
    }
    else {NextVolley=Now+1;NextGround=Now+4;Recovery=Now;}
    Action=0;
}

void ACastleQueen::UpdateAttack(ACastlePlayer* Player,bool CanSee)
{
    ++AIAttackTicks;const float Now=GetWorld()->GetTimeSeconds();
    if(CanSee && Action==0 && Now>=Recovery && Now>=NextVolley && Now<NextGround) BeginAction(1,.7f);
}

void ACastleQueen::UpdateGroundSlam(ACastlePlayer* Player)
{
    ++AISlamTicks;const float Now=GetWorld()->GetTimeSeconds();
    if(Action!=0 || Now<Recovery || Now<NextGround)return;
    FHitResult Hit;FCollisionQueryParams Query(NAME_None,false,this);Query.AddIgnoredActor(Player);
    const FVector Start=Player->GetActorLocation()+FVector(0,0,60);
    GetWorld()->LineTraceSingleByChannel(Hit,Start,Start-FVector(0,0,900),ECC_Visibility,Query);
    BlastCenter=Hit.bBlockingHit?Hit.ImpactPoint+FVector(0,0,3):Player->GetActorLocation()-FVector(0,0,85);
    Telegraph->SetWorldLocation(BlastCenter);Telegraph->SetVisibility(true);++AIBlastEvents;BeginAction(2,1.4f);
}

void ACastleQueen::UpdatePursuit(ACastlePlayer* Player, bool CanSee)
{
    ++AIPursuitTicks;
    auto* Movement = GetCharacterMovement();
    if (Action == 3)
    {
        LeaveCover();
        Movement->StopMovementImmediately();
        return;
    }
    const FVector Center = GetActorLocation();
    const FVector Delta = Player->GetActorLocation() - Center;
    const FVector Toward = Delta.GetSafeNormal2D();
    SetActorRotation(Toward.Rotation());
    if (UpdateCover(Player) || !Movement->IsMovingOnGround()) return;

    const float Range = Delta.Size2D();
    const float Radial = Range < 850.f ? -1.f : (Range > 1150.f ? .35f : -.15f);
    const float Side = FMath::Fmod(GetWorld()->GetTimeSeconds(), 3.2f) < 1.6f ? 1.f : -1.f;
    const FVector Desired = (Toward * Radial + FVector::CrossProduct(FVector::UpVector, Toward) * (.85f * Side)).GetSafeNormal();
    const auto* Capsule = GetCapsuleComponent();
    const FVector Feet = Center - FVector(0, 0, Capsule->GetScaledCapsuleHalfHeight());
    const auto Shape = FCollisionShape::MakeCapsule(Capsule->GetScaledCapsuleRadius()-3, Capsule->GetScaledCapsuleHalfHeight()-3);
    FCollisionQueryParams Query(SCENE_QUERY_STAT(QueenStrafe), false, this);
    float BestScore = -BIG_NUMBER;
    FVector Best = FVector::ZeroVector;
    for (float Angle : {0.f,35.f,-35.f,70.f,-70.f,110.f,-110.f,150.f,-150.f,180.f})
    {
        const FVector Dir = Desired.RotateAngleAxis(Angle, FVector::UpVector);
        FHitResult Obstacle, Ground;
        const bool Blocked = GetWorld()->SweepSingleByChannel(Obstacle, Center, Center + Dir*180.f,
            FQuat::Identity, ECC_Pawn, Shape, Query);
        const float Clear = Blocked ? Obstacle.Time : 1.f;
        const float Score = FVector::DotProduct(Dir, Desired)*2.f + Clear;
        if (Clear < .12f || Score <= BestScore) continue;
        const FVector Ahead = Feet + Dir*55.f;
        if (!GetWorld()->LineTraceSingleByChannel(Ground, Ahead+FVector(0,0,55), Ahead-FVector(0,0,55), ECC_Pawn, Query)
            || !Movement->IsWalkable(Ground)) continue;
        BestScore = Score;
        Best = Dir;
        if (!Blocked && Angle == 0.f) break;
    }
    AddMovementInput(Best);
}

void ACastleQueen::LeaveCover()
{
    if (bSeekingCover)
        if (auto* AI = Cast<AAIController>(GetController())) AI->StopMovement();
    bSeekingCover = false;
    HoldUntil = 0;
}

bool ACastleQueen::UpdateCover(ACastlePlayer* Player)
{
    const float Now = GetWorld()->GetTimeSeconds();
    auto* AI = Cast<AAIController>(GetController());
    if (!AI) return false;
    const FVector Eye = Player->Camera->GetComponentLocation();
    FCollisionQueryParams Query(SCENE_QUERY_STAT(QueenCover), false, this);
    Query.AddIgnoredActor(Player);
    auto Protected = [&](const FVector& Feet)
    {
        const FVector Edge = FVector::CrossProduct(FVector::UpVector,(Feet-Eye).GetSafeNormal2D())*42.f;
        for (const FVector Offset : {FVector(0,0,110), FVector(0,0,280), Edge+FVector(0,0,195), -Edge+FVector(0,0,195)})
            if (!GetWorld()->LineTraceTestByChannel(Eye, Feet+Offset, ECC_Visibility, Query)) return false;
        return true;
    };
    if (bSeekingCover)
    {
        if (Now < CoverDeadline && Protected(CoverGoal))
        {
            if (FVector::Dist2D(GetActorLocation(), CoverGoal) <= 35.f)
            {
                if (!HoldUntil) { AI->StopMovement(); HoldUntil = Now + 1.25f; }
                if (Now < HoldUntil && Protected(GetCharacterMovement()->GetActorFeetLocation())) return true;
            }
            else if (AI->GetMoveStatus() != EPathFollowingStatus::Idle) return true;
        }
        LeaveCover();
        NextCover = Now + 1.5f;
        return false;
    }
    if (Now > ThreatUntil || Now < NextCover || !GetCharacterMovement()->IsMovingOnGround()) return false;
    NextCover = Now + .5f;
    auto* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
    if (!Nav) return false;
    float BestLength = MAX_flt;
    for (const FVector& Top : Director->BossCoverPoints)
    {
        const FVector Behind = (Top-Eye).GetSafeNormal2D();
        FNavLocation Goal;
        if (!Nav->ProjectPointToNavigation(Top-FVector(0,0,320)+Behind*165, Goal, FVector(55,55,110))) continue;
        if (FVector::Dist2D(GetActorLocation(),Goal.Location) >= BestLength || !Protected(Goal.Location)) continue;
        auto* Path = UNavigationSystemV1::FindPathToLocationSynchronously(this, GetActorLocation(), Goal.Location, this);
        if (!Path || !Path->IsValid() || Path->IsPartial()) continue;
        const float Length = Path->GetPathLength();
        if (Length < BestLength) { BestLength = Length; CoverGoal = Goal.Location; }
    }
    UE_LOG(LogTemp,Verbose,TEXT("Cover search Phase=%d Start=%s Count=%d Length=%f Goal=%s"),bPhaseTwo,*GetActorLocation().ToString(),Director->BossCoverPoints.Num(),BestLength,*CoverGoal.ToString());
    if (BestLength == MAX_flt || AI->MoveToLocation(CoverGoal,15.f,false,true,false,true,nullptr,false) == EPathFollowingRequestResult::Failed) return false;
    bSeekingCover = true;
    CoverDeadline = Now + BestLength/240.f + 3.f;
    return true;
}

float ACastleQueen::TakeDamage(float Damage,const FDamageEvent& Event,AController* DamageInstigator,AActor* Causer)
{
    if(Action==3)return 0;
    const float Taken=Super::TakeDamage(Damage,Event,DamageInstigator,Causer);
    if (Taken > 0 && !bDead && Cast<ACastlePlayer>(Causer)) ThreatUntil = GetWorld()->GetTimeSeconds()+1.f;
    if(bDead) { LeaveCover(); ClearQueenBolts(); }
    return Taken;
}

bool ACastleQueen::AcceptBoltHit()
{
    const float Now=GetWorld()->GetTimeSeconds();if(Now-LastHit<.12f)return false;LastHit=Now;return true;
}

ACastleQueenBolt::ACastleQueenBolt()
{
    Mesh->SetMaterial(0,QueenMaterial(TEXT("M_QueenChargeCore")));
    auto Make=[&](const TCHAR* Name,FVector Scale,FVector Offset)
    {
        auto* Part=CreateDefaultSubobject<UStaticMeshComponent>(Name);Part->SetupAttachment(Mesh);
        Part->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Sphere.Sphere")));
        Part->SetMaterial(0,QueenMaterial(TEXT("M_QueenChargeHalo")));Part->SetRelativeScale3D(Scale);Part->SetRelativeLocation(Offset);
        Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);Part->SetCanEverAffectNavigation(false);Part->SetCastShadow(false);
    };
    Make(TEXT("QueenBoltHalo"),FVector(2.5f),FVector::ZeroVector);Make(TEXT("QueenBoltTail"),FVector(4.5f,.8f,.8f),FVector(-150,0,0));
}

void ACastleQueenBolt::Tick(float Dt)
{
    AActor::Tick(Dt);
    const FVector Start=GetActorLocation(),End=Start+Velocity*Dt;SetActorRotation(Velocity.Rotation());
    FHitResult Hit;FCollisionQueryParams Query(NAME_None,false,this);Query.AddIgnoredActor(Shooter);
    GetWorld()->SweepSingleByChannel(Hit,Start,End,FQuat::Identity,ECC_Visibility,FCollisionShape::MakeSphere(8),Query);
    if(Hit.bBlockingHit)
    {
        if(auto* Player=Cast<ACastlePlayer>(Hit.GetActor()))
            if(auto* Queen=Cast<ACastleQueen>(Shooter);Queen&&Queen->AcceptBoltHit()) UGameplayStatics::ApplyDamage(Player,Damage,nullptr,Shooter,nullptr);
        Destroy();
    }
    else SetActorLocation(End);
}
