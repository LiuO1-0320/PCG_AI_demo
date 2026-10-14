#include "RogueliteAssemblyTestRig.h"
#include "RoguelitePrototype.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "NavigationSystem.h"
#include "NavigationData.h"
#include "NavMesh/RecastNavMesh.h"
#include "NavMesh/NavMeshBoundsVolume.h"
#include "EngineUtils.h"
#include "UObject/UnrealType.h"

ARogueliteAssemblyTestRig::ARogueliteAssemblyTestRig()
{
    PrimaryActorTick.bCanEverTick = false;
    SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("TestRoot")));
}
bool ARogueliteAssemblyTestRig::IsC2World() const
{
    return GetWorld() && GetWorld()->GetOutermost()->GetName().EndsWith(TEXT("L_Roguelite_CorridorAssembly_Dev"));
}
void ARogueliteAssemblyTestRig::BuildLocalNavigation()
{
    NavigationBuildEvidence.Reset();
    const auto Evidence = [this](const FString& Message, bool bError = false)
    {
        NavigationBuildEvidence.Add(Message);
        if (bError)
        { UE_LOG(LogRoguelitePrototype, Warning, TEXT("P03C2_NAV_BUILD %s"), *Message); }
        else
        { UE_LOG(LogRoguelitePrototype, Display, TEXT("P03C2_NAV_BUILD %s"), *Message); }
    };
#if WITH_EDITOR
    if (!IsC2World() || GetWorld()->IsGameWorld())
    { Evidence(TEXT("Refused: only the Editor P03-C2 test map is authorized."), true); return; }
    auto* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
    if (!Nav || Nav->IsTemplate() || Nav->GetWorld() != GetWorld())
    { Evidence(TEXT("Navigation Unavailable: no owned world NavigationSystem instance."), true); return; }
    int32 BoundsCount = 0;
    for (TActorIterator<ANavMeshBoundsVolume> It(GetWorld()); It; ++It)
        if (It->ActorHasTag(TEXT("P03C2_LocalNav"))) { Nav->OnNavigationBoundsUpdated(*It); ++BoundsCount; }
    if (BoundsCount != 1)
    { Evidence(FString::Printf(TEXT("Refused: expected one P03C2_LocalNav bounds, found %d."), BoundsCount), true); return; }
    if (Nav->IsNavigationBuildingLocked(ENavigationBuildLock::AsyncLoadLock))
    { Evidence(TEXT("Navigation waiting: map assets are still loading/compiling. Wait for loading to finish, then click Build Local Navigation again; no registrations changed."), true); return; }
    if (Nav->IsNavigationBuildingLocked(static_cast<uint8>(~ENavigationBuildLock::NoUpdateInEditor)))
    { Evidence(TEXT("Navigation Unavailable: this world has a navigation build lock. No registrations changed; do not override engine loading/custom locks."), true); return; }

    // UE 5.8 OverrideSupportedAgents only filters the CDO's agents. Changing just
    // Recast's dimensions leaves its registration key inconsistent with the world.
    // Edit the existing editable UPROPERTY on this transient world instance only.
    // Never change a CDO, call SaveConfig, or apply this to another map/PIE world.
    FArrayProperty* AgentProperty = FindFProperty<FArrayProperty>(Nav->GetClass(), TEXT("SupportedAgents"));
    const FStructProperty* AgentStruct = AgentProperty ? CastField<FStructProperty>(AgentProperty->Inner) : nullptr;
    if (!AgentStruct || AgentStruct->Struct != FNavDataConfig::StaticStruct() || Nav->GetSupportedAgents().Num() != 1)
    { Evidence(TEXT("Refused: this isolated test supports exactly one reflected FNavDataConfig agent."), true); return; }
    TArray<FNavDataConfig>* WorldAgents = AgentProperty->ContainerPtrToValuePtr<TArray<FNavDataConfig>>(Nav);
    const TArray<FNavDataConfig> BeforeAgents = *WorldAgents;
    auto* Mesh = Cast<ARecastNavMesh>(Nav->GetDefaultNavDataInstance(FNavigationSystem::Create));
    if (!IsValid(Mesh) || Mesh->IsActorBeingDestroyed() || Mesh->GetWorld() != GetWorld())
    { Evidence(TEXT("Navigation Unavailable: no valid Recast for this test world."), true); return; }
    for (ANavigationData* Registered : Nav->NavDataSet)
    {
        if (IsValid(Registered) && !Registered->IsA<ARecastNavMesh>())
        { Evidence(TEXT("Refused: registered navigation includes another implementation."), true); return; }
    }

    TSet<ANavigationData*> OldRecast;
    for (ANavigationData* Registered : Nav->NavDataSet) if (IsValid(Registered)) OldRecast.Add(Registered);
    for (ANavigationData* Pending : Nav->NavDataRegistrationQueue)
        if (IsValid(Pending) && Pending->IsA<ARecastNavMesh>()) OldRecast.Add(Pending);
    OldRecast.Add(Mesh);
    for (ANavigationData* Old : OldRecast)
    {
        // Repair a stale old key from the earlier Mesh-only setter before removing
        // it. One agent is required above, so the previous world key is unambiguous.
        Old->SetConfig(BeforeAgents[0]);
        Nav->UnregisterNavData(Old);
    }

    FNavDataConfig LocalAgent = BeforeAgents[0];
    LocalAgent.AgentRadius = 35; LocalAgent.AgentHeight = 180;
    LocalAgent.SetNavDataClass(Mesh->GetClass());
    (*WorldAgents)[0] = LocalAgent;
    Mesh->Modify();
    Mesh->SetConfig(LocalAgent);
    Mesh->AgentRadius = LocalAgent.AgentRadius; Mesh->AgentHeight = LocalAgent.AgentHeight;
    // Register after both sides agree. Do not PostEditChange here: it schedules
    // another registration while the old key is still being changed.
    // The synchronous RegisterNavData API is protected in UE 5.8. Queue the
    // candidate via the public API; Build processes it before rebuilding tiles.
    Nav->RequestRegistrationDeferred(*Mesh);
    Nav->Build();
    if (!Mesh->IsRegistered() || !Nav->NavDataSet.Contains(Mesh))
    {
        Nav->UnregisterNavData(Mesh);
        *WorldAgents = BeforeAgents;
        Mesh->SetConfig(BeforeAgents[0]);
        Mesh->AgentRadius = BeforeAgents[0].AgentRadius; Mesh->AgentHeight = BeforeAgents[0].AgentHeight;
        Nav->RequestRegistrationDeferred(*Mesh);
        Nav->Build();
        Evidence(FString::Printf(TEXT("Navigation Unavailable: Recast=%s was not registered by Build; previous world agent restored, Registered=%d."),
            *Mesh->GetPathName(), Mesh->IsRegistered()), true);
        return;
    }
    Evidence(FString::Printf(TEXT("World-only agent %.1fx%.1f -> 35x180; Recast=%s Registered=%d. No CDO/global config changed."),
        BeforeAgents[0].AgentRadius, BeforeAgents[0].AgentHeight, *Mesh->GetPathName(), Mesh->IsRegistered()));
    Evidence(FString::Printf(TEXT("Build completed; busy=%d, registered data=%d. Click Revalidate Connections to prove a complete path; build alone is not Navigation Passed."),
        UNavigationSystemV1::IsNavigationBeingBuiltOrLocked(GetWorld()), Nav->NavDataSet.Num()));
#else
    Evidence(TEXT("Local nav authoring requires Editor."), true);
#endif
}

