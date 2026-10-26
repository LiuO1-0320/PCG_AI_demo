#include "RogueliteRoomAssembly.h"
#include "Components/SceneComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "NavigationSystem.h"
#include "NavigationData.h"
#include "NavigationPath.h"
#if WITH_EDITOR
#include "ScopedTransaction.h"
#endif

namespace
{
    bool Overlap(const FBox& A, const FBox& B, float Epsilon)
    {
        return FMath::Min(A.Max.X, B.Max.X) - FMath::Max(A.Min.X, B.Min.X) > Epsilon
            && FMath::Min(A.Max.Y, B.Max.Y) - FMath::Max(A.Min.Y, B.Min.Y) > Epsilon
            && FMath::Min(A.Max.Z, B.Max.Z) - FMath::Max(A.Min.Z, B.Min.Z) > Epsilon;
    }
    ERogueliteCorridorEndpoint OtherEnd(ERogueliteCorridorEndpoint End)
    {
        return End == ERogueliteCorridorEndpoint::Start ? ERogueliteCorridorEndpoint::End : ERogueliteCorridorEndpoint::Start;
    }
    FString ConnectionContext(ARogueliteParametricRoom* A, ERogueliteRoomSide SideA,
        ARogueliteParametricCorridor* C, ERogueliteCorridorEndpoint EndA, ARogueliteParametricRoom* B, ERogueliteRoomSide SideB)
    {
        return FString::Printf(TEXT("P03C2 RoomA=%s SocketA=%s Corridor=%s EndpointA=%s RoomB=%s SocketB=%s: "),
            *GetPathNameSafe(A), *UEnum::GetValueAsString(SideA), *GetPathNameSafe(C), *UEnum::GetValueAsString(EndA),
            *GetPathNameSafe(B), *UEnum::GetValueAsString(SideB));
    }
}

FGuid ARogueliteRoomAssembly::GetRoomSocketConnectionId(ARogueliteParametricRoom* Room, ERogueliteRoomSide Side)
{
    if (!IsValid(Room) || !Room->GetWorld()) return FGuid();
    for (TActorIterator<ARogueliteRoomAssembly> It(Room->GetWorld()); It; ++It)
        for (const FRogueliteRoomConnection& C : It->Connections)
            if (C.bActive && ((C.RoomA == Room && C.SocketA == Side) || (C.RoomB == Room && C.SocketB == Side))) return C.Id;
    return FGuid();
}

FGuid ARogueliteRoomAssembly::GetCorridorConnectionId(ARogueliteParametricCorridor* Connector)
{
    if (!IsValid(Connector) || !Connector->GetWorld()) return FGuid();
    for (TActorIterator<ARogueliteRoomAssembly> It(Connector->GetWorld()); It; ++It)
        for (const FRogueliteRoomConnection& C : It->Connections)
            if (C.bActive && C.Corridor == Connector) return C.Id;
    return FGuid();
}

bool ARogueliteRoomAssembly::IsCorridorOccupied(ARogueliteParametricCorridor* Connector, const FGuid& ExceptId) const
{
    for (TActorIterator<ARogueliteRoomAssembly> It(GetWorld()); It; ++It)
        for (const FRogueliteRoomConnection& C : It->Connections)
            if (C.bActive && !(It.operator->() == this && C.Id == ExceptId) && C.Corridor == Connector) return true;
    return false;
}

bool ARogueliteRoomAssembly::HasOtherConnection(AActor* Actor, const FGuid& ExceptId) const
{
    for (TActorIterator<ARogueliteRoomAssembly> It(GetWorld()); It; ++It)
        for (const FRogueliteRoomConnection& C : It->Connections)
            if (C.bActive && !(It.operator->() == this && C.Id == ExceptId)
                && (C.RoomA == Actor || C.RoomB == Actor || C.Corridor == Actor)) return true;
    return false;
}

void ARogueliteRoomAssembly::RecordValidation(FRogueliteRoomConnection& C, const FRogueliteAssemblyResult& R)
{
    C.bGeometryValid = R.bSucceeded && R.bGeometryPassed && R.bCapsulePassed;
    C.Navigation = R.Navigation;
    C.ValidationStatus = R.bSucceeded ? ERogueliteConnectionValidation::Passed : ERogueliteConnectionValidation::Failed;
    ++C.ValidationRevision;
    C.ValidationMessage = R.Message;
}

bool ARogueliteRoomAssembly::ComputeCorridorTransforms(ARogueliteParametricRoom* A, ERogueliteRoomSide SideA,
    ARogueliteParametricCorridor* C, ERogueliteCorridorEndpoint EndA, ARogueliteParametricRoom* B, ERogueliteRoomSide SideB,
    FTransform& OutCorridor, FTransform& OutRoomB, FString& OutReason) const
{
    OutCorridor = FTransform::Identity;
    OutRoomB = FTransform::Identity;
    FRogueliteAssemblyResult R;
    FRogueliteDoorSocket DA, DB;
    if (!ValidateEndpoints(A, SideA, B, SideB, true, DA, DB, R)) { OutReason = R.Message; return false; }
    if (!IsValid(C) || C->IsActorBeingDestroyed() || C->GetWorld() != GetWorld())
    { OutReason = TEXT("Invalid corridor actor/world."); return false; }
    FRogueliteCorridorSocket CA, CB;
    if (!C->TryGetCorridorSocket(EndA, CA) || !C->TryGetCorridorSocket(OtherEnd(EndA), CB))
    { OutReason = TEXT("Invalid corridor endpoint, dimensions or transform: ") + CA.DoorSocket.ValidationMessage; return false; }
    if (!C->IsRoomSocketCompatible(EndA, DA, OutReason) || !C->IsRoomSocketCompatible(OtherEnd(EndA), DB, OutReason)) return false;
    TArray<AActor*> Attached;
    C->GetAttachedActors(Attached);
    if (!Attached.IsEmpty()) { OutReason = TEXT("Corridor has attached actors; rigid template assembly is not implemented."); return false; }
    return ComputeCorridorSocketTransforms(DA, DB, CA.DoorSocket, CB.DoorSocket, DoorSizeTolerance, OutCorridor, OutRoomB, OutReason);
}

