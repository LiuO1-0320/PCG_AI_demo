#include "CastleMemoryArtChecks.h"

#include "CastleDemo.h"
#include "CastleExplorationDimensions.h"
#include "Components/CapsuleComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"

bool CastleCheckMemoryRoomArt(ACastleDirector* Director, ACastlePlayer* Player, FString& Error)
{
    Error.Reset();
    const auto Fail = [&Error](const FString& Reason)
    {
        Error = Reason;
        return false;
    };

    if (!IsInGameThread() || !IsValid(Director) || !IsValid(Player) || !Director->bReady || Director->Floor != 1 ||
        !Director->GetWorld() || Director->GetWorld() != Player->GetWorld() || !Director->GetWorld()->IsGameWorld())
        return Fail(TEXT("Memory room art check requires the generated first floor and its player in a game world."));

    UWorld* World = Director->GetWorld();
    const UCapsuleComponent* Capsule = Player->GetCapsuleComponent();
    if (!Capsule || !Player->GetActorEnableCollision())
        return Fail(TEXT("Memory room art check requires the active player capsule."));

    const FCastleRoom* Room = nullptr;
    for (const FCastleRoom& Candidate : Director->Rooms)
        if (Candidate.bActive && Candidate.Type == ECastleRoom::Chest)
        {
            if (Room) return Fail(TEXT("Expected exactly one first-floor memory treasure room."));
            Room = &Candidate;
        }
    if (!Room || !Room->Doors) return Fail(TEXT("No connected first-floor memory treasure room."));

    const FName ArtTag(TEXT("CastleMemoryRoomArt"));
    const FName VisualTag(TEXT("CastleMemoryVisual"));
    const FName ShellTag(TEXT("CastleMemoryCollision"));
    const FName BlockAll(TEXT("BlockAll"));
    int32 ArtCount = 0, VisualCount = 0, ShellCount = 0;
    for (TActorIterator<AActor> It(World); It; ++It)
    {
        const bool IsRoomArt = It->ActorHasTag(ArtTag);
        if (IsRoomArt) ++ArtCount;
        TInlineComponentArray<UPrimitiveComponent*> Primitives(*It);
        for (const UPrimitiveComponent* Component : Primitives)
        {
            if (!IsValid(Component) || !Component->IsRegistered()) continue;
            if (Component->ComponentHasTag(ShellTag))
            {
                ++ShellCount;
                if ((Component->IsVisible() && !Component->bHiddenInGame && !It->IsHidden()) ||
                    Component->GetCollisionProfileName() != BlockAll || Component->GetCollisionEnabled() == ECollisionEnabled::NoCollision ||
                    Component->GetCollisionResponseToChannel(ECC_Visibility) != ECR_Block || !Component->CanEverAffectNavigation())
                    return Fail(FString::Printf(TEXT("Original memory room shell is not hidden BlockAll with navigation: %s"), *Component->GetPathName()));
            }
            else if (IsRoomArt || Component->ComponentHasTag(VisualTag))
            {
                ++VisualCount;
                if (Component->GetCollisionEnabled() != ECollisionEnabled::NoCollision || Component->CanEverAffectNavigation())
                    return Fail(FString::Printf(TEXT("Memory room visual affects collision or navigation: %s"), *Component->GetPathName()));
            }
        }
    }
    int32 ExpectedDoors=0;for(int32 Side=0;Side<4;++Side)if(Room->Doors&(1<<Side))++ExpectedDoors;
    if (ArtCount!=2 || !VisualCount || ShellCount!=9+2*ExpectedDoors)
        return Fail(FString::Printf(TEXT("Memory room replacement is missing (art actors %d, visual components %d, hidden collision components %d)."), ArtCount, VisualCount, ShellCount));

    // Check the hidden collision independently of the fitted art meshes.
    constexpr float Half = CastleExploration::RoomHalf;
    constexpr float DoorHalf = 240.f;
    constexpr float JambWidth = Half - DoorHalf;
    TArray<TPair<FVector,FVector>> Expected;
    Expected.Emplace(Room->Center+FVector(0,0,-35),FVector(2*Half+100,2*Half+100,70));
    const FVector Normals[]={FVector(1,0,0),FVector(0,1,0),FVector(-1,0,0),FVector(0,-1,0)};
    for(int32 Side=0;Side<4;++Side)
    {
        const bool AlongX=Side%2==0;const FVector N=Normals[Side];
        const FVector T=AlongX?FVector(0,1,0):FVector(1,0,0);
        if(Room->Doors&(1<<Side))
        {
            for(int32 Sign:{-1,1})Expected.Emplace(Room->Center+N*Half+T*(Sign*(DoorHalf+JambWidth*.5f))+FVector(0,0,240),AlongX?FVector(85,JambWidth,480):FVector(JambWidth,85,480));
            Expected.Emplace(Room->Center+N*Half+FVector(0,0,445),AlongX?FVector(90,480,70):FVector(480,90,70));
        }
        else Expected.Emplace(Room->Center+N*Half+FVector(0,0,240),AlongX?FVector(85,2*Half,480):FVector(2*Half,85,480));
    }
    for(int32 X:{-1,1})for(int32 Y:{-1,1})Expected.Emplace(Room->Center+FVector(X*(Half-65),Y*(Half-65),260),FVector(135,135,520));
    for(TActorIterator<AActor> It(World);It;++It)
    {
        TInlineComponentArray<UPrimitiveComponent*> Components(*It);
        for(auto* Component:Components)if(Component->ComponentHasTag(ShellTag))
        {
            const int32 Match=Expected.IndexOfByPredicate([&](const auto& Box){return Component->GetComponentLocation().Equals(Box.Key,.1) && (Component->GetComponentScale()*100).Equals(Box.Value,.1) && Component->GetComponentRotation().IsNearlyZero(.1);});
            if(Match==INDEX_NONE)return Fail(TEXT("Original collision box position, size or rotation changed"));
            Expected.RemoveAt(Match);
        }
    }
    if(!Expected.IsEmpty())return Fail(TEXT("Original collision shell is incomplete"));

    const FVector ChestPosition = Room->Center + FVector(230, 0, 45);
    bool FoundChest = false;
    for (TActorIterator<ACastleInteractable> It(World); It; ++It)
        if (It->Kind == ECastleInteract::Chest && FVector::DistSquared(It->GetActorLocation(), ChestPosition) < 1.0)
        { FoundChest = true; break; }
    if (!FoundChest) return Fail(TEXT("The memory chest is missing from its original interaction position."));

    FCollisionQueryParams Query(SCENE_QUERY_STAT(CastleMemoryArtCollision), false, Player);
    const FCollisionResponseParams PlayerResponse(Capsule->GetCollisionResponseToChannels());
    const auto PlayerShape = FCollisionShape::MakeCapsule(Capsule->GetScaledCapsuleRadius(), Capsule->GetScaledCapsuleHalfHeight());
    const FVector PlayerHeight(0, 0, Capsule->GetScaledCapsuleHalfHeight() + 3);
    const FVector Directions[] = {FVector(1,0,0), FVector(0,1,0), FVector(-1,0,0), FVector(0,-1,0)};
    FVector BoltStart, BoltDirection;
    int32 DoorCount = 0, WallCount = 0;
    for (int32 Side = 0; Side < UE_ARRAY_COUNT(Directions); ++Side)
    {
        const FVector Normal = Directions[Side];
        const FVector Tangent(-Normal.Y, Normal.X, 0);
        const bool Open = (Room->Doors & (1 << Side)) != 0;
        if (Open)
        {
            ++DoorCount;
            // Sweep with the real player's capsule and movement responses, from inside the room
            // into the adjoining corridor. Near-jamb probes cover both sides of the original opening.
            const float Edge = FMath::Max(0.f, 240.f - Capsule->GetScaledCapsuleRadius() - 8.f);
            for (const float Offset : {0.f, -Edge, Edge})
            {
                FHitResult Hit;
                const FVector Lane = Room->Center + PlayerHeight + Tangent * Offset;
                if (World->SweepSingleByChannel(Hit, Lane + Normal * (Half - 220), Lane + Normal * (CastleExploration::CellSpacing * .5f),
                    FQuat::Identity, Capsule->GetCollisionObjectType(), PlayerShape, Query, PlayerResponse))
                    return Fail(FString::Printf(TEXT("Memory room door %d blocks the player capsule at offset %.0f: %s / %s"),
                        Side, Offset, *GetNameSafe(Hit.GetActor()), *GetNameSafe(Hit.GetComponent())));
            }
        }

        // Use the enemy bolt's Visibility channel, sphere radius and straight swept movement.
        // For an open side check both original jambs; for a closed side check the center.
        const auto CheckWall = [&](float Offset)
        {
            const FVector Lane = Room->Center + FVector(0, 0, 180) + Tangent * Offset;
            const FVector Start = Lane + Normal * (Half - 150);
            FHitResult Hit;
            const auto Blocked = World->SweepSingleByChannel(Hit, Start, Lane + Normal * (Half + 100),
                FQuat::Identity, ECC_Visibility, FCollisionShape::MakeSphere(8.f), Query);
            const auto* Component = Hit.GetComponent();
            if (!Blocked || !Component || !Component->ComponentHasTag(ShellTag) || Component->GetCollisionProfileName() != BlockAll)
                return Fail(FString::Printf(TEXT("Memory room wall %d / %.0f cm did not stop the bolt sweep on the original shell: %s"),
                    Side, Offset, *GetNameSafe(Component)));
            ++WallCount;
            if (WallCount == 1) { BoltStart = Start; BoltDirection = Normal; }
            return true;
        };
        if (Open ? (!CheckWall(-440.f) || !CheckWall(440.f)) : !CheckWall(0.f)) return false;
    }

    FHitResult FloorHit;
    if (!World->LineTraceSingleByChannel(FloorHit, Room->Center + FVector(0,0,200), Room->Center - FVector(0,0,100),
        ECC_Visibility, Query) || !FloorHit.GetComponent() || !FloorHit.GetComponent()->ComponentHasTag(ShellTag) ||
        !FMath::IsNearlyEqual(FloorHit.ImpactPoint.Z, Room->Center.Z, 1.0))
        return Fail(TEXT("The original floor collision is missing or its walking surface changed."));

    FActorSpawnParameters Spawn;
    Spawn.Owner = Director;
    Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    auto* Bolt = World->SpawnActor<ACastleBolt>(BoltStart, BoltDirection.Rotation(), Spawn);
    if (!Bolt) return Fail(TEXT("Could not spawn a bolt for the memory room wall check."));
    Bolt->Shooter = Player; // The test shot ignores the player, as the preceding sweep did.
    Bolt->Velocity = BoltDirection * 1000.f;
    Bolt->SetActorTickEnabled(false);
    const TWeakObjectPtr<ACastleBolt> Probe(Bolt);
    Bolt->Tick(.28f);
    if (Probe.IsValid() && !Probe->IsActorBeingDestroyed())
    {
        Probe->Destroy();
        return Fail(TEXT("A real Castle bolt passed through the original memory room wall."));
    }

    UE_LOG(LogTemp, Display, TEXT("CASTLE_MEMORY_ART_COLLISION_OK Seed=%d Doors=%d DoorMask=%d WallHits=%d WallBlocksBolt=1 FloorZ=%.1f Art=%d Visuals=%d Shell=%d"),
        Director->RunSeed, DoorCount, Room->Doors, WallCount, FloorHit.ImpactPoint.Z, ArtCount, VisualCount, ShellCount);
    return true;
}
