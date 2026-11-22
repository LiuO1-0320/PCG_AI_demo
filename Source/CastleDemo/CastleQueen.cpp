#include "CastleQueen.h"
#include "QueenCombatAnimation.h"
#include "QueenBlinkEffect.h"
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
    auto ShieldPiece = [&](FName Name, FVector Offset, FVector Scale, const TCHAR* Material)
    {
        auto* Piece=CreateDefaultSubobject<UStaticMeshComponent>(Name);
        Piece->SetupAttachment(GetRootComponent());
        Piece->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Sphere.Sphere")));
        Piece->SetMaterial(0,QueenMaterial(Material));
        Piece->SetRelativeLocation(Offset); Piece->SetRelativeScale3D(Scale);
        Piece->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Piece->SetCollisionResponseToAllChannels(ECR_Ignore);
        Piece->SetCollisionResponseToChannel(ECC_Visibility,ECR_Block);
        Piece->SetGenerateOverlapEvents(false); Piece->SetCanEverAffectNavigation(false);
        Piece->SetCastShadow(false); Piece->SetVisibility(false);
        return Piece;
    };
    ShieldShell=ShieldPiece(TEXT("QueenShield"),FVector(0,0,66),FVector(2.7,2.7,3.4),TEXT("M_QueenShield"));
    WeakPoints.Add(ShieldPiece(TEXT("ShieldLeft"),FVector(130,-103,72),FVector(.52),TEXT("M_QueenChargeCore")));
    WeakPoints.Add(ShieldPiece(TEXT("ShieldRight"),FVector(130,103,72),FVector(.52),TEXT("M_QueenChargeCore")));
    WeakPoints.Add(ShieldPiece(TEXT("ShieldCrown"),FVector(100,0,206),FVector(.52),TEXT("M_QueenChargeCore")));

    for (int32 I=0; I<WeakPoints.Num(); ++I)
    {
        auto* Point=WeakPoints[I].Get();
        auto* Gold=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Demo/Materials/M_Gold.M_Gold"));
        if (Gold) Point->SetMaterial(0,Gold);
        for (int32 J=0; J<8; ++J)
        {
            auto* Spark=CreateDefaultSubobject<UStaticMeshComponent>(*FString::Printf(TEXT("WeakPoint%dSpark%d"),I,J));
            Spark->SetupAttachment(Point);
            Spark->SetStaticMesh(Point->GetStaticMesh());
            Spark->SetMaterial(0,QueenMaterial(TEXT("M_QueenChargeCore")));
            const float Angle=2.f*PI*J/8;
            Spark->SetRelativeLocation(FVector(0,73.f*FMath::Cos(Angle),73.f*FMath::Sin(Angle)));
            Spark->SetRelativeScale3D(FVector(.12f));
            Spark->SetCollisionEnabled(ECollisionEnabled::NoCollision);
            Spark->SetCanEverAffectNavigation(false); Spark->SetCastShadow(false);
            Spark->SetVisibility(false);
        }
    }
}

void ACastleQueen::Configure(bool InBoss,int32 Floor,ACastleDirector* InDirector)
{
    DirectBreakLevel = Floor >= 3 ? 10 : 8;
    WeakPointsHit=0; HideShield();
    LeaveCover();
    LastCoverIndex = INDEX_NONE; NextCover = 0;
    Super::Configure(InBoss,Floor,InDirector);
    SetActorScale3D(FVector(1));
    Label->SetVisibility(false);
    // Preserve demo's navigation agent; the visual is fitted to boss1's 300 cm height.
    TInlineComponentArray<UStaticMeshComponent*> Pieces(this);
    for(auto* Piece:Pieces) if(Piece!=Telegraph) Piece->SetVisibility(false);
    auto* Skin=LoadObject<USkeletalMesh>(nullptr,TEXT("/Game/BossPrototype/QueenDemo/SK_QueenDemo.SK_QueenDemo"));
    QueenMesh->SetSkeletalMesh(Skin);
    QueenMesh->SetAnimInstanceClass(UQueenCombatAnimation::StaticClass());
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
    const float Now=GetWorld()->GetTimeSeconds();NextVolley=Now;NextGround=Now;Action=0;bPhaseTwo=false;
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
    else if(auto* Animation=NewAction==2?ChargeAnimation.Get():ShootAnimation.Get()) { QueenMesh->SetAnimation(Animation); QueenMesh->Play(false); }
    UE_LOG(LogTemp,Display,TEXT("CASTLE_QUEEN_ACTION State=%d PhaseTwo=%d"),Action,bPhaseTwo);
}