bool ARogueliteRoomAssembly::ComputeCorridorSocketTransforms(const FRogueliteDoorSocket& DA, const FRogueliteDoorSocket& DB,
    const FRogueliteDoorSocket& CA, const FRogueliteDoorSocket& CB, float SizeTolerance,
    FTransform& OutCorridor, FTransform& OutRoomB, FString& OutReason)
{
    OutCorridor=FTransform::Identity; OutRoomB=FTransform::Identity;
    if(!FMath::IsFinite(SizeTolerance) || SizeTolerance<0) { OutReason=TEXT("Invalid socket size tolerance."); return false; }
    for(const auto* D:{&DA,&DB,&CA,&CB})
    {
        if(!D->bValid || D->LocalPosition.ContainsNaN() || D->WorldPosition.ContainsNaN()
            || D->LocalOutwardDirection.ContainsNaN() || D->WorldOutwardDirection.ContainsNaN()
            || !FMath::IsFinite(D->DoorWidth) || !FMath::IsFinite(D->DoorHeight)
            || D->DoorWidth<=0 || D->DoorHeight<=0
            || FMath::Abs(D->DoorWidth-DA.DoorWidth)>SizeTolerance || FMath::Abs(D->DoorHeight-DA.DoorHeight)>SizeTolerance)
        { OutReason=TEXT("Socket specifications invalid/incompatible; calculation only, no reservation."); return false; }
        for(const auto& N:{D->LocalOutwardDirection,D->WorldOutwardDirection})
            if(FMath::Abs(N.SizeSquared()-1)>.0001 || FMath::Abs(N.Z)>.0001 || FMath::Abs(N.X*N.Y)>.0001)
            {OutReason=TEXT("Socket direction must be a normalized horizontal quarter-turn normal.");return false;}
    }
    const float YawC = FMath::RoundToFloat((DA.WorldOutwardDirection.Rotation().Yaw + 180 - CA.LocalOutwardDirection.Rotation().Yaw) / 90) * 90;
    const FQuat RotationC = FRotator(0, FRotator::NormalizeAxis(YawC), 0).Quaternion();
    OutCorridor = FTransform(RotationC, DA.WorldPosition - RotationC.RotateVector(CA.LocalPosition), FVector::OneVector);
    const FVector EndPosition = OutCorridor.TransformPosition(CB.LocalPosition);
    const FVector EndDirection = OutCorridor.TransformVectorNoScale(CB.LocalOutwardDirection);
    const float YawB = FMath::RoundToFloat((EndDirection.Rotation().Yaw + 180 - DB.LocalOutwardDirection.Rotation().Yaw) / 90) * 90;
    const FQuat RotationB = FRotator(0, FRotator::NormalizeAxis(YawB), 0).Quaternion();
    OutRoomB = FTransform(RotationB, EndPosition - RotationB.RotateVector(DB.LocalPosition), FVector::OneVector);
    OutReason = TEXT("Calculated transforms only; occupancy, collision, reservations and navigation require assembly validation.");
    return true;
}

bool ARogueliteRoomAssembly::CheckCorridorOccupancy(ARogueliteParametricRoom* A, ARogueliteParametricCorridor* C,
    ARogueliteParametricRoom* B, const FTransform& CT, const FTransform& BT, FRogueliteAssemblyResult& R) const
{
    // Reuse P03-A room footprint and room-to-room validation, also considering other corridors.
    if (!CheckOccupancy(A, B, BT, R, C)) return false;
    const FBox Local = C->GetLocalOccupancyBounds();
    if (!Local.IsValid) { R.Message = TEXT("Corridor parameter occupancy unavailable."); return false; }
    R.OccupancyCorridor = Local.TransformBy(CT);
    if (Overlap(R.OccupancyCorridor, R.OccupancyA, PositionTolerance) || Overlap(R.OccupancyCorridor, R.OccupancyB, PositionTolerance))
    { R.Message = TEXT("Corridor intersects RoomA/RoomB parameter occupancy; boundary contact alone is allowed."); return false; }
    for (TActorIterator<ARogueliteParametricRoom> It(GetWorld()); It; ++It)
    {
        if (*It == A || *It == B) continue;
        const double X = It->RoomLength * .5 + It->WallThickness, Y = It->RoomWidth * .5 + It->WallThickness;
        const FBox Bounds = FBox(FVector(-X, -Y, -It->FloorThickness), FVector(X, Y, It->RoomHeight)).TransformBy(It->GetActorTransform());
        if (Overlap(R.OccupancyCorridor, Bounds, PositionTolerance))
        { R.Message = TEXT("Corridor occupancy intersects room: ") + It->GetPathName(); return false; }
    }
    for (TActorIterator<ARogueliteParametricCorridor> It(GetWorld()); It; ++It)
    {
        if (*It == C) continue;
        const FBox Bounds = It->GetWorldOccupancyBounds();
        if (!Bounds.IsValid || Overlap(R.OccupancyCorridor, Bounds, PositionTolerance))
        { R.Message = TEXT("Corridor occupancy conflicts with corridor: ") + It->GetPathName(); return false; }
    }
    return true;
}

