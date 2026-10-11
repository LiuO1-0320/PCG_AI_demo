#include "RogueliteParametricCorridor.h"
#include "RoguelitePrototype.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
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
    void UpdateCorridorMesh(UStaticMeshComponent* Component, const FVector& Center, const FVector& Size, bool bActive = true)
    {
        if (!Component) return;
        if (!bActive || Size.GetMin() <= UE_SMALL_NUMBER)
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
}

ARogueliteParametricCorridor::ARogueliteParametricCorridor()
{
    PrimaryActorTick.bCanEverTick = false;
    CorridorRoot = CreateDefaultSubobject<USceneComponent>(TEXT("CorridorRoot"));
    SetRootComponent(CorridorRoot);
    CorridorRoot->SetMobility(EComponentMobility::Static);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
    const TCHAR* Names[] = {TEXT("Floor"), TEXT("LeftWall"), TEXT("RightWall"), TEXT("Ceiling"),
        TEXT("StartFrameLeft"), TEXT("StartFrameRight"), TEXT("StartFrameLintel"),
        TEXT("EndFrameLeft"), TEXT("EndFrameRight"), TEXT("EndFrameLintel")};
    for (const TCHAR* Name : Names)
    {
        UStaticMeshComponent* Mesh = CreateDefaultSubobject<UStaticMeshComponent>(Name);
        Mesh->SetupAttachment(CorridorRoot);
        Mesh->SetMobility(EComponentMobility::Static);
        Mesh->SetStaticMesh(Cube.Object);
        Mesh->SetCollisionProfileName(TEXT("BlockAll"));
        Mesh->SetGenerateOverlapEvents(false);
        ShellMeshes.Add(Mesh);
    }
#if WITH_EDITORONLY_DATA
    for (const TCHAR* Name : {TEXT("StartSocketMarker"), TEXT("EndSocketMarker")})
    {
        UArrowComponent* Marker = CreateEditorOnlyDefaultSubobject<UArrowComponent>(Name);
        if (Marker)
        {
            Marker->SetupAttachment(CorridorRoot);
            Marker->SetMobility(EComponentMobility::Static);
            Marker->SetHiddenInGame(true);
            Marker->SetCollisionEnabled(ECollisionEnabled::NoCollision);
            Marker->SetArrowLength(120.0f);
        }
        SocketMarkers.Add(Marker);
    }
#endif
}

bool ARogueliteParametricCorridor::ValidateShell(FString& OutReason) const
{
    const auto Check = [&OutReason](float Value, float Minimum, const TCHAR* Name)
    {
        if (!FMath::IsFinite(Value) || Value < Minimum || Value > 100000.0f)
        {
            OutReason = FString::Printf(TEXT("P03C1_INVALID_DIMENSION Property=%s Value=%.3f; expected %.1f..100000 cm; original retained."), Name, Value, Minimum);
            return false;
        }
        return true;
    };
    if (!Check(CorridorLength, 100, TEXT("CorridorLength")) || !Check(CorridorWidth, 100, TEXT("CorridorWidth"))
        || !Check(CorridorHeight, 100, TEXT("CorridorHeight")) || !Check(WallThickness, 1, TEXT("WallThickness"))
        || !Check(FloorThickness, 1, TEXT("FloorThickness"))) return false;
    if (2 * WallThickness >= CorridorLength)
    {
        OutReason = TEXT("P03C1_INVALID_DIMENSION: CorridorLength must exceed 2*WallThickness; end frames must not overlap; original retained.");
        return false;
    }
    const FTransform Transform = GetActorTransform();
    const FRotator Rotation = Transform.Rotator();
    const float SnappedYaw = FMath::RoundToFloat(Rotation.Yaw / 90.0f) * 90.0f;
    if (Transform.ContainsNaN() || !Transform.GetScale3D().Equals(FVector::OneVector, 0.0001)
        || FMath::Abs(Rotation.Pitch) > 0.01f || FMath::Abs(Rotation.Roll) > 0.01f
        || FMath::Abs(FMath::FindDeltaAngleDegrees(Rotation.Yaw, SnappedYaw)) > 0.01f)
    {
        OutReason = TEXT("P03C1_INVALID_TRANSFORM: use Scale=1, Pitch/Roll=0, Yaw=0/90/180/270; Actor transform retained.");
        return false;
    }
    OutReason.Reset();
    return true;
}

