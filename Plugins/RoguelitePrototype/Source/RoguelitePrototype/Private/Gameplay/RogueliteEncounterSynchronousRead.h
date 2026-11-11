#pragma once

#include "CoreMinimal.h"
#include "Gameplay/RogueliteCombatSliceActors.h"
#include "Gameplay/RogueliteCombatSliceEncounter.h"
#include "Gameplay/RogueliteCombatSlicePlayer.h"
#include "RogueliteRuntimeLoopAssembly.h"
#include "RogueliteRuntimeNavigationSystem.h"
#include "Tests/RogueliteCombatSliceRig.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "UObject/ObjectKey.h"
#include "UObject/TopLevelAssetPath.h"

// This value belongs to one Encounter Tick stack. The caller separately proves
// its private roster is native/Dormant and invalidates it before every action,
// callback or reentry. It never replaces an independent strong query.
struct FRogueliteEncounterSynchronousRead final
{
    explicit FRogueliteEncounterSynchronousRead(const URogueliteSliceEncounter* InOwner)
        : Owner(InOwner), OwnerKey(InOwner)
    {
    }

    ~FRogueliteEncounterSynchronousRead() { Close(); }
    FRogueliteEncounterSynchronousRead(const FRogueliteEncounterSynchronousRead&) = delete;
    FRogueliteEncounterSynchronousRead(FRogueliteEncounterSynchronousRead&&) = delete;
    FRogueliteEncounterSynchronousRead& operator=(const FRogueliteEncounterSynchronousRead&) = delete;
    FRogueliteEncounterSynchronousRead& operator=(FRogueliteEncounterSynchronousRead&&) = delete;

    static bool Admit(const URogueliteSliceEncounter* Encounter,
        const ARogueliteRuntimeLoopAssembly* Source, const ARogueliteSlicePlayer* Player)
    {
        if (!AdmitRelatedObjects(Encounter, Source, Player)) return false;
        return AdmitWorld(Source, Source->AActor::GetWorld());
    }

    // All failure checks precede MoveTemp, so a refused handoff preserves the
    // original Snapshot and the caller can continue its existing failure path.
    bool Produce(const ARogueliteRuntimeLoopAssembly* Source, const ARogueliteSlicePlayer* Player,
        FRogueliteLoopTraversalSnapshot&& Snapshot)
    {
        if (bClosed || bInvalidated || bProduced || bConsumed
            || !Admit(Owner, Source, Player) || OwnerKey != FObjectKey(Owner)
            || !Snapshot.bCurrent || !Snapshot.bGeometryValidated || !Snapshot.bNavigationReady
            || Snapshot.NavigationState != TEXT("NavigationReady")
            || Snapshot.BatchId != Source->Batch.BatchId || Snapshot.Seed != Source->Batch.Seed
            || Snapshot.GraphRevision != Source->CommittedLoops.GraphRevision
            || Snapshot.GeometryRevision != Source->CommittedLoops.GeometryRevision)
        {
            Invalidate();
            return false;
        }

        CaptureKey(Source, Player);
        StableDigest = Snapshot.StableDigest;
        StoredTraversal.Emplace(MoveTemp(Snapshot));
        bProduced = true;
        return true;
    }

    // An attempted consumption spends the request even when its identity was
    // invalidated. Out is untouched unless every check succeeds.
    bool Consume(const ARogueliteRuntimeLoopAssembly* Source, const ARogueliteSlicePlayer* Player,
        FRogueliteLoopTraversalSnapshot& Out)
    {
        if (bConsumed) return false;
        bConsumed = true;
        if (bClosed || bInvalidated || !bProduced || !StoredTraversal.IsSet()
            || !Admit(Owner, Source, Player) || !MatchesKey(Source, Player))
        {
            Invalidate();
            return false;
        }

        const auto& Snapshot = StoredTraversal.GetValue();
        if (!Snapshot.bCurrent || !Snapshot.bGeometryValidated || !Snapshot.bNavigationReady
            || Snapshot.NavigationState != TEXT("NavigationReady") || Snapshot.BatchId != BatchId
            || Snapshot.Seed != BatchSeed || Snapshot.GraphRevision != GraphRevision
            || Snapshot.GeometryRevision != GeometryRevision || Snapshot.StableDigest != StableDigest)
        {
            Invalidate();
            return false;
        }
        Out = MoveTemp(StoredTraversal.GetValue());
        StoredTraversal.Reset();
        return true;
    }

    void Invalidate()
    {
        bInvalidated = true;
        StoredTraversal.Reset();
    }