FRogueliteAssemblyResult ARogueliteRoomAssembly::TryAlignCorridorConnection(ARogueliteParametricRoom* A, ERogueliteRoomSide SideA,
    ARogueliteParametricCorridor* C, ERogueliteCorridorEndpoint EndA, ARogueliteParametricRoom* B, ERogueliteRoomSide SideB)
{
    FRogueliteAssemblyResult R;
    const FString Prefix = ConnectionContext(A, SideA, C, EndA, B, SideB);
    int32 Existing = Connections.IndexOfByPredicate([&](const FRogueliteRoomConnection& Item)
    { return Item.bActive && Item.RoomA == A && Item.SocketA == SideA && Item.RoomB == B && Item.SocketB == SideB
        && Item.Corridor == C && Item.Mode == ERogueliteConnectionMode::StraightCorridor; });
    const FGuid ExceptId = Existing == INDEX_NONE ? FGuid() : Connections[Existing].Id;
    R.ConnectionId = ExceptId;
    const auto Finish = [&]()
    {
        R.Message = Prefix + R.Message;
        // Reservation topology remains unchanged on failure. Refresh diagnostics rather than keeping stale success.
        if (!R.bSucceeded && Existing != INDEX_NONE) RecordValidation(Connections[Existing], R);
        PublishResult(R);
        return R;
    };
    if (!ComputeCorridorTransforms(A, SideA, C, EndA, B, SideB, R.ProposedCorridorTransform, R.ProposedTargetTransform, R.Message)) return Finish();
    if (IsOccupied(A, SideA, ExceptId) || IsOccupied(B, SideB, ExceptId) || IsCorridorOccupied(C, ExceptId))
    { R.Message = TEXT("Socket/corridor reserved by another connection; no movement."); return Finish(); }
    if (HasOtherConnection(B, ExceptId) || HasOtherConnection(C, ExceptId))
    { R.Message = TEXT("Target RoomB/corridor belongs to another connected subgraph; automatic subgraph movement refused."); return Finish(); }
    if (!CheckCorridorOccupancy(A, C, B, R.ProposedCorridorTransform, R.ProposedTargetTransform, R)) return Finish();
    const FTransform BeforeC = C->GetActorTransform(), BeforeB = B->GetActorTransform();
    const bool MoveC = !BeforeC.Equals(R.ProposedCorridorTransform, .0001), MoveB = !BeforeB.Equals(R.ProposedTargetTransform, .0001);
    if (GetWorld()->IsGameWorld() && ((MoveC && C->HasActorBegunPlay() && C->GetRootComponent()->Mobility == EComponentMobility::Static)
        || (MoveB && B->HasActorBegunPlay() && B->GetRootComponent()->Mobility == EComponentMobility::Static)))
    { R.Message = TEXT("Registered Static runtime shells cannot be moved safely. Compute transforms before FinishSpawning, then validate the placed actors."); return Finish(); }
#if WITH_EDITOR
    TUniquePtr<FScopedTransaction> Transaction;
    if (!GetWorld()->IsGameWorld())
    {
        Transaction = MakeUnique<FScopedTransaction>(NSLOCTEXT("Roguelite", "CorridorAssembly", "Align Roguelite Corridor Connection"));
        Modify(); C->Modify(); B->Modify();
        for (UActorComponent* Component : C->GetComponents()) Component->Modify();
        for (UActorComponent* Component : B->GetComponents()) Component->Modify();
    }
#endif
    FRogueliteRoomConnection Candidate;
    Candidate.RoomA = A; Candidate.SocketA = SideA; Candidate.RoomB = B; Candidate.SocketB = SideB;
    Candidate.Corridor = C; Candidate.EndpointA = EndA; Candidate.EndpointB = OtherEnd(EndA);
    Candidate.Mode = ERogueliteConnectionMode::StraightCorridor; Candidate.Id = ExceptId;
    const bool MovedC = !MoveC || C->SetActorTransform(R.ProposedCorridorTransform, false, nullptr, ETeleportType::TeleportPhysics);
    const bool MovedB = !MoveB || B->SetActorTransform(R.ProposedTargetTransform, false, nullptr, ETeleportType::TeleportPhysics);
    if (MoveC) C->RebuildCorridor();
    if (MoveB) B->RebuildRoom();
    if (!MovedC || !MovedB || !ValidateCorridorConnection(Candidate, R))
    {
        if (R.Message.IsEmpty()) R.Message = TEXT("Failed to apply transforms.");
        if (MoveC) { C->SetActorTransform(BeforeC, false, nullptr, ETeleportType::TeleportPhysics); C->RebuildCorridor(); }
        if (MoveB) { B->SetActorTransform(BeforeB, false, nullptr, ETeleportType::TeleportPhysics); B->RebuildRoom(); }
        R.bCorridorRestoredOnFailure = C->GetActorTransform().Equals(BeforeC, .0001);
        R.bTargetRestoredOnFailure = B->GetActorTransform().Equals(BeforeB, .0001);
        R.bSucceeded = false;
#if WITH_EDITOR
        if (Transaction) Transaction->Cancel();
#endif
        return Finish();
    }
    if (Existing == INDEX_NONE) { Existing = Connections.AddDefaulted(); Candidate.Id = FGuid::NewGuid(); }
    else Candidate.ValidationRevision = Connections[Existing].ValidationRevision;
    Connections[Existing] = Candidate;
    R.ConnectionId = Candidate.Id;
    RecordValidation(Connections[Existing], R);
    C->RebuildCorridor(); // Read-only derived reservation cache now sees the committed record.
    return Finish();
}

void ARogueliteRoomAssembly::AlignCorridorConnection()
{
    TryAlignCorridorConnection(RoomA, SocketA, Corridor, CorridorEndpointA, RoomB, SocketB);
}

void ARogueliteRoomAssembly::DisconnectCorridorConnection()
{
    const int32 Index = Connections.IndexOfByPredicate([&](const FRogueliteRoomConnection& C)
    { return C.RoomA == RoomA && C.SocketA == SocketA && C.RoomB == RoomB && C.SocketB == SocketB && C.Corridor == Corridor
        && C.Mode == ERogueliteConnectionMode::StraightCorridor; });
    FRogueliteAssemblyResult R;
    if (Index != INDEX_NONE) ReleaseConnection(Connections[Index].Id);
    R.Message = TEXT("P03C2 selected connection released/already absent; actors and door configuration unchanged.");
    PublishResult(R);
}

