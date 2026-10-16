#include "RogueliteParametricRoom.h"
#include "RoguelitePrototype.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "UObject/ConstructorHelpers.h"

#if WITH_EDITORONLY_DATA
#include "Components/ArrowComponent.h"
#endif

#if WITH_EDITOR
#include "Logging/MessageLog.h"
#include "Misc/UObjectToken.h"
#endif

namespace
{
    void UpdateShell(UStaticMeshComponent* Component, const FVector& Center, const FVector& Size, bool bActive = true)
    {
        if (!Component)
        {
            return;
        }
        if (!bActive)
        {
            Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
            Component->SetVisibility(false);
            return;
        }
        if (Component->GetStaticMesh())
        {
            const FVector MeshSize = Component->GetStaticMesh()->GetBoundingBox().GetSize();
            if (MeshSize.GetMin() > UE_SMALL_NUMBER)
            {
                Component->SetRelativeTransform(FTransform(FQuat::Identity, Center, Size / MeshSize));
            }
        }
        Component->SetVisibility(true);
        Component->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    }

    const TCHAR* SideName(ERogueliteRoomSide Side)
    {
        switch (Side)
        {
        case ERogueliteRoomSide::North: return TEXT("North");
        case ERogueliteRoomSide::South: return TEXT("South");
        case ERogueliteRoomSide::East: return TEXT("East");
        case ERogueliteRoomSide::West: return TEXT("West");
        default: return TEXT("Unknown");
        }
    }
}

ARogueliteParametricRoom::ARogueliteParametricRoom()
{
    PrimaryActorTick.bCanEverTick = false;
    RoomRoot = CreateDefaultSubobject<USceneComponent>(TEXT("RoomRoot"));
    SetRootComponent(RoomRoot);
    RoomRoot->SetMobility(EComponentMobility::Static);

    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
    const auto CreateShell = [this](const TCHAR* Name, UStaticMesh* Mesh)
    {
        UStaticMeshComponent* Component = CreateDefaultSubobject<UStaticMeshComponent>(Name);
        Component->SetupAttachment(RoomRoot);
        Component->SetMobility(EComponentMobility::Static);
        Component->SetStaticMesh(Mesh);
        Component->SetCollisionProfileName(TEXT("BlockAll"));
        Component->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
        Component->SetGenerateOverlapEvents(false);
        return Component;
    };
    Floor = CreateShell(TEXT("Floor"), Cube.Object);
    NorthWall = CreateShell(TEXT("NorthWall"), Cube.Object);
    SouthWall = CreateShell(TEXT("SouthWall"), Cube.Object);
    EastWall = CreateShell(TEXT("EastWall"), Cube.Object);
    WestWall = CreateShell(TEXT("WestWall"), Cube.Object);

    const TCHAR* RightNames[] = {TEXT("NorthWallRight"), TEXT("SouthWallRight"), TEXT("EastWallRight"), TEXT("WestWallRight")};
    const TCHAR* LintelNames[] = {TEXT("NorthWallLintel"), TEXT("SouthWallLintel"), TEXT("EastWallLintel"), TEXT("WestWallLintel")};
#if WITH_EDITORONLY_DATA
    const TCHAR* MarkerNames[] = {TEXT("NorthDoorMarker"), TEXT("SouthDoorMarker"), TEXT("EastDoorMarker"), TEXT("WestDoorMarker")};
#endif
    for (int32 SideIndex = 0; SideIndex < 4; ++SideIndex)
    {
        UStaticMeshComponent* Right = CreateShell(RightNames[SideIndex], Cube.Object);
        UStaticMeshComponent* Lintel = CreateShell(LintelNames[SideIndex], Cube.Object);
        UpdateShell(Right, FVector::ZeroVector, FVector::OneVector, false);
        UpdateShell(Lintel, FVector::ZeroVector, FVector::OneVector, false);
        DoorRightSegments.Add(Right);
        DoorLintelSegments.Add(Lintel);
#if WITH_EDITORONLY_DATA
        UArrowComponent* Marker = CreateEditorOnlyDefaultSubobject<UArrowComponent>(MarkerNames[SideIndex]);
        if (Marker)
        {
            Marker->SetupAttachment(RoomRoot);
            Marker->SetMobility(EComponentMobility::Static);
            Marker->SetHiddenInGame(true);
            Marker->SetCollisionEnabled(ECollisionEnabled::NoCollision);
            Marker->SetArrowLength(120.0f);
            Marker->SetVisibility(false);
        }
        DoorSocketMarkers.Add(Marker);
#endif
    }
}

