#include "CastlePCGPreview.h"

#if WITH_EDITOR
#include "CastleDemo.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Elements/PCGAttributeFilter.h"
#include "Elements/PCGTransformPoints.h"
#include "Misc/PackageName.h"
#include "PCGGraph.h"
#include "PCGInputOutputSettings.h"
#include "PCGNode.h"
#include "UObject/SavePackage.h"

namespace
{
    constexpr const TCHAR* Folder = TEXT("/Game/Demo/PCGAuthoring");
    const FName CastleFlowIn(TEXT("In"));
    const FName CastleFlowOut(TEXT("Out"));

    void Describe(UPCGNode& Node, FName Title, int32 X, int32 Y, const TCHAR* Detail)
    {
        Node.NodeTitle = Title;
        Node.SetNodePosition(X, Y);
        Node.NodeComment = Detail;
    }

    void Link(UPCGGraph& Graph, UPCGNode& From, FName FromPin, UPCGNode& To, FName ToPin)
    {
        checkf(From.GetOutputPin(FromPin) && To.GetInputPin(ToPin), TEXT("Invalid pins: %s to %s"), *FromPin.ToString(), *ToPin.ToString());
        Graph.AddEdge(&From, FromPin, &To, ToPin);
    }

    UPCGNode& AddSource(UPCGGraph& Graph, ECastlePCGPreview Mode, FName Title, const TCHAR* Comment)
    {
        UCastlePCGPreviewSettings* Settings = nullptr;
        UPCGNode& Node = *Graph.AddNodeOfType(Settings);
        Settings->Preview = Mode;
        Node.UpdateAfterSettingsChangeDuringCreation();
        Describe(Node, Title, 0, 0, Comment);
        return Node;
    }

    void Output(UPCGGraph& Graph, UPCGNode& From, FName FromPin, FName OutputPin)
    {
        UPCGNode& Node = *Graph.GetOutputNode();
        if (OutputPin != CastleFlowOut && !Node.GetInputPin(OutputPin))
        {
            auto* Settings = CastChecked<UPCGGraphInputOutputSettings>(Node.GetSettings());
            const FName Label = Settings->AddPin(FPCGPinProperties(OutputPin, EPCGDataType::Point)).Label;
            check(Label == OutputPin);
            Node.UpdateAfterSettingsChangeDuringCreation();
        }
        Link(Graph, From, FromPin, Node, OutputPin);
    }

    UPCGNode& AddFilter(UPCGGraph& Graph, UPCGNode& Source, FName SourcePin, FName Attribute,
        EPCGAttributeFilterOperator Operator, int32 Constant, FName Title, int32 Y)
    {
        UPCGAttributeFilteringSettings* Settings = nullptr;
        UPCGNode& Node = *Graph.AddNodeOfType(Settings);
        Settings->TargetAttribute.SetAttributeName(Attribute);
        Settings->Operator = Operator;
        Settings->bUseConstantThreshold = true;
        Settings->AttributeTypes.Type = EPCGMetadataTypes::Integer32;
        Settings->AttributeTypes.Int32Value = Constant;
        Node.UpdateAfterSettingsChangeDuringCreation();
        Describe(Node, Title, 360, Y, TEXT("Inspect the input attribute and the two output branches."));
        Link(Graph, Source, SourcePin, Node, CastleFlowIn);
        return Node;
    }

    void BuildExploration(UPCGGraph& Graph)
    {
        auto& Source = AddSource(Graph, ECastlePCGPreview::Exploration, TEXT("Seeded 4 x 4 floor"),
            TEXT("Same route generator as CastleDemo. The floor has a seven-room primary route, connected side rooms and a second route to the exit."));
        auto& Routes = AddFilter(Graph, Source, TEXT("Rooms"), TEXT("RouteOrder"),
            EPCGAttributeFilterOperator::GreaterOrEqual, 0, TEXT("Main route or side room"), 0);
        Output(Graph, Source, TEXT("Rooms"), CastleFlowOut);
        Output(Graph, Routes, PCGPinConstants::DefaultInFilterLabel, TEXT("MainRoute"));
        Output(Graph, Routes, PCGPinConstants::DefaultOutFilterLabel, TEXT("SideRooms"));
        Output(Graph, Source, TEXT("Corridors"), TEXT("Corridors"));
    }

