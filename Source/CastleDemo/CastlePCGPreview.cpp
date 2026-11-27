#include "CastlePCGPreview.h"

#include "CastleDemo.h"
#include "CastleExplorationDimensions.h"
#include "Data/PCGPointData.h"
#include "Metadata/PCGMetadata.h"
#include "PCGContext.h"

void CastleGenerateFloorNodes(int32 RunSeed, int32 Floor, TArray<FCastleRoom>& Rooms, TArray<int32>& Route);

namespace
{
    const FIntPoint Directions[] = {{1, 0}, {0, 1}, {-1, 0}, {0, -1}};

    int32 MixCastleSeed(int32 RunSeed, int32 Floor, uint32 Purpose)
    {
        // Match the production exploration, vault, and guard reward stream salts.
        uint32 Value = uint32(RunSeed) ^ (uint32(Floor) * 0x9e3779b9u) ^ Purpose;
        Value ^= Value >> 16;
        Value *= 0x7feb352du;
        Value ^= Value >> 15;
        Value *= 0x846ca68bu;
        Value ^= Value >> 16;
        return int32(Value);
    }

    int32 Neighbor(int32 Index, int32 Side, int32 Width, int32 Height)
    {
        const int32 X = Index % Width + Directions[Side].X;
        const int32 Y = Index / Width + Directions[Side].Y;
        return X >= 0 && X < Width && Y >= 0 && Y < Height ? Y * Width + X : INDEX_NONE;
    }

    TArray<FCastleRoom> MakeGrid(int32 Width, int32 Height, float Spacing, FVector Origin)
    {
        TArray<FCastleRoom> Rooms;
        Rooms.SetNum(Width * Height);
        for (int32 Index = 0; Index < Rooms.Num(); ++Index)
        {
            Rooms[Index].Center = Origin + FVector(Index % Width * Spacing, Index / Width * Spacing, 0);
            Rooms[Index].bActive = false;
        }
        return Rooms;
    }

    TArray<FCastleRoom> MakeExploration(int32 Seed, int32 Floor)
    {
        auto Rooms = MakeGrid(4, 4, CastleExploration::CellSpacing, FVector::ZeroVector);
        TArray<int32> Route;
        CastleGenerateFloorNodes(Seed, Floor, Rooms, Route);
        return Rooms;
    }

    TArray<FCastleRoom> MakeVault(FRandomStream& Random)
    {
        auto Rooms = MakeGrid(2, 3, 2100.f, FVector(0, 10000, 0));
        // Preserve GenerateCave's enumeration order so the same draw picks the same walk.
        TArray<TArray<int32>> Paths;
        TArray<int32> Path;
        TFunction<void(int32)> Visit = [&](int32 Index)
        {
            Path.Add(Index);
            if (Path.Num() == 4)
                Paths.Add(Path);
            else
                for (int32 Side = 0; Side < 4; ++Side)
                {
                    const int32 Next = Neighbor(Index, Side, 2, 3);
                    if (Next != INDEX_NONE && !Path.Contains(Next))
                        Visit(Next);
                }
            Path.Pop();
        };
        for (int32 Index = 0; Index < Rooms.Num(); ++Index)
            Visit(Index);

        const TArray<int32>& Route = Paths[Random.RandRange(0, Paths.Num() - 1)];
        for (int32 Order = 0; Order < Route.Num(); ++Order)
        {
            FCastleRoom& Room = Rooms[Route[Order]];
            Room.bActive = true;
            Room.RouteOrder = Order;
            Room.Type = Order == 0 ? ECastleRoom::Entrance : Order == 3 ? ECastleRoom::Exit : ECastleRoom::Chest;
            if (Order > 0)
                for (int32 Side = 0; Side < 4; ++Side)
                    if (Neighbor(Route[Order - 1], Side, 2, 3) == Route[Order])
                    {
                        Rooms[Route[Order - 1]].Doors |= 1 << Side;
                        Room.Doors |= 1 << ((Side + 2) % 4);
                        break;
                    }
        }
        return Rooms;
    }

    UPCGPointData* AddOutput(FPCGContext& Context, FName Pin)
    {
        auto* Data = FPCGContext::NewObject_AnyThread<UPCGPointData>(&Context);
        auto& Output = Context.OutputData.TaggedData.Emplace_GetRef();
        Output.Pin = Pin;
        Output.Data = Data;
        return Data;
    }

    FPCGPoint& AddPoint(UPCGPointData& Data, FVector Position, FVector HalfExtent, int32 Seed)
    {
        FPCGPoint& Point = Data.GetMutablePoints().AddDefaulted_GetRef();
        Point.Transform.SetLocation(Position);
        Point.BoundsMin = -HalfExtent;
        Point.BoundsMax = HalfExtent;
        Point.Seed = Seed;
        Point.MetadataEntry = Data.Metadata->AddEntry();
        return Point;
    }

