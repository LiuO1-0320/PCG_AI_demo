#include "RogueliteRoomAssembly.h"
#include "RogueliteRoomTemplate.h"
#include "RoguelitePrototype.h"
#include "Components/SceneComponent.h"
#include "Components/ActorComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "CollisionShape.h"
#include "NavigationSystem.h"
#include "NavigationData.h"
#include "NavigationPath.h"

#if WITH_EDITOR
#include "ScopedTransaction.h"
#endif

namespace
{
    FString RoomName(const ARogueliteParametricRoom* Room)
    {
        return IsValid(Room) ? Room->GetPathName() : TEXT("<invalid room>");
    }

    FString Context(const ARogueliteParametricRoom* A, ERogueliteRoomSide SideA,
        const ARogueliteParametricRoom* B, ERogueliteRoomSide SideB)
    {
        return FString::Printf(TEXT("P03A RoomA=%s SocketA=%s RoomB=%s SocketB=%s: "),
            *RoomName(A), *UEnum::GetValueAsString(SideA), *RoomName(B), *UEnum::GetValueAsString(SideB));
    }

    bool IsQuarterTurn(const ARogueliteParametricRoom* Room, float AngleTolerance)
    {
        const FTransform Transform = Room->GetActorTransform();
        const FRotator Rotation = Transform.Rotator();
        return !Transform.ContainsNaN() && Transform.GetScale3D().Equals(FVector::OneVector, 0.0001)
            && FMath::Abs(FRotator::NormalizeAxis(Rotation.Pitch)) <= AngleTolerance
            && FMath::Abs(FRotator::NormalizeAxis(Rotation.Roll)) <= AngleTolerance
            && FMath::Abs(FMath::FindDeltaAngleDegrees(Rotation.Yaw, FMath::RoundToFloat(Rotation.Yaw / 90.0f) * 90.0f)) <= AngleTolerance;
    }

    bool HasValidDimensions(const ARogueliteParametricRoom* Room)
    {
        return FMath::IsFinite(Room->RoomLength) && Room->RoomLength >= 100
            && FMath::IsFinite(Room->RoomWidth) && Room->RoomWidth >= 100
            && FMath::IsFinite(Room->RoomHeight) && Room->RoomHeight >= 100
            && FMath::IsFinite(Room->WallThickness) && Room->WallThickness >= 1
            && FMath::IsFinite(Room->FloorThickness) && Room->FloorThickness >= 1;
    }

    FBox Occupancy(const ARogueliteParametricRoom* Room, const FTransform& Transform)
    {
        const double HalfX = Room->RoomLength * 0.5 + Room->WallThickness;
        const double HalfY = Room->RoomWidth * 0.5 + Room->WallThickness;
        // Includes the room volume, both outer wall faces and the floor below Z=0.
        return FBox(FVector(-HalfX, -HalfY, -Room->FloorThickness), FVector(HalfX, HalfY, Room->RoomHeight))
            .TransformBy(Transform);
    }

    bool InteriorsOverlap(const FBox& A, const FBox& B, float Tolerance)
    {
        return FMath::Min(A.Max.X, B.Max.X) - FMath::Max(A.Min.X, B.Min.X) > Tolerance
            && FMath::Min(A.Max.Y, B.Max.Y) - FMath::Max(A.Min.Y, B.Min.Y) > Tolerance
            && FMath::Min(A.Max.Z, B.Max.Z) - FMath::Max(A.Min.Z, B.Min.Z) > Tolerance;
    }

    bool Matches(const FRogueliteRoomConnection& Connection, ARogueliteParametricRoom* A,
        ERogueliteRoomSide SideA, ARogueliteParametricRoom* B, ERogueliteRoomSide SideB)
    {
        return (Connection.RoomA == A && Connection.SocketA == SideA && Connection.RoomB == B && Connection.SocketB == SideB)
            || (Connection.RoomA == B && Connection.SocketA == SideB && Connection.RoomB == A && Connection.SocketB == SideA);
    }
}

ARogueliteRoomAssembly::ARogueliteRoomAssembly()
{
    PrimaryActorTick.bCanEverTick = false;
    SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("AssemblyRoot")));
}