bool ARogueliteParametricCorridor::ValidateOpening(FString& OutReason) const
{
    if (!ValidateShell(OutReason)) return false;
    if (!FMath::IsFinite(DoorWidth) || !FMath::IsFinite(DoorHeight) || DoorWidth < 1 || DoorHeight < 1
        || DoorWidth > CorridorWidth || DoorHeight > CorridorHeight)
    {
        OutReason = FString::Printf(TEXT("P03C1_INVALID_OPENING: DoorWidth/Height=%.3f/%.3f must be positive and within CorridorWidth/Height=%.3f/%.3f; original retained; both ends sealed."),
            DoorWidth, DoorHeight, CorridorWidth, CorridorHeight);
        return false;
    }
    OutReason.Reset();
    return true;
}

FRogueliteCorridorSocket ARogueliteParametricCorridor::CalculateSocket(ERogueliteCorridorEndpoint Endpoint) const
{
    FRogueliteCorridorSocket Socket;
    Socket.Endpoint = Endpoint;
    if (Endpoint != ERogueliteCorridorEndpoint::Start && Endpoint != ERogueliteCorridorEndpoint::End)
    {
        Socket.DoorSocket.ValidationMessage = TEXT("P03C1_UNKNOWN_SOCKET: expected Start or End.");
        return Socket;
    }
    const float Sign = Endpoint == ERogueliteCorridorEndpoint::Start ? -1.0f : 1.0f;
    FRogueliteDoorSocket& Door = Socket.DoorSocket;
    Door.Side = Endpoint == ERogueliteCorridorEndpoint::Start ? ERogueliteRoomSide::South : ERogueliteRoomSide::North;
    Door.LocalOutwardDirection = FVector(Sign, 0, 0);
    Door.LocalPosition = FVector(FMath::IsFinite(CorridorLength) && CorridorLength > 0 && CorridorLength <= 100000 ? Sign * CorridorLength * 0.5f : 0, 0, 0);
    Door.WorldPosition = GetActorTransform().TransformPosition(Door.LocalPosition);
    Door.WorldOutwardDirection = GetActorTransform().TransformVectorNoScale(Door.LocalOutwardDirection).GetSafeNormal();
    Door.DoorWidth = FMath::IsFinite(DoorWidth) ? DoorWidth : 0;
    Door.DoorHeight = FMath::IsFinite(DoorHeight) ? DoorHeight : 0;
    Door.WallThickness = FMath::IsFinite(WallThickness) ? WallThickness : 0;
    Door.AppliedOffset = 0;
    Door.bValid = ValidateOpening(Door.ValidationMessage);
    // ConnectionId and bConnected intentionally remain empty/free until P03-C2 owns a registry.
    return Socket;
}

TArray<FRogueliteCorridorSocket> ARogueliteParametricCorridor::GetCorridorSockets() const
{
    return {CalculateSocket(ERogueliteCorridorEndpoint::Start), CalculateSocket(ERogueliteCorridorEndpoint::End)};
}

bool ARogueliteParametricCorridor::TryGetCorridorSocket(ERogueliteCorridorEndpoint Endpoint, FRogueliteCorridorSocket& OutSocket) const
{
    OutSocket = CalculateSocket(Endpoint);
    return OutSocket.DoorSocket.bValid;
}

bool ARogueliteParametricCorridor::IsRoomSocketCompatible(ERogueliteCorridorEndpoint Endpoint, const FRogueliteDoorSocket& RoomSocket, FString& OutReason) const
{
    const FRogueliteCorridorSocket Socket = CalculateSocket(Endpoint);
    if (!Socket.DoorSocket.bValid || !RoomSocket.bValid || !FMath::IsFinite(RoomSocket.DoorWidth) || !FMath::IsFinite(RoomSocket.DoorHeight)
        || FMath::Abs(RoomSocket.DoorWidth - DoorWidth) > 0.1f || FMath::Abs(RoomSocket.DoorHeight - DoorHeight) > 0.1f)
    {
        OutReason = FString::Printf(TEXT("P03C1_INCOMPATIBLE_SPEC Corridor=%s Endpoint=%s: require valid equal opening specs within 0.1 cm; corridor %.2fx%.2f, room %.2fx%.2f. %s"),
            *GetPathName(), *UEnum::GetValueAsString(Endpoint), DoorWidth, DoorHeight, RoomSocket.DoorWidth, RoomSocket.DoorHeight, *Socket.DoorSocket.ValidationMessage);
        return false;
    }
    OutReason = TEXT("Opening specifications compatible; world alignment, occupancy, reservations and navigation NOT checked.");
    return true;
}

FBox ARogueliteParametricCorridor::GetLocalOccupancyBounds() const
{
    FString Reason;
    if (!ValidateShell(Reason)) return FBox(ForceInit);
    return FBox(FVector(-CorridorLength * 0.5f, -CorridorWidth * 0.5f - WallThickness, -FloorThickness),
        FVector(CorridorLength * 0.5f, CorridorWidth * 0.5f + WallThickness, CorridorHeight + WallThickness));
}

