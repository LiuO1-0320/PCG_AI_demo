#include "RogueliteLoopCommitMapCommandlet.h"
#include "RogueliteLoopCommitTestRig.h"
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
URogueliteLoopCommitMapCommandlet::URogueliteLoopCommitMapCommandlet(){IsClient=false;IsServer=false;IsEditor=true;LogToConsole=true;}
int32 URogueliteLoopCommitMapCommandlet::Main(const FString&)
{
#if WITH_EDITOR
    const FString Name=TEXT("/Game/Roguelite/Maps/L_Roguelite_LoopCommit_Dev");const auto File=FPackageName::LongPackageNameToFilename(Name,FPackageName::GetMapPackageExtension());
    if(FPaths::FileExists(File)){UE_LOG(LogRoguelitePrototype,Error,TEXT("FIXED_LOOP_REFUSE_EXISTING_MAP"));return 1;}
    auto* W=UEditorLoadingAndSavingUtils::NewBlankMap(false);if(!W)return 2;W->GetWorldSettings()->DefaultGameMode=ARogueliteDevGameMode::StaticClass();
    auto* Rig=W->SpawnActor<ARogueliteLoopCommitTestRig>();if(!Rig)return 3;Rig->SetActorLabel(TEXT("B4B2A_FixedLoopCommit"));
    W->SpawnActor<APlayerStart>(FVector(0,-600,150),FRotator::ZeroRotator);W->SpawnActor<ADirectionalLight>(FVector(0,0,1000),FRotator(-45,-30,0));W->SpawnActor<ASkyLight>();
    if(!IFileManager::Get().MakeDirectory(*FPaths::GetPath(File),true)||!UEditorLoadingAndSavingUtils::SaveMap(W,Name))return 4;
    UE_LOG(LogRoguelitePrototype,Display,TEXT("FIXED_LOOP_NEW_MAP_SAVED %s RuntimeNavigationNotConfigured=1 SavedTreeActors=0"),*Name);return 0;
#else
    return 1;
#endif
}
