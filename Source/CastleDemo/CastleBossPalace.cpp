#include "CastleDemo.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "Engine/StaticMesh.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/ExponentialHeightFog.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Engine/Light.h"
#include "Components/LightComponent.h"

void ACastleDirector::SetBossPalaceEnvironment(bool bActive)
{
    for (AActor* Actor : GeneratedActors)
    {
        if (!IsValid(Actor) || !Actor->ActorHasTag(TEXT("CastleBossArena"))) continue;
        if (auto* Post = Cast<APostProcessVolume>(Actor)) Post->bEnabled = bActive;
        if (auto* Fog = Cast<AExponentialHeightFog>(Actor)) Fog->GetComponent()->SetVisibility(bActive);
    }
}

bool ACastleDirector::BuildBossPalaceShell()
{
    UWorld* Reference = LoadObject<UWorld>(nullptr, TEXT("/Game/BossPalace/Maps/L_BossPalaceShell.L_BossPalaceShell"));
    if (!Reference || !Reference->PersistentLevel)
    {
        UE_LOG(LogTemp, Error, TEXT("CASTLE_PALACE_MISSING: reference shell could not be loaded"));
        return false;
    }
    int32 Count = 0, FillShadowsRemoved = 0, FloorShadowsRemoved = 0;
    for (AActor* Template : Reference->PersistentLevel->Actors)
    {
        if (!Template || !Template->GetRootComponent() || Template->IsA<ACastleDirector>()) continue;
        const FString Class = Template->GetClass()->GetName();
        if (Class != TEXT("StaticMeshActor") && Class != TEXT("PointLight") && Class != TEXT("RectLight") &&
            Class != TEXT("SpotLight") && Class != TEXT("DecalActor") && Class != TEXT("NiagaraActor") &&
            Class != TEXT("ExponentialHeightFog") && Class != TEXT("PostProcessVolume")) continue;
        FActorSpawnParameters Params;
        Params.Template = Template;
        Params.TransformScaleMethod = ESpawnActorScaleMethod::OverrideRootScale;
        Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        FTransform Transform = Template->GetActorTransform();
        Transform.AddToTranslation(BossCenter);
        if (AActor* Copy = GetWorld()->SpawnActor<AActor>(Template->GetClass(), Transform, Params))
        {
            // Candle/portrait fill lights retain their illumination without duplicating
            // the whole hall into dozens of non-Nanite VSM marking queues.
            // The two key spotlights retain the architectural and character shadows.
            if (Class == TEXT("PointLight") || Class == TEXT("RectLight"))
                if (auto* Light=Cast<ALight>(Copy))
                {
                    Light->GetLightComponent()->SetCastShadows(false);
                    ++FillShadowsRemoved;
                }
            TInlineComponentArray<UStaticMeshComponent*> Meshes(Copy);
            for (auto* Mesh : Meshes)
            {
                if (!Mesh->GetStaticMesh()) continue;
                const FVector Size=Mesh->GetStaticMesh()->GetBoundingBox().GetSize()*Mesh->GetComponentScale().GetAbs();
                const bool Foundation=Size.X>4000 && Size.Y>3000 && Size.Z<100;
                if (Foundation || Mesh->GetStaticMesh()->GetName().StartsWith(TEXT("SM_InteriorFloor")))
                {
                    // Flat paving still receives shadows; it need not cast through the floor below.
                    Mesh->SetCastShadow(false);
                    ++FloorShadowsRemoved;
                }
            }
            TrackGenerated(Copy);
            ++Count;
        }
    }
    SetBossPalaceEnvironment(Region == TEXT("BOSS ARENA"));
    UE_LOG(LogTemp, Display, TEXT("CASTLE_PALACE_SHELL_OK Actors=%d"), Count);
    UE_LOG(LogTemp, Display, TEXT("CASTLE_PALACE_SHADOW_BUDGET FillLights=%d FlatFloors=%d KeySpotlightsPreserved=1"),FillShadowsRemoved,FloorShadowsRemoved);
    return Count > 800;
}

void ACastleDirector::AddPalaceMesh(const TCHAR* Path, FVector Center, FVector Size, FRotator Rotation)
{
    UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, Path);
    if (!Mesh) { UE_LOG(LogTemp, Error, TEXT("CASTLE_PALACE_MESH_MISSING %s"), Path); return; }
    const FBox Box = Mesh->GetBoundingBox();
    const FVector Extent = Box.GetSize().ComponentMax(FVector(1));
    const FVector Scale = Size / Extent;
    FActorSpawnParameters Params;
    Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    AActor* Actor = GetWorld()->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, Params);
    auto* Component = NewObject<UStaticMeshComponent>(Actor);
    Actor->SetRootComponent(Component);
    Component->SetMobility(EComponentMobility::Movable);
    Component->SetStaticMesh(Mesh);
    Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Component->SetCanEverAffectNavigation(false);
    Component->RegisterComponent();
    Actor->SetActorTransform(FTransform(Rotation, Center - Rotation.RotateVector(Box.GetCenter() * Scale), Scale));
    TrackGenerated(Actor);
}