FBox ARogueliteParametricCorridor::GetWorldOccupancyBounds() const
{
    const FBox Local = GetLocalOccupancyBounds();
    return Local.IsValid ? Local.TransformBy(GetActorTransform()) : FBox(ForceInit);
}

void ARogueliteParametricCorridor::UpdateEndFrame(int32 Index, bool bOpeningValid)
{
    const float Sign = Index == 0 ? -1.0f : 1.0f;
    const float X = Sign * (CorridorLength * 0.5f - WallThickness * 0.5f);
    const int32 Base = 4 + Index * 3;
    if (!bOpeningValid)
    {
        UpdateCorridorMesh(ShellMeshes[Base], FVector(X, 0, CorridorHeight * 0.5f), FVector(WallThickness, CorridorWidth, CorridorHeight));
        UpdateCorridorMesh(ShellMeshes[Base + 1], FVector::ZeroVector, FVector::OneVector, false);
        UpdateCorridorMesh(ShellMeshes[Base + 2], FVector::ZeroVector, FVector::OneVector, false);
        return;
    }
    const float SideWidth = (CorridorWidth - DoorWidth) * 0.5f;
    for (int32 Side = 0; Side < 2; ++Side)
    {
        const float Y = (Side == 0 ? -1.0f : 1.0f) * (DoorWidth * 0.5f + SideWidth * 0.5f);
        UpdateCorridorMesh(ShellMeshes[Base + Side], FVector(X, Y, CorridorHeight * 0.5f), FVector(WallThickness, SideWidth, CorridorHeight));
    }
    const float LintelHeight = CorridorHeight - DoorHeight;
    UpdateCorridorMesh(ShellMeshes[Base + 2], FVector(X, 0, DoorHeight + LintelHeight * 0.5f), FVector(WallThickness, DoorWidth, LintelHeight));
}

void ARogueliteParametricCorridor::RebuildCorridor()
{
    ValidationWarnings.Reset();
    bHasPassageTestResult = false;
    bPassageTestPassed = false;
    PassageTestEvidence.Reset();
    FString Reason;
    const bool bShellValid = ValidateShell(Reason);
    if (!bShellValid)
    {
        ValidationWarnings.Add(Reason);
        for (UStaticMeshComponent* Mesh : ShellMeshes) UpdateCorridorMesh(Mesh, FVector::ZeroVector, FVector::OneVector, false);
    }
    else
    {
        UpdateCorridorMesh(ShellMeshes[0], FVector(0, 0, -FloorThickness * 0.5f), FVector(CorridorLength, CorridorWidth + 2 * WallThickness, FloorThickness));
        UpdateCorridorMesh(ShellMeshes[1], FVector(0, -(CorridorWidth + WallThickness) * 0.5f, CorridorHeight * 0.5f), FVector(CorridorLength, WallThickness, CorridorHeight));
        UpdateCorridorMesh(ShellMeshes[2], FVector(0, (CorridorWidth + WallThickness) * 0.5f, CorridorHeight * 0.5f), FVector(CorridorLength, WallThickness, CorridorHeight));
        UpdateCorridorMesh(ShellMeshes[3], FVector(0, 0, CorridorHeight + WallThickness * 0.5f), FVector(CorridorLength, CorridorWidth + 2 * WallThickness, WallThickness));
        const bool bOpeningValid = ValidateOpening(Reason);
        if (!bOpeningValid) ValidationWarnings.Add(Reason);
        UpdateEndFrame(0, bOpeningValid);
        UpdateEndFrame(1, bOpeningValid);
    }
    StartSocket = CalculateSocket(ERogueliteCorridorEndpoint::Start);
    EndSocket = CalculateSocket(ERogueliteCorridorEndpoint::End);
#if WITH_EDITORONLY_DATA
    for (int32 Index = 0; Index < SocketMarkers.Num(); ++Index)
    {
        if (UArrowComponent* Marker = SocketMarkers[Index])
        {
            const FRogueliteDoorSocket& Socket = Index == 0 ? StartSocket.DoorSocket : EndSocket.DoorSocket;
            Marker->SetRelativeLocationAndRotation(Socket.LocalPosition, Socket.LocalOutwardDirection.Rotation());
            Marker->SetArrowColor(Socket.bValid ? FColor::Green : FColor::Red);
            Marker->SetVisibility(true);
        }
    }
#endif
    for (const FString& Warning : ValidationWarnings)
    {
        UE_LOG(LogRoguelitePrototype, Warning, TEXT("P03C1_CORRIDOR_WARNING Corridor=%s %s"), *GetPathName(), *Warning);
    }
}

