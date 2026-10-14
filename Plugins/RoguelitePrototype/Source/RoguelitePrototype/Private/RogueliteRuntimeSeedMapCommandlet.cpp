#include "RogueliteRuntimeSeedMapCommandlet.h"
#include "RogueliteRuntimeSeedAssembly.h"
#include "RogueliteDevGameMode.h"
#include "RoguelitePrototype.h"
#if WITH_EDITOR
#include "Engine/World.h"
#include "Engine/DirectionalLight.h"
#include "Engine/SkyLight.h"
#include "GameFramework/PlayerStart.h"
#include "GameFramework/WorldSettings.h"
#include "FileHelpers.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#endif
URogueliteRuntimeSeedMapCommandlet::URogueliteRuntimeSeedMapCommandlet(){IsClient=false;IsServer=false;IsEditor=true;LogToConsole=true;}
int32 URogueliteRuntimeSeedMapCommandlet::Main(const FString&)
{
#if WITH_EDITOR
    const FString Name=TEXT("/Game/Roguelite/Maps/L_Roguelite_RuntimeSeedAssembly_Dev");
    const FString File=FPackageName::LongPackageNameToFilename(Name,FPackageName::GetMapPackageExtension());
    if(FPaths::FileExists(File)){UE_LOG(LogRoguelitePrototype,Error,TEXT("B3A_REFUSE_EXISTING_MAP"));return 1;}
    auto* W=UEditorLoadingAndSavingUtils::NewBlankMap(false);if(!W)return 2;
    W->GetWorldSettings()->DefaultGameMode=ARogueliteDevGameMode::StaticClass();
    auto* Rig=W->SpawnActor<ARogueliteRuntimeSeedAssembly>();if(!Rig)return 3;Rig->SetActorLabel(TEXT("P03B3A_RuntimeSeedAssembly"));
    W->SpawnActor<APlayerStart>(FVector(0,0,150),FRotator::ZeroRotator);
    W->SpawnActor<ADirectionalLight>(FVector(0,0,1000),FRotator(-45,-30,0));W->SpawnActor<ASkyLight>();
    if(!IFileManager::Get().MakeDirectory(*FPaths::GetPath(File),true) || !UEditorLoadingAndSavingUtils::SaveMap(W,Name))return 4;
    UE_LOG(LogRoguelitePrototype,Display,TEXT("B3A_NEW_MAP_SAVED %s; ZERO saved Room/Corridor/Assembly actors."),*Name);return 0;
#else
    return 1;
#endif
}
