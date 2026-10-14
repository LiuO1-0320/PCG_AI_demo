#include "RogueliteC2NavigationSystem.h"
#include "RoguelitePrototype.h"
#include "Engine/World.h"
#include "NavMesh/RecastNavMesh.h"

void URogueliteC2NavigationSystem::Configure(const UNavigationSystemConfig& Config)
{
    Super::Configure(Config);

    // Configure runs on the newly created World instance before nav-data registration.
    // Keep project defaults, CDOs and every other map's navigation unchanged.
    const UWorld* World = GetWorld();
    if (IsTemplate() || !World || !World->GetOutermost()->GetName().EndsWith(TEXT("L_Roguelite_CorridorAssembly_Dev")))
    {
        return;
    }
    if (SupportedAgents.Num() != 1)
    {
        UE_LOG(LogRoguelitePrototype, Error, TEXT("P03C2_NAV_CONFIG Refused World=%s: expected one supported agent, found %d."),
            *World->GetPathName(), SupportedAgents.Num());
        return;
    }

    SupportedAgents[0].AgentRadius = 35.f;
    SupportedAgents[0].AgentHeight = 180.f;
    SupportedAgents[0].SetNavDataClass(ARecastNavMesh::StaticClass());
    UE_LOG(LogRoguelitePrototype, Display, TEXT("P03C2_NAV_CONFIG World=%s GameWorld=%d Agent=35x180 before registration; project defaults unchanged."),
        *World->GetPathName(), World->IsGameWorld());
}
