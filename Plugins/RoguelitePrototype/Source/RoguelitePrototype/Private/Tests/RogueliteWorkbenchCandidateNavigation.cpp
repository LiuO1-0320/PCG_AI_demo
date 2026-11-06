#include "Tests/RogueliteWorkbenchCandidateNavigation.h"
#include "RogueliteRuntimeNavigationSystem.h"
#include "RogueliteNavigationTestCharacter.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "UObject/UnrealType.h"
#include "UObject/Package.h"

void URogueliteWorkbenchCandidateNavigation::Configure(const UNavigationSystemConfig& Config)
{
    Super::Configure(Config);
    if(IsTemplate()||!GetWorld()||!GetWorld()->GetOutermost()->GetName().EndsWith(TEXT("L_Roguelite_WorkbenchCandidates_Dev")))return;
    const auto* Capsule=GetDefault<ARogueliteNavigationTestCharacter>()->GetCapsuleComponent();
    FNavDataConfig Agent;Agent.Name=TEXT("RogueliteB3BStandingTest");
    Agent.AgentRadius=Capsule->GetUnscaledCapsuleRadius()+1;Agent.AgentHeight=Capsule->GetUnscaledCapsuleHalfHeight()*2+4;
    Agent.SetNavDataClass(ARogueliteRuntimeRecastNavMesh::StaticClass());SupportedAgents={Agent};DefaultAgentName=Agent.Name;
    bAutoCreateNavigationData=true;bGenerateNavigationOnlyAroundNavigationInvokers=false;
    bInitialBuildingLocked=false;bSkipAgentHeightCheckWhenPickingNavData=false;
}

ROGUELITEPROTOTYPE_API bool ConfigureM1BWorkbenchNavigation(UWorld* World)
{
#if WITH_EDITOR
    if(!World||World->WorldType!=EWorldType::Editor||World->GetOutermost()->GetName()!=TEXT("/Game/Roguelite/Tests/P03M1B/L_Roguelite_WorkbenchCandidates_Dev"))return false;
    auto* Settings=World->GetWorldSettings();auto* Config=NewObject<UNavigationSystemModuleConfig>(Settings);
    Config->NavigationSystemClass=FSoftClassPath(URogueliteWorkbenchCandidateNavigation::StaticClass());
    auto* Property=FindFProperty<FObjectPropertyBase>(AWorldSettings::StaticClass(),TEXT("NavigationSystemConfig"));if(!Property)return false;
    Property->SetObjectPropertyValue_InContainer(Settings,Config);
    auto* Bounds=World->SpawnActor<ARogueliteRuntimeNavigationBounds>();if(!Bounds)return false;
    Bounds->ConfigureBounds(FBox(FVector(-150,-150,-100),FVector(150,150,300)));Bounds->SetActorLabel(TEXT("M1B_NavigationBootstrap"));return true;
#else
    return false;
#endif
}
