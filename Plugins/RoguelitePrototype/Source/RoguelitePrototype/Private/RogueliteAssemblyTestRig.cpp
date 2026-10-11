#include "RogueliteAssemblyTestRig.h"
#include "RoguelitePrototype.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "NavigationSystem.h"
#include "NavigationData.h"
#include "NavMesh/RecastNavMesh.h"
#include "NavMesh/NavMeshBoundsVolume.h"
#include "EngineUtils.h"

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
#if WITH_EDITOR
    if (!IsC2World() || GetWorld()->IsGameWorld())
    { NavigationBuildEvidence.Add(TEXT("Refused: only the Editor P03-C2 test map is authorized.")); return; }
    auto* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
    if (!Nav) { NavigationBuildEvidence.Add(TEXT("Navigation Unavailable: world has no NavigationSystem.")); return; }
    int32 BoundsCount = 0;
    for (TActorIterator<ANavMeshBoundsVolume> It(GetWorld()); It; ++It)
        if (It->ActorHasTag(TEXT("P03C2_LocalNav"))) { Nav->OnNavigationBoundsUpdated(*It); ++BoundsCount; }
    if (BoundsCount != 1) { NavigationBuildEvidence.Add(TEXT("Refused: expected one tagged local nav bounds.")); return; }
    auto* Data = Nav->GetDefaultNavDataInstance(FNavigationSystem::Create);
    if (auto* Mesh = Cast<ARecastNavMesh>(Data))
    {
        // Actor-local test settings only. Never SaveConfig or alter a class default/global agent list.
        Mesh->Modify();
        FNavDataConfig Config = Mesh->GetConfig();
        Config.AgentRadius = 35; Config.AgentHeight = 180;
        Mesh->SetConfig(Config);
        Mesh->AgentRadius = 35; Mesh->AgentHeight = 180;
        Mesh->PostEditChange();
        NavigationBuildEvidence.Add(FString::Printf(TEXT("Local Recast=%s RequestedAgentRadius=35 Height=180; no global configuration changed."), *Mesh->GetPathName()));
    }
    else NavigationBuildEvidence.Add(TEXT("Navigation Unavailable: default data is not Recast."));
    Nav->Build();
    NavigationBuildEvidence.Add(FString::Printf(TEXT("Build called; async state=%d. Revalidate after completion; this is not a path success claim."),
        UNavigationSystemV1::IsNavigationBeingBuiltOrLocked(GetWorld())));
    for (const FString& Line : NavigationBuildEvidence) UE_LOG(LogRoguelitePrototype, Display, TEXT("P03C2_NAV_BUILD %s"), *Line);
#else
    NavigationBuildEvidence.Add(TEXT("Local nav authoring requires Editor."));
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
