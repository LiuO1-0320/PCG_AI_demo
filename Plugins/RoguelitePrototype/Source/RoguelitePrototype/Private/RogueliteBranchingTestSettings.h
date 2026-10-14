#pragma once
#include "RogueliteSeedAssembly.h"

// Explicit test values only. Existing B2/B3 map/CDO defaults are never changed.
inline FRogueliteSeedLayoutSettings RogueliteBranchingTestSettings(int32 Seed=0)
{
    FRogueliteSeedLayoutSettings S;S.Seed=Seed;S.AlgorithmVersion=2;S.ConfigVersion=2;S.TemplateCatalogVersion=2;
    S.MinMainNodes=S.MaxMainNodes=3;S.MinBranches=S.MaxBranches=3;S.MinBranchDepth=1;S.MaxBranchDepth=2;
    S.MinNodes=S.MaxNodes=8;S.MinDecisionNodes=S.MaxDecisionNodes=3;S.MinNestedDecisionNodes=1;S.MaxBranchGenerationDepth=3;
    S.MaxDegree=3;S.GraphAttempts=32;S.RecursiveBranchWeight=3;S.MaxCandidatesPerEdge=128;
    for(auto& R:S.TypeAssignment.Rules)
    {
        if(R.Type==ERogueliteSeedRoomType::Combat){R.Weight=4;R.MinCount=2;R.MaxCount=5;}
        else if(R.Type!=ERogueliteSeedRoomType::Final){R.Weight=1;R.MinCount=1;R.MaxCount=2;}
    }
    const TArray<ERogueliteSeedRoomType> All={ERogueliteSeedRoomType::Combat,ERogueliteSeedRoomType::Challenge,ERogueliteSeedRoomType::SupplyShop,ERogueliteSeedRoomType::Final,ERogueliteSeedRoomType::Event};
    FRogueliteSeedTemplateOption J;J.Id=TEXT("Junction3");J.TemplateClass=FSoftObjectPath(TEXT("/Game/Roguelite/RoomTemplates/BP_Room_Junction3.BP_Room_Junction3_C"));J.AllowedTypes=All;
    FRogueliteSeedTemplateOption W;W.Id=TEXT("Warehouse");W.TemplateClass=FSoftObjectPath(TEXT("/Game/Roguelite/RoomTemplates/BP_Room_Warehouse.BP_Room_Warehouse_C"));W.AllowedTypes=All;S.Templates={J,W};
    FRogueliteSeedEncounterOption E;E.ProfileId=TEXT("Test_NoEncounter");E.AllowedTypes=All;S.Encounters={E};
    FRogueliteSeedCorridorProfile C;C.LengthCandidates={1000,2000,3000,5000};S.Corridors={C};return S;
}