    auto* Integer(UPCGPointData& Data, FName Name)
    {
        return Data.Metadata->CreateAttribute<int32>(Name, 0, false, false);
    }

    void EmitLayout(FPCGContext& Context, const TArray<FCastleRoom>& Cells, bool bVault, int32 Seed)
    {
        auto& Rooms = *AddOutput(Context, TEXT("Rooms"));
        auto& Corridors = *AddOutput(Context, TEXT("Corridors"));
        auto* CellIndex = Integer(Rooms, TEXT("CellIndex"));
        auto* RoomType = Integer(Rooms, TEXT("RoomType"));
        auto* RoomName = Rooms.Metadata->CreateAttribute<FString>(TEXT("RoomName"), FString(), false, false);
        auto* Order = Integer(Rooms, TEXT("RouteOrder"));
        auto* Doors = Integer(Rooms, TEXT("DoorMask"));
        auto* From = Integer(Corridors, TEXT("FromCell"));
        auto* To = Integer(Corridors, TEXT("ToCell"));
        const int32 Width = bVault ? 2 : 4;
        const int32 Height = bVault ? 3 : 4;
        const float Radius = bVault ? 900.f : CastleExploration::RoomHalf;
        const float Spacing = bVault ? 2100.f : CastleExploration::CellSpacing;
        for (int32 Index = 0; Index < Cells.Num(); ++Index)
        {
            const FCastleRoom& Room = Cells[Index];
            if (!Room.bActive)
                continue;
            auto& Point = AddPoint(Rooms, Room.Center, FVector(Radius, Radius, 80.f), Index ^ Seed);
            CellIndex->SetValue(Point.MetadataEntry, Index);
            RoomType->SetValue(Point.MetadataEntry, int32(Room.Type));
            RoomName->SetValue(Point.MetadataEntry, StaticEnum<ECastleRoom>()->GetNameStringByValue(int64(Room.Type)));
            Order->SetValue(Point.MetadataEntry, Room.RouteOrder);
            Doors->SetValue(Point.MetadataEntry, Room.Doors);
            // Only positive grid directions are emitted, one point per shared passage.
            for (int32 Side = 0; Side < 2; ++Side)
                if (Room.Doors & (1 << Side))
                {
                    const int32 Next = Neighbor(Index, Side, Width, Height);
                    const FVector Center = (Room.Center + Cells[Next].Center) * 0.5f;
                    auto& Link = AddPoint(Corridors, Center, FVector(Spacing * 0.5f - Radius, 280, 80), Index ^ Seed);
                    Link.Transform.SetRotation(FRotator(0, Side == 0 ? 0 : 90, 0).Quaternion());
                    From->SetValue(Link.MetadataEntry, Index);
                    To->SetValue(Link.MetadataEntry, Next);
                }
        }
    }

    struct FChestPoints
    {
        explicit FChestPoints(FPCGContext& Context, FName Pin)
            : Data(*AddOutput(Context, Pin))
            , Relic(Integer(Data, TEXT("RelicKind")))
            , Fragments(Integer(Data, TEXT("Fragments")))
            , Ammo(Integer(Data, TEXT("Ammo")))
            , RoomIndex(Integer(Data, TEXT("CellIndex")))
        {
        }

        void Add(FVector Center, int32 Seed, int32 Floor, int32 Index, int32 Reward, bool bAmmo)
        {
            const auto Entry = AddPoint(Data, Center, FVector(48), Seed ^ Index).MetadataEntry;
            Relic->SetValue(Entry, Reward);
            Fragments->SetValue(Entry, 25 + 5 * Floor);
            Ammo->SetValue(Entry, bAmmo ? 48 : 0);
            RoomIndex->SetValue(Entry, Index);
        }

        UPCGPointData& Data;
        FPCGMetadataAttribute<int32>* Relic;
        FPCGMetadataAttribute<int32>* Fragments;
        FPCGMetadataAttribute<int32>* Ammo;
        FPCGMetadataAttribute<int32>* RoomIndex;
    };