void ARogueliteParametricRoom::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);
    RebuildRoom();
}

void ARogueliteParametricRoom::PostRegisterAllComponents()
{
    Super::PostRegisterAllComponents();
    // Refresh transient socket diagnostics on map load, before play, without Tick.
    RebuildRoom();
}

FBox ARogueliteParametricRoom::CalculateInteriorBounds() const
{
    return FBox(FVector(-RoomLength * 0.5, -RoomWidth * 0.5, 0),
                FVector(RoomLength * 0.5, RoomWidth * 0.5, RoomHeight));
}

void ARogueliteParametricRoom::RebuildRoom()
{
    ValidationWarnings.Reset();
    const auto Sanitize = [this](float& Value, float Minimum, const TCHAR* Name)
    {
        if (!FMath::IsFinite(Value) || Value < Minimum)
        {
            ValidationWarnings.Add(FString::Printf(TEXT("P01_INVALID_DIMENSION Room=%s Property=%s: 非法尺寸已限制为至少 %.1f cm。"),
                *GetPathName(), Name, Minimum));
            Value = Minimum;
        }
    };
    Sanitize(RoomLength, 100.0f, TEXT("RoomLength"));
    Sanitize(RoomWidth, 100.0f, TEXT("RoomWidth"));
    Sanitize(RoomHeight, 100.0f, TEXT("RoomHeight"));
    Sanitize(WallThickness, 1.0f, TEXT("WallThickness"));
    Sanitize(FloorThickness, 1.0f, TEXT("FloorThickness"));

    if (!GetActorScale3D().Equals(FVector::OneVector, KINDA_SMALL_NUMBER))
    {
        ValidationWarnings.Add(FString::Printf(TEXT("P01_ACTOR_SCALE Room=%s: 请将 Actor Scale 设为 1,1,1，使用 RoomLength/Width/Height 控制尺寸。"), *GetPathName()));
    }

    UpdateShell(Floor, FVector(0, 0, -FloorThickness * 0.5),
        FVector(RoomLength + 2 * WallThickness, RoomWidth + 2 * WallThickness, FloorThickness));
    DoorSockets.Reset();
    UpdateDoorWall(ERogueliteRoomSide::North, NorthWall);
    UpdateDoorWall(ERogueliteRoomSide::South, SouthWall);
    UpdateDoorWall(ERogueliteRoomSide::East, EastWall);
    UpdateDoorWall(ERogueliteRoomSide::West, WestWall);

    ValidateManualPOIs();
    for (const FString& Warning : ValidationWarnings)
    {
        UE_LOG(LogRoguelitePrototype, Warning, TEXT("%s"), *Warning);
    }
}

const FRogueliteDoorConfig& ARogueliteParametricRoom::GetDoorConfig(ERogueliteRoomSide Side) const
{
    switch (Side)
    {
    case ERogueliteRoomSide::South: return SouthDoor;
    case ERogueliteRoomSide::East: return EastDoor;
    case ERogueliteRoomSide::West: return WestDoor;
    default: return NorthDoor;
    }
}

FRogueliteDoorSocket ARogueliteParametricRoom::CalculateDoorSocket(ERogueliteRoomSide Side) const
{
    return CalculateDoorSocketWithConfig(Side, GetDoorConfig(Side));
}

bool ARogueliteParametricRoom::PreviewEnabledDoorSocket(ERogueliteRoomSide Side, FRogueliteDoorSocket& OutSocket, bool& bRequiresEnable) const
{
    OutSocket = FRogueliteDoorSocket();
    bRequiresEnable = false;
    if (static_cast<uint8>(Side) > static_cast<uint8>(ERogueliteRoomSide::West)) return false;
    auto Config = GetDoorConfig(Side);
    bRequiresEnable = !Config.bEnabled;
    Config.bEnabled = true; // Local value copy only; existing wall/Actor remains untouched.
    OutSocket = CalculateDoorSocketWithConfig(Side, Config);
    return OutSocket.bValid;
}