void ARogueliteRoomAssembly::PostLoad()
{
    Super::PostLoad();
    for (FRogueliteRoomConnection& Connection : Connections)
    {
        // Preserve endpoints and reservations, but never claim stale saved collision checks are current.
        Connection.bGeometryValid = false;
        Connection.Navigation = ERogueliteNavigationCheck::NotChecked;
        Connection.ValidationStatus = ERogueliteConnectionValidation::NotChecked;
        Connection.ValidationMessage = TEXT("已重载：端点/占用保留，请点击 Revalidate Connections 重新检查几何、胶囊和导航。");
    }
}

bool ARogueliteRoomAssembly::ValidateSettings(FRogueliteAssemblyResult& Result) const
{
    if (!FMath::IsFinite(PositionTolerance) || PositionTolerance < 0.001f || PositionTolerance > 1
        || !FMath::IsFinite(AngleToleranceDegrees) || AngleToleranceDegrees < 0.001f || AngleToleranceDegrees > 1
        || !FMath::IsFinite(DoorSizeTolerance) || DoorSizeTolerance < 0 || DoorSizeTolerance > 1
        || !FMath::IsFinite(TestCapsuleRadius) || TestCapsuleRadius < 1
        || !FMath::IsFinite(TestCapsuleHalfHeight) || TestCapsuleHalfHeight < TestCapsuleRadius
        || !FMath::IsFinite(CapsuleClearance) || CapsuleClearance <= PositionTolerance)
    {
        Result.Message = TEXT("检查参数非法：位置/尺寸容差须在 0–1 cm，角度容差在 0.001–1 度；胶囊半高须不小于半径，净距须大于位置容差。");
        return false;
    }
    Result.CapsuleRadius = TestCapsuleRadius;
    Result.CapsuleHalfHeight = TestCapsuleHalfHeight;
    return true;
}

bool ARogueliteRoomAssembly::ValidateEndpoints(ARogueliteParametricRoom* A, ERogueliteRoomSide SideA,
    ARogueliteParametricRoom* B, ERogueliteRoomSide SideB, bool bWillMoveB,
    FRogueliteDoorSocket& DoorA, FRogueliteDoorSocket& DoorB, FRogueliteAssemblyResult& Result) const
{
    if (!IsValid(A) || !IsValid(B) || A == B || A->IsActorBeingDestroyed() || B->IsActorBeingDestroyed()
        || !GetWorld() || A->GetWorld() != GetWorld() || B->GetWorld() != GetWorld())
    {
        Result.Message = TEXT("需要同一 World 中两个不同且有效的房间 Actor。");
        return false;
    }
    if (static_cast<uint8>(SideA) > 3 || static_cast<uint8>(SideB) > 3)
    {
        Result.Message = TEXT("指定 Socket 不存在：第一版仅 North/South/East/West。");
        return false;
    }
    if (!ValidateSettings(Result))
    {
        return false;
    }
    if (!HasValidDimensions(A) || !HasValidDimensions(B) || !IsQuarterTurn(A, AngleToleranceDegrees) || !IsQuarterTurn(B, AngleToleranceDegrees))
    {
        Result.Message = TEXT("尺寸/Transform 非法：房间必须采用 Scale=1、Pitch/Roll=0、Yaw 为 90 度整数倍及合法厘米尺寸。");
        return false;
    }
    if (!A->TryGetDoorSocket(SideA, DoorA))
    {
        Result.Message = TEXT("RoomA Socket 未启用或非法。") + DoorA.ValidationMessage;
        return false;
    }
    if (!B->TryGetDoorSocket(SideB, DoorB))
    {
        Result.Message = TEXT("RoomB Socket 未启用或非法。") + DoorB.ValidationMessage;
        return false;
    }
    if (FMath::Abs(DoorA.DoorWidth - DoorB.DoorWidth) > DoorSizeTolerance
        || FMath::Abs(DoorA.DoorHeight - DoorB.DoorHeight) > DoorSizeTolerance)
    {
        Result.Message = FString::Printf(TEXT("门规格不兼容：A=%.2f×%.2f，B=%.2f×%.2f cm；本轮要求相同净宽/净高。"),
            DoorA.DoorWidth, DoorA.DoorHeight, DoorB.DoorWidth, DoorB.DoorHeight);
        return false;
    }
    if (DoorA.DoorWidth < 2 * (TestCapsuleRadius + CapsuleClearance)
        || DoorA.DoorHeight < 2 * TestCapsuleHalfHeight + 2 * CapsuleClearance)
    {
        Result.Message = TEXT("门洞不足以容纳当前测试胶囊及净距；不能仅靠射线宣布通行。");
        return false;
    }
    if (bWillMoveB)
    {
        TArray<AActor*> Attached;
        B->GetAttachedActors(Attached);
        if (B->ManualPOIActors.Num() > 0 || Attached.Num() > 0)
        {
            Result.Message = TEXT("INDEPENDENT_POI_MOVE_REFUSED: target contains ManualPOIActors or attached Actors. These independent Actors remain protected; only components owned by a RoomTemplate instance are assembled.");
            return false;
        }
    }
    for (ARogueliteParametricRoom* Room : {A, B})
    {
        if (auto* Template = Cast<ARogueliteRoomTemplate>(Room))
        {
            const auto Validation = Template->EvaluateTemplate();
            if (!Validation.bValid)
            {
                Result.Message = FString::Join(Validation.Issues, TEXT(" | "));
                return false;
            }
        }
    }
    return true;
}