    void Close()
    {
        bClosed = true;
        StoredTraversal.Reset();
    }

    bool IsProduced() const { return bProduced; }
    bool IsConsumed() const { return bConsumed; }
    bool IsInvalidated() const { return bInvalidated; }
    bool IsClosed() const { return bClosed; }

private:
#if WITH_DEV_AUTOMATION_TESTS
    friend struct FRogueliteEncounterSyncReadTestAccess;
#endif

    static bool AdmitRelatedObjects(const URogueliteSliceEncounter* Encounter,
        const ARogueliteRuntimeLoopAssembly* Source, const ARogueliteSlicePlayer* Player)
    {
        if (!IsInGameThread() || !IsValid(Encounter) || Encounter->GetClass() != URogueliteSliceEncounter::StaticClass()
            || !IsValid(Source) || Source->GetClass() != ARogueliteCombatSliceRig::StaticClass()
            || !IsValid(Player) || Player->GetClass() != ARogueliteSlicePlayer::StaticClass()) return false;

        UWorld* World = Source->AActor::GetWorld();
        const auto* Ready = Source->NavigationReadiness.Get();
        const auto* Registry = Source->Batch.Registry.Get();
        if (!IsValid(World) || World->GetClass() != UWorld::StaticClass() || !World->IsGameWorld() || World->bIsTearingDown
            || Encounter->GetOwner() != Source || Encounter->UActorComponent::GetWorld() != World
            || Encounter->GetState() != ERogueliteSliceEncounterState::AwaitingEntry
            || Player->AActor::GetWorld() != World || Player->GetDungeonSource() != Source
            || !Source->Batch.BatchId.IsValid() || Player->GetBoundBatch() != Source->Batch.BatchId
            || !Source->bDungeonReady || !Source->bGeometryValidated
            || !Source->Batch.bSucceeded || !Source->Batch.bGeometryValid || !Source->CommittedLoops.bRuntimePrepared
            || Source->CommittedLoops.SourceBatchId != Source->Batch.BatchId
            || !IsValid(Registry) || Registry->GetClass() != ARogueliteRoomAssembly::StaticClass()
            || Registry->GetOwner() != Source || Registry->AActor::GetWorld() != World
            || !IsValid(Ready) || Ready->GetClass() != URogueliteNavigationReadinessComponent::StaticClass()
            || Ready->GetOwner() != Source || Ready->UActorComponent::GetWorld() != World
            || !Ready->Report.bReady || Ready->Report.State != TEXT("NavigationReady")
            || Ready->Report.BatchId != Source->Batch.BatchId || Ready->Report.World != World->GetPathName()
            || Ready->Report.Epoch.IsEmpty() || Ready->Report.Epoch == TEXT("InvalidRuntimeGeometry")) return false;

        const auto* Health = Player->GetHealthComponent();
        const auto* Capsule = Player->GetCapsuleComponent();
        const auto* Movement = Player->GetCharacterMovement();
        if (!IsValid(Health) || Health->GetClass() != URogueliteSliceHealth::StaticClass()
            || Health->GetOwner() != Player || Health->UActorComponent::GetWorld() != World
            || !IsValid(Capsule) || Capsule->GetClass() != UCapsuleComponent::StaticClass()
            || Capsule->GetOwner() != Player || Capsule->UActorComponent::GetWorld() != World
            || Player->GetRootComponent() != Capsule || !Capsule->IsRegistered()
            || !IsValid(Movement) || Movement->GetClass() != UCharacterMovementComponent::StaticClass()
            || Movement->GetOwner() != Player || Movement->UActorComponent::GetWorld() != World) return false;

        const auto* Navigation = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
        if (!IsValid(Navigation) || Navigation->GetClass() != URogueliteRuntimeNavigationSystem::StaticClass()
            || Navigation->NavDataSet.IsEmpty()) return false;
        bool bReportDataRegistered = false;
        for (const ANavigationData* Data : Navigation->NavDataSet)
        {
            if (!IsValid(Data) || Data->GetClass() != ARogueliteRuntimeRecastNavMesh::StaticClass()
                || Data->AActor::GetWorld() != World) return false;
            bReportDataRegistered |= Data->GetPathName() == Ready->Report.NavData;
        }
        return bReportDataRegistered && !UNavigationSystemV1::IsNavigationBeingBuiltOrLocked(World);
    }