FRogueliteDoorSocket ARogueliteParametricRoom::CalculateDoorSocketWithConfig(ERogueliteRoomSide Side, const FRogueliteDoorConfig& Config) const
{
    const bool bAlongX = Side == ERogueliteRoomSide::East || Side == ERogueliteRoomSide::West;
    const FVector Tangent = bAlongX ? FVector::ForwardVector : FVector::RightVector;
    FVector Normal;
    switch (Side)
    {
    case ERogueliteRoomSide::North: Normal = FVector::ForwardVector; break;
    case ERogueliteRoomSide::South: Normal = -FVector::ForwardVector; break;
    case ERogueliteRoomSide::East: Normal = FVector::RightVector; break;
    default: Normal = -FVector::RightVector; break;
    }
    FRogueliteDoorSocket Socket;
    Socket.Side = Side;
    Socket.DoorWidth = FMath::IsFinite(Config.DoorWidth) ? Config.DoorWidth : 0.0f;
    Socket.DoorHeight = FMath::IsFinite(Config.DoorHeight) ? Config.DoorHeight : 0.0f;
    Socket.WallThickness = WallThickness;
    Socket.AppliedOffset = Config.bAutoCenter || !FMath::IsFinite(Config.ManualOffset) ? 0.0f : Config.ManualOffset;
    Socket.LocalOutwardDirection = Normal;
    Socket.LocalPosition = Normal * ((bAlongX ? RoomWidth : RoomLength) * 0.5 + WallThickness) + Tangent * Socket.AppliedOffset;
    Socket.WorldPosition = GetActorTransform().TransformPosition(Socket.LocalPosition);
    Socket.WorldOutwardDirection = GetActorTransform().TransformVectorNoScale(Normal).GetSafeNormal();
    if (!Config.bEnabled)
    {
        return Socket;
    }

    FString Reason;
    const float Span = bAlongX ? RoomLength : RoomWidth;
    if (!FMath::IsFinite(Config.DoorWidth) || Config.DoorWidth < 1.0f
        || !FMath::IsFinite(Config.DoorHeight) || Config.DoorHeight < 1.0f)
    {
        Reason = TEXT("DoorWidth / DoorHeight 必须是有限且至少 1 cm 的尺寸");
    }
    else if (!FMath::IsFinite(MinDoorSideWall) || MinDoorSideWall < 1.0f
        || !FMath::IsFinite(MinDoorLintelHeight) || MinDoorLintelHeight < 1.0f)
    {
        Reason = TEXT("侧墙与顶部余量必须是有限且至少 1 cm 的尺寸");
    }
    else if (Config.DoorWidth > Span - 2 * MinDoorSideWall)
    {
        Reason = FString::Printf(TEXT("DoorWidth=%.1f 太大：墙长 %.1f，需保留两端各 %.1f cm"), Config.DoorWidth, Span, MinDoorSideWall);
    }
    else if (Config.DoorHeight > RoomHeight - MinDoorLintelHeight)
    {
        Reason = FString::Printf(TEXT("DoorHeight=%.1f 太高：墙高 %.1f，顶部需保留 %.1f cm"), Config.DoorHeight, RoomHeight, MinDoorLintelHeight);
    }
    else if (!Config.bAutoCenter && !FMath::IsFinite(Config.ManualOffset))
    {
        Reason = TEXT("ManualOffset 必须是有限数值");
    }
    else if (FMath::Abs(Socket.AppliedOffset) > (Span - Config.DoorWidth) * 0.5 - MinDoorSideWall)
    {
        Reason = FString::Printf(TEXT("ManualOffset=%.1f 超限：合法范围为 [-%.1f, %.1f] cm"),
            Config.ManualOffset, (Span - Config.DoorWidth) * 0.5 - MinDoorSideWall, (Span - Config.DoorWidth) * 0.5 - MinDoorSideWall);
    }
    if (!Reason.IsEmpty())
    {
        Socket.ValidationMessage = FString::Printf(TEXT("P02_INVALID_DOOR Room=%s Side=%s: %s；保留配置，此侧恢复完整墙体。"),
            *GetPathName(), SideName(Side), *Reason);
        return Socket;
    }
    Socket.bValid = true;
    return Socket;
}