void ARogueliteAssemblyTestRig::BeginPlay()
{
    Super::BeginPlay();
    if (!bRunRuntimeProbe || !IsC2World()) return;
    bRuntimeProbeRan = true;
    UWorld* World = GetWorld();
    auto* Registry = World->SpawnActor<ARogueliteRoomAssembly>();
    auto* A = World->SpawnActorDeferred<ARogueliteParametricRoom>(ARogueliteParametricRoom::StaticClass(),
        FTransform(FVector(0, 30000, 0)), this, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
    auto* B = World->SpawnActorDeferred<ARogueliteParametricRoom>(ARogueliteParametricRoom::StaticClass(),
        FTransform(FVector(9000, 30000, 0)), this, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
    auto* C = World->SpawnActorDeferred<ARogueliteParametricCorridor>(ARogueliteParametricCorridor::StaticClass(),
        FTransform(FVector(9000, 34000, 0)), this, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
    if (!Registry || !A || !B || !C)
    { RuntimeEvidence.Add(TEXT("Deferred actor spawn failed.")); return; }
    Registry->bCheckNavigation = false;
    A->RoomHeight = 400; A->NorthDoor.bEnabled = true; A->NorthDoor.bAutoCenter = false; A->NorthDoor.ManualOffset = 125;
    B->RoomLength = 3000; B->RoomWidth = 2500; B->RoomHeight = 500; B->WallThickness = 35;
    B->SouthDoor.bEnabled = true; B->SouthDoor.bAutoCenter = false; B->SouthDoor.ManualOffset = -200;
    C->CorridorLength = 2000; C->CorridorWidth = 500; C->CorridorHeight = 350; C->WallThickness = 25; C->FloorThickness = 30;
    FTransform CT, BT; FString Reason;
    const bool Computed = Registry->ComputeCorridorTransforms(A, ERogueliteRoomSide::North, C, ERogueliteCorridorEndpoint::Start,
        B, ERogueliteRoomSide::South, CT, BT, Reason);
    A->FinishSpawning(A->GetActorTransform());
    C->FinishSpawning(Computed ? CT : C->GetActorTransform());
    B->FinishSpawning(Computed ? BT : B->GetActorTransform());
    const FTransform BeforeA = A->GetActorTransform();
    const auto Result = Registry->TryAlignCorridorConnection(A, ERogueliteRoomSide::North, C, ERogueliteCorridorEndpoint::Start,
        B, ERogueliteRoomSide::South);
    RuntimeEvidence.Add(FString::Printf(TEXT("Computed=%d RoomAStatic=%d CorridorStatic=%d RoomBStatic=%d Begun=%d/%d/%d"),
        Computed, A->GetRootComponent()->Mobility == EComponentMobility::Static, C->GetRootComponent()->Mobility == EComponentMobility::Static,
        B->GetRootComponent()->Mobility == EComponentMobility::Static, A->HasActorBegunPlay(), C->HasActorBegunPlay(), B->HasActorBegunPlay()));
    RuntimeEvidence.Add(FString::Printf(TEXT("Runtime native API success=%d Geometry=%d Capsule=%d Probes=%d Connections=%d AUnchanged=%d"),
        Result.bSucceeded, Result.bGeometryPassed, Result.bCapsulePassed, Result.Probes.Num(), Registry->Connections.Num(), A->GetActorTransform().Equals(BeforeA)));
    for (const auto& P : Result.Probes)
        if (P.Name.Contains(TEXT("PawnCapsule"))) RuntimeEvidence.Add(FString::Printf(TEXT("%s Passed=%d Blocking=%d Distance=%.3f Hit=%s"), *P.Name, P.bPassed, P.bBlockingHit, P.HitDistance, *P.HitObject));
    const FTransform BeforeC = C->GetActorTransform(), BeforeB = B->GetActorTransform();
    const auto Rejected = Registry->TryAlignCorridorConnection(A, ERogueliteRoomSide::North, C, ERogueliteCorridorEndpoint::End,
        B, ERogueliteRoomSide::South);
    const bool SafeRejection = !Rejected.bSucceeded && Rejected.Message.Contains(TEXT("Static runtime"))
        && C->GetActorTransform().Equals(BeforeC) && B->GetActorTransform().Equals(BeforeB) && Registry->Connections.Num() == 1;
    RuntimeEvidence.Add(FString::Printf(TEXT("PostBeginPlayStaticMoveRejected=%d Reason=%s"), SafeRejection, *Rejected.Message));
    const auto Rechecked = Registry->RevalidateConnection(Result.ConnectionId);
    const auto NullTarget = Registry->TryAlignCorridorConnection(A, ERogueliteRoomSide::North, C, ERogueliteCorridorEndpoint::Start, nullptr, ERogueliteRoomSide::South);
    const auto MissingEnd = Registry->TryAlignCorridorConnection(A, ERogueliteRoomSide::North, C, static_cast<ERogueliteCorridorEndpoint>(255), B, ERogueliteRoomSide::South);
    const auto MissingSide = Registry->TryAlignCorridorConnection(A, static_cast<ERogueliteRoomSide>(255), C, ERogueliteCorridorEndpoint::Start, B, ERogueliteRoomSide::South);
    const auto MissingId = Registry->RevalidateConnection(FGuid::NewGuid());
    const bool InvalidInputsRejected = !NullTarget.bSucceeded && !MissingEnd.bSucceeded && !MissingSide.bSucceeded && !MissingId.bSucceeded
        && !NullTarget.Message.IsEmpty() && !MissingEnd.Message.IsEmpty() && !MissingSide.Message.IsEmpty() && !MissingId.Message.IsEmpty()
        && C->GetActorTransform().Equals(BeforeC) && B->GetActorTransform().Equals(BeforeB) && Registry->Connections.Num() == 1;
    RuntimeEvidence.Add(FString::Printf(TEXT("InvalidNativeInputsRejected=%d NullTarget=%s UnknownEndpoint=%s UnknownSide=%s UnknownId=%s"),
        InvalidInputsRejected, *NullTarget.Message, *MissingEnd.Message, *MissingSide.Message, *MissingId.Message));
    // Restore the actual record's fresh diagnostic after these intentional API failures.
    Registry->RevalidateConnection(Result.ConnectionId);
    bRuntimeProbePassed = Computed && Result.bSucceeded && Result.bCapsulePassed && SafeRejection && Rechecked.bSucceeded && InvalidInputsRejected;
    RuntimeEvidence.Add(TEXT("Runtime navigation not tested; probe deliberately outside local Editor nav area."));
    for (const auto& Line : RuntimeEvidence) UE_LOG(LogRoguelitePrototype, Display, TEXT("P03C2_RUNTIME_PROBE %s"), *Line);
    UE_LOG(LogRoguelitePrototype, Display, TEXT("P03C2_RUNTIME_RESULT Passed=%d"), bRuntimeProbePassed);
}