    void BuildVault(UPCGGraph& Graph)
    {
        auto& Source = AddSource(Graph, ECastlePCGPreview::Vault, TEXT("Seeded 2 x 3 vault"),
            TEXT("Select one legal four-room walk. Use floor 2 or 3 to match a floor with a playable vault."));
        auto& Entrance = AddFilter(Graph, Source, TEXT("Rooms"), TEXT("RouteOrder"),
            EPCGAttributeFilterOperator::Equal, 0, TEXT("Entrance at order 0"), 0);
        auto& Exit = AddFilter(Graph, Source, TEXT("Rooms"), TEXT("RouteOrder"),
            EPCGAttributeFilterOperator::Equal, 3, TEXT("Exit at order 3"), 220);
        Output(Graph, Source, TEXT("Rooms"), CastleFlowOut);
        Output(Graph, Entrance, PCGPinConstants::DefaultInFilterLabel, TEXT("Entrance"));
        Output(Graph, Exit, PCGPinConstants::DefaultInFilterLabel, TEXT("Exit"));
        Output(Graph, Source, TEXT("Corridors"), TEXT("Corridors"));
    }

    void BuildChests(UPCGGraph& Graph)
    {
        auto& Source = AddSource(Graph, ECastlePCGPreview::Chests, TEXT("Seeded chest sites and rewards"),
            TEXT("The outputs carry RelicKind, Fragments and Ammo. Elite output is opt-in using a preview death location. The graph never grants rewards."));
        auto Place = [&](FName Pin, FVector Offset, int32 Y, const TCHAR* Detail)
        {
            UPCGTransformPointsSettings* Settings = nullptr;
            UPCGNode& Node = *Graph.AddNodeOfType(Settings);
            Settings->OffsetMin = Settings->OffsetMax = Offset;
            Describe(Node, Pin, 360, Y, Detail);
            Link(Graph, Source, Pin, Node, CastleFlowIn);
            Output(Graph, Node, CastleFlowOut, Pin);
        };
        Place(TEXT("Memory"), FVector(230, 0, 45), 0, TEXT("Exploration reward on the chest room branch."));
        Place(TEXT("Trap"), FVector(580, 450, 45), 175, TEXT("Exploration reward in trap rooms."));
        Place(TEXT("Vault"), FVector(420, 400, 45), 350, TEXT("No chest at the entrance. The next room rolls a relic, then the last two use relic 2."));
        Place(TEXT("Elite"), FVector(0, 0, 35), 525, TEXT("Elite reward uses the death XY, fixed Z, and no ammo."));
    }

    void BuildLoot(UPCGGraph& Graph)
    {
        auto& Source = AddSource(Graph, ECastlePCGPreview::GuardTickets, TEXT("Guard kill ticket stream"),
            TEXT("Default total: 15 tickets. Each ordinary kill advances its ordinal, including an empty drop. Set the starting ordinal and sample count for inspection."));
        auto& Drop = AddFilter(Graph, Source, TEXT("Tickets"), TEXT("Ticket"),
            EPCGAttributeFilterOperator::LesserOrEqual, 3, TEXT("Tickets 1–3 grant a mod"), 0);
        Output(Graph, Drop, PCGPinConstants::DefaultInFilterLabel, CastleFlowOut);
        Output(Graph, Drop, PCGPinConstants::DefaultOutFilterLabel, TEXT("NoDrop"));
        for (auto [Ticket, Name] : {TPair<int32, FName>(1, TEXT("RapidFire")),
            TPair<int32, FName>(2, TEXT("Spread")), TPair<int32, FName>(3, TEXT("InfiniteAmmo"))})
        {
            auto& Mod = AddFilter(Graph, Source, TEXT("Tickets"), TEXT("Ticket"),
                EPCGAttributeFilterOperator::Equal, Ticket, Name, Ticket * 175);
            Output(Graph, Mod, PCGPinConstants::DefaultInFilterLabel, Name);
        }
    }

