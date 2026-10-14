#include "RogueliteRuntimeNavigationSystem.h"
#include "RogueliteNavigationTestCharacter.h"
#include "RoguelitePrototype.h"
#include "Components/BoxComponent.h"
#include "Components/BrushComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"

void URogueliteRuntimeNavigationSystem::Configure(const UNavigationSystemConfig& Config)
{
    Super::Configure(Config);
    if(IsTemplate() || !GetWorld())return;
    const FString MapName=GetWorld()->GetOutermost()->GetName();
    // Keep this configuration map-local; B4 uses the unchanged B3B readiness checks.
    if(!MapName.EndsWith(TEXT("L_Roguelite_RuntimeNavigation_Dev")) &&
       !MapName.EndsWith(TEXT("L_Roguelite_Branching_Dev")))return;
    const auto* Capsule=GetDefault<ARogueliteNavigationTestCharacter>()->GetCapsuleComponent();
    FNavDataConfig Agent;
    Agent.Name=TEXT("RogueliteB3BStandingTest");
    Agent.AgentRadius=Capsule->GetUnscaledCapsuleRadius()+1.f;
    Agent.AgentHeight=Capsule->GetUnscaledCapsuleHalfHeight()*2.f+4.f;
    Agent.SetNavDataClass(ARogueliteRuntimeRecastNavMesh::StaticClass());
    SupportedAgents={Agent};
    // Super::Configure selected the previous agent's name. A replacement agent
    // must also replace that name, otherwise GetDefaultNavDataInstance rejects it.
    DefaultAgentName=Agent.Name;
    bAutoCreateNavigationData=true;
    bGenerateNavigationOnlyAroundNavigationInvokers=false;
    bInitialBuildingLocked=false;
    bSkipAgentHeightCheckWhenPickingNavData=false;
    UE_LOG(LogRoguelitePrototype,Display,TEXT("B3B_NAV_CONFIG World=%s Agent=%.1fx%.1f Dynamic class=%s Invokers=0 MapLocal=1"),
        *GetWorld()->GetPathName(),Agent.AgentRadius,Agent.AgentHeight,*Agent.GetNavDataClass<ANavigationData>()->GetName());
}
ARogueliteRuntimeRecastNavMesh::ARogueliteRuntimeRecastNavMesh(const FObjectInitializer& Initializer):Super(Initializer)
{
    RuntimeGeneration=ERuntimeGenerationType::Dynamic;
}
ARogueliteRuntimeNavigationBounds::ARogueliteRuntimeNavigationBounds(const FObjectInitializer& Initializer):Super(Initializer)
{
    GetBrushComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    GetBrushComponent()->SetCanEverAffectNavigation(false);
    Coverage=CreateDefaultSubobject<UBoxComponent>(TEXT("NavigationCoverage"));
    Coverage->SetupAttachment(GetRootComponent());
    Coverage->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Coverage->SetCanEverAffectNavigation(false);
    Coverage->SetBoxExtent(FVector(100));
}
void ARogueliteRuntimeNavigationBounds::ConfigureBounds(const FBox& Bounds)
{
    // Geometry actors never move. Only this owned, non-gameplay coverage volume is resized.
    GetRootComponent()->SetMobility(EComponentMobility::Movable);
    Coverage->SetMobility(EComponentMobility::Movable);
    SetActorLocation(Bounds.GetCenter());
    Coverage->SetBoxExtent(Bounds.GetExtent());
}