    void EmitChests(FPCGContext& Context, const UCastlePCGPreviewSettings& Settings, int32 Seed, int32 Floor)
    {
        FChestPoints Memories(Context, TEXT("Memory")), Traps(Context, TEXT("Trap"));
        FChestPoints VaultChests(Context, TEXT("Vault")), Elite(Context, TEXT("Elite"));
        const auto Exploration = MakeExploration(Seed, Floor);
        FRandomStream ExploreRandom(MixCastleSeed(Seed, Floor, 0x43a2f17bu));
        for (int32 Index = 0; Index < Exploration.Num(); ++Index)
        {
            const auto& Room = Exploration[Index];
            if (!Room.bActive)
                continue;
            switch (Room.Type)
            {
            case ECastleRoom::Combat:
                // Each sentinel position consumes two draws before later rewards are sampled.
                for (int32 Draw = 0; Draw < 2 * (2 + Floor); ++Draw)
                    ExploreRandom.GetFraction();
                break;
            case ECastleRoom::Event:
                ExploreRandom.RandRange(0, 2);
                break;
            case ECastleRoom::Chest:
                Memories.Add(Room.Center, Seed, Floor, Index, ExploreRandom.RandRange(0, 2), true);
                break;
            case ECastleRoom::Trap:
                Traps.Add(Room.Center, Seed, Floor, Index, ExploreRandom.RandRange(0, 2), true);
                break;
            default:
                break;
            }
        }
        if (Floor >= 2)
        {
            FRandomStream VaultRandom(MixCastleSeed(Seed, Floor, 0xcabed123u));
            const auto Vault = MakeVault(VaultRandom);
            for (int32 Index = 0; Index < Vault.Num(); ++Index)
            {
                const auto& Room = Vault[Index];
                if (Room.bActive && Room.RouteOrder > 0)
                    VaultChests.Add(Room.Center, Seed, Floor, Index, Room.RouteOrder == 1 ? VaultRandom.RandRange(0, 2) : 2, true);
            }
        }
        if (Settings.bShowEliteDeathChest)
            Elite.Add(FVector(Settings.EliteDeathPosition.X, Settings.EliteDeathPosition.Y, 0), Seed, Floor, INDEX_NONE, Seed % 3, false);
    }

    class FCastlePCGPreviewElement final : public IPCGElement
    {
        bool ExecuteInternal(FPCGContext* Context) const override
        {
            const auto& Settings = *Context->GetInputSettings<UCastlePCGPreviewSettings>();
            const int32 Seed = FMath::Max(1, Settings.RunSeed);
            const int32 Floor = FMath::Clamp(Settings.Floor, 1, 3);
            switch (Settings.Preview)
            {
            case ECastlePCGPreview::Exploration:
                EmitLayout(*Context, MakeExploration(Seed, Floor), false, Seed);
                break;
            case ECastlePCGPreview::Vault:
            {
                FRandomStream Random(MixCastleSeed(Seed, Floor, 0xcabed123u));
                EmitLayout(*Context, MakeVault(Random), true, Seed);
                break;
            }
            case ECastlePCGPreview::Chests:
                EmitChests(*Context, Settings, Seed, Floor);
                break;
            case ECastlePCGPreview::GuardTickets:
            {
                auto& Data = *AddOutput(*Context, TEXT("Tickets"));
                auto* Ticket = Integer(Data, TEXT("Ticket"));
                auto* Kill = Integer(Data, TEXT("GuardKillOrdinal"));
                const int32 Count = FMath::Clamp(Settings.PreviewRolls, 1, 512);
                const int32 Start = FMath::Clamp(Settings.FirstGuardKill, 1, MAX_int32 - Count);
                const int32 Total = FMath::Clamp(Settings.TotalTickets, 3, 10000);
                for (int32 Index = 0; Index < Count; ++Index)
                {
                    const int32 Ordinal = Start + Index;
                    FRandomStream Random(MixCastleSeed(Seed, Floor, 0xb13c7a09u + uint32(Ordinal)));
                    const FVector Position(Index % 12 * 95, Index / 12 * 95, 0);
                    auto& Point = AddPoint(Data, Position, FVector(38), Ordinal);
                    Ticket->SetValue(Point.MetadataEntry, Random.RandRange(1, Total));
                    Kill->SetValue(Point.MetadataEntry, Ordinal);
                }
                break;
            }
            }
            return true;
        }
    };
}

TArray<FPCGPinProperties> UCastlePCGPreviewSettings::OutputPinProperties() const
{
    switch (Preview)
    {
    case ECastlePCGPreview::Chests:
        return {FPCGPinProperties(TEXT("Memory"), EPCGDataType::Point), FPCGPinProperties(TEXT("Trap"), EPCGDataType::Point),
            FPCGPinProperties(TEXT("Vault"), EPCGDataType::Point), FPCGPinProperties(TEXT("Elite"), EPCGDataType::Point)};
    case ECastlePCGPreview::GuardTickets:
        return {FPCGPinProperties(TEXT("Tickets"), EPCGDataType::Point)};
    default:
        return {FPCGPinProperties(TEXT("Rooms"), EPCGDataType::Point), FPCGPinProperties(TEXT("Corridors"), EPCGDataType::Point)};
    }
}

FPCGElementPtr UCastlePCGPreviewSettings::CreateElement() const
{
    return MakeShared<FCastlePCGPreviewElement>();
}