void ACastleQueen::Tick(float Dt)
{
    Super::Tick(Dt);
    auto* Player=Cast<ACastlePlayer>(UGameplayStatics::GetPlayerPawn(this,0));
    const bool Active=CanRunAI(Player);const float Now=GetWorld()->GetTimeSeconds();
    if (bSeekingCover && (!Active || !IsInDecisionRange(Player) || Action==3)) LeaveCover();
    if(!Active) {if(Action==3)HideShield();Action=0;FlashUntil=0;Telegraph->SetVisibility(false);QueenMesh->Stop();ClearQueenBolts();}
    constexpr float TravelSpeed = 320.f;
    const bool bRunningToCover = bPhaseTwo && bSeekingCover && !bAtCover;
    const float CombatSpeed = bRunningToCover ? TravelSpeed * 1.2f : TravelSpeed;
    GetCharacterMovement()->MaxWalkSpeed = !Active || Action == 2 || bAtCover ? 0.f : (Action==3 ? 210.f : CombatSpeed);
    if (Active)
    {
        const FVector Velocity = GetVelocity();
        const FVector Facing = Action!=3 && Velocity.SizeSquared2D() > 25.f*25.f ? Velocity : Player->GetActorLocation()-GetActorLocation();
        if (!Facing.IsNearlyZero())
            SetActorRotation(FRotator(0,FMath::FixedTurn(GetActorRotation().Yaw,Facing.Rotation().Yaw,420.f*Dt),0));
    }
    if (Action==3)
        for (UStaticMeshComponent* Point : WeakPoints)
            if (Point->IsVisible()) Point->SetRelativeRotation(FRotator(0,0,FMath::Fmod(Now*85.f,360.f)));
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

void ACastleQueen::HideShield()
{
    ShieldShell->SetVisibility(false); ShieldShell->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    for (UStaticMeshComponent* Point : WeakPoints)
    {
        Point->SetVisibility(false,true);
        Point->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    }
}

void ACastleQueen::StartShield(ACastlePlayer* Player)
{
    LeaveCover();
    GetCharacterMovement()->StopMovementImmediately();
    BeginAction(3,5.f);
    ShieldWaypoint=0;
    NextShieldMove=0;
    SetActorRotation((Player->GetActorLocation()-GetActorLocation()).GetSafeNormal2D().Rotation());
    if (ChargeAnimation)
    {
        QueenMesh->SetAnimation(ChargeAnimation);
        QueenMesh->SetPlayRate(ChargeAnimation->GetPlayLength()/5.f);
        QueenMesh->Play(false);
    }
    WeakPointsHit=0;
    ShieldShell->SetVisibility(true); ShieldShell->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    for (UStaticMeshComponent* Point : WeakPoints)
    {
        Point->SetVisibility(true,true);
        Point->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    }
}

bool ACastleQueen::HandleShieldShot(const FHitResult& Hit, int32 GunLevel)
{
    if (Action!=3 || WeakPointsHit==3) return false;
    if (GunLevel>=DirectBreakLevel) WeakPointsHit=3;
    else
    {
        auto* Point=Cast<UStaticMeshComponent>(Hit.GetComponent());
        if (!Point || !WeakPoints.Contains(Point) || !Point->IsVisible()) return false;
        Point->SetVisibility(false,true);
        Point->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        ++WeakPointsHit;
    }
    if (WeakPointsHit==3) HideShield();
    return true;
}

void ACastleQueen::ResolvePendingBlast(ACastlePlayer* Player)
{
    const float Now=GetWorld()->GetTimeSeconds();
    if(Action!=3 && !bPhaseTwo && Health>0 && Health<=MaxHealth*.5f)
    {
        StartShield(Player);return;
    }
    if(Action==0 || Now<Due || (Action==3 && WeakPointsHit<3))return;
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
        Telegraph->SetVisibility(false);
        LastCoverIndex=CoverIndex;
        LeaveCover();
        if (CanRunAI(Player)) BlinkAfterSlam(Player);
        NextGround=Now+2.5f;NextVolley=FMath::Max(NextVolley,Now+.9f);Recovery=Now+.5f;
    }
    else {if (auto* AI=Cast<AAIController>(GetController())) AI->StopMovement();
        bPhaseTwo=true; QueenMesh->SetPlayRate(1.f); NextVolley=Now+1;NextGround=Now;Recovery=Now;}
    Action=0;
}

void ACastleQueen::UpdateAttack(ACastlePlayer* Player,bool CanSee)
{
    ++AIAttackTicks;const float Now=GetWorld()->GetTimeSeconds();
    if(CanSee && !bAtCover && Action==0 && Now>=Recovery && Now>=NextVolley) BeginAction(1,.7f);
}