bool ARogueliteRoomAssembly::IsOccupied(ARogueliteParametricRoom* Room, ERogueliteRoomSide Side, const FGuid& ExceptId) const
{
    for (TActorIterator<ARogueliteRoomAssembly> It(GetWorld()); It; ++It)
    {
        for (const FRogueliteRoomConnection& Connection : It->Connections)
        {
            if (Connection.bActive && !(It.operator->() == this && Connection.Id == ExceptId)
                && ((Connection.RoomA == Room && Connection.SocketA == Side) || (Connection.RoomB == Room && Connection.SocketB == Side)))
            {
                return true;
            }
        }
    }
    return false;
}

bool ARogueliteRoomAssembly::CheckOccupancy(ARogueliteParametricRoom* A, ARogueliteParametricRoom* B,
    const FTransform& TargetTransform, FRogueliteAssemblyResult& Result, ARogueliteParametricCorridor* IgnoreCorridor) const
{
    Result.OccupancyA = Occupancy(A, A->GetActorTransform());
    Result.OccupancyB = Occupancy(B, TargetTransform);
    for (TActorIterator<ARogueliteParametricRoom> It(GetWorld()); It; ++It)
    {
        if (It.operator->() == B)
        {
            continue;
        }
        if (!HasValidDimensions(*It))
        {
            Result.Message = TEXT("场景中的房间尺寸非法，无法可靠检查占用：") + It->GetPathName();
            return false;
        }
        const FBox OtherBounds = Occupancy(*It, It->GetActorTransform());
        if (InteriorsOverlap(Result.OccupancyB, OtherBounds, PositionTolerance))
        {
            Result.Message = TEXT("目标房间占用空间非法重叠：") + It->GetPathName();
            return false;
        }
    }
    for (TActorIterator<ARogueliteParametricCorridor> It(GetWorld()); It; ++It)
    {
        if (*It == IgnoreCorridor) continue;
        const FBox Bounds = It->GetWorldOccupancyBounds();
        if (!Bounds.IsValid || InteriorsOverlap(Result.OccupancyB, Bounds, PositionTolerance))
        {
            Result.Message = TEXT("Room occupancy conflicts with corridor: ") + It->GetPathName();
            return false;
        }
    }
    return true;
}

