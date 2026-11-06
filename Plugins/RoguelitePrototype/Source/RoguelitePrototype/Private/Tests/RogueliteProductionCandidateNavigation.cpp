#include "RogueliteProductionCandidateNavigation.h"
#include "RogueliteNavigationTestCharacter.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/WorldSettings.h"
#include "UObject/UnrealType.h"
#include "UObject/Package.h"
void URogueliteProductionCandidateNavigation::Configure(const UNavigationSystemConfig& Config)
{
    Super::Configure(Config);
    if(IsTemplate()||!GetWorld()||!GetWorld()->GetOutermost()->GetName().EndsWith(TEXT("L_Roguelite_ProductionReadiness_Dev")))return;
    const auto* C=GetDefault<ARogueliteNavigationTestCharacter>()->GetCapsuleComponent();
    FNavDataConfig A;A.Name=TEXT("RogueliteB3BStandingTest");A.AgentRadius=C->GetUnscaledCapsuleRadius()+1;A.AgentHeight=C->GetUnscaledCapsuleHalfHeight()*2+4;
    A.SetNavDataClass(ARogueliteRuntimeRecastNavMesh::StaticClass());SupportedAgents={A};DefaultAgentName=A.Name;
    bAutoCreateNavigationData=true;bGenerateNavigationOnlyAroundNavigationInvokers=false;bInitialBuildingLocked=false;bSkipAgentHeightCheckWhenPickingNavData=false;
}
ROGUELITEPROTOTYPE_API bool ConfigureM1CandidateNavigation(UWorld* W)
{
#if WITH_EDITOR
    if(!W||W->WorldType!=EWorldType::Editor||W->GetOutermost()->GetName()!=TEXT("/Game/Roguelite/Tests/P03M1/L_Roguelite_ProductionReadiness_Dev"))return false;
    auto* WS=W->GetWorldSettings();auto* C=NewObject<UNavigationSystemModuleConfig>(WS);C->NavigationSystemClass=FSoftClassPath(URogueliteProductionCandidateNavigation::StaticClass());
    auto* P=FindFProperty<FObjectPropertyBase>(AWorldSettings::StaticClass(),TEXT("NavigationSystemConfig"));if(!P)return false;P->SetObjectPropertyValue_InContainer(WS,C);
    auto* B=W->SpawnActor<ARogueliteRuntimeNavigationBounds>();if(!B)return false;B->ConfigureBounds(FBox(FVector(-150,-150,-100),FVector(150,150,300)));B->SetActorLabel(TEXT("M1A_NavigationBootstrap"));return true;
#else
    return false;
#endif
}