void ACastleQueen::UpdateGroundSlam(ACastlePlayer* Player)
{
    ++AISlamTicks;const float Now=GetWorld()->GetTimeSeconds();
    if(!bAtCover || Action!=0 || Now<Recovery || Now<NextGround)return;
    GetCharacterMovement()->StopMovementImmediately();
    FHitResult Hit;FCollisionQueryParams Query(NAME_None,false,this);Query.AddIgnoredActor(Player);
    const FVector Start=Player->GetActorLocation()+FVector(0,0,60);
    GetWorld()->LineTraceSingleByChannel(Hit,Start,Start-FVector(0,0,900),ECC_Visibility,Query);
    BlastCenter=Hit.bBlockingHit?Hit.ImpactPoint+FVector(0,0,3):Player->GetActorLocation()-FVector(0,0,85);
    Telegraph->SetWorldLocation(BlastCenter);Telegraph->SetVisibility(true);++AIBlastEvents;BeginAction(2,1.4f);
}

void ACastleQueen::UpdatePursuit(ACastlePlayer* Player, bool CanSee)
{
    ++AIPursuitTicks;
    if (Action == 3)
    {
        UpdateShieldPatrol();
        return;
    }
    if (Action != 2) UpdateCover(Player);
}

void ACastleQueen::UpdateShieldPatrol()
{
    auto* AI=Cast<AAIController>(GetController());
    const float Now=GetWorld()->GetTimeSeconds();
    if (!AI || Now<NextShieldMove || AI->GetMoveStatus()==EPathFollowingStatus::Moving) return;
    auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
    if (!Nav) return;
    NextShieldMove=Now+.35f;
    for (int32 I=0; I<6; ++I)
    {
        const FVector Offset=FVector(560,0,0).RotateAngleAxis(ShieldWaypoint*60.f,FVector::UpVector);
        ShieldWaypoint=(ShieldWaypoint+1)%6;
        FNavLocation Goal;
        if (!Nav->ProjectPointToNavigation(Director->BossCenter+Offset,Goal,FVector(120,120,150))) continue;
        if (AI->MoveToLocation(Goal.Location,45.f,false,true,false,true,nullptr,false)==EPathFollowingRequestResult::RequestSuccessful) break;
    }
}

void ACastleQueen::BlinkAfterSlam(ACastlePlayer* Player)
{
    auto* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
    if (!Nav) return;
    const FVector From = GetActorLocation();
    const FVector Eye = Player->Camera->GetComponentLocation();
    const FVector OldBearing = (From - Eye).GetSafeNormal2D();
    const auto* Capsule = GetCapsuleComponent();
    FCollisionQueryParams Query(SCENE_QUERY_STAT(QueenBlink), false, this);
    FCollisionQueryParams SightQuery = Query;
    SightQuery.AddIgnoredActor(Player);
    FVector Best = FVector::ZeroVector;
    float BestScore = -BIG_NUMBER;
    auto Consider = [&](const FVector& Feet)
    {
        FNavLocation Ground;
        if (!Nav->ProjectPointToNavigation(Feet, Ground, FVector(50,50,75))) return;
        const FVector Landing = Ground.Location + FVector(0,0,Capsule->GetScaledCapsuleHalfHeight()+2);
        const float Range = FVector::Dist2D(Landing, Eye);
        if (Range < 700 || Range > 3000 || FVector::Dist2D(From,Landing) < 500) return;
        if (GetWorld()->OverlapBlockingTestByProfile(Landing,FQuat::Identity,Capsule->GetCollisionProfileName(),
            Capsule->GetCollisionShape(),Query)) return;
        if (GetWorld()->LineTraceTestByChannel(Landing+FVector(0,0,60),Eye,ECC_Visibility,SightQuery)) return;
        const float Flank = 1.f - FVector::DotProduct(OldBearing,(Landing-Eye).GetSafeNormal2D());
        const float Score = 500.f*Flank - FMath::Abs(Range-1400.f) + .4f*(Ground.Location.Z-Director->BossCenter.Z);
        if (Score > BestScore) { BestScore = Score; Best = Landing; }
    };
    for (const FVector& Platform : Director->BossPlatforms) Consider(Platform);
    for (int32 I=0; I<Director->BossCoverPoints.Num(); ++I)
    {
        if (I == LastCoverIndex) continue;
        const FVector Base = Director->BossCoverPoints[I] - FVector(0,0,320);
        const FVector Side = FVector::CrossProduct(FVector::UpVector,(Base-Eye).GetSafeNormal2D());
        Consider(Base+Side*220.f);
        Consider(Base-Side*220.f);
    }
    if (BestScore == -BIG_NUMBER)
    {
        // Arena furniture can occlude every pillar-side shot on some layouts.
        for (float Radius : {750.f, 1200.f})
            for (int32 Angle = 0; Angle < 360; Angle += 30)
                Consider(Director->BossCenter + FVector(Radius,0,0).RotateAngleAxis(Angle,FVector::UpVector));
    }
    if (BestScore == -BIG_NUMBER) return;
    if (TeleportTo(Best,(Eye-Best).GetSafeNormal2D().Rotation()))
    {
        GetCharacterMovement()->StopMovementImmediately();
        const FVector FeetOffset(0,0,Capsule->GetScaledCapsuleHalfHeight());
        auto SpawnFlash = [&](const FVector& Center, bool bArrival)
        {
            if (auto* Effect = GetWorld()->SpawnActor<AQueenBlinkEffect>(Center-FeetOffset,FRotator::ZeroRotator))
                Effect->Start(bArrival);
        };
        SpawnFlash(From,false);
        SpawnFlash(GetActorLocation(),true);
        UE_LOG(LogTemp,Display,TEXT("CASTLE_QUEEN_BLINK From=%s To=%s Range=%.0f"),*From.ToCompactString(),*GetActorLocation().ToCompactString(),FVector::Dist2D(Best,Eye));
    }
}