bool ARogueliteRoomAssembly::ValidateDirect(ARogueliteParametricRoom* A, ERogueliteRoomSide SideA,
    ARogueliteParametricRoom* B, ERogueliteRoomSide SideB, FRogueliteAssemblyResult& Result) const
{
    FRogueliteDoorSocket DoorA, DoorB;
    if (!ValidateEndpoints(A, SideA, B, SideB, false, DoorA, DoorB, Result)
        || !CheckOccupancy(A, B, B->GetActorTransform(), Result))
    {
        return false;
    }
    Result.SocketDistance = FVector::Distance(DoorA.WorldPosition, DoorB.WorldPosition);
    Result.NormalDot = FVector::DotProduct(DoorA.WorldOutwardDirection, DoorB.WorldOutwardDirection);
    if (Result.SocketDistance > PositionTolerance
        || Result.NormalDot > -FMath::Cos(FMath::DegreesToRadians(static_cast<double>(AngleToleranceDegrees))))
    {
        Result.Message = FString::Printf(TEXT("Direct 门槛/朝向未对齐：距离 %.4f cm，法线 dot=%.6f；存在间隙或方向错误。"),
            Result.SocketDistance, Result.NormalDot);
        return false;
    }

    const FVector Center = DoorA.WorldPosition;
    const FVector Normal = DoorA.WorldOutwardDirection;
    const FVector Tangent = FVector::CrossProduct(FVector::UpVector, Normal).GetSafeNormal();
    FCollisionQueryParams RayParams(SCENE_QUERY_STAT(RogueliteAssemblyRay), true);
    RayParams.AddIgnoredActor(this);
    bool bAllProbesPassed = true;
    const auto Ray = [&](const FString& Name, const FVector& Start, const FVector& End, AActor* ExpectedActor)
    {
        FHitResult Hit;
        const bool bHit = GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, RayParams);
        FRogueliteAssemblyProbe Probe;
        Probe.Name = Name;
        Probe.Start = Start;
        Probe.End = End;
        Probe.bExpectedBlocking = ExpectedActor != nullptr;
        Probe.bBlockingHit = bHit;
        Probe.HitDistance = Hit.Distance;
        Probe.HitObject = bHit && Hit.GetComponent() ? Hit.GetComponent()->GetPathName() : FString();
        Probe.bPassed = ExpectedActor ? bHit && Hit.GetActor() == ExpectedActor : !bHit;
        bAllProbesPassed &= Probe.bPassed;
        Result.Probes.Add(Probe);
    };
    const FVector InsideA = Center - Normal * (A->WallThickness + TestCapsuleRadius + 20);
    const FVector InsideB = Center + Normal * (B->WallThickness + TestCapsuleRadius + 20);
    for (float Offset : {-DoorA.DoorWidth * 0.5f + 1, 0.0f, DoorA.DoorWidth * 0.5f - 1})
    {
        for (float Height : {1.0f, DoorA.DoorHeight * 0.5f, DoorA.DoorHeight - 1})
        {
            const FVector Shift = Tangent * Offset + FVector::UpVector * Height;
            Ray(FString::Printf(TEXT("Opening offset=%.1f z=%.1f"), Offset, Height), InsideA + Shift, InsideB + Shift, nullptr);
        }
    }
    for (int32 Index = 0; Index < 2; ++Index)
    {
        ARogueliteParametricRoom* Room = Index == 0 ? A : B;
        const FVector Outward = Index == 0 ? Normal : -Normal;
        for (float Sign : {-1.0f, 1.0f})
        {
            const FVector Shift = Tangent * (Sign * (DoorA.DoorWidth * 0.5f + 10)) + FVector::UpVector * (DoorA.DoorHeight * 0.5f);
            Ray(FString::Printf(TEXT("%s solid side %.0f"), Index == 0 ? TEXT("A") : TEXT("B"), Sign),
                Center - Outward * (Room->WallThickness + 25) + Shift, Center + Outward * 25 + Shift, Room);
        }
        const float LintelZ = DoorA.DoorHeight + FMath::Min(10.0f, (Room->RoomHeight - DoorA.DoorHeight) * 0.5f);
        const FVector LintelShift = FVector::UpVector * LintelZ;
        Ray(Index == 0 ? TEXT("A lintel") : TEXT("B lintel"),
            Center - Outward * (Room->WallThickness + 25) + LintelShift, Center + Outward * 25 + LintelShift, Room);
        const FVector FloorPoint = Index == 0 ? InsideA : InsideB;
        Ray(Index == 0 ? TEXT("A floor") : TEXT("B floor"), FloorPoint + FVector::UpVector * 20,
            FloorPoint - FVector::UpVector * 20, Room);
    }
    if (!bAllProbesPassed)
    {
        Result.Message = TEXT("Direct 射线检查失败：开口被阻挡或侧墙/横梁/地板碰撞缺失；见 Probes 的 HitObject。");
        return false;
    }
    Result.bGeometryPassed = true;

    FCollisionQueryParams SweepParams(SCENE_QUERY_STAT(RogueliteAssemblyCapsule), false);
    SweepParams.AddIgnoredActor(this);
    const FCollisionShape Capsule = FCollisionShape::MakeCapsule(TestCapsuleRadius, TestCapsuleHalfHeight);
    const auto Sweep = [&](const FString& Name, const FVector& Start, const FVector& End)
    {
        FHitResult Hit;
        const bool bBlocked = GetWorld()->SweepSingleByChannel(Hit, Start, End, FQuat::Identity, ECC_Pawn, Capsule, SweepParams);
        const bool bStartOverlap = GetWorld()->OverlapBlockingTestByChannel(Start, FQuat::Identity, ECC_Pawn, Capsule, SweepParams);
        const bool bEndOverlap = GetWorld()->OverlapBlockingTestByChannel(End, FQuat::Identity, ECC_Pawn, Capsule, SweepParams);
        FRogueliteAssemblyProbe Probe;
        Probe.Name = Name;
        Probe.Start = Start;
        Probe.End = End;
        Probe.bBlockingHit = bBlocked || bStartOverlap || bEndOverlap;
        Probe.HitDistance = Hit.Distance;
        Probe.HitObject = Hit.GetComponent() ? Hit.GetComponent()->GetPathName() : FString();
        Probe.bPassed = !Probe.bBlockingHit;
        bAllProbesPassed &= Probe.bPassed;
        Result.Probes.Add(Probe);
    };
    const float EdgeOffset = DoorA.DoorWidth * 0.5f - TestCapsuleRadius - CapsuleClearance;
    for (float Offset : {-EdgeOffset, 0.0f, EdgeOffset})
    {
        const FVector Shift = Tangent * Offset + FVector::UpVector * (TestCapsuleHalfHeight + CapsuleClearance);
        Sweep(FString::Printf(TEXT("PawnCapsule A-to-B offset=%.1f"), Offset), InsideA + Shift, InsideB + Shift);
    }
    const FVector CapsuleZ = FVector::UpVector * (TestCapsuleHalfHeight + CapsuleClearance);
    Sweep(TEXT("PawnCapsule B-to-A center"), InsideB + CapsuleZ, InsideA + CapsuleZ);
    // A complex ray at two meshes' exact shared triangle edge can miss due to
    // floating-point edge ownership. Verify physical ground support with the
    // actual Pawn capsule instead of pretending a zero-width seam ray is robust.
    for (float Offset : {-EdgeOffset, 0.0f, EdgeOffset})
    {
        FRogueliteAssemblyProbe Probe;
        Probe.Name = FString::Printf(TEXT("PawnCapsule seam ground offset=%.1f"), Offset);
        Probe.Start = Center + Tangent * Offset + CapsuleZ;
        Probe.End = Center + Tangent * Offset + FVector::UpVector * (TestCapsuleHalfHeight - CapsuleClearance);
        Probe.bExpectedBlocking = true;
        FHitResult Hit;
        Probe.bBlockingHit = GetWorld()->SweepSingleByChannel(Hit, Probe.Start, Probe.End,
            FQuat::Identity, ECC_Pawn, Capsule, SweepParams);
        Probe.HitDistance = Hit.Distance;
        Probe.HitObject = Hit.GetComponent() ? Hit.GetComponent()->GetPathName() : FString();
        Probe.bPassed = Probe.bBlockingHit && !Hit.bStartPenetrating
            && (Hit.GetActor() == A || Hit.GetActor() == B)
            && Hit.GetComponent() && Hit.GetComponent()->GetFName() == TEXT("Floor")
            && Hit.ImpactNormal.Z > 0.99f;
        bAllProbesPassed &= Probe.bPassed;
        Result.Probes.Add(Probe);
    }
    if (!bAllProbesPassed)
    {
        Result.Message = TEXT("Pawn 胶囊 Sweep/Overlap 被阻挡或门槛地面支撑缺失；恢复目标 Transform，不创建连接。");
        return false;
    }
    Result.bCapsulePassed = true;
    Result.Navigation = ERogueliteNavigationCheck::NotChecked;
    if (bCheckNavigation)
    {
        UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
        ANavigationData* NavData = Nav ? Nav->GetDefaultNavDataInstance(FNavigationSystem::DontCreate) : nullptr;
        Result.Navigation = ERogueliteNavigationCheck::Unavailable;
        Result.NavigationMessage = TEXT("几何与胶囊通过；当前无可用 NavMesh，导航尚未验证。");
        if (Nav && NavData && !UNavigationSystemV1::IsNavigationBeingBuiltOrLocked(GetWorld()))
        {
            const FNavDataConfig& Config = NavData->GetConfig();
            if (Config.AgentRadius + PositionTolerance < TestCapsuleRadius || Config.AgentHeight + PositionTolerance < 2 * TestCapsuleHalfHeight)
            {
                Result.NavigationMessage = TEXT("NavMesh Agent 尺寸不足以代表测试胶囊，导航尚未验证。");
            }
            else
            {
                FNavLocation StartNav, EndNav;
                if (Nav->ProjectPointToNavigation(InsideA, StartNav, FVector(20, 20, 50), NavData)
                    && Nav->ProjectPointToNavigation(InsideB, EndNav, FVector(20, 20, 50), NavData))
                {
                    FPathFindingQuery Query(this, *NavData, StartNav.Location, EndNav.Location);
                    Query.SetAllowPartialPaths(false);
                    const FPathFindingResult Path = Nav->FindPathSync(Query);
                    const bool bPathPassed = Path.IsSuccessful() && Path.Path.IsValid() && !Path.Path->IsPartial();
                    Result.Navigation = bPathPassed ? ERogueliteNavigationCheck::Passed : ERogueliteNavigationCheck::Failed;
                    Result.NavigationMessage = bPathPassed ? TEXT("两侧导航投影与完整路径通过。") : TEXT("已有 NavMesh 覆盖两侧，但完整路径失败。");
                    if (!bPathPassed)
                    {
                        Result.Message = Result.NavigationMessage;
                        return false;
                    }
                }
                else
                {
                    Result.NavigationMessage = TEXT("NavMesh 未覆盖门洞两侧测试点，导航尚未验证。");
                }
            }
        }
    }
    else
    {
        Result.NavigationMessage = TEXT("导航检查被关闭；不代表导航已通过。");
    }
    Result.bSucceeded = true;
    Result.Message = TEXT("Direct 几何对齐、占用、射线与 Pawn 胶囊检查通过。") + Result.NavigationMessage;
    return true;
}