    void BuildBoss(UPCGGraph& Graph)
    {
        ACastleDirector::ConfigureBossGraph(&Graph);
        UPCGNode* Assembly = nullptr;
        UPCGNode* Platforms = nullptr;
        UPCGNode* Cover = nullptr;
        for (UPCGNode* Node : Graph.GetNodes())
        {
            if (Node->NodeTitle == TEXT("Platform position and yaw")) Platforms = Node;
            if (Node->NodeTitle == TEXT("Cover position and yaw")) Cover = Node;
            if (Node->GetSettings()->IsA<UCastleLayoutSettings>()) Assembly = Node;
        }
        check(Assembly && Platforms && Cover);
        Graph.RemoveNode(Assembly);
        Output(Graph, *Platforms, CastleFlowOut, TEXT("Platforms"));
        Output(Graph, *Cover, CastleFlowOut, TEXT("Cover"));
        Graph.GetOutputNode()->SetNodePosition(1280, 160);
    }
}
#endif

UCastlePCGAuthoringCommandlet::UCastlePCGAuthoringCommandlet()
{
    IsClient = false;
    IsServer = false;
    LogToConsole = true;
}

int32 UCastlePCGAuthoringCommandlet::Main(const FString&)
{
#if WITH_EDITOR
    const struct FGraphDefinition
    {
        const TCHAR* Name;
        const TCHAR* Description;
        void (*Build)(UPCGGraph&);
    } Definitions[] = {
        {TEXT("PCG_Castle_Exploration_Layout"), TEXT("Seeded room topology and corridors for the Castle exploration floor."), BuildExploration},
        {TEXT("PCG_Castle_Vault_Layout"), TEXT("Seeded four-room vault walk with entry, exit and corridor outputs."), BuildVault},
        {TEXT("PCG_Castle_Boss_Layout"), TEXT("Original native platform and cover sampling nodes, with preview point outputs."), BuildBoss},
        {TEXT("PCG_Castle_Guard_Loot"), TEXT("Guard kill tickets, empty rolls and three independently inspectable weapon-mod outputs."), BuildLoot},
        {TEXT("PCG_Castle_Chest_Placement"), TEXT("Exploration, trap, vault and elite chest data and placement offsets. Preview only."), BuildChests}
    };

    for (const auto& Definition : Definitions)
    {
        const FString Path = FString::Printf(TEXT("%s/%s"), Folder, Definition.Name);
        if (FPackageName::DoesPackageExist(Path))
        {
            UE_LOG(LogTemp, Error, TEXT("Refusing to replace existing PCG asset: %s"), *Path);
            return 1;
        }
    }
    for (const auto& Definition : Definitions)
    {
        const FString Path = FString::Printf(TEXT("%s/%s"), Folder, Definition.Name);
        UPackage* Package = CreatePackage(*Path);
        UPCGGraph* Graph = NewObject<UPCGGraph>(Package, Definition.Name, RF_Public | RF_Standalone | RF_Transactional);
        Graph->Description = FText::FromString(Definition.Description);
        Graph->GetInputNode()->SetNodePosition(-330, -160);
        Graph->GetOutputNode()->SetNodePosition(800, 140);
        Definition.Build(*Graph);
        FAssetRegistryModule::AssetCreated(Graph);
        Package->MarkPackageDirty();
        const FString Filename = FPackageName::LongPackageNameToFilename(Path, FPackageName::GetAssetPackageExtension());
        FSavePackageArgs Args;
        Args.TopLevelFlags = RF_Public | RF_Standalone;
        if (!UPackage::SavePackage(Package, Graph, *Filename, Args))
            return 1;
        UE_LOG(LogTemp, Display, TEXT("CASTLE_PCG_GRAPH_SAVED %s"), *Path);
    }
    return 0;
#else
    return 1;
#endif
}