void ACastleQueen::LeaveCover()
{
    if (bSeekingCover)
        if (auto* AI = Cast<AAIController>(GetController())) AI->StopMovement();
    bSeekingCover = false;
    bAtCover = false;
    CoverIndex = INDEX_NONE;
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
        if ((bAtCover || Now < CoverDeadline) && Protected(CoverGoal))
        {
            if (FVector::Dist2D(GetActorLocation(), CoverGoal) <= 35.f)
            {
                if (Protected(GetCharacterMovement()->GetActorFeetLocation()))
                {
                    if (!bAtCover) { AI->StopMovement(); GetCharacterMovement()->StopMovementImmediately(); }
                    bAtCover = true;
                    return true;
                }
            }
            else if (AI->GetMoveStatus() != EPathFollowingStatus::Idle) return true;
        }
        LeaveCover();
        NextCover = Now + .15f;
        return false;
    }
    if (Now < NextCover || !GetCharacterMovement()->IsMovingOnGround()) return false;
    NextCover = Now + .5f;
    auto* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
    if (!Nav) return false;
    float BestLength = MAX_flt;
    for (int32 I = 0; I < Director->BossCoverPoints.Num(); ++I)
    {
        const FVector& Top = Director->BossCoverPoints[I];
        const FVector Behind = (Top-Eye).GetSafeNormal2D();
        FNavLocation Goal;
        if (!Nav->ProjectPointToNavigation(Top-FVector(0,0,320)+Behind*165, Goal, FVector(55,55,110))) continue;
        if (FVector::Dist2D(GetActorLocation(),Goal.Location) >= BestLength || !Protected(Goal.Location)) continue;
        auto* Path = UNavigationSystemV1::FindPathToLocationSynchronously(this, GetActorLocation(), Goal.Location, this);
        if (!Path || !Path->IsValid() || Path->IsPartial()) continue;
        const float Length = Path->GetPathLength();
        // Prefer changing positions after a slam, retaining the last pillar as a fallback.
        const float Score = Length + (I == LastCoverIndex ? 20000.f : 0.f);
        if (Score < BestLength) { BestLength = Score; CoverGoal = Goal.Location; CoverIndex = I; CoverDeadline = Now + Length/240.f + 3.f; }
    }
    UE_LOG(LogTemp,Verbose,TEXT("Cover search Phase=%d Start=%s Count=%d Length=%f Goal=%s"),bPhaseTwo,*GetActorLocation().ToString(),Director->BossCoverPoints.Num(),BestLength,*CoverGoal.ToString());
    if (BestLength == MAX_flt || AI->MoveToLocation(CoverGoal,15.f,false,true,false,true,nullptr,false) == EPathFollowingRequestResult::Failed) return false;
    bSeekingCover = true;
    return true;
}

float ACastleQueen::TakeDamage(float Damage,const FDamageEvent& Event,AController* DamageInstigator,AActor* Causer)
{
    if(Action==3)return 0;
    if (!bPhaseTwo && Health > MaxHealth*.5f) Damage=FMath::Min(Damage,Health-MaxHealth*.5f);
    const float Taken=Super::TakeDamage(Damage,Event,DamageInstigator,Causer);
    if (Taken>0 && !bPhaseTwo && !bDead && Health<=MaxHealth*.5f)
        if (auto* Player=Cast<ACastlePlayer>(UGameplayStatics::GetPlayerPawn(this,0))) {StartShield(Player);return Taken;}
    if (Taken > 0 && !bDead)
        if (auto* Anim = Cast<UQueenCombatAnimation>(QueenMesh->GetAnimInstance()))
        {
            const FVector Away = Causer ? (GetActorLocation()-Causer->GetActorLocation()).GetSafeNormal2D() : FVector::ZeroVector;
            Anim->ReactToHit(FVector::DotProduct(Away,GetActorRightVector()));
        }
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
            if(auto* Queen=Cast<ACastleQueen>(Shooter);Queen&&Queen->AcceptBoltHit()) Player->ReceiveBoltHit(Damage,Shooter);
        Destroy();
    }
    else SetActorLocation(End);
}