bool ARogueliteRoomAssembly::ValidateCorridorConnection(const FRogueliteRoomConnection& C, FRogueliteAssemblyResult& R, bool bSkipNavigation) const
{
    FRogueliteDoorSocket DA, DB;
    if (!ValidateEndpoints(C.RoomA, C.SocketA, C.RoomB, C.SocketB, false, DA, DB, R)) return false;
    FRogueliteCorridorSocket CA, CB;
    if (!IsValid(C.Corridor) || C.Corridor->GetWorld() != GetWorld() || C.EndpointB != OtherEnd(C.EndpointA)
        || !C.Corridor->TryGetCorridorSocket(C.EndpointA, CA) || !C.Corridor->TryGetCorridorSocket(C.EndpointB, CB))
    { R.Message = TEXT("Invalid corridor/mapping/openings/transform."); return false; }
    if (!C.Corridor->IsRoomSocketCompatible(C.EndpointA, DA, R.Message) || !C.Corridor->IsRoomSocketCompatible(C.EndpointB, DB, R.Message)) return false;
    if (IsOccupied(C.RoomA, C.SocketA, C.Id) || IsOccupied(C.RoomB, C.SocketB, C.Id) || IsCorridorOccupied(C.Corridor, C.Id))
    { R.Message = TEXT("Conflicting reservation; connection invalid."); return false; }
    if (!CheckCorridorOccupancy(C.RoomA, C.Corridor, C.RoomB, C.Corridor->GetActorTransform(), C.RoomB->GetActorTransform(), R)) return false;
    R.SocketDistance = FVector::Distance(DA.WorldPosition, CA.DoorSocket.WorldPosition);
    R.SecondSocketDistance = FVector::Distance(DB.WorldPosition, CB.DoorSocket.WorldPosition);
    R.NormalDot = FVector::DotProduct(DA.WorldOutwardDirection, CA.DoorSocket.WorldOutwardDirection);
    R.SecondNormalDot = FVector::DotProduct(DB.WorldOutwardDirection, CB.DoorSocket.WorldOutwardDirection);
    const double Opposite = -FMath::Cos(FMath::DegreesToRadians(static_cast<double>(AngleToleranceDegrees)));
    if (R.SocketDistance > PositionTolerance || R.SecondSocketDistance > PositionTolerance || R.NormalDot > Opposite || R.SecondNormalDot > Opposite)
    { R.Message = FString::Printf(TEXT("Threshold mismatch: distances %.4f/%.4f cm, normal dots %.6f/%.6f."), R.SocketDistance, R.SecondSocketDistance, R.NormalDot, R.SecondNormalDot); return false; }
    return CheckCorridorPassage(C, DA, DB, R, bSkipNavigation);
}