    static bool AdmitWorld(const ARogueliteRuntimeLoopAssembly* Source, UWorld* World)
    {
        // Immutable metadata only. These are the PERF1D1 audited actual classes;
        // unknown descendants never inherit admission through a native parent.
        static const TSet<FTopLevelAssetPath> Actors = {
            FTopLevelAssetPath(TEXT("/Script/RoguelitePrototype.RogueliteRuntimeLoopAssembly")),
            FTopLevelAssetPath(TEXT("/Script/Engine.WorldSettings")),
            FTopLevelAssetPath(TEXT("/Script/Engine.Brush")),
            FTopLevelAssetPath(TEXT("/Script/RoguelitePrototype.RogueliteRuntimeNavigationBounds")),
            FTopLevelAssetPath(TEXT("/Script/Engine.DirectionalLight")),
            FTopLevelAssetPath(TEXT("/Script/Engine.SkyLight")),
            FTopLevelAssetPath(TEXT("/Script/Engine.PlayerStart")),
            FTopLevelAssetPath(TEXT("/Script/RoguelitePrototype.RogueliteCombatSliceRig")),
            FTopLevelAssetPath(TEXT("/Script/Engine.Actor")),
            FTopLevelAssetPath(TEXT("/Script/RoguelitePrototype.RogueliteRuntimeRecastNavMesh")),
            FTopLevelAssetPath(TEXT("/Script/Water.BuoyancyManager")),
            FTopLevelAssetPath(TEXT("/Script/Engine.DefaultPhysicsVolume")),
            FTopLevelAssetPath(TEXT("/Script/GameplayDebugger.GameplayDebuggerPlayerManager")),
            FTopLevelAssetPath(TEXT("/Script/RoguelitePrototype.RogueliteSliceGameMode")),
            FTopLevelAssetPath(TEXT("/Script/Engine.GameSession")),
            FTopLevelAssetPath(TEXT("/Script/Engine.ParticleEventManager")),
            FTopLevelAssetPath(TEXT("/Script/Engine.GameNetworkManager")),
            FTopLevelAssetPath(TEXT("/Script/Engine.GameStateBase")),
            FTopLevelAssetPath(TEXT("/Script/AIModule.AIController")),
            FTopLevelAssetPath(TEXT("/Script/NavigationSystem.AbstractNavData")),
            FTopLevelAssetPath(TEXT("/Script/RoguelitePrototype.RogueliteSlicePlayerController")),
            FTopLevelAssetPath(TEXT("/Script/Engine.PlayerState")),
            FTopLevelAssetPath(TEXT("/Script/Engine.PlayerCameraManager")),
            FTopLevelAssetPath(TEXT("/Script/RoguelitePrototype.RogueliteSliceHUD")),
            FTopLevelAssetPath(TEXT("/Script/RoguelitePrototype.RogueliteSlicePlayer")),
            FTopLevelAssetPath(TEXT("/Script/GameplayDebugger.GameplayDebuggerCategoryReplicator")),
            FTopLevelAssetPath(TEXT("/Script/RoguelitePrototype.RogueliteRoomAssembly")),
            FTopLevelAssetPath(TEXT("/Script/RoguelitePrototype.RogueliteParametricCorridor")),
            FTopLevelAssetPath(TEXT("/Script/RoguelitePrototype.RogueliteSliceEnemy")),
            FTopLevelAssetPath(TEXT("/Script/RoguelitePrototype.RogueliteSliceGate")),
            FTopLevelAssetPath(TEXT("/Script/RoguelitePrototype.RogueliteRoomTemplate")),
            FTopLevelAssetPath(TEXT("/Script/RoguelitePrototype.RogueliteProductionCandidateRoom")),
            FTopLevelAssetPath(TEXT("/Script/RoguelitePrototype.RogueliteNavigationTestCharacter")),
            FTopLevelAssetPath(TEXT("/Script/RoguelitePrototype.RogueliteNavigationTestController")),
            FTopLevelAssetPath(TEXT("/Script/RoguelitePrototype.RogueliteSliceRecoveryProbe"))
        };
        static const TSet<FTopLevelAssetPath> Primitives = {
            FTopLevelAssetPath(TEXT("/Script/Engine.BrushComponent")),
            FTopLevelAssetPath(TEXT("/Script/Engine.BoxComponent")),
            FTopLevelAssetPath(TEXT("/Script/Engine.ArrowComponent")),
            FTopLevelAssetPath(TEXT("/Script/Engine.BillboardComponent")),
            FTopLevelAssetPath(TEXT("/Script/Engine.CapsuleComponent")),
            FTopLevelAssetPath(TEXT("/Script/NavigationSystem.NavMeshRenderingComponent")),
            FTopLevelAssetPath(TEXT("/Script/Engine.SkeletalMeshComponent")),
            FTopLevelAssetPath(TEXT("/Script/Engine.CameraProxyMeshComponent")),
            FTopLevelAssetPath(TEXT("/Script/Engine.DrawFrustumComponent")),
            FTopLevelAssetPath(TEXT("/Script/GameplayDebugger.GameplayDebuggerRenderingComponent")),
            FTopLevelAssetPath(TEXT("/Script/Engine.StaticMeshComponent"))
        };
        static const TSet<FTopLevelAssetPath> BlueprintActors = {
            FTopLevelAssetPath(TEXT("/Game/Roguelite/ProductionCandidates/P03M1/Templates/BP_M1_Safehouse.BP_M1_Safehouse_C")),
            FTopLevelAssetPath(TEXT("/Game/Roguelite/ProductionCandidates/P03M1/Templates/BP_M1_Normal_Straight.BP_M1_Normal_Straight_C")),
            FTopLevelAssetPath(TEXT("/Game/Roguelite/ProductionCandidates/P03M1/Templates/BP_M1_Normal_Corner.BP_M1_Normal_Corner_C")),
            FTopLevelAssetPath(TEXT("/Game/Roguelite/ProductionCandidates/P03M1/Templates/BP_M1_Normal_Junction.BP_M1_Normal_Junction_C")),
            FTopLevelAssetPath(TEXT("/Game/Roguelite/ProductionCandidates/P03M1/Templates/BP_M1_FloorExit.BP_M1_FloorExit_C")),
            FTopLevelAssetPath(TEXT("/Game/Roguelite/ProductionCandidates/P03M1/Templates/BP_M1_FinalBoss.BP_M1_FinalBoss_C"))
        };
        const auto ActorAdmitted = [&](const AActor* Actor)
        {
            if (!IsValid(Actor) || Actor->AActor::GetWorld() != World) return false;
            const UClass* Actual = Actor->GetClass();
            const UClass* Native = Actual;
            while (Native && !Native->HasAnyClassFlags(CLASS_Native)) Native = Native->GetSuperClass();
            return Native && Actors.Contains(Native->GetClassPathName())
                && (Actual == Native || BlueprintActors.Contains(Actual->GetClassPathName()));
        };
        for (const auto& Room : Source->Batch.Rooms) if (!ActorAdmitted(Room.Room)) return false;
        for (const auto& Edge : Source->Batch.Edges) if (!ActorAdmitted(Edge.Corridor)) return false;
        for (const auto& Edge : Source->CommittedLoops.Edges) if (!ActorAdmitted(Edge.Corridor)) return false;
        for (TActorIterator<AActor> It(World); It; ++It)
        {
            if (!ActorAdmitted(*It)) return false;
            TInlineComponentArray<UPrimitiveComponent*> Components;
            It->GetComponents(Components);
            for (const auto* Component : Components)
            {
                if (!IsValid(Component) || !Component->GetClass()->HasAnyClassFlags(CLASS_Native)
                    || !Primitives.Contains(Component->GetClass()->GetClassPathName())) return false;
            }
        }
        for (const auto& Room : Source->Batch.Rooms)
        {
            for (const auto* Component : Room.Room->GetComponents())
            {
                if (!Component || (!Cast<UPrimitiveComponent>(Component)
                    && Component->GetClass() != USceneComponent::StaticClass())) return false;
                const auto* Mesh = Cast<UStaticMeshComponent>(Component);
                if (Mesh && Mesh->GetStaticMesh() && Mesh->GetStaticMesh()->GetClass() != UStaticMesh::StaticClass()) return false;
            }
        }
        return true;
    }

