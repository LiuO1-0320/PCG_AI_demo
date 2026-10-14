#include "RogueliteMapPreviewCommandlet.h"
#include "RogueliteDungeonMapTestRig.h"
#include "RogueliteDevGameMode.h"
#include "FileHelpers.h"
#include "Engine/World.h"
#include "Engine/DirectionalLight.h"
#include "Engine/SkyLight.h"
#include "GameFramework/PlayerStart.h"
#include "GameFramework/WorldSettings.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
URogueliteMapPreviewCommandlet::URogueliteMapPreviewCommandlet(){IsClient=false;IsServer=false;IsEditor=true;LogToConsole=true;}
int32 URogueliteMapPreviewCommandlet::Main(const FString&)
{
    const FString Name=TEXT("/Game/Roguelite/Maps/L_Roguelite_MapPreview_Dev");const auto File=FPackageName::LongPackageNameToFilename(Name,FPackageName::GetMapPackageExtension());
    if(FPaths::FileExists(File)){UE_LOG(LogTemp,Error,TEXT("E1_REFUSE_EXISTING_MAP"));return 1;}
    auto* W=UEditorLoadingAndSavingUtils::NewBlankMap(false);if(!W)return 2;W->GetWorldSettings()->DefaultGameMode=ARogueliteDevGameMode::StaticClass();
    auto* Rig=W->SpawnActor<ARogueliteDungeonMapTestRig>();if(!Rig)return 3;Rig->SetActorLabel(TEXT("P03E1_MapData"));
    W->SpawnActor<APlayerStart>(FVector(0,-600,150),FRotator::ZeroRotator);W->SpawnActor<ADirectionalLight>(FVector(0,0,1000),FRotator(-45,-30,0));W->SpawnActor<ASkyLight>();
    if(!IFileManager::Get().MakeDirectory(*FPaths::GetPath(File),true)||!UEditorLoadingAndSavingUtils::SaveMap(W,Name))return 4;
    UE_LOG(LogTemp,Display,TEXT("E1_NEW_MAP_SAVED %s NoSavedDungeon=1"),*Name);return 0;
}