bool ARogueliteRoomAssembly::CheckCorridorPassage(const FRogueliteRoomConnection& C,
    const FRogueliteDoorSocket& DA, const FRogueliteDoorSocket& DB, FRogueliteAssemblyResult& R, bool bSkipNavigation) const
{
    const bool OpenA = C.RoomA->GetDoorOpeningMode(C.SocketA) == ERogueliteOpeningMode::OpenPassage;
    const bool OpenB = C.RoomB->GetDoorOpeningMode(C.SocketB) == ERogueliteOpeningMode::OpenPassage;
    if (OpenA != OpenB)
    { R.Message = TEXT("HF03 prototype requires matching Legacy/OpenPassage modes at both ends; mixed transitions are not implemented."); return false; }
    const bool OpenPassage = OpenA && OpenB;
    if (OpenPassage && (DA.DoorWidth != C.Corridor->CorridorWidth || DB.DoorWidth != C.Corridor->CorridorWidth
        || DA.DoorHeight != C.RoomA->RoomHeight || DB.DoorHeight != C.RoomB->RoomHeight
        || DA.DoorHeight != C.Corridor->CorridorHeight || DB.DoorHeight != C.Corridor->CorridorHeight))
    { R.Message = TEXT("HF03 OpenPassage requires full corridor width and equal finished-floor-to-wall/ceiling heights. Unequal-height adaptation is not implemented; original configuration retained."); return false; }
    const FVector Z = FVector::UpVector;
    FCollisionQueryParams Rays(SCENE_QUERY_STAT(RogueliteCorridorConnectionRay), true);
    Rays.AddIgnoredActor(this);
    bool Geometry = true, Capsules = true;
    const auto Ray = [&](const FString& Name, const FVector& Start, const FVector& End, AActor* Expected, AActor* Ignore = nullptr)
    {
        FCollisionQueryParams Params = Rays;
        if (Ignore) Params.AddIgnoredActor(Ignore);
        FHitResult Hit;
        const bool Blocked = GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params);
        FRogueliteAssemblyProbe P;
        P.Name = Name; P.Start = Start; P.End = End; P.bExpectedBlocking = Expected != nullptr;
        P.bBlockingHit = Blocked; P.HitDistance = Hit.Distance; P.HitObject = GetPathNameSafe(Hit.GetComponent());
        P.bPassed = Expected ? Blocked && Hit.GetActor() == Expected : !Blocked;
        Geometry &= P.bPassed; R.Probes.Add(P);
    };
    const FCollisionShape Capsule = FCollisionShape::MakeCapsule(TestCapsuleRadius, TestCapsuleHalfHeight);
    FCollisionQueryParams Sweeps(SCENE_QUERY_STAT(RogueliteCorridorConnectionCapsule), false);
    Sweeps.AddIgnoredActor(this);
    const auto Sweep = [&](const FString& Name, const FVector& Start, const FVector& End, bool Ground = false, AActor* FloorA = nullptr, AActor* FloorB = nullptr)
    {
        FHitResult Hit;
        const bool Blocked = GetWorld()->SweepSingleByChannel(Hit, Start, End, FQuat::Identity, ECC_Pawn, Capsule, Sweeps);
        const bool StartOverlap = GetWorld()->OverlapBlockingTestByChannel(Start, FQuat::Identity, ECC_Pawn, Capsule, Sweeps);
        const bool EndOverlap = !Ground && GetWorld()->OverlapBlockingTestByChannel(End, FQuat::Identity, ECC_Pawn, Capsule, Sweeps);
        FRogueliteAssemblyProbe P;
        P.Name = Name; P.Start = Start; P.End = End; P.bExpectedBlocking = Ground;
        P.bBlockingHit = Blocked || StartOverlap || EndOverlap; P.HitDistance = Hit.Distance;
        P.HitObject = GetPathNameSafe(Hit.GetComponent());
        P.bPassed = Ground ? Blocked && !StartOverlap && !Hit.bStartPenetrating && (Hit.GetActor() == FloorA || Hit.GetActor() == FloorB)
            && Hit.GetComponent() && Hit.GetComponent()->GetFName() == TEXT("Floor") && Hit.ImpactNormal.Z > .99f : !P.bBlockingHit;
        Capsules &= P.bPassed; R.Probes.Add(P);
    };
    const float Edge = DA.DoorWidth * .5f - TestCapsuleRadius - CapsuleClearance;
    const FVector CapsuleZ = Z * (TestCapsuleHalfHeight + CapsuleClearance);
    FVector InsideRooms[2], InsideCorridor[2];
    const FRogueliteDoorSocket Doors[] = {DA, DB};
    ARogueliteParametricRoom* Rooms[] = {C.RoomA, C.RoomB};
    for (int32 Pair = 0; Pair < 2; ++Pair)
    {
        const auto& Door = Doors[Pair]; auto* Room = Rooms[Pair];
        const FVector Center = Door.WorldPosition, N = Door.WorldOutwardDirection;
        const FVector T = FVector::CrossProduct(Z, N).GetSafeNormal();
        InsideRooms[Pair] = Center - N * (Room->WallThickness + 2 * TestCapsuleRadius + 20);
        InsideCorridor[Pair] = Center + N * (C.Corridor->WallThickness + 2 * TestCapsuleRadius + 20);
        const FString Label = Pair == 0 ? TEXT("A-C") : TEXT("C-B");
        for (float Offset : {-Door.DoorWidth * .5f + 1, 0.0f, Door.DoorWidth * .5f - 1})
            for (float Height : {1.0f, Door.DoorHeight * .5f, Door.DoorHeight - 1})
                Ray(Label + FString::Printf(TEXT(" opening %.1f z%.1f"), Offset, Height), InsideRooms[Pair] + T * Offset + Z * Height,
                    InsideCorridor[Pair] + T * Offset + Z * Height, nullptr);
        for (int32 Which = 0; Which < 2; ++Which)
        {
            AActor* Entity = Which == 0 ? static_cast<AActor*>(Room) : C.Corridor.Get();
            AActor* Ignore = Which == 0 ? C.Corridor.Get() : static_cast<AActor*>(Room);
            const FVector Out = Which == 0 ? N : -N;
            const float Thick = Which == 0 ? Room->WallThickness : C.Corridor->WallThickness;
            const float Height = Which == 0 ? Room->RoomHeight : C.Corridor->CorridorHeight;
            const float Width = Which == 0 ? (Door.Side == ERogueliteRoomSide::North || Door.Side == ERogueliteRoomSide::South ? Room->RoomWidth : Room->RoomLength) : C.Corridor->CorridorWidth;
            const float SideOffset = Door.DoorWidth * .5f + FMath::Min(10.0f, FMath::Max(Thick * .5f, (Width - Door.DoorWidth) * .25f));
            for (float Sign : {-1.0f, 1.0f})
            {
                const FVector Shift = T * (Sign * SideOffset) + Z * Door.DoorHeight * .5f;
                Ray(Label + FString::Printf(TEXT(" entity%d side%.0f"), Which, Sign), Center + Out * (Thick + 25) + Shift,
                    Center - Out * (Thick + 25) + Shift, Entity, Ignore);
            }
            if (OpenPassage && Which == 0)
            {
                // This opening reaches the wall top; prove the aperture is clear and
                // independently prove the adjacent wall ends remain solid to the top.
                const FVector Shift = Z * (Door.DoorHeight - 1);
                Ray(Label + TEXT(" OpenPassage upper aperture clear"), Center + Out * (Thick + 25) + Shift,
                    Center - Out * (Thick + 25) + Shift, nullptr);
                for (float Sign : {-1.0f, 1.0f})
                {
                    const FVector Bound = T * (Sign * SideOffset) + Shift;
                    Ray(Label + FString::Printf(TEXT(" OpenPassage upper wall boundary %.0f"), Sign),
                        Center + Out * (Thick + 25) + Bound, Center - Out * (Thick + 25) + Bound, Room, C.Corridor);
                }
            }
            else
            {
                // Legacy names, positions, expectations and count stay unchanged.
                const float LintelHeight = Height > Door.DoorHeight ? Door.DoorHeight + (Height - Door.DoorHeight) * .5f : Height + Thick * .5f;
                const FVector Shift = Z * LintelHeight;
                const bool AbsentRoofEdge = Which == 1 && !C.Corridor->bGenerateCeiling && Height <= Door.DoorHeight;
                Ray(Label + (AbsentRoofEdge ? FString(TEXT(" open roof edge clear")) : OpenPassage ? FString(TEXT(" corridor roof edge")) : FString::Printf(TEXT(" entity%d lintel"), Which)),
                    Center + Out * (Thick + 25) + Shift, Center - Out * (Thick + 25) + Shift, AbsentRoofEdge ? nullptr : Entity, Ignore);
            }
            const FVector Floor = Which == 0 ? InsideRooms[Pair] : InsideCorridor[Pair];
            Ray(Label + FString::Printf(TEXT(" entity%d floor"), Which), Floor + Z * 20, Floor - Z * 20, Entity);
        }
        for (float Offset : {-Edge, 0.0f, Edge})
        {
            const FVector Shift = T * Offset;
            Sweep(Label + FString::Printf(TEXT(" PawnCapsule forward %.1f"), Offset), InsideRooms[Pair] + Shift + CapsuleZ, InsideCorridor[Pair] + Shift + CapsuleZ);
            Sweep(Label + FString::Printf(TEXT(" PawnCapsule ground %.1f"), Offset), Center + Shift + CapsuleZ,
                Center + Shift + Z * (TestCapsuleHalfHeight - CapsuleClearance), true, Room, C.Corridor);
        }
        Sweep(Label + TEXT(" PawnCapsule reverse"), InsideCorridor[Pair] + CapsuleZ, InsideRooms[Pair] + CapsuleZ);
        if (OpenPassage)
        {
            const FVector UpperCapsuleZ = Z * (Door.DoorHeight - TestCapsuleHalfHeight - CapsuleClearance);
            for (float Offset : {-Edge, 0.0f, Edge})
                Sweep(Label + FString::Printf(TEXT(" OpenPassage upper capsule clear %.1f"), Offset),
                    InsideRooms[Pair] + T * Offset + UpperCapsuleZ, InsideCorridor[Pair] + T * Offset + UpperCapsuleZ);
            for (int32 SideIndex = 0; SideIndex < 4; ++SideIndex)
            {
                const auto Side = static_cast<ERogueliteRoomSide>(SideIndex);
                if (Side == Door.Side) continue;
                FRogueliteDoorSocket OtherDoor;
                if (Room->TryGetDoorSocket(Side, OtherDoor)) continue;
                const bool AlongY = Side == ERogueliteRoomSide::East || Side == ERogueliteRoomSide::West;
                const float Sign = Side == ERogueliteRoomSide::South || Side == ERogueliteRoomSide::West ? -1.0f : 1.0f;
                const FVector LocalN = AlongY ? FVector(0, Sign, 0) : FVector(Sign, 0, 0);
                const FVector NWall = Room->GetActorTransform().TransformVectorNoScale(LocalN);
                const FVector Wall = Room->GetActorTransform().TransformPosition(LocalN * ((AlongY ? Room->RoomWidth : Room->RoomLength) * .5f + Room->WallThickness * .5f));
                const FVector Distance = NWall * (Room->WallThickness + 2 * TestCapsuleRadius + 20);
                FRogueliteAssemblyProbe P;
                P.Name = Label + FString::Printf(TEXT(" OpenPassage closed perimeter PawnCapsule side%d"), SideIndex);
                P.Start = Wall - Distance + CapsuleZ; P.End = Wall + Distance + CapsuleZ; P.bExpectedBlocking = true;
                FHitResult Hit;
                const bool StartOverlap = GetWorld()->OverlapBlockingTestByChannel(P.Start, FQuat::Identity, ECC_Pawn, Capsule, Sweeps);
                const bool Blocked = GetWorld()->SweepSingleByChannel(Hit, P.Start, P.End, FQuat::Identity, ECC_Pawn, Capsule, Sweeps);
                P.bBlockingHit = Blocked; P.HitDistance = Hit.Distance; P.HitObject = GetPathNameSafe(Hit.GetComponent());
                P.bPassed = Blocked && !StartOverlap && !Hit.bStartPenetrating && Hit.GetActor() == Room && FMath::Abs(Hit.ImpactNormal.Z) < .01f;
                Capsules &= P.bPassed; R.Probes.Add(P);
            }
        }
    }
    const FVector T = FVector::CrossProduct(Z, DA.WorldOutwardDirection).GetSafeNormal();
    for (float Offset : {-Edge, 0.0f, Edge})
        Sweep(FString::Printf(TEXT("A-C-B PawnCapsule entire %.1f"), Offset), InsideRooms[0] + T * Offset + CapsuleZ, InsideRooms[1] + T * Offset + CapsuleZ);
    Sweep(TEXT("A-C-B PawnCapsule reverse entire"), InsideRooms[1] + CapsuleZ, InsideRooms[0] + CapsuleZ);
    for (float Offset : {-Edge, 0.0f, Edge})
        Sweep(FString::Printf(TEXT("Corridor internal PawnCapsule %.1f"), Offset), InsideCorridor[0] + T * Offset + CapsuleZ, InsideCorridor[1] + T * Offset + CapsuleZ);
    const FTransform CT = C.Corridor->GetActorTransform();
    const FVector Middle = CT.TransformPosition(FVector(0, 0, 100));
    Ray(TEXT("Corridor floor"), CT.GetLocation() + Z * 20, CT.GetLocation() - Z * 20, C.Corridor);
    Ray(C.Corridor->bGenerateCeiling ? TEXT("Corridor ceiling") : TEXT("Corridor open top center"), Middle,
        Middle + Z * C.Corridor->CorridorHeight, C.Corridor->bGenerateCeiling ? C.Corridor.Get() : nullptr);
    for (float Sign : {-1.0f, 1.0f})
        Ray(TEXT("Corridor side"), Middle, CT.TransformPosition(FVector(0, Sign * (C.Corridor->CorridorWidth * .5f + C.Corridor->WallThickness + 20), 100)), C.Corridor);
    if (!C.Corridor->bGenerateCeiling)
    {
        // Interior only: keep an entire capsule away from both Legacy end headers.
        const float SafeX = C.Corridor->CorridorLength * .5f - C.Corridor->WallThickness - TestCapsuleRadius - CapsuleClearance;
        if (SafeX <= 0)
        { R.Message = TEXT("Ceiling OFF: corridor interior is too short to verify an upward Pawn capsule clear of the end headers."); return false; }
        const float SafeY = C.Corridor->CorridorWidth * .5f - TestCapsuleRadius - CapsuleClearance;
        for (float X : {-SafeX * .5f, 0.0f, SafeX * .5f})
            for (float Y : {-SafeY, 0.0f, SafeY})
                Sweep(FString::Printf(TEXT("Corridor open top PawnCapsule x%.1f y%.1f"), X, Y),
                    CT.TransformPosition(FVector(X, Y, C.Corridor->CorridorHeight - TestCapsuleHalfHeight - CapsuleClearance)),
                    CT.TransformPosition(FVector(X, Y, C.Corridor->CorridorHeight + C.Corridor->WallThickness + TestCapsuleHalfHeight + CapsuleClearance)));
    }
    R.bGeometryPassed = Geometry; R.bCapsulePassed = Capsules;
    if (!Geometry || !Capsules)
    { R.Message = TEXT("Collision validation failed: see seam/internal/whole-path Probes and blocking component."); return false; }
    // Non-parametric world obstacles anywhere inside the moved footprints are also forbidden.
    FCollisionQueryParams Occupied(SCENE_QUERY_STAT(RogueliteCorridorPhysicalOccupancy), false);
    Occupied.AddIgnoredActor(this); Occupied.AddIgnoredActor(C.RoomA); Occupied.AddIgnoredActor(C.RoomB); Occupied.AddIgnoredActor(C.Corridor);
    for (const FBox& Bounds : {R.OccupancyB, R.OccupancyCorridor})
        if (GetWorld()->OverlapBlockingTestByChannel(Bounds.GetCenter(), FQuat::Identity, ECC_Pawn,
            FCollisionShape::MakeBox(Bounds.GetExtent() - FVector(PositionTolerance)), Occupied))
        { R.Message = TEXT("Moved room/corridor occupancy intersects a blocking world object outside the verified path."); return false; }
    const bool EvaluateNavigation = bCheckNavigation && !bSkipNavigation;
    R.Navigation = EvaluateNavigation ? ERogueliteNavigationCheck::Unavailable : ERogueliteNavigationCheck::NotChecked;
    R.NavigationMessage = EvaluateNavigation ? TEXT("Navigation Unavailable: no ready NavMesh covering both rooms for this capsule.") : TEXT("Navigation not requested.");
    if (EvaluateNavigation)
    {
        auto* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
        auto* Data = Nav ? Nav->GetDefaultNavDataInstance(FNavigationSystem::DontCreate) : nullptr;
        if (Nav && Data)
            R.NavigationMessage = FString::Printf(TEXT("Navigation Unavailable: NavData=%s Agent=%.1fx%.1f Required=%.1fx%.1f BuildOrLocked=%d; both endpoints must also project onto ready navigation."),
                *Data->GetPathName(), Data->GetConfig().AgentRadius, Data->GetConfig().AgentHeight, TestCapsuleRadius, 2 * TestCapsuleHalfHeight,
                UNavigationSystemV1::IsNavigationBeingBuiltOrLocked(GetWorld()));
        if (Nav && Data && !UNavigationSystemV1::IsNavigationBeingBuiltOrLocked(GetWorld())
            && Data->GetConfig().AgentRadius + PositionTolerance >= TestCapsuleRadius && Data->GetConfig().AgentHeight + PositionTolerance >= 2 * TestCapsuleHalfHeight)
        {
            FNavLocation Start, End;
            if (Nav->ProjectPointToNavigation(C.RoomA->GetActorLocation(), Start, FVector(30, 30, 100), Data)
                && Nav->ProjectPointToNavigation(C.RoomB->GetActorLocation(), End, FVector(30, 30, 100), Data))
            {
                FPathFindingQuery Query(this, *Data, Start.Location, End.Location);
                Query.SetAllowPartialPaths(false);
                const auto Path = Nav->FindPathSync(Query);
                R.bNavigationPartial = Path.Path.IsValid() && Path.Path->IsPartial();
                const bool Passed = Path.IsSuccessful() && Path.Path.IsValid() && !R.bNavigationPartial;
                R.Navigation = Passed ? ERogueliteNavigationCheck::Passed : ERogueliteNavigationCheck::Failed;
                if (Path.Path.IsValid()) for (const FNavPathPoint& Point : Path.Path->GetPathPoints()) R.NavigationPathPoints.Add(Point.Location);
                R.NavigationMessage = FString::Printf(TEXT("NavData=%s Agent=%.1fx%.1f PathSuccessful=%d Partial=%d Points=%d"),
                    *Data->GetPathName(), Data->GetConfig().AgentRadius, Data->GetConfig().AgentHeight, Passed, R.bNavigationPartial, R.NavigationPathPoints.Num());
                if (!Passed) { R.Message = R.NavigationMessage; return false; }
            }
        }
    }
    R.bSucceeded = true;
    R.Message = TEXT("StraightCorridor geometry, occupancy and actual Pawn capsule passed. ") + R.NavigationMessage;
    return true;
}

