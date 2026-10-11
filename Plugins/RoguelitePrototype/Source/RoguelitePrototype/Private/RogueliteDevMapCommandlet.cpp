#include "RogueliteDevMapCommandlet.h"
#include "RogueliteDevGameMode.h"
#include "RoguelitePrototype.h"

#if WITH_EDITOR
#include "Components/StaticMeshComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/SkyLight.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "FileHelpers.h"
#include "GameFramework/PlayerStart.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/FileManager.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#endif

URogueliteDevMapCommandlet::URogueliteDevMapCommandlet()
{
    IsClient = false;
    IsServer = false;
    IsEditor = true;
    LogToConsole = true;
}

int32 URogueliteDevMapCommandlet::Main(const FString& Params)
{
#if WITH_EDITOR
    const FString MapPath = TEXT("/Game/Roguelite/Maps/L_Roguelite_Dev");
    const FString MapFilename = FPackageName::LongPackageNameToFilename(MapPath, FPackageName::GetMapPackageExtension());
    if (FPaths::FileExists(MapFilename))
    {
        UE_LOG(LogRoguelitePrototype, Error, TEXT("Refusing to overwrite existing map: %s"), *MapFilename);
        return 1;
    }

    UWorld* World = UEditorLoadingAndSavingUtils::NewBlankMap(false);
    if (!World)
    {
        UE_LOG(LogRoguelitePrototype, Error, TEXT("Could not create blank development world."));
        return 2;
    }
    World->GetWorldSettings()->DefaultGameMode = ARogueliteDevGameMode::StaticClass();

    AStaticMeshActor* Floor = World->SpawnActor<AStaticMeshActor>();
    UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
    APlayerStart* Start = World->SpawnActor<APlayerStart>(FVector(0, 0, 100), FRotator::ZeroRotator);
    ADirectionalLight* Sun = World->SpawnActor<ADirectionalLight>(FVector(0, 0, 500), FRotator(-45, -30, 0));
    ASkyLight* Sky = World->SpawnActor<ASkyLight>();
    if (!Floor || !Cube || !Start || !Sun || !Sky)
    {
        UE_LOG(LogRoguelitePrototype, Error, TEXT("Could not create development map scaffold."));
        return 3;
    }
    Floor->SetActorLabel(TEXT("Roguelite Dev Floor"));
    Floor->GetStaticMeshComponent()->SetStaticMesh(Cube);
    Floor->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));
    Floor->SetActorLocation(FVector(0, 0, -10));
    Floor->SetActorScale3D(FVector(30, 30, 0.2));
    Start->SetActorLabel(TEXT("Roguelite Dev PlayerStart"));

    if (!IFileManager::Get().MakeDirectory(*FPaths::GetPath(MapFilename), true)
        || !UEditorLoadingAndSavingUtils::SaveMap(World, MapPath))
    {
        UE_LOG(LogRoguelitePrototype, Error, TEXT("Could not save development map: %s"), *MapFilename);
        return 4;
    }
    UE_LOG(LogRoguelitePrototype, Display, TEXT("ROGUELITE_DEV_MAP_CREATED %s"), *MapPath);
    return 0;
#else
    UE_LOG(LogRoguelitePrototype, Error, TEXT("Development map creation requires an Editor target."));
    return 1;
#endif
}
