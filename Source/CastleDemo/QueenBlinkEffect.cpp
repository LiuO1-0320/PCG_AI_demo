#include "QueenBlinkEffect.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
    constexpr int32 SparkCount = 24;
    constexpr float Duration = .6f;
}

AQueenBlinkEffect::AQueenBlinkEffect()
{
    PrimaryActorTick.bCanEverTick = true;
    Sparks = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("VioletSparks"));
    SetRootComponent(Sparks);
    Sparks->SetMobility(EComponentMobility::Movable);
    Sparks->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Sparks->SetGenerateOverlapEvents(false);
    Sparks->SetCanEverAffectNavigation(false);
    Sparks->SetCastShadow(false);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Material(TEXT("/Game/BossPrototype/Materials/M_QueenChargeCore.M_QueenChargeCore"));
    Sparks->SetStaticMesh(Sphere.Object);
    Sparks->SetMaterial(0, Material.Object);
    Glow = CreateDefaultSubobject<UPointLightComponent>(TEXT("BlinkGlow"));
    Glow->SetupAttachment(Sparks);
    Glow->SetRelativeLocation(FVector(0,0,50));
    Glow->SetLightColor(FLinearColor(.52f,.16f,1.f));
    Glow->SetAttenuationRadius(320);
    Glow->SetCastShadows(false);
}

void AQueenBlinkEffect::Start(bool bAtDestination)
{
    bArrival = bAtDestination;
    UpdateEffect();
    SetLifeSpan(Duration);
    UE_LOG(LogTemp,Verbose,TEXT("Queen blink effect arrival=%d sparks=%d location=%s"),bArrival,Sparks->GetInstanceCount(),*GetActorLocation().ToCompactString());
}

void AQueenBlinkEffect::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    Age += DeltaSeconds;
    UpdateEffect();
}

void AQueenBlinkEffect::UpdateEffect()
{
    const float T = FMath::Clamp(Age / Duration, 0.f, 1.f);
    const float Progress = 1.f - FMath::Square(1.f-T);
    const float Radius = bArrival ? FMath::Lerp(42.f,150.f,Progress) : FMath::Lerp(105.f,16.f,Progress);
    const bool Initial = Sparks->GetInstanceCount() == 0;
    for (int32 I=0; I<SparkCount; ++I)
    {
        const float Angle = 2.f*PI*I/SparkCount + (bArrival ? -T : T)*.8f;
        const float Height = bArrival ? 15.f + (I%3)*14.f + 23.f*FMath::Sin(PI*T)
                                      : 16.f + (I%3)*30.f + 100.f*Progress;
        const FVector Position(Radius*FMath::Cos(Angle),Radius*FMath::Sin(Angle),Height);
        const float Size = .065f*(1.f-T);
        const FTransform Pose(FQuat::Identity,Position,FVector(Size,Size,Size*(bArrival ? 1.5f : 3.f)));
        if (Initial) Sparks->AddInstance(Pose);
        else Sparks->UpdateInstanceTransform(I,Pose,false,I==SparkCount-1,true);
    }
    Glow->SetIntensity(1500.f*FMath::Square(1.f-T));
}
