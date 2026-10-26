#include "RogueliteOpenPassageIntegrationNavigation.h"
#include "RogueliteNavigationTestCharacter.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/WorldSettings.h"
#include "UObject/UnrealType.h"

void URogueliteOpenPassageIntegrationNavigation::Configure(const UNavigationSystemConfig& Config)
{
    Super::Configure(Config);
    if(IsTemplate()||!GetWorld()||!GetWorld()->GetOutermost()->GetName().EndsWith(TEXT("L_Roguelite_OpenPassageIntegration_Dev")))return;
    const auto* Capsule=GetDefault<ARogueliteNavigationTestCharacter>()->GetCapsuleComponent();
    FNavDataConfig Agent;Agent.Name=TEXT("RogueliteB3BStandingTest");
    Agent.AgentRadius=Capsule->GetUnscaledCapsuleRadius()+1.f;
    Agent.AgentHeight=Capsule->GetUnscaledCapsuleHalfHeight()*2.f+4.f;
    Agent.SetNavDataClass(ARogueliteRuntimeRecastNavMesh::StaticClass());
    SupportedAgents={Agent};DefaultAgentName=Agent.Name;
    bAutoCreateNavigationData=true;bGenerateNavigationOnlyAroundNavigationInvokers=false;
    bInitialBuildingLocked=false;bSkipAgentHeightCheckWhenPickingNavData=false;
}

ROGUELITEPROTOTYPE_API bool ConfigureHF03CMapNavigation(UWorld* W)
{
#if WITH_EDITOR
    if(!W||W->WorldType!=EWorldType::Editor||W->GetOutermost()->GetName()!=TEXT("/Game/Roguelite/Maps/L_Roguelite_OpenPassageIntegration_Dev"))return false;
    auto* WS=W->GetWorldSettings();auto* Config=NewObject<UNavigationSystemModuleConfig>(WS);
    Config->NavigationSystemClass=FSoftClassPath(URogueliteOpenPassageIntegrationNavigation::StaticClass());
    auto* Property=FindFProperty<FObjectPropertyBase>(AWorldSettings::StaticClass(),TEXT("NavigationSystemConfig"));if(!Property)return false;
    Property->SetObjectPropertyValue_InContainer(WS,Config);
    bool Present=false;for(TActorIterator<ARogueliteRuntimeNavigationBounds> I(W);I;++I)if(!I->IsActorBeingDestroyed())Present=true;
    if(!Present){auto* Bounds=W->SpawnActor<ARogueliteRuntimeNavigationBounds>();Bounds->ConfigureBounds(FBox(FVector(-150,-150,-100),FVector(150,150,300)));Bounds->SetActorLabel(TEXT("HF03C_NavigationBootstrap"));}
    return true;
#else
    return false;
#endif
}
