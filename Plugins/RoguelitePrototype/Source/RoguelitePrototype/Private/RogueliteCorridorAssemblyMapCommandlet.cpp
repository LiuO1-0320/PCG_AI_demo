#include "RogueliteCorridorAssemblyMapCommandlet.h"
#include "RogueliteAssemblyTestRig.h"
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
#endif
URogueliteCorridorAssemblyMapCommandlet::URogueliteCorridorAssemblyMapCommandlet()
{ IsClient = false; IsServer = false; IsEditor = true; LogToConsole = true; }
int32 URogueliteCorridorAssemblyMapCommandlet::Main(const FString& Params)
{
#if WITH_EDITOR
    const FString Name = TEXT("/Game/Roguelite/Maps/L_Roguelite_CorridorAssembly_Dev");
    const FString Filename = FPackageName::LongPackageNameToFilename(Name, FPackageName::GetMapPackageExtension());
    if (FPaths::FileExists(Filename)) { UE_LOG(LogRoguelitePrototype, Error, TEXT("Refuse existing C2 map")); return 1; }
    auto* World = UEditorLoadingAndSavingUtils::NewBlankMap(false);
    if (!World) return 2;
    World->GetWorldSettings()->DefaultGameMode = ARogueliteDevGameMode::StaticClass();
    auto* A = World->SpawnActor<ARogueliteParametricRoom>(FVector::ZeroVector, FRotator::ZeroRotator);
    auto* B = World->SpawnActor<ARogueliteParametricRoom>(FVector(9000,0,0), FRotator::ZeroRotator);
    auto* C = World->SpawnActor<ARogueliteParametricCorridor>(FVector(9000,3000,0), FRotator::ZeroRotator);
    auto* Assembly = World->SpawnActor<ARogueliteRoomAssembly>();
    auto* Rig = World->SpawnActor<ARogueliteAssemblyTestRig>();
    if (!A || !B || !C || !Assembly || !Rig) return 3;
    A->SetActorLabel(TEXT("P03C2_RoomA")); B->SetActorLabel(TEXT("P03C2_RoomB")); C->SetActorLabel(TEXT("P03C2_Corridor"));
    Assembly->SetActorLabel(TEXT("P03C2_Assembly")); Rig->SetActorLabel(TEXT("P03C2_TestRig")); Rig->bRunRuntimeProbe = true;
    A->RoomHeight = 400; A->NorthDoor.bEnabled = true; A->RebuildRoom();
    B->RoomLength = 3000; B->RoomWidth = 2500; B->RoomHeight = 500; B->SouthDoor.bEnabled = true; B->RebuildRoom();
    C->CorridorLength = 2000; C->CorridorWidth = 500; C->CorridorHeight = 350; C->RebuildCorridor();
    Assembly->RoomA = A; Assembly->RoomB = B; Assembly->Corridor = C;
    Assembly->ConnectionMode = ERogueliteConnectionMode::StraightCorridor;
    Assembly->bCheckNavigation = false;
    // NewBlankMap in a commandlet has no usable physics scene. Seed transforms only;
    // reserve/validate the connection in the real Editor after collision registration.
    FTransform CT, BT; FString Reason;
    if (!Assembly->ComputeCorridorTransforms(A, ERogueliteRoomSide::North, C, ERogueliteCorridorEndpoint::Start,
        B, ERogueliteRoomSide::South, CT, BT, Reason))
    { UE_LOG(LogRoguelitePrototype, Error, TEXT("C2 initial transform calculation: %s"), *Reason); return 4; }
    C->SetActorTransform(CT); B->SetActorTransform(BT); C->RebuildCorridor(); B->RebuildRoom();
    UE_LOG(LogRoguelitePrototype, Display, TEXT("P03C2_INITIAL_PLACEMENT_UNVERIFIED: no connection reservation; actual Editor physics tests required."));
    Assembly->bCheckNavigation = true;
    auto* Bounds = World->SpawnActor<ANavMeshBoundsVolume>(FVector(2000,0,500), FRotator::ZeroRotator);
    if (!Bounds) return 5;
    auto* Builder = NewObject<UCubeBuilder>(); Builder->X = 20000; Builder->Y = 20000; Builder->Z = 2000;
    UActorFactory::CreateBrushForVolumeActor(Bounds, Builder);
    Bounds->SetActorLabel(TEXT("P03C2_LocalNavBounds")); Bounds->Tags.Add(TEXT("P03C2_LocalNav"));
    Bounds->GetBrushComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    World->SpawnActor<APlayerStart>(FVector(0,0,150), FRotator::ZeroRotator);
    World->SpawnActor<ADirectionalLight>(FVector(0,0,1000), FRotator(-45,-30,0));
    World->SpawnActor<ASkyLight>();
    if (!IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filename), true) || !UEditorLoadingAndSavingUtils::SaveMap(World, Name)) return 6;
    UE_LOG(LogRoguelitePrototype, Display, TEXT("P03C2_NEW_BLANK_MAP_SAVED %s"), *Name);
    return 0;
#else
    return 1;
#endif
}
