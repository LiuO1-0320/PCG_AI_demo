#include "RogueliteRoomTemplateMapCommandlet.h"
#include "RogueliteRoomTemplateTestRig.h"
#include "RogueliteRoomTemplateNavigationSystem.h"
#include "RogueliteDevGameMode.h"
#include "RoguelitePrototype.h"
#if WITH_EDITOR
#include "Engine/World.h"
#include "Engine/DirectionalLight.h"
#include "Engine/SkyLight.h"
#include "GameFramework/PlayerStart.h"
#include "GameFramework/WorldSettings.h"
#include "NavMesh/NavMeshBoundsVolume.h"
#include "Builders/CubeBuilder.h"
#include "ActorFactories/ActorFactory.h"
#include "Components/BrushComponent.h"
#include "FileHelpers.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "UObject/UnrealType.h"
#endif
URogueliteRoomTemplateMapCommandlet::URogueliteRoomTemplateMapCommandlet()
{ IsClient=false; IsServer=false; IsEditor=true; LogToConsole=true; }
int32 URogueliteRoomTemplateMapCommandlet::Main(const FString& Params)
{
#if WITH_EDITOR
    const FString Name=TEXT("/Game/Roguelite/Maps/L_Roguelite_RoomTemplate_Dev");
    const FString Filename=FPackageName::LongPackageNameToFilename(Name,FPackageName::GetMapPackageExtension());
    if (FPaths::FileExists(Filename)) { UE_LOG(LogRoguelitePrototype, Error, TEXT("P03D_REFUSE_EXISTING_MAP %s"),*Name); return 1; }
    auto* World=UEditorLoadingAndSavingUtils::NewBlankMap(false); if (!World) return 2;
    auto* Settings=World->GetWorldSettings(); Settings->DefaultGameMode=ARogueliteDevGameMode::StaticClass();
    auto* NavConfig=NewObject<UNavigationSystemModuleConfig>(Settings);
    NavConfig->NavigationSystemClass=FSoftClassPath(URogueliteRoomTemplateNavigationSystem::StaticClass());
    // The public override is Transient in UE 5.8.3. Set the reflected, serialized
    // map-local property through the Editor object API so reload/PIE retain it.
    auto* NavProperty=FindFProperty<FObjectPropertyBase>(AWorldSettings::StaticClass(),TEXT("NavigationSystemConfig"));
    if (!NavProperty) return 6;
    NavProperty->SetObjectPropertyValue_InContainer(Settings,NavConfig);
    auto* A=World->SpawnActor<ARogueliteParametricRoom>();
    auto* C=World->SpawnActor<ARogueliteParametricCorridor>(FVector(9000,3000,0),FRotator::ZeroRotator);
    auto* Assembly=World->SpawnActor<ARogueliteRoomAssembly>();
    auto* Rig=World->SpawnActor<ARogueliteRoomTemplateTestRig>();
    if (!A || !C || !Assembly || !Rig) return 3;
    A->SetActorLabel(TEXT("P03D_RoomA")); C->SetActorLabel(TEXT("P03D_Corridor"));
    Assembly->SetActorLabel(TEXT("P03D_Assembly")); Rig->SetActorLabel(TEXT("P03D_TestRig"));
    A->NorthDoor.bEnabled=true; A->NorthDoor.bAutoCenter=false; A->NorthDoor.ManualOffset=250; A->RebuildRoom();
    C->CorridorLength=2000; C->CorridorWidth=500; C->CorridorHeight=350; C->RebuildCorridor();
    Assembly->RoomA=A; Assembly->Corridor=C; Assembly->ConnectionMode=ERogueliteConnectionMode::StraightCorridor; Assembly->bCheckNavigation=true;
    Rig->Assembly=Assembly; Rig->bRunRuntimeProbe=true;
    auto* Bounds=World->SpawnActor<ANavMeshBoundsVolume>(FVector(2000,0,500),FRotator::ZeroRotator);
    if (!Bounds) return 4;
    auto* Builder=NewObject<UCubeBuilder>(); Builder->X=20000; Builder->Y=20000; Builder->Z=2000;
    UActorFactory::CreateBrushForVolumeActor(Bounds,Builder);
    Bounds->SetActorLabel(TEXT("P03D_LocalNavBounds")); Bounds->Tags.Add(TEXT("P03D_LocalNav"));
    Bounds->GetBrushComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Bounds->GetBrushComponent()->SetCanEverAffectNavigation(false);
    World->SpawnActor<APlayerStart>(FVector(0,0,150),FRotator::ZeroRotator);
    World->SpawnActor<ADirectionalLight>(FVector(0,0,1000),FRotator(-45,-30,0)); World->SpawnActor<ASkyLight>();
    if (!IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filename),true) || !UEditorLoadingAndSavingUtils::SaveMap(World,Name)) return 5;
    UE_LOG(LogRoguelitePrototype, Display, TEXT("P03D_NEW_MAP_SAVED %s; no Blueprint/connection/physics verification claimed."),*Name);
    return 0;
#else
    return 1;
#endif
}