TArray<FRogueliteDoorSocket> ARogueliteParametricRoom::GetDoorSockets() const
{
    TArray<FRogueliteDoorSocket> Result;
    for (int32 Index = 0; Index < 4; ++Index)
    {
        const ERogueliteRoomSide Side = static_cast<ERogueliteRoomSide>(Index);
        if (GetDoorConfig(Side).bEnabled)
        {
            Result.Add(CalculateDoorSocket(Side));
        }
    }
    return Result;
}

bool ARogueliteParametricRoom::TryGetDoorSocket(ERogueliteRoomSide Side, FRogueliteDoorSocket& OutSocket) const
{
    if (static_cast<uint8>(Side) > static_cast<uint8>(ERogueliteRoomSide::West))
    {
        OutSocket = FRogueliteDoorSocket();
        return false;
    }
    OutSocket = CalculateDoorSocket(Side);
    return GetDoorConfig(Side).bEnabled && OutSocket.bValid;
}

void ARogueliteParametricRoom::UpdateDoorWall(ERogueliteRoomSide Side, UStaticMeshComponent* FullOrLeft)
{
    const int32 Index = static_cast<int32>(Side);
    if (!DoorRightSegments.IsValidIndex(Index) || !DoorLintelSegments.IsValidIndex(Index))
    {
        return;
    }
    const FRogueliteDoorConfig& Config = GetDoorConfig(Side);
    const FRogueliteDoorSocket Socket = CalculateDoorSocket(Side);
    if (Config.bEnabled)
    {
        DoorSockets.Add(Socket);
        if (!Socket.bValid)
        {
            ValidationWarnings.Add(Socket.ValidationMessage);
        }
    }
    const bool bAlongX = Side == ERogueliteRoomSide::East || Side == ERogueliteRoomSide::West;
    const FVector Tangent = bAlongX ? FVector::ForwardVector : FVector::RightVector;
    const float HalfSpan = (bAlongX ? RoomLength : RoomWidth + 2 * WallThickness) * 0.5;
    const FVector WallCenter = Socket.LocalOutwardDirection * ((bAlongX ? RoomWidth : RoomLength) + WallThickness) * 0.5;
    const auto Size = [this, bAlongX](float AlongWall, float Height)
    {
        return bAlongX ? FVector(AlongWall, WallThickness, Height) : FVector(WallThickness, AlongWall, Height);
    };
    if (!Config.bEnabled || !Socket.bValid)
    {
        UpdateShell(FullOrLeft, WallCenter + FVector(0, 0, RoomHeight * 0.5), Size(2 * HalfSpan, RoomHeight));
        UpdateShell(DoorRightSegments[Index], FVector::ZeroVector, FVector::OneVector, false);
        UpdateShell(DoorLintelSegments[Index], FVector::ZeroVector, FVector::OneVector, false);
    }
    else
    {
        const float OpeningMin = Socket.AppliedOffset - Config.DoorWidth * 0.5;
        const float OpeningMax = Socket.AppliedOffset + Config.DoorWidth * 0.5;
        UpdateShell(FullOrLeft, WallCenter + Tangent * ((-HalfSpan + OpeningMin) * 0.5) + FVector(0, 0, RoomHeight * 0.5),
            Size(OpeningMin + HalfSpan, RoomHeight));
        UpdateShell(DoorRightSegments[Index], WallCenter + Tangent * ((OpeningMax + HalfSpan) * 0.5) + FVector(0, 0, RoomHeight * 0.5),
            Size(HalfSpan - OpeningMax, RoomHeight));
        UpdateShell(DoorLintelSegments[Index], WallCenter + Tangent * Socket.AppliedOffset + FVector(0, 0, (RoomHeight + Config.DoorHeight) * 0.5),
            Size(Config.DoorWidth, RoomHeight - Config.DoorHeight));
    }
#if WITH_EDITORONLY_DATA
    if (DoorSocketMarkers.IsValidIndex(Index) && DoorSocketMarkers[Index])
    {
        UArrowComponent* Marker = DoorSocketMarkers[Index];
        const float MarkerHeight = FMath::IsFinite(Config.DoorHeight) ? FMath::Clamp(Config.DoorHeight * 0.5f, 0.0f, RoomHeight) : RoomHeight * 0.5f;
        Marker->SetRelativeLocation(Socket.LocalPosition + FVector(0, 0, MarkerHeight));
        Marker->SetRelativeRotation(Socket.LocalOutwardDirection.Rotation());
        Marker->SetArrowFColor(Socket.bValid ? FColor::Green : FColor::Red);
        Marker->SetVisibility(Config.bEnabled);
    }
#endif
}

