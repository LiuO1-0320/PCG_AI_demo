#include "CastleDemo.h"
#include "PCGGraph.h"
#include "PCGNode.h"
#include "Elements/PCGCreatePoints.h"
#include "Elements/PCGRandomChoice.h"
#include "Elements/PCGSelectPoints.h"
#include "Elements/PCGTransformPoints.h"

void ACastleDirector::ConfigureBossGraph(UPCGGraph* Graph)
{
    auto Label = [](UPCGNode* Node, FName Title, int32 X, int32 Y)
    {
        Node->NodeTitle = Title;
#if WITH_EDITOR
        Node->SetNodePosition(X, Y);
#endif
    };
    auto AddSites = [&](FName Title, int32 Y, const TArray<FVector>& Locations)
    {
        UPCGCreatePointsSettings* Sites = nullptr;
        auto* Node = Graph->AddNodeOfType(Sites);
        Sites->PointsToCreate.Reset();
        for (const FVector& Location : Locations)
        {
            FPCGPoint& Point = Sites->PointsToCreate.AddDefaulted_GetRef();
            Point.Transform.SetLocation(Location);
            Point.Seed = Sites->PointsToCreate.Num();
        }
        Label(Node, Title, 0, Y);
        return Node;
    };
    auto Choose = [&](UPCGNode* Sites, FName Title, int32 Y, int32 Count)
    {
        UPCGRandomChoiceSettings* Choice = nullptr;
        auto* Node = Graph->AddNodeOfType(Choice);
        Choice->FixedNumber = Count;
        Label(Node, Title, 300, Y);
        Graph->AddEdge(Sites, TEXT("Out"), Node, TEXT("In"));
        return Node;
    };
    auto Jitter = [&](FName Title, int32 Y, FVector Offset, float Yaw)
    {
        UPCGTransformPointsSettings* Transform = nullptr;
        auto* Node = Graph->AddNodeOfType(Transform);
        Transform->OffsetMin = -Offset;
        Transform->OffsetMax = Offset;
        Transform->RotationMin.Yaw = -Yaw;
        Transform->RotationMax.Yaw = Yaw;
        Label(Node, Title, 920, Y);
        return Node;
    };

    TArray<FVector> Corners;
    for (int32 X : {-1, 1})
        for (int32 Y : {-1, 1})
            Corners.Emplace(X * 1430, Y * 525, 240);
    auto* Decks = Choose(AddSites(TEXT("Platform corners"), 0, Corners), TEXT("Two guaranteed platforms"), 0, 2);
    UPCGSelectPointsSettings* OptionalSettings = nullptr;
    auto* Optional = Graph->AddNodeOfType(OptionalSettings);
    OptionalSettings->Ratio = 0.5f;
    Label(Optional, TEXT("Optional platforms"), 600, 160);
    Graph->AddEdge(Decks, TEXT("Discarded"), Optional, TEXT("In"));
    auto* DeckPose = Jitter(TEXT("Platform position and yaw"), 0, FVector(30, 15, 0), 4);
    Graph->AddEdge(Decks, TEXT("Chosen"), DeckPose, TEXT("In"));
    Graph->AddEdge(Optional, TEXT("Out"), DeckPose, TEXT("In"));

    // Even at maximum jitter adjacent sites keep 300 cm between column centers.
    TArray<FVector> SideLanes;
    for (int32 Y : {-1, 1})
        for (int32 I = -4; I <= 4; ++I)
            SideLanes.Emplace(I * 400, Y * 1450, 0);
    auto* Cover = Choose(AddSites(TEXT("Cover side lanes"), 480, SideLanes), TEXT("Sample cover sites"), 480, 10);
    auto* CoverPose = Jitter(TEXT("Cover position and yaw"), 480, FVector(50, 90, 0), 180);
    Graph->AddEdge(Cover, TEXT("Chosen"), CoverPose, TEXT("In"));

    UCastleLayoutSettings* Arena = nullptr;
    auto* Assemble = Graph->AddNodeOfType(Arena);
    Arena->GeneratorRegion = ECastlePCGRegion::BossArena;
    Assemble->UpdateAfterSettingsChangeDuringCreation();
    Label(Assemble, TEXT("Build playable palace"), 1260, 180);
    Graph->AddEdge(DeckPose, TEXT("Out"), Assemble, TEXT("Platforms"));
    Graph->AddEdge(CoverPose, TEXT("Out"), Assemble, TEXT("Cover"));
    Graph->AddEdge(Assemble, TEXT("Out"), Graph->GetOutputNode(), TEXT("Out"));
#if WITH_EDITOR
    Graph->GetOutputNode()->SetNodePosition(1570, 180);
    Graph->GetInputNode()->SetNodePosition(-320, -180);
#endif
}