void ARogueliteParametricCorridor::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);
    RebuildCorridor();
}

void ARogueliteParametricCorridor::PostRegisterAllComponents()
{
    Super::PostRegisterAllComponents();
    RebuildCorridor();
}

void ARogueliteParametricCorridor::CheckPassage()
{
    bHasPassageTestResult = true;
    bPassageTestPassed = false;
    PassageTestEvidence.Reset();
    FString Reason;
    if (!GetWorld() || !ValidateOpening(Reason) || !FMath::IsFinite(TestCapsuleRadius) || !FMath::IsFinite(TestCapsuleHalfHeight)
        || !FMath::IsFinite(CapsuleClearance) || TestCapsuleRadius < 1 || TestCapsuleHalfHeight < TestCapsuleRadius || CapsuleClearance < 0.1f
        || DoorWidth < 2 * (TestCapsuleRadius + CapsuleClearance) || DoorHeight < 2 * (TestCapsuleHalfHeight + CapsuleClearance))
    {
        PassageTestEvidence.Add(TEXT("P03C1_PASSAGE_REJECTED: invalid opening or capsule/clearance does not fit. ") + Reason);
        return;
    }
    FCollisionQueryParams Params(SCENE_QUERY_STAT(RogueliteCorridorCapsule), false);
    const FCollisionShape Capsule = FCollisionShape::MakeCapsule(TestCapsuleRadius, TestCapsuleHalfHeight);
    const float Z = TestCapsuleHalfHeight + CapsuleClearance;
    const float Extent = CorridorLength * 0.5f + TestCapsuleRadius + CapsuleClearance;
    bool bAllPassed = true;
    const auto Sweep = [&](float Offset, bool bReverse)
    {
        const FVector Start = GetActorTransform().TransformPosition(FVector(bReverse ? Extent : -Extent, Offset, Z));
        const FVector End = GetActorTransform().TransformPosition(FVector(bReverse ? -Extent : Extent, Offset, Z));
        FHitResult Hit;
        const bool bBlocked = GetWorld()->SweepSingleByChannel(Hit, Start, End, FQuat::Identity, ECC_Pawn, Capsule, Params);
        const bool bStartOverlap = GetWorld()->OverlapBlockingTestByChannel(Start, FQuat::Identity, ECC_Pawn, Capsule, Params);
        const bool bEndOverlap = GetWorld()->OverlapBlockingTestByChannel(End, FQuat::Identity, ECC_Pawn, Capsule, Params);
        const bool bPassed = !bBlocked && !bStartOverlap && !bEndOverlap;
        bAllPassed &= bPassed;
        PassageTestEvidence.Add(FString::Printf(TEXT("PawnCapsule Offset=%.3f Reverse=%d Start=%s End=%s Radius=%.3f HalfHeight=%.3f Blocked=%d StartOverlap=%d EndOverlap=%d StartPenetration=%d Distance=%.3f Component=%s Passed=%d"),
            Offset, bReverse, *Start.ToCompactString(), *End.ToCompactString(), TestCapsuleRadius, TestCapsuleHalfHeight,
            bBlocked, bStartOverlap, bEndOverlap, Hit.bStartPenetrating, Hit.Distance, *GetPathNameSafe(Hit.GetComponent()), bPassed));
    };
    const float Edge = DoorWidth * 0.5f - TestCapsuleRadius - CapsuleClearance;
    for (float Offset : {-Edge, 0.0f, Edge}) Sweep(Offset, false);
    Sweep(0.0f, true);
    bPassageTestPassed = bAllPassed;
    UE_LOG(LogRoguelitePrototype, Log, TEXT("P03C1_CAPSULE_CHECK Corridor=%s Passed=%d Queries=4 Navigation=NotTested"), *GetPathName(), bAllPassed);
}

#if WITH_EDITOR
void ARogueliteParametricCorridor::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
    Super::PostEditChangeProperty(PropertyChangedEvent);
    RebuildCorridor();
}

void ARogueliteParametricCorridor::PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedEvent)
{
    Super::PostEditChangeChainProperty(PropertyChangedEvent);
    RebuildCorridor();
}

void ARogueliteParametricCorridor::PostEditMove(bool bFinished)
{
    Super::PostEditMove(bFinished);
    RebuildCorridor();
}

void ARogueliteParametricCorridor::CheckForErrors()
{
    Super::CheckForErrors();
    RebuildCorridor();
    for (const FString& Warning : ValidationWarnings)
    {
        FMessageLog("MapCheck").Warning()->AddToken(FUObjectToken::Create(this))->AddToken(FTextToken::Create(FText::FromString(Warning)));
    }
}
#endif