FRogueliteAssemblyResult ARogueliteRoomAssembly::TryAlignRoomToSocket(ARogueliteParametricRoom* FixedRoom,
    ERogueliteRoomSide FixedSocket, ARogueliteParametricRoom* TargetRoom, ERogueliteRoomSide TargetSocket, ERogueliteConnectionMode Mode)
{
    FRogueliteAssemblyResult Result;
    const FString Prefix = Context(FixedRoom, FixedSocket, TargetRoom, TargetSocket);
    const auto Finish = [&]()
    {
        Result.Message = Prefix + Result.Message;
        PublishResult(Result);
        return Result;
    };
    // Generic endpoints/reservations do not impose adjacency. Only the Direct implementation below does.
    FRogueliteDoorSocket DoorA, DoorB;
    if (!ValidateEndpoints(FixedRoom, FixedSocket, TargetRoom, TargetSocket, true, DoorA, DoorB, Result))
    {
        return Finish();
    }
    int32 ExistingIndex = Connections.IndexOfByPredicate([&](const FRogueliteRoomConnection& Connection)
    {
        return Connection.bActive && Matches(Connection, FixedRoom, FixedSocket, TargetRoom, TargetSocket);
    });
    const FGuid ExceptId = ExistingIndex == INDEX_NONE ? FGuid() : Connections[ExistingIndex].Id;
    if (HasOtherConnection(TargetRoom, ExceptId))
    {
        Result.Message = TEXT("Target RoomB belongs to another active connection; moving a connected subgraph is unsupported.");
        return Finish();
    }
    if (IsOccupied(FixedRoom, FixedSocket, ExceptId) || IsOccupied(TargetRoom, TargetSocket, ExceptId))
    {
        Result.Message = TEXT("至少一个 Socket 已被其他当前连接占用；请先明确释放对应连接。");
        return Finish();
    }
    if (Mode != ERogueliteConnectionMode::Direct)
    {
        Result.Message = TEXT("连接模式尚未实现：") + UEnum::GetValueAsString(Mode) + TEXT("；本轮仅 Direct，未移动房间。");
        return Finish();
    }
    if (ExistingIndex != INDEX_NONE && Connections[ExistingIndex].Mode != Mode)
    {
        Result.Message = TEXT("已有连接模式不同，需先释放后再改变连接方式。");
        return Finish();
    }
    const double DesiredYaw = FMath::RoundToDouble((DoorA.WorldOutwardDirection.Rotation().Yaw + 180.0
        - DoorB.LocalOutwardDirection.Rotation().Yaw) / 90.0) * 90.0;
    const FQuat Rotation = FRotator(0, FRotator::NormalizeAxis(DesiredYaw), 0).Quaternion();
    Result.ProposedTargetTransform = FTransform(Rotation,
        DoorA.WorldPosition - Rotation.RotateVector(DoorB.LocalPosition), FVector::OneVector);
    if (TargetRoom->IsA<ARogueliteRoomTemplate>() && GetWorld()->IsGameWorld() && TargetRoom->HasActorBegunPlay()
        && TargetRoom->GetRootComponent()->Mobility == EComponentMobility::Static
        && !TargetRoom->GetActorTransform().Equals(Result.ProposedTargetTransform, .0001))
    {
        Result.Message = TEXT("P03D_STATIC_TEMPLATE: compute final placement before FinishSpawning; registered Static template cannot be repositioned at runtime.");
        return Finish();
    }
    if (!CheckOccupancy(FixedRoom, TargetRoom, Result.ProposedTargetTransform, Result))
    {
        return Finish();
    }
    const FTransform Before = TargetRoom->GetActorTransform();
#if WITH_EDITOR
    TUniquePtr<FScopedTransaction> Transaction;
    if (!GetWorld()->IsGameWorld())
    {
        Transaction = MakeUnique<FScopedTransaction>(NSLOCTEXT("Roguelite", "DirectAssembly", "Align Roguelite Rooms"));
        Modify();
        TargetRoom->Modify();
        for (UActorComponent* Component : TargetRoom->GetComponents())
        {
            Component->Modify();
        }
    }
#endif
    const bool bMoved = TargetRoom->SetActorTransform(Result.ProposedTargetTransform, false, nullptr, ETeleportType::TeleportPhysics);
    TargetRoom->RebuildRoom();
    if (!bMoved || !TargetRoom->GetActorTransform().Equals(Result.ProposedTargetTransform, PositionTolerance)
        || !ValidateDirect(FixedRoom, FixedSocket, TargetRoom, TargetSocket, Result))
    {
        if (Result.Message.IsEmpty()) Result.Message = TEXT("设置目标 Transform 失败。");
        TargetRoom->SetActorTransform(Before, false, nullptr, ETeleportType::TeleportPhysics);
        TargetRoom->RebuildRoom();
        Result.bTargetRestoredOnFailure = TargetRoom->GetActorTransform().Equals(Before, 0.0001);
        Result.bSucceeded = false;
#if WITH_EDITOR
        if (Transaction) Transaction->Cancel();
#endif
        return Finish();
    }
    if (ExistingIndex == INDEX_NONE)
    {
        ExistingIndex = Connections.AddDefaulted();
        Connections[ExistingIndex].Id = FGuid::NewGuid();
    }
    FRogueliteRoomConnection& Connection = Connections[ExistingIndex];
    Connection.RoomA = FixedRoom;
    Connection.SocketA = FixedSocket;
    Connection.RoomB = TargetRoom;
    Connection.SocketB = TargetSocket;
    Connection.Mode = Mode;
    Connection.bActive = true;
    Connection.bGeometryValid = true;
    Connection.Navigation = Result.Navigation;
    Connection.ValidationMessage = Prefix + Result.Message;
    RecordValidation(Connection, Result);
    Result.ConnectionId = Connection.Id;
    return Finish();
}