    void CaptureKey(const ARogueliteRuntimeLoopAssembly* Source, const ARogueliteSlicePlayer* Player)
    {
        SourceKey = FObjectKey(Source);
        PlayerKey = FObjectKey(Player);
        WorldKey = FObjectKey(Source->AActor::GetWorld());
        RegistryKey = FObjectKey(Source->Batch.Registry.Get());
        ReadinessKey = FObjectKey(Source->NavigationReadiness.Get());
        HealthKey = FObjectKey(Player->GetHealthComponent());
        CapsuleKey = FObjectKey(Player->GetCapsuleComponent());
        MovementKey = FObjectKey(Player->GetCharacterMovement());
        Batch = &Source->Batch;
        Settings = &Source->Settings;
        Loops = &Source->CommittedLoops;
        BatchId = Source->Batch.BatchId;
        SourceSeed = Source->Seed;
        BatchSeed = Source->Batch.Seed;
        GraphRevision = Source->CommittedLoops.GraphRevision;
        GeometryRevision = Source->CommittedLoops.GeometryRevision;
        ConfigurationDigest = Source->Batch.ConfigurationDigest;
        GraphDigest = Source->Batch.GraphDigest;
        LayoutDigest = Source->Batch.LayoutDigest;
        GeometryDigest = Source->CommittedLoops.GeometryDigest;
        SourceTransform = Source->GetActorTransform();
        const auto& Report = Source->NavigationReadiness->Report;
        NavigationEpoch = Report.Epoch;
        NavigationWorld = Report.World;
        NavigationDataPath = Report.NavData;
        NavigationLayoutDigest = Report.LayoutDigest;
        const auto* Navigation = FNavigationSystem::GetCurrent<UNavigationSystemV1>(Source->AActor::GetWorld());
        NavigationKey = FObjectKey(Navigation);
        for (const ANavigationData* Data : Navigation->NavDataSet) NavigationDataKeys.Add(FObjectKey(Data));
    }

