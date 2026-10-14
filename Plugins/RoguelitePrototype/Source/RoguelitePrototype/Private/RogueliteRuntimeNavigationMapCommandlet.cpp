#include "RogueliteRuntimeNavigationMapCommandlet.h"
#include "RogueliteNavigationReadinessTestRig.h"
#include "RogueliteRuntimeNavigationSystem.h"
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
#include "UObject/UnrealType.h"
#endif
URogueliteRuntimeNavigationMapCommandlet::URogueliteRuntimeNavigationMapCommandlet(){IsClient=false;IsServer=false;IsEditor=true;LogToConsole=true;}
int32 URogueliteRuntimeNavigationMapCommandlet::Main(const FString&)
{
#if WITH_EDITOR
    const FString Name=TEXT("/Game/Roguelite/Maps/L_Roguelite_RuntimeNavigation_Dev");
    const auto File=FPackageName::LongPackageNameToFilename(Name,FPackageName::GetMapPackageExtension());
    if(FPaths::FileExists(File)){UE_LOG(LogRoguelitePrototype,Error,TEXT("B3B_REFUSE_EXISTING_MAP"));return 1;}
    auto* W=UEditorLoadingAndSavingUtils::NewBlankMap(false);if(!W)return 2;
    auto* WS=W->GetWorldSettings();WS->DefaultGameMode=ARogueliteDevGameMode::StaticClass();
    auto* Config=NewObject<UNavigationSystemModuleConfig>(WS);Config->NavigationSystemClass=FSoftClassPath(URogueliteRuntimeNavigationSystem::StaticClass());
    auto* Property=FindFProperty<FObjectPropertyBase>(AWorldSettings::StaticClass(),TEXT("NavigationSystemConfig"));if(!Property)return 3;
    Property->SetObjectPropertyValue_InContainer(WS,Config);
    auto* R=W->SpawnActor<ARogueliteNavigationReadinessTestRig>();if(!R)return 4;R->SetActorLabel(TEXT("P03B3B_RuntimeNavigation"));
    // Empty bootstrap coverage retains the map-local NavData through World initialization.
    // It is not a saved dungeon or per-room coverage. The runtime batch computes its own box.
    auto* Bootstrap=W->SpawnActor<ARogueliteRuntimeNavigationBounds>();Bootstrap->ConfigureBounds(FBox(FVector(-150,-150,-100),FVector(150,150,300)));
    Bootstrap->SetActorLabel(TEXT("P03B3B_NavigationBootstrap"));
    W->SpawnActor<APlayerStart>(FVector(0,-600,150),FRotator::ZeroRotator);
    W->SpawnActor<ADirectionalLight>(FVector(0,0,1000),FRotator(-45,-30,0));W->SpawnActor<ASkyLight>();
    if(!IFileManager::Get().MakeDirectory(*FPaths::GetPath(File),true) || !UEditorLoadingAndSavingUtils::SaveMap(W,Name))return 5;
    UE_LOG(LogRoguelitePrototype,Display,TEXT("B3B_NEW_MAP_SAVED %s MapLocalNav=1 NoSavedDungeon=1"),*Name);return 0;
#else
    return 1;
#endif
}