void ARogueliteRoomAssembly::AlignSelectedRooms()
{
    if (ConnectionMode == ERogueliteConnectionMode::StraightCorridor)
    {
        AlignCorridorConnection();
        return;
    }
    TryAlignRoomToSocket(RoomA, SocketA, RoomB, SocketB, ConnectionMode);
}

void ARogueliteRoomAssembly::PublishResult(const FRogueliteAssemblyResult& Result)
{
    LastResult = Result;
    if (!Result.bSucceeded || Result.Navigation != ERogueliteNavigationCheck::Passed)
    {
        UE_LOG(LogRoguelitePrototype, Warning, TEXT("%s"), *Result.Message);
    }
    else
    {
        UE_LOG(LogRoguelitePrototype, Display, TEXT("%s"), *Result.Message);
    }
}

void ARogueliteRoomAssembly::RevalidateConnections()
{
#if WITH_EDITOR
    if (GetWorld() && !GetWorld()->IsGameWorld()) Modify();
#endif
    if (Connections.IsEmpty())
    {
        FRogueliteAssemblyResult Empty;
        Empty.Message = TEXT("P03A 当前没有连接可重新检查。");
        PublishResult(Empty);
    }
    TArray<FGuid> Ids;
    for (const FRogueliteRoomConnection& Connection : Connections)
        if (Connection.bActive) Ids.Add(Connection.Id);
    for (const FGuid& Id : Ids) RevalidateConnection(Id);
}