FRogueliteAssemblyResult ARogueliteRoomAssembly::TryRegisterRuntimePlacedConnection(ARogueliteParametricRoom* A,ERogueliteRoomSide SideA,
    ARogueliteParametricCorridor* C,ARogueliteParametricRoom* B,ERogueliteRoomSide SideB,FGuid SourceBatch,FName LoopCandidate)
{return RegisterPlacedConnection(A,SideA,C,B,SideB,SourceBatch,LoopCandidate,false);}

FRogueliteAssemblyResult ARogueliteRoomAssembly::TryRegisterConfiguredPlacedConnection(ARogueliteParametricRoom* A,ERogueliteRoomSide SideA,
    ARogueliteParametricCorridor* C,ARogueliteParametricRoom* B,ERogueliteRoomSide SideB,FGuid SourceBatch,FName LoopCandidate)
{return RegisterPlacedConnection(A,SideA,C,B,SideB,SourceBatch,LoopCandidate,true);}

FRogueliteAssemblyResult ARogueliteRoomAssembly::RegisterPlacedConnection(ARogueliteParametricRoom* A,ERogueliteRoomSide SideA,
    ARogueliteParametricCorridor* C,ARogueliteParametricRoom* B,ERogueliteRoomSide SideB,FGuid SourceBatch,FName LoopCandidate,bool bConfigured)
{
    FRogueliteAssemblyResult R;
    const auto Finish=[&](){PublishResult(R);return R;};
    if(!GetWorld()||(!GetWorld()->IsGameWorld()&&!(bConfigured&&GetWorld()->WorldType==EWorldType::Editor))||!SourceBatch.IsValid()||!IsValid(A)||!IsValid(B)||!IsValid(C)
        ||A->GetWorld()!=GetWorld()||B->GetWorld()!=GetWorld()||C->GetWorld()!=GetWorld())
    {R.Message=TEXT("Runtime placed registration requires live actors in the same GameWorld; no writes.");return Finish();}
    if(!ComputeCorridorTransforms(A,SideA,C,ERogueliteCorridorEndpoint::Start,B,SideB,R.ProposedCorridorTransform,R.ProposedTargetTransform,R.Message))return Finish();
    if(!C->GetActorTransform().Equals(R.ProposedCorridorTransform,.0001)||!B->GetActorTransform().Equals(R.ProposedTargetTransform,.0001))
    {R.Message=TEXT("Runtime final geometry is not already aligned; moving registered actors is forbidden.");return Finish();}
    if(IsOccupied(A,SideA,FGuid())||IsOccupied(B,SideB,FGuid())||IsCorridorOccupied(C,FGuid()))
    {R.Message=TEXT("Runtime socket/corridor already reserved; duplicate publication refused.");return Finish();}
    if(!LoopCandidate.IsNone()&&(!SourceBatch.IsValid()||C->GetOwner()!=this||!C->ActorHasTag(TEXT("RogueliteFixedLoopOwned"))))
    {R.Message=TEXT("Runtime loop ownership/source batch missing; no reservation.");return Finish();}
    FRogueliteRoomConnection Record;Record.RoomA=A;Record.SocketA=SideA;Record.RoomB=B;Record.SocketB=SideB;
    Record.Corridor=C;Record.EndpointA=ERogueliteCorridorEndpoint::Start;Record.EndpointB=ERogueliteCorridorEndpoint::End;Record.Mode=ERogueliteConnectionMode::StraightCorridor;
    // The exact existing geometry/occupancy/59-Probe/capsule validator is reused, without a navigation claim.
    if(!ValidateCorridorConnection(Record,R,true))return Finish();
    if(bConfigured?!HasCompleteCorridorEvidence(A,SideA,C,B,SideB,R):(R.Probes.Num()!=59||!R.bGeometryPassed||!R.bCapsulePassed))
    {R.bSucceeded=false;R.Message=TEXT("Original C2 complete geometry/capsule evidence required.");return Finish();}
    Record.Id=FGuid::NewGuid();Record.bOwnedFixedLoop=!LoopCandidate.IsNone();Record.LoopSourceBatchId=SourceBatch;Record.LoopCandidateId=LoopCandidate;
    RecordValidation(Record,R);Connections.Add(Record);R.ConnectionId=Record.Id;
    R.Message=TEXT("Runtime final placed geometry registered by original C2; no Transform/collision/mobility changed. NavigationPending.");return Finish();
}