void ARogueliteParametricRoom::ValidateManualPOIs()
{
    OutOfBoundsManualPOIActors.Reset();
    const FBox Interior = CalculateInteriorBounds().ExpandBy(0.1);
    const FTransform RoomTransform = GetActorTransform();
    TSet<AActor*> Checked;
    for (AActor* POI : ManualPOIActors)
    {
        if (!IsValid(POI) || Checked.Contains(POI))
        {
            continue;
        }
        Checked.Add(POI);
        if (POI == this || POI->GetWorld() != GetWorld())
        {
            ValidationWarnings.Add(FString::Printf(TEXT("P01_INVALID_POI Room=%s POI=%s: 引用必须是同一场景中的其他手工 Actor。"), *GetPathName(), *POI->GetPathName()));
            continue;
        }
        // Include non-colliding geometry and child actors; never write to the POI.
        const FBox WorldBounds = POI->GetComponentsBoundingBox(true, true);
        if (!WorldBounds.IsValid)
        {
            ValidationWarnings.Add(FString::Printf(TEXT("P01_POI_NO_BOUNDS Room=%s POI=%s: 没有可检查的组件 Bounds。"), *GetPathName(), *POI->GetPathName()));
            continue;
        }
        FBox LocalBounds(ForceInit);
        for (int32 Corner = 0; Corner < 8; ++Corner)
        {
            const FVector WorldCorner((Corner & 1) ? WorldBounds.Max.X : WorldBounds.Min.X,
                (Corner & 2) ? WorldBounds.Max.Y : WorldBounds.Min.Y,
                (Corner & 4) ? WorldBounds.Max.Z : WorldBounds.Min.Z);
            LocalBounds += RoomTransform.InverseTransformPosition(WorldCorner);
        }
        if (!Interior.IsInsideOrOn(LocalBounds.Min) || !Interior.IsInsideOrOn(LocalBounds.Max))
        {
            OutOfBoundsManualPOIActors.Add(POI);
            ValidationWarnings.Add(FString::Printf(TEXT("P01_POI_OUT_OF_BOUNDS 手工 POI 越界：Room=%s POI=%s Path=%s LocalMin=%s LocalMax=%s；仅警告，不修改 POI。"),
                *GetActorNameOrLabel(), *POI->GetActorNameOrLabel(), *POI->GetPathName(), *LocalBounds.Min.ToString(), *LocalBounds.Max.ToString()));
        }
    }
}

#if WITH_EDITOR
void ARogueliteParametricRoom::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
    Super::PostEditChangeProperty(PropertyChangedEvent);
    RebuildRoom();
}

void ARogueliteParametricRoom::PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedEvent)
{
    Super::PostEditChangeChainProperty(PropertyChangedEvent);
    RebuildRoom();
}

void ARogueliteParametricRoom::PostEditMove(bool bFinished)
{
    Super::PostEditMove(bFinished);
    RebuildRoom();
}

void ARogueliteParametricRoom::CheckForErrors()
{
    Super::CheckForErrors();
    RebuildRoom();
    for (const FString& Warning : ValidationWarnings)
    {
        FMessageLog(TEXT("MapCheck")).Warning()
            ->AddToken(FUObjectToken::Create(this))
            ->AddToken(FTextToken::Create(FText::FromString(Warning)));
    }
}
#endif