FRogueliteAssemblyResult ARogueliteRoomAssembly::RevalidateConnection(FGuid ConnectionId)
{
#if WITH_EDITOR
    if (GetWorld() && !GetWorld()->IsGameWorld()) Modify();
#endif
    FRogueliteAssemblyResult Result;
    const int32 Index = Connections.IndexOfByPredicate([&](const FRogueliteRoomConnection& Item) { return Item.Id == ConnectionId && Item.bActive; });
    if (Index == INDEX_NONE)
    {
        Result.Message = TEXT("ConnectionId not found or inactive: ") + ConnectionId.ToString();
        PublishResult(Result);
        return Result;
    }
    {
        FRogueliteRoomConnection& Connection = Connections[Index];
        Result.ConnectionId = Connection.Id;
        const FString Prefix = Context(Connection.RoomA, Connection.SocketA, Connection.RoomB, Connection.SocketB);
        if (Connection.Mode == ERogueliteConnectionMode::StraightCorridor)
        {
            ValidateCorridorConnection(Connection, Result);
        }
        else if (Connection.Mode != ERogueliteConnectionMode::Direct)
        {
            Result.Message = TEXT("此连接模式尚未实现验证；保留逻辑端点与占用，不套用 Direct 的邻接限制。");
        }
        else if (IsOccupied(Connection.RoomA, Connection.SocketA, Connection.Id) || IsOccupied(Connection.RoomB, Connection.SocketB, Connection.Id))
        {
            Result.Message = TEXT("存在重复 Socket 占用，连接无效。");
        }
        else
        {
            ValidateDirect(Connection.RoomA, Connection.SocketA, Connection.RoomB, Connection.SocketB, Result);
        }
        Result.Message = Prefix + Result.Message;
        RecordValidation(Connection, Result);
        if (IsValid(Connection.Corridor)) Connection.Corridor->RebuildCorridor();
        PublishResult(Result);
    }
    return Result;
}