bool ARogueliteRoomAssembly::ReleaseRuntimePlacedConnection(FGuid Id,FGuid SourceBatch)
{
    if(!GetWorld()||!GetWorld()->IsGameWorld()||!SourceBatch.IsValid())return false;
    const int32 Index=Connections.IndexOfByPredicate([&](const auto& R){return R.Id==Id;});
    if(Index==INDEX_NONE)return true;
    if(Connections[Index].LoopSourceBatchId!=SourceBatch)return false;
    Connections.RemoveAt(Index);return true; // No Static rebuild; whole owned batch is being discarded.
}

bool ARogueliteRoomAssembly::HasCompleteCorridorEvidence(ARogueliteParametricRoom* A,ERogueliteRoomSide SideA,
    ARogueliteParametricCorridor* C,ARogueliteParametricRoom* B,ERogueliteRoomSide SideB,const FRogueliteAssemblyResult& R)
{
    if(!IsValid(A)||!IsValid(B)||!IsValid(C)||!R.bSucceeded||!R.bGeometryPassed||!R.bCapsulePassed)return false;
    const bool Open=A->GetDoorOpeningMode(SideA)==ERogueliteOpeningMode::OpenPassage;
    if(Open!=(B->GetDoorOpeningMode(SideB)==ERogueliteOpeningMode::OpenPassage))return false;
    for(const auto& P:R.Probes)if(!P.bPassed||P.Start.ContainsNaN()||P.End.ContainsNaN())return false;
    const auto Count=[&](const FString& Name,bool Prefix=false)
    {int32 N=0;for(const auto& P:R.Probes)N+=Prefix?P.Name.StartsWith(Name):P.Name==Name;return N;};
    bool Good=true;int32 Closed=0;
    ARogueliteParametricRoom* Rooms[]={A,B};const ERogueliteRoomSide Sides[]={SideA,SideB};
    for(int32 Pair=0;Pair<2;++Pair)
    {
        const FString L=Pair?TEXT("C-B"):TEXT("A-C");FRogueliteDoorSocket Door;
        if(!Rooms[Pair]->TryGetDoorSocket(Sides[Pair],Door))return false;
        Good&=Count(L+TEXT(" opening "),true)==9;
        for(int32 Which=0;Which<2;++Which)
        {
            Good&=Count(L+FString::Printf(TEXT(" entity%d side"),Which),true)==2;
            Good&=Count(L+FString::Printf(TEXT(" entity%d floor"),Which))==1;
            if(Open&&Which==0)
            {Good&=Count(L+TEXT(" OpenPassage upper aperture clear"))==1;Good&=Count(L+TEXT(" OpenPassage upper wall boundary "),true)==2;}
            else
            {
                const bool Absent=Which==1&&!C->bGenerateCeiling&&C->CorridorHeight<=Door.DoorHeight;
                const FString Name=Absent?TEXT(" open roof edge clear"):Open?TEXT(" corridor roof edge"):FString::Printf(TEXT(" entity%d lintel"),Which);
                Good&=Count(L+Name)==1;
            }
        }
        Good&=Count(L+TEXT(" PawnCapsule forward "),true)==3&&Count(L+TEXT(" PawnCapsule ground "),true)==3&&Count(L+TEXT(" PawnCapsule reverse"))==1;
        if(Open)
        {
            Good&=Count(L+TEXT(" OpenPassage upper capsule clear "),true)==3;
            for(int32 I=0;I<4;++I)if(static_cast<ERogueliteRoomSide>(I)!=Sides[Pair])
            {
                FRogueliteDoorSocket Other;if(Rooms[Pair]->TryGetDoorSocket(static_cast<ERogueliteRoomSide>(I),Other))continue;
                ++Closed;Good&=Count(L+FString::Printf(TEXT(" OpenPassage closed perimeter PawnCapsule side%d"),I))==1;
            }
        }
    }
    Good&=Count(TEXT("A-C-B PawnCapsule entire "),true)==3&&Count(TEXT("A-C-B PawnCapsule reverse entire"))==1;
    Good&=Count(TEXT("Corridor internal PawnCapsule "),true)==3&&Count(TEXT("Corridor floor"))==1&&Count(TEXT("Corridor side"))==2;
    Good&=Count(C->bGenerateCeiling?TEXT("Corridor ceiling"):TEXT("Corridor open top center"))==1;
    Good&=Count(TEXT("Corridor open top PawnCapsule "),true)==(C->bGenerateCeiling?0:9);
    const int32 Expected=59+(Open?10+Closed:0)+(C->bGenerateCeiling?0:9);
    return Good&&R.Probes.Num()==Expected;
}

FRogueliteAssemblyResult ARogueliteRoomAssembly::RevalidateConfiguredConnection(FGuid Id)
{
    FRogueliteAssemblyResult R;R.ConnectionId=Id;
    auto* C=Connections.FindByPredicate([&](const auto& X){return X.Id==Id&&X.bActive&&X.Mode==ERogueliteConnectionMode::StraightCorridor;});
    if(!C){R.Message=TEXT("Configured ConnectionId absent/inactive; no geometry or lease changed.");PublishResult(R);return R;}
    const bool Passed=ValidateCorridorConnection(*C,R,true)&&HasCompleteCorridorEvidence(C->RoomA,C->SocketA,C->Corridor,C->RoomB,C->SocketB,R);
    if(!Passed){R.bSucceeded=false;R.Message=TEXT("Configured physical revalidation failed: ")+R.Message;}
    RecordValidation(*C,R);PublishResult(R);return R; // Intentionally no Rebuild, Transform, mobility or parameter writes.
}