    bool MatchesKey(const ARogueliteRuntimeLoopAssembly* Source, const ARogueliteSlicePlayer* Player) const
    {
        const auto& Report = Source->NavigationReadiness->Report;
        if (OwnerKey != FObjectKey(Owner) || SourceKey != FObjectKey(Source) || PlayerKey != FObjectKey(Player)
            || WorldKey != FObjectKey(Source->AActor::GetWorld()) || RegistryKey != FObjectKey(Source->Batch.Registry.Get())
            || ReadinessKey != FObjectKey(Source->NavigationReadiness.Get()) || HealthKey != FObjectKey(Player->GetHealthComponent())
            || CapsuleKey != FObjectKey(Player->GetCapsuleComponent()) || MovementKey != FObjectKey(Player->GetCharacterMovement())
            || Batch != &Source->Batch || Settings != &Source->Settings || Loops != &Source->CommittedLoops
            || BatchId != Source->Batch.BatchId || SourceSeed != Source->Seed || BatchSeed != Source->Batch.Seed
            || GraphRevision != Source->CommittedLoops.GraphRevision || GeometryRevision != Source->CommittedLoops.GeometryRevision
            || ConfigurationDigest != Source->Batch.ConfigurationDigest || GraphDigest != Source->Batch.GraphDigest
            || LayoutDigest != Source->Batch.LayoutDigest || GeometryDigest != Source->CommittedLoops.GeometryDigest
            || !SourceTransform.Equals(Source->GetActorTransform(), 0) || NavigationEpoch != Report.Epoch
            || NavigationWorld != Report.World || NavigationDataPath != Report.NavData
            || NavigationLayoutDigest != Report.LayoutDigest) return false;
        const auto* Navigation = FNavigationSystem::GetCurrent<UNavigationSystemV1>(Source->AActor::GetWorld());
        if (NavigationKey != FObjectKey(Navigation) || NavigationDataKeys.Num() != Navigation->NavDataSet.Num()) return false;
        for (int32 Index = 0; Index < NavigationDataKeys.Num(); ++Index)
        {
            if (NavigationDataKeys[Index] != FObjectKey(Navigation->NavDataSet[Index])) return false;
        }
        return true;
    }

    const URogueliteSliceEncounter* Owner = nullptr;
    FObjectKey OwnerKey, SourceKey, PlayerKey, WorldKey, RegistryKey, ReadinessKey;
    FObjectKey HealthKey, CapsuleKey, MovementKey, NavigationKey;
    TArray<FObjectKey, TInlineAllocator<2>> NavigationDataKeys;
    const FRogueliteSeedBatch* Batch = nullptr;
    const FRogueliteSeedLayoutSettings* Settings = nullptr;
    const FRogueliteCommittedLoopBatch* Loops = nullptr;
    FGuid BatchId;
    int32 SourceSeed = 0, BatchSeed = 0, GraphRevision = 0, GeometryRevision = 0;
    FString ConfigurationDigest, GraphDigest, LayoutDigest, GeometryDigest;
    FString NavigationEpoch, NavigationWorld, NavigationDataPath, NavigationLayoutDigest, StableDigest;
    FTransform SourceTransform = FTransform::Identity;
    TOptional<FRogueliteLoopTraversalSnapshot> StoredTraversal;
    bool bProduced = false, bConsumed = false, bInvalidated = false, bClosed = false;
};