bool ARogueliteRoomAssembly::ReleaseConnection(FGuid ConnectionId)
{
    const int32 Index = Connections.IndexOfByPredicate([&](const FRogueliteRoomConnection& Item) { return Item.Id == ConnectionId; });
    if (Index == INDEX_NONE) return true; // Idempotent release; there is no reservation left.
#if WITH_EDITOR
    TUniquePtr<FScopedTransaction> Transaction;
    if (GetWorld() && !GetWorld()->IsGameWorld())
    {
        Transaction = MakeUnique<FScopedTransaction>(NSLOCTEXT("Roguelite", "ReleaseConnection", "Release Roguelite Connection"));
        Modify();
    }
#endif
    ARogueliteParametricCorridor* ReleasedCorridor = Connections[Index].Corridor;
    Connections.RemoveAt(Index);
    if (IsValid(ReleasedCorridor)) ReleasedCorridor->RebuildCorridor(); // Derived socket cache only.
    return true;
}

void ARogueliteRoomAssembly::DisconnectSelectedRooms()
{
    const int32 Index = Connections.IndexOfByPredicate([&](const FRogueliteRoomConnection& Item) { return Matches(Item, RoomA, SocketA, RoomB, SocketB); });
    FRogueliteAssemblyResult Result;
    Result.Message = Context(RoomA, SocketA, RoomB, SocketB)
        + (Index != INDEX_NONE && ReleaseConnection(Connections[Index].Id)
            ? TEXT("逻辑连接已释放；未移动房间或修改门洞。") : TEXT("没有找到所选连接。"));
    PublishResult(Result);
}
