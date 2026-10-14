#include "RogueliteRoomTemplateNavigationSystem.h"
#include "RoguelitePrototype.h"
#include "Engine/World.h"
#include "NavMesh/RecastNavMesh.h"
void URogueliteRoomTemplateNavigationSystem::Configure(const UNavigationSystemConfig& Config)
{
    Super::Configure(Config);
    if (IsTemplate() || !GetWorld() || !GetWorld()->GetOutermost()->GetName().EndsWith(TEXT("L_Roguelite_RoomTemplate_Dev"))) return;
    if (SupportedAgents.Num() != 1)
    { UE_LOG(LogRoguelitePrototype, Error, TEXT("P03D_NAV_CONFIG refused: isolated map requires one supported agent.")); return; }
    SupportedAgents[0].AgentRadius = 35; SupportedAgents[0].AgentHeight = 180;
    SupportedAgents[0].SetNavDataClass(ARecastNavMesh::StaticClass());
    UE_LOG(LogRoguelitePrototype, Display, TEXT("P03D_NAV_CONFIG World=%s Game=%d Agent=35x180 before registration; no global config change."),
        *GetWorld()->GetPathName(), GetWorld()->IsGameWorld());
}
