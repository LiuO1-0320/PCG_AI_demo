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
    if(!Active) {Action=0;FlashUntil=0;Telegraph->SetVisibility(false);QueenMesh->Stop();ClearQueenBolts();}
    GetCharacterMovement()->MaxWalkSpeed=Action==3?0.f:(Action==1||Action==2?55.f:220.f);
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

void ACastleQueen::UpdatePursuit(ACastlePlayer* Player,bool CanSee)
{
    // Keep the demo's MoveToActor/Recast navigation rather than boss1's local sweep movement.
    Super::UpdatePursuit(Player,CanSee);
    if(Action==3) if(auto* AI=Cast<AAIController>(GetController())) AI->StopMovement();
}

float ACastleQueen::TakeDamage(float Damage,const FDamageEvent& Event,AController* DamageInstigator,AActor* Causer)
{
    if(Action==3)return 0;
    const float Taken=Super::TakeDamage(Damage,Event,DamageInstigator,Causer);
    if(bDead)ClearQueenBolts();
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
