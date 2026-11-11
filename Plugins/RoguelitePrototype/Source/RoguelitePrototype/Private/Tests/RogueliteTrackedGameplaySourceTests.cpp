#include "Tests/RogueliteTrackedGameplaySourceTestTypes.h"

#include "RogueliteRoomTemplate.h"

void URogueliteTrackedGameplayCallback::Sample(const TCHAR* Event)
{
    ++Calls;
    if (!Source)
    {
        bEveryOldEpochRejected = bEveryRefreshRejected = bEveryStatusNonValid = bEveryEpochChanged = false;
        Evidence += FString(Event) + TEXT(" Source unavailable during callback\n");
        return;
    }
    FString StatusReason, RefreshReason;
    const uint64 EpochAtEntry = Source->GetGameplayObservationEpoch();
    const bool Current = Source->IsGameplayObservationEpochCurrent(PreviousEpoch);
    const auto Status = Source->GetGameplayObservationStatus(StatusReason);
    const bool Refreshed = Source->RefreshGameplayObservation(RefreshReason);
    const uint64 Epoch = Source->GetGameplayObservationEpoch();
    bEveryOldEpochRejected &= !Current;
    bEveryRefreshRejected &= !Refreshed;
    bEveryStatusNonValid &= Status != ERogueliteGameplayObservationStatus::ValidObservation;
    bEveryEpochChanged &= EpochAtEntry != PreviousEpoch;
    Evidence += FString::Printf(TEXT("Event=%s OldEpoch=%llu EpochAtCallbackEntry=%llu EpochAfterQueries=%llu Current=%d Status=%d Refresh=%d StatusReason=%s RefreshReason=%s\n"),
        Event, PreviousEpoch, EpochAtEntry, Epoch, Current, int32(Status), Refreshed, *StatusReason, *RefreshReason);
}

void URogueliteTrackedGameplayCallback::ActorDestroyed(AActor* Actor)
{
    Sample(TEXT("RealActorDestroyed"));
}

void URogueliteTrackedGameplayCallback::ActorEnded(AActor* Actor, EEndPlayReason::Type Reason)
{
    Sample(TEXT("RealActorEndPlay"));
}

ECollisionEnabled::Type URogueliteTrackedGameplayGetter::GetCollisionEnabled() const
{
    ++Calls;
    if (auto* OwnerSource = Source.Get(); OwnerSource && !bInCallback)
    {
        TGuardValue<bool> CallbackGuard(bInCallback, true);
        if (bChangeRoomLength && !bChanged && !OwnerSource->Batch.Rooms.IsEmpty())
        {
            bChanged = true;
            OwnerSource->Batch.Rooms[0].Room->RoomLength += 10;
        }
        if (bAttemptRefresh && ReentryCalls < 4)
        {
            ++ReentryCalls;
            FString Reason;
            bEveryNestedRefreshRejected &= !OwnerSource->RefreshGameplayObservation(Reason);
        }
    }
    return Super::GetCollisionEnabled();
}

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "Gameplay/RogueliteGameplayObservation.h"
#include "Gameplay/RogueliteCombatSliceActors.h"
#include "Gameplay/RogueliteCombatSliceEncounter.h"
#include "Gameplay/RogueliteCombatSlicePlayer.h"
#include "RogueliteDynamicGameplayBridge.h"
#include "RogueliteFloorProfile.h"
#include "RogueliteParametricCorridor.h"
#include "RogueliteNavigationReadinessComponent.h"
#include "RogueliteRuntimeNavigationSystem.h"
#include "Tests/RogueliteCombatSliceRig.h"
#include "Components/BoxComponent.h"
#include "Editor.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/World.h"
#include "Engine/OverlapResult.h"
#include "EngineUtils.h"
#include "HAL/FileManager.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Tests/AutomationEditorCommon.h"
#include "UObject/StrongObjectPtr.h"
#include "NavigationSystem.h"
#include <atomic>

// Read-only test diagnostics for the existing Source-owned transaction fences.
// Definitions stay in Source.cpp; this test creates no parallel validity state.
namespace RogueliteObservedGameplay
{
extern TMap<FObjectKey,int32> Writes;
extern int32 UnknownWorldWrites;
extern std::atomic<int32> UnsupportedWrites;
}

namespace TrackedGameplaySourceTests
{
const TCHAR* Map = TEXT("/Game/Roguelite/Tests/P04G2/L_Roguelite_TwoCombatRooms_Dev");

FRogueliteLoopTraversalSnapshot OriginalSnapshot(ARogueliteRuntimeLoopAssembly& Source)
{
    auto Value = FRogueliteLoopCommitQueries::Capture(&Source, Source.Batch, Source.Settings, Source.CommittedLoops);
    Value.bNavigationReady = Value.bCurrent && Source.IsDungeonReady();
    Value.NavigationState = Value.bNavigationReady ? TEXT("NavigationReady") : TEXT("NavigationPending");
    return Value;
}

FString SnapshotText(const FRogueliteLoopTraversalSnapshot& Snapshot)
{
    FString Text;
    FRogueliteLoopTraversalSnapshot::StaticStruct()->ExportText(Text, &Snapshot, nullptr, nullptr, PPF_None, nullptr);
    return Text;
}

AActor* SpawnBlocker(UWorld& World, UBoxComponent*& Box)
{
    auto* Actor = World.SpawnActor<AActor>();
    if (!Actor) return nullptr;
    Box = NewObject<UBoxComponent>(Actor);
    Actor->SetRootComponent(Box);
    Actor->AddInstanceComponent(Box);
    Box->SetMobility(EComponentMobility::Movable);
    Box->SetCanEverAffectNavigation(false);
    Box->SetBoxExtent(FVector(50));
    Box->SetCollisionProfileName(TEXT("BlockAll"));
    Box->RegisterComponent();
    Actor->SetActorLocation(FVector(11000, 11000, 200));
    return Actor;
}

ARogueliteRuntimeLoopAssembly* SpawnInactiveSource(UWorld& World, UClass* Class)
{
    auto* Other = World.SpawnActorDeferred<ARogueliteRuntimeLoopAssembly>(Class,
        FTransform(FVector(150000, 150000, 0)), nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
    if (!Other) return nullptr;
    Other->bAutoGenerate = false;
    Other->bGenerateOnBeginPlay = false;
    Other->bEnableRuntimeNavigation = false;
    Other->bNavigationEnabled = false;
    Other->FinishSpawning(FTransform(FVector(150000, 150000, 0)));
    return Other;
}

struct FRun final : IAutomationLatentCommand
{
    FAutomationTestBase* Test;
    FString Case, Rows;
    double Started = FPlatformTime::Seconds();
    int32 Stage = 0;
    bool bGood = true;
    uint64 SavedEpoch = 0;
    FGuid SavedBatch;
    TWeakObjectPtr<ARogueliteCombatSliceRig> BoundSource;
    TWeakObjectPtr<AActor> TestObstacle;
    TStrongObjectPtr<URogueliteTrackedGameplayCallback> Callback;
    double WindowStarted = 0;
    double NavigationRecoveryStarted = 0;
    int32 NavigationRecoverySamples = 0;
    bool bSawStrictNotReady = false;
    bool bSawNavigationPending = false;
    bool bSawNavigationRunning = false;

    FRun(FAutomationTestBase* InTest, FString InCase) : Test(InTest), Case(MoveTemp(InCase)) {}

    void Check(const TCHAR* Label, bool Passed)
    {
        bGood &= Passed;
        Test->TestTrue(Label, Passed);
        Rows += FString::Printf(TEXT("Check=%s Pass=%d\n"), Label, Passed);
    }

    bool Publish(ARogueliteRuntimeLoopAssembly& Source, const TCHAR* Label)
    {
        FString Reason;
        const bool Refreshed = Source.RefreshGameplayObservation(Reason);
        Check(Label, Refreshed);
        Rows += FString::Printf(TEXT("Publish=%s Result=%d Epoch=%llu Reason=%s\n"),
            Label, Refreshed, Source.GetGameplayObservationEpoch(), *Reason);
        if (Refreshed)
        {
            Check(TEXT("Fresh published epoch is consumable"),
                Source.IsGameplayObservationEpochCurrent(Source.GetGameplayObservationEpoch()));
        }
        return Refreshed;
    }

    void Revoked(ARogueliteRuntimeLoopAssembly& Source, uint64 Epoch, const TCHAR* Label)
    {
        const uint64 EpochBeforeQueries = Source.GetGameplayObservationEpoch();
        Check(Label, !Source.IsGameplayObservationEpochCurrent(Epoch));
        FString Reason;
        const auto Status = Source.GetGameplayObservationStatus(Reason);
        Rows += FString::Printf(TEXT("Status=%d EpochBeforeQueries=%llu EpochAfterQueries=%llu OldEpoch=%llu Reason=%s\n"),
            int32(Status), EpochBeforeQueries, Source.GetGameplayObservationEpoch(), Epoch, *Reason);
    }

    void RejectRefresh(ARogueliteRuntimeLoopAssembly& Source, const TCHAR* Label)
    {
        FString Reason;
        Check(Label, !Source.RefreshGameplayObservation(Reason));
        Rows += FString(Label) + TEXT(" Reason=") + Reason + TEXT("\n");
    }

    FRogueliteLoopTraversalSnapshot Differential(ARogueliteRuntimeLoopAssembly& Source, const TCHAR* Label)
    {
        const auto Expected = OriginalSnapshot(Source);
        const auto Actual = Source.GetTraversalSnapshot();
        Check(Label, FRogueliteLoopTraversalSnapshot::StaticStruct()->CompareScriptStruct(&Expected, &Actual, 0));
        Rows += FString::Printf(TEXT("Snapshot=%s\nExpected=%s\nActual=%s\n"),
            Label, *SnapshotText(Expected), *SnapshotText(Actual));
        return Actual;
    }

    void CheckCallback(const TCHAR* Label)
    {
        Check(Label, Callback.IsValid() && Callback->Calls > 0);
        if (!Callback.IsValid()) return;
        Check(TEXT("Real callback rejects previous observation"), Callback->bEveryOldEpochRejected);
        Check(TEXT("Real callback cannot republish during mutation"), Callback->bEveryRefreshRejected);
        Check(TEXT("Real callback reports no valid observation"), Callback->bEveryStatusNonValid);
        Check(TEXT("Observation epoch changed before real callback"), Callback->bEveryEpochChanged);
        Rows += Callback->Evidence;
    }

    bool Finish(ARogueliteCombatSliceRig* Source, const TCHAR* Reason)
    {
        if (TestObstacle.IsValid()) TestObstacle->Destroy();
        if (Source && !Source->Batch.OwnedActors.IsEmpty())
            Check(TEXT("Actual virtual Source clears only test batch"), Source->ClearRuntime());
        Rows += FString::Printf(TEXT("Passed=%d\nReason=%s\n"), bGood, Reason);
        FString Directory;
        if (FParse::Value(FCommandLine::Get(), TEXT("PERF21BEvidenceDir="), Directory))
        {
            IFileManager::Get().MakeDirectory(*Directory, true);
            const FString File = Directory / (Case + TEXT("-") + FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT(".txt"));
            Check(TEXT("Independent detailed evidence saved"), FFileHelper::SaveStringToFile(Rows, *File));
        }
        return true;
    }

    struct FOccupancyEvidence
    {
        int32 BlockingHits = 0;
        int32 LeasedDormantHits = 0;
    };

    FOccupancyEvidence RecordOccupancy(ARogueliteRuntimeLoopAssembly& Source, const FRogueliteSeedEdgeRecord& Edge, const TCHAR* Label)
    {
        FOccupancyEvidence Evidence;
        auto* Registry = Source.Batch.Registry.Get();
        const auto Connections = Registry->GetConnectionRecords();
        const auto* Connection = Connections.FindByPredicate([&](const auto& Item) { return Item.Id == Edge.ConnectionId; });
        Check(TEXT("Diagnostic uses the actual active C2 connection"), Connection && Connection->bActive);
        if (!Connection) return Evidence;
        const auto& Result = Edge.Validation;
        Check(TEXT("Diagnostic occupancy records match current physical transforms"),
            Result.ProposedTargetTransform.Equals(Connection->RoomB->GetActorTransform(), .0001)
            && Result.ProposedCorridorTransform.Equals(Connection->Corridor->GetActorTransform(), .0001));
        FCollisionQueryParams Params(SCENE_QUERY_STAT(RogueliteCorridorPhysicalOccupancy), false);
        Params.AddIgnoredActor(Registry); Params.AddIgnoredActor(Connection->RoomA);
        Params.AddIgnoredActor(Connection->RoomB); Params.AddIgnoredActor(Connection->Corridor);
        Rows += FString::Printf(TEXT("OccupancyQuery=%s World=%s Batch=%s Edge=%d Parent=%d Child=%d Connection=%s Channel=ECC_Pawn Rotation=Identity TraceComplex=%d Tolerance=%.6f\n"),
            Label, *Source.GetWorld()->GetPathName(), *Source.Batch.BatchId.ToString(), Edge.EdgeId, Edge.ParentNode, Edge.ChildNode,
            *Edge.ConnectionId.ToString(), Params.bTraceComplex, Registry->PositionTolerance);
        for (AActor* Ignore : {static_cast<AActor*>(Registry), static_cast<AActor*>(Connection->RoomA.Get()), static_cast<AActor*>(Connection->RoomB.Get()), static_cast<AActor*>(Connection->Corridor.Get())})
            Rows += FString::Printf(TEXT("OriginalIgnore Actor=%s Class=%s ObjectId=%u\n"), *GetPathNameSafe(Ignore), *GetPathNameSafe(Ignore ? Ignore->GetClass() : nullptr), Ignore ? Ignore->GetUniqueID() : 0);
        int32 BoxIndex = 0;
        for (const FBox& Bounds : {Result.OccupancyB, Result.OccupancyCorridor})
        {
            Check(TEXT("Diagnostic uses valid actual C2 occupancy box"), Bounds.IsValid != 0);
            if (!Bounds.IsValid) continue;
            const FVector Extent = Bounds.GetExtent()-FVector(Registry->PositionTolerance);
            const auto Shape = FCollisionShape::MakeBox(Extent);
            TArray<FOverlapResult> Hits;
            Source.GetWorld()->OverlapMultiByChannel(Hits, Bounds.GetCenter(), FQuat::Identity, ECC_Pawn, Shape, Params);
            const bool OriginalBlocked = Source.GetWorld()->OverlapBlockingTestByChannel(Bounds.GetCenter(), FQuat::Identity, ECC_Pawn, Shape, Params);
            int32 Blocking = 0;
            Rows += FString::Printf(TEXT("OccupancyBox=%d Center=%s Extent=%s HitCount=%d OriginalBlocking=%d\n"),
                BoxIndex++, *Bounds.GetCenter().ToString(), *Extent.ToString(), Hits.Num(), OriginalBlocked);
            for (const auto& Hit : Hits)
            {
                auto* Actor = Hit.GetActor(); auto* Component = Hit.GetComponent();
                const auto* Enemy = Cast<ARogueliteSliceEnemy>(Actor);
                const bool Dormant = Enemy && Enemy->IsDormant();
                const bool Leased = Component && FRogueliteDynamicGameplayBridge::IsComponentIsolated(&Source, Source.Batch.BatchId, Component);
                const bool Ignored = Actor == Registry || Actor == Connection->RoomA || Actor == Connection->RoomB || Actor == Connection->Corridor;
                Blocking += Hit.bBlockingHit;
                Evidence.LeasedDormantHits += Hit.bBlockingHit && Dormant && Leased;
                Rows += FString::Printf(TEXT("OccupancyHit Edge=%d Room=%d Actor=%s ActorClass=%s ActorId=%u ActorOwner=%s Component=%s ComponentClass=%s ComponentId=%u ComponentOwner=%s Collision=%d ObjectType=%d PawnResponse=%d Mobility=%d Blocking=%d DormantEnemy=%d EnemyNode=%d ValidG1ALease=%d IgnoredByOriginal=%d\n"),
                    Edge.EdgeId, Edge.ChildNode, *GetPathNameSafe(Actor), *GetPathNameSafe(Actor ? Actor->GetClass() : nullptr), Actor ? Actor->GetUniqueID() : 0,
                    *GetPathNameSafe(Actor ? Actor->GetOwner() : nullptr), *GetPathNameSafe(Component), *GetPathNameSafe(Component ? Component->GetClass() : nullptr), Component ? Component->GetUniqueID() : 0,
                    *GetPathNameSafe(Component ? Component->GetOwner() : nullptr), Component ? int32(Component->GetCollisionEnabled()) : -1,
                    Component ? int32(Component->GetCollisionObjectType()) : -1, Component ? int32(Component->GetCollisionResponseToChannel(ECC_Pawn)) : -1,
                    Component ? int32(Component->Mobility) : -1, Hit.bBlockingHit, Dormant, Enemy ? Enemy->GetNodeId() : INDEX_NONE, Leased, Ignored);
            }
            Check(TEXT("Read-only multi overlap agrees with original blocking test"), OriginalBlocked == (Blocking > 0));
            Evidence.BlockingHits += Blocking;
        }
        return Evidence;
    }

    bool BeginWatchObstacleChecks(ARogueliteCombatSliceRig& SourceValue)
    {
        auto* Source = &SourceValue;
        UWorld* World = Source->AActor::GetWorld();
        UBoxComponent* Box = nullptr;
        TestObstacle = SpawnBlocker(*World, Box);
        Check(TEXT("Actual uncontrolled BlockAll geometry exists"), TestObstacle.IsValid() && Box);
        SavedEpoch = Source->GetGameplayObservationEpoch();
        const auto Strong = Differential(*Source, TEXT("Real BlockAll original strong Snapshot fields equal"));
        Check(TEXT("Original scanner rejects external blocker"), !Strong.bCurrent || !Strong.bNavigationReady);
        Revoked(*Source, SavedEpoch, TEXT("Strong external-obstacle failure invalidates tracked token"));
        if (TestObstacle.IsValid()) TestObstacle->Destroy();
        TestObstacle.Reset();
        if (!Publish(*Source, TEXT("Removed blocker requires fresh full strong success")))
            return Finish(Source, TEXT("Fixture failed to recover after test-owned blocker removal"));
        SavedEpoch = Source->GetGameplayObservationEpoch();
        TestObstacle = SpawnBlocker(*World, Box);
        WindowStarted = FPlatformTime::Seconds();
        Rows += TEXT("RetainedBlockAllUsesOriginalWatchNoHighFrequencyStrongPolling=1\n");
        Stage = 1;
        return false;
    }

    bool CheckIndependentEmptyBatch(ARogueliteCombatSliceRig& OldSource)
    {
        UWorld* World = OldSource.AActor::GetWorld();
        TStrongObjectPtr<URogueliteFloorProfile> Profile(OldSource.FloorProfile.Get());
        const auto Catalog = OldSource.PreparedTemplates;
        const int32 Seed = OldSource.Seed;
        const FGuid RejectedBatch = OldSource.Batch.BatchId;
        if (!OldSource.ClearRuntime()) { Check(TEXT("Rejected fixture clears through original owned-batch API"), false); return false; }
        Check(TEXT("Rejected fixture actors fully cleared before new positive"), OldSource.Batch.OwnedActors.IsEmpty());
        if (!OldSource.Destroy()) { Check(TEXT("Old rejected fixture Source retires before independent positive"), false); return false; }
        auto* Clean = World->SpawnActorDeferred<ARogueliteRuntimeLoopAssembly>(ARogueliteRuntimeLoopAssembly::StaticClass(), FTransform::Identity,
            nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        Check(TEXT("Independent native geometry-only Source allocated"), Clean != nullptr);
        if (!Clean) return false;
        Clean->bAutoGenerate = false; Clean->bGenerateOnBeginPlay = false; Clean->bNavigationEnabled = false;
        Clean->FloorProfile = Profile.Get(); Clean->PreparedTemplates = Catalog; Clean->Seed = Seed;
        Clean->FinishSpawning(FTransform::Identity);
        const bool Generated = Clean->GenerateRuntime();
        Check(TEXT("Independent complete four-room batch generated without Gameplay binding"), Generated && Clean->Batch.Rooms.Num() == 4 && Clean->Batch.Edges.Num() == 3);
        Check(TEXT("Independent positive uses a new Batch identity"), Clean->Batch.BatchId.IsValid() && Clean->Batch.BatchId != RejectedBatch);
        int32 PawnCount = 0;
        for (TActorIterator<APawn> It(World); It; ++It) if (!It->IsActorBeingDestroyed()) ++PawnCount;
        Check(TEXT("Independent geometry-only positive has no live Gameplay Pawns"), PawnCount == 0);
        Rows += FString::Printf(TEXT("IndependentPositive Source=%s Class=%s World=%s Batch=%s PreviousRejectedBatch=%s Rooms=%d Tree=%d Loop=%d WorldPawnCount=%d\n"),
            *Clean->GetPathName(), *Clean->GetClass()->GetPathName(), *World->GetPathName(), *Clean->Batch.BatchId.ToString(), *RejectedBatch.ToString(),
            Clean->Batch.Rooms.Num(), Clean->Batch.Edges.Num(), Clean->CommittedLoops.Edges.Num(), PawnCount);
        if (Generated)
        {
            int32 Blocking = 0;
            for (const auto& Edge : Clean->Batch.Edges) Blocking += RecordOccupancy(*Clean, Edge, TEXT("IndependentPositiveBeforeBatchRevalidate")).BlockingHits;
            Check(TEXT("Every occupancy box in complete positive batch is actually unblocked"), Blocking == 0);
            const int32 Revision = Clean->CommittedLoops.GeometryRevision;
            FString Reason;
            const bool Revalidated = FRogueliteRuntimeLoopAssembler::RevalidateConfigured(Clean, Clean->Batch, Clean->Settings, Clean->CommittedLoops, Reason);
            Check(TEXT("Independent complete batch revalidation succeeds"), Revalidated);
            Check(TEXT("Independent complete revalidation advances geometry revision once"), Clean->CommittedLoops.GeometryRevision == Revision+1);
            int32 PassedEdges = 0;
            for (const auto& Edge : Clean->Batch.Edges)
            {
                const bool Passed = Edge.Validation.bSucceeded && Edge.Validation.bGeometryPassed && Edge.Validation.bCapsulePassed;
                Check(TEXT("Every positive batch edge retains original full C2 geometry and capsule checks"), Passed);
                PassedEdges += Passed;
                Rows += FString::Printf(TEXT("IndependentPositiveEdge Edge=%d Connection=%s Pass=%d Probes=%d Reason=%s\n"), Edge.EdgeId, *Edge.ConnectionId.ToString(), Passed, Edge.Validation.Probes.Num(), *Edge.Validation.Message);
            }
            Check(TEXT("Full batch success covers all three actual connections"), PassedEdges == 3 && Clean->Batch.Registry->GetConnectionRecords().Num() == 3);
            Check(TEXT("Independent full batch retains original strong Current"), FRogueliteRuntimeLoopAssembler::Current(Clean, Clean->Batch, Clean->Settings, Clean->CommittedLoops));
            Differential(*Clean, TEXT("Independent full batch Snapshot and Reason equal original geometry-only semantics"));
            Check(TEXT("Geometry-only positive does not claim navigation Ready"), !Clean->IsDungeonReady());
            Rows += TEXT("IndependentPositiveReason=")+Reason+TEXT(" NavigationReadyNotClaimed=1 ObservationPublicationNotClaimed=1\n");
        }
        if (!Clean->Batch.OwnedActors.IsEmpty()) Check(TEXT("Independent positive owned batch cleanup succeeds"), Clean->ClearRuntime());
        Check(TEXT("Independent positive cleanup leaves no owned actors"), Clean->Batch.OwnedActors.IsEmpty());
        Clean->Destroy();
        return bGood;
    }

    void RecordNavigationState(ARogueliteRuntimeLoopAssembly& Source, const TCHAR* Label, bool FullReport = true)
    {
        UWorld* World = Source.AActor::GetWorld();
        auto* Navigation = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
        const uint64 EpochBeforeStrong = Source.GetGameplayObservationEpoch();
        const bool Current = FRogueliteRuntimeLoopAssembler::Current(&Source, Source.Batch, Source.Settings, Source.CommittedLoops);
        const auto Original = OriginalSnapshot(Source);
        const auto Snapshot = Source.GetTraversalSnapshot();
        const bool StrictReady = Source.IsDungeonReady();
        const bool ComponentReady = Source.NavigationReadiness->IsCurrentBatchReady();
        FString ObservationReason;
        const auto Observation = Source.GetGameplayObservationStatus(ObservationReason);
        const bool Busy = UNavigationSystemV1::IsNavigationBeingBuiltOrLocked(World);
        const int32 Depth = RogueliteObservedGameplay::Writes.FindRef(FObjectKey(World));
        Rows += FString::Printf(TEXT("Timeline=%s Wall=%.6f WorldTime=%.6f Frame=%llu Batch=%s Current=%d StrictReady=%d ComponentReady=%d OriginalCurrent=%d OriginalReady=%d SnapshotCurrent=%d SnapshotReady=%d SnapshotReason=%s Observation=%d EpochBeforeStrong=%llu EpochAfterStrong=%llu ObservationReason=%s TransactionDepth=%d UnknownWorldDepth=%d UnsupportedThreadDepth=%d NavBusy=%d NavLocked=%d NavDirty=%d NavDirtyAreas=%d NavRunning=%d NavRunningTasks=%d NavRemainingTasks=%d\n"),
            Label, FPlatformTime::Seconds()-Started, World->GetTimeSeconds(), GFrameCounter,
            *Source.Batch.BatchId.ToString(), Current, StrictReady, ComponentReady,
            Original.bCurrent, Original.bNavigationReady, Snapshot.bCurrent, Snapshot.bNavigationReady, *Snapshot.Reason,
            int32(Observation), EpochBeforeStrong, Source.GetGameplayObservationEpoch(), *ObservationReason,
            Depth, RogueliteObservedGameplay::UnknownWorldWrites, RogueliteObservedGameplay::UnsupportedWrites.load(),
            Busy, Navigation && Navigation->IsNavigationBuildingLocked(), Navigation && Navigation->HasDirtyAreasQueued(),
            Navigation ? Navigation->GetNumDirtyAreas() : -1, Navigation && Navigation->IsNavigationBuildInProgress(),
            Navigation ? Navigation->GetNumRunningBuildTasks() : -1, Navigation ? Navigation->GetNumRemainingBuildTasks() : -1);
        const auto& Report = Source.NavigationReadiness->Report;
        Rows += FString::Printf(TEXT("NavigationIdentity ReportState=%s ReportReady=%d ReportBatch=%s ReportEpoch=%s ReportNavData=%s NavSystem=%s NavSystemId=%u\n"),
            *Report.State, Report.bReady, *Report.BatchId.ToString(), *Report.Epoch, *Report.NavData,
            *GetPathNameSafe(Navigation), Navigation ? Navigation->GetUniqueID() : 0);
        if (Navigation)
            for (const ANavigationData* Data : Navigation->NavDataSet)
                Rows += FString::Printf(TEXT("ActualNavData=%s ObjectId=%u World=%s\n"),
                    *GetPathNameSafe(Data), Data ? Data->GetUniqueID() : 0, *GetPathNameSafe(Data ? Data->GetWorld() : nullptr));
        if (FullReport)
        {
            FString ReportText;
            FRogueliteNavigationReadinessEvidence::StaticStruct()->ExportText(ReportText, &Report, nullptr, nullptr, PPF_None, nullptr);
            Rows += TEXT("FullNavigationReport=") + ReportText + TEXT("\n");
            Rows += TEXT("FullOriginalSnapshot=") + SnapshotText(Original) + TEXT("\n");
            Rows += TEXT("FullSnapshot=") + SnapshotText(Snapshot) + TEXT("\n");
        }
    }

    void CheckReportAndRevisionBoundaries(ARogueliteCombatSliceRig& Source)
    {
        const auto OriginalReport = Source.NavigationReadiness->Report;
        for (const TCHAR* NavigationState : {TEXT("NavigationPending"), TEXT("NavigationFailed"), TEXT("NavigationTimedOut"), TEXT("NavigationUnavailable")})
        {
            Source.NavigationReadiness->Report.bReady = false;
            Source.NavigationReadiness->Report.State = NavigationState;
            RejectRefresh(Source, TEXT("Injected nonReady report state cannot publish"));
            Differential(Source, TEXT("Injected report-state boundary preserves original strict Snapshot fields"));
            Rows += TEXT("InjectedNavigationReportState=") + FString(NavigationState) + TEXT(" ActualBuildFailureNotClaimed=1\n");
        }
        Source.NavigationReadiness->Report = OriginalReport;
        Publish(Source, TEXT("Restored original Ready report requires full fresh validation"));
        for (const bool Graph : {true, false})
        {
            const uint64 Before = Source.GetGameplayObservationEpoch();
            int32& Revision = Graph ? Source.CommittedLoops.GraphRevision : Source.CommittedLoops.GeometryRevision;
            const int32 Saved = Revision;
            ++Revision;
            Revoked(Source, Before, Graph ? TEXT("GraphRevision identity invalidates observation") : TEXT("GeometryRevision identity invalidates observation"));
            RejectRefresh(Source, TEXT("Mismatched navigation epoch cannot publish"));
            Differential(Source, TEXT("Revision failure retains original complete Snapshot semantics"));
            Revision = Saved;
            Publish(Source, TEXT("Restored revision requires fresh strong success"));
        }
        Source.SetGameplayReadMode(ERogueliteGameplayReadMode::DeveloperStrict);
        Differential(Source, TEXT("Switching back retains every strict Snapshot field"));
        RejectRefresh(Source, TEXT("DeveloperStrict never publishes a tracked observation"));
    }

    bool AwaitNavigationRecovery(ARogueliteCombatSliceRig& Source)
    {
        ++NavigationRecoverySamples;
        UWorld* World = Source.AActor::GetWorld();
        auto* Navigation = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
        const bool Busy = UNavigationSystemV1::IsNavigationBeingBuiltOrLocked(World);
        const bool Running = Navigation && Navigation->IsNavigationBuildInProgress();
        const bool StrictReady = Source.IsDungeonReady();
        const auto Original = OriginalSnapshot(Source);
        bSawStrictNotReady |= !StrictReady || !Original.bNavigationReady;
        bSawNavigationPending |= Busy;
        bSawNavigationRunning |= Running;
        RecordNavigationState(Source, TEXT("CollisionRecoveryPoll"), NavigationRecoverySamples == 1 || StrictReady);
        if (Source.Batch.BatchId != SavedBatch || !Source.bPlayerBridgeReady)
        {
            Check(TEXT("Original recovery fixture and Batch remain alive"), false);
            Rows += TEXT("Classification=C Fixture lifecycle ended before strict recovery\n");
            return Finish(&Source, TEXT("Case C: original fixture ended"));
        }
        if (StrictReady && Original.bCurrent && Original.bNavigationReady)
        {
            if (!Publish(Source, TEXT("Restored floor collision requires fresh strong success")))
            {
                Rows += TEXT("Classification=B Original strict Ready recovered but Observation rejected\n");
                RecordNavigationState(Source, TEXT("CaseBPublicationRejected"));
                return Finish(&Source, TEXT("Case B: implementation investigation required"));
            }
            Check(TEXT("Recovery waited for actual unlocked idle navigation"), !Busy);
            Check(TEXT("Original published epoch stays revoked across recovery"), !Source.IsGameplayObservationEpochCurrent(SavedEpoch));
            Check(TEXT("No controlled write scope leaked across recovery"), RogueliteObservedGameplay::Writes.FindRef(FObjectKey(World)) == 0);
            RecordNavigationState(Source, TEXT("FreshObservationAfterStrictRecovery"));
            const TCHAR* Classification = bSawStrictNotReady && bSawNavigationPending ? TEXT("A") : TEXT("AsyncWaitNotReproduced");
            Rows += FString::Printf(TEXT("Classification=%s RecoverySeconds=%.6f Samples=%d SawStrictNotReady=%d SawNavPending=%d SawActualNavRunning=%d\n"),
                Classification,
                FPlatformTime::Seconds()-NavigationRecoveryStarted, NavigationRecoverySamples, bSawStrictNotReady, bSawNavigationPending, bSawNavigationRunning);
            CheckReportAndRevisionBoundaries(Source);
            return Finish(&Source, TEXT("Actual strict navigation recovered before Observation publication"));
        }
        RejectRefresh(Source, TEXT("Observation stays rejected while original strict navigation is not Ready"));
        if (!bGood)
            return Finish(&Source, TEXT("Unexpected publication before strict navigation recovered"));
        if (FPlatformTime::Seconds()-NavigationRecoveryStarted > 30)
        {
            Check(TEXT("Original strict navigation recovers within bounded 30 seconds"), false);
            Rows += TEXT("Classification=C Original strict Ready did not recover before bounded timeout\n");
            RecordNavigationState(Source, TEXT("CaseCRecoveryTimeout"));
            return Finish(&Source, TEXT("Case C: original navigation recovery timed out"));
        }
        return false;
    }

    bool Update() override;
};

bool FRun::Update()
{
    UWorld* World = GEditor ? GEditor->PlayWorld.Get() : nullptr;
    if (Case == TEXT("WorldEnd") && Stage == 1)
    {
        if (World) return false;
        CheckCallback(TEXT("Real PIE World termination callback executed"));
        return Finish(nullptr, TEXT("PIE ended before inspection of retained callback evidence"));
    }
    if (FPlatformTime::Seconds() - Started > 210)
    {
        Check(TEXT("Bounded original G2 Ready fixture"), false);
        return Finish(BoundSource.Get(), TEXT("PIE or strict Ready fixture timed out"));
    }
    if (!World) return false;
    ARogueliteCombatSliceRig* Source = nullptr;
    for (TActorIterator<ARogueliteCombatSliceRig> It(World); It; ++It) { Source = *It; break; }
    if (!Source) return false;
    BoundSource = Source;
    if (Case == TEXT("StrictAndStrongSemantics") && Stage == 2)
        return AwaitNavigationRecovery(*Source);
    if (Case == TEXT("IndependentWritersAndWatch") && Stage == 4)
    {
        const bool Ready = Source->IsDungeonReady();
        RecordNavigationState(*Source, TEXT("IndependentFactoryCleanupRecovery"), Ready);
        if (Ready)
        {
            if (!Publish(*Source, TEXT("Factory test-owned instance removed before fresh full Ready publication")))
                return Finish(Source, TEXT("Strict recovered but factory cleanup observation rejected"));
            return BeginWatchObstacleChecks(*Source);
        }
        RejectRefresh(*Source, TEXT("Factory cleanup cannot publish before actual strict navigation Ready"));
        if (!bGood || FPlatformTime::Seconds()-NavigationRecoveryStarted > 30)
        {
            Check(TEXT("Factory cleanup original strict navigation recovers within 30 seconds"), false);
            return Finish(Source, TEXT("Independent factory cleanup recovery failed"));
        }
        return false;
    }

    if (Case == TEXT("IndependentWritersAndWatch") && Stage == 1)
    {
        if (Source->Batch.bSucceeded) return false;
        Check(TEXT("Original Watch detects retained unregistered blocker and clears batch"), Source->Batch.OwnedActors.IsEmpty());
        Revoked(*Source, SavedEpoch, TEXT("Watch invalidates old observation"));
        RejectRefresh(*Source, TEXT("Cleared Source cannot republish"));
        Rows += FString::Printf(TEXT("ActualWatchDetectionWallSeconds=%.6f\n"), FPlatformTime::Seconds() - WindowStarted);
        return Finish(Source, TEXT("Watch window measured, not treated as synchronous detection"));
    }
    if (!Source->SliceFailure.IsEmpty())
    {
        Check(TEXT("Original G2 fixture generated successfully"), false);
        Rows += TEXT("FixtureFailure=") + Source->SliceFailure + TEXT("\n");
        return Finish(Source, TEXT("Original fixture failure"));
    }
    if (!Source->bPlayerBridgeReady || !Source->IsDungeonReady()) return false;
    if (Case == TEXT("FormalSourceLifecycle") && Stage == 1)
    {
        Check(TEXT("Real replacement batch gets a different identity"), Source->Batch.BatchId != SavedBatch);
        Check(TEXT("New batch has both complete AwaitingEntry encounters"),
            Source->Encounter && Source->SecondEncounter &&
            Source->Encounter->GetState() == ERogueliteSliceEncounterState::AwaitingEntry &&
            Source->SecondEncounter->GetState() == ERogueliteSliceEncounterState::AwaitingEntry);
        Publish(*Source, TEXT("Only fresh fully Ready replacement batch can publish"));
        Revoked(*Source, SavedEpoch, TEXT("Replacement never reauthorizes original token"));
        return Finish(Source, TEXT("Formal virtual Clear/Generate replacement completed"));
    }
    Check(TEXT("Exact native G2 Source fixture"), Source->GetClass() == ARogueliteCombatSliceRig::StaticClass());
    Check(TEXT("Two complete existing G2 encounters"), Source->Encounter && Source->SecondEncounter && Source->bEnableTwoCombatRooms);
    if (!bGood) return Finish(Source, TEXT("Refuse incomplete G2 domain fixture"));
    Rows += FString::Printf(TEXT("World=%s Seed=%d Batch=%s\n"), *World->GetPathName(), Source->Seed, *Source->Batch.BatchId.ToString());
    Check(TEXT("Permanent default remains DeveloperStrict"), Source->GetGameplayReadMode() == ERogueliteGameplayReadMode::DeveloperStrict);
    if (Case == TEXT("StrictAndStrongSemantics"))
        RecordNavigationState(*Source, TEXT("FirstActualNavigationReady"));
    Differential(*Source, TEXT("All strict Snapshot fields and Reason equal original"));
    Source->SetGameplayReadMode(ERogueliteGameplayReadMode::TrackedGameplay);
    if (!Publish(*Source, TEXT("Explicit fresh strong query publishes original Ready state")))
        return Finish(Source, TEXT("Observation admission unavailable"));
    const uint64 Epoch = Source->GetGameplayObservationEpoch();
    if (Case == TEXT("StrictAndStrongSemantics"))
        RecordNavigationState(*Source, TEXT("InitialObservationPublished"));

    if (Case == TEXT("StrictAndStrongSemantics"))
    {
        auto* Room = Source->Batch.Rooms[0].Room.Get();
        const float Length = Room->RoomLength;
        Room->RoomLength += 10;
        // Raw parameter edits are not a controlled writer. Preserve the original RegistrationAudit boundary.
        const auto Snapshot = Differential(*Source, TEXT("Raw RoomLength preserves every old Snapshot field"));
        Check(TEXT("Known raw-parameter boundary remains Current true, Ready false"), Snapshot.bCurrent && !Snapshot.bNavigationReady);
        Revoked(*Source, Epoch, TEXT("Original strong rejection revokes observation despite bCurrent true"));
        RejectRefresh(*Source, TEXT("Current true without full Ready cannot publish"));
        Room->RoomLength = Length;
        Publish(*Source, TEXT("Restored geometry requires fresh full strong success"));
        UStaticMeshComponent* Floor = nullptr;
        TInlineComponentArray<UStaticMeshComponent*> Parts;
        Room->GetComponents(Parts);
        for (auto* Part : Parts) if (Part->GetFName() == FName(TEXT("Floor"))) Floor = Part;
        Check(TEXT("Actual registered room floor located"), Floor != nullptr);
        if (Floor)
        {
            Check(TEXT("Mutation target is a real registered Static floor"), Floor->IsRegistered() && Floor->Mobility == EComponentMobility::Static);
            Rows += FString::Printf(TEXT("FloorIdentity=%s ObjectId=%u Registered=%d Mobility=%d CollisionBefore=%d\n"),
                *Floor->GetPathName(), Floor->GetUniqueID(), Floor->IsRegistered(), int32(Floor->Mobility), int32(Floor->GetCollisionEnabled()));
            const auto Collision = Floor->GetCollisionEnabled();
            const uint64 BeforeCollision = Source->GetGameplayObservationEpoch();
            RecordNavigationState(*Source, TEXT("BeforeActualStaticCollisionChange"));
            Floor->SetCollisionEnabled(ECollisionEnabled::NoCollision);
            RecordNavigationState(*Source, TEXT("ActualStaticCollisionDisabled"));
            const auto Fault = Differential(*Source, TEXT("Actual floor collision mutation preserves strict Snapshot failure semantics"));
            Check(TEXT("Original full Ready rejects changed floor collision"), !Fault.bNavigationReady);
            Revoked(*Source, BeforeCollision, TEXT("Strong floor-collision failure revokes old observation"));
            RejectRefresh(*Source, TEXT("Changed registered floor collision cannot publish"));
            Floor->SetCollisionEnabled(Collision);
            RecordNavigationState(*Source, TEXT("ActualStaticCollisionRestoredSameTick"));
            Check(TEXT("Actual Static collision setting restored before yielding"), Floor->GetCollisionEnabled() == Collision);
            SavedEpoch = BeforeCollision;
            SavedBatch = Source->Batch.BatchId;
            NavigationRecoveryStarted = FPlatformTime::Seconds();
            bSawStrictNotReady = !Source->IsDungeonReady();
            bSawNavigationPending = UNavigationSystemV1::IsNavigationBeingBuiltOrLocked(World);
            auto* Navigation = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
            bSawNavigationRunning = Navigation && Navigation->IsNavigationBuildInProgress();
            if (!Source->IsDungeonReady())
                RejectRefresh(*Source, TEXT("Observation rejected during same-tick collision recovery navigation work"));
            Stage = 2;
            return false;
        }
        return Finish(Source, TEXT("Actual registered Floor component unavailable"));
    }
    else if (Case == TEXT("ControlledNavigation") || Case == TEXT("FIX02OccupancyFixtures"))
    {
        Callback.Reset(NewObject<URogueliteTrackedGameplayCallback>());
        Callback->Source = Source;
        Callback->PreviousEpoch = Epoch;
        int32 Bound = 0;
        for (TActorIterator<ARogueliteRuntimeNavigationBounds> It(World); It; ++It)
            if (It->GetOwner() == Source)
            {
                It->OnDestroyed.AddDynamic(Callback.Get(), &URogueliteTrackedGameplayCallback::ActorDestroyed);
                ++Bound;
            }
        Check(TEXT("Actual navigation bounds destruction callback bound"), Bound > 0);
        Source->NavigationReadiness->Cancel();
        CheckCallback(TEXT("Direct public Cancel destroyed its actual bounds"));
        Revoked(*Source, Epoch, TEXT("Direct Cancel invalidates observation synchronously"));
        RejectRefresh(*Source, TEXT("Cancelled original navigation cannot publish"));
        Differential(*Source, TEXT("Cancel Snapshot and Reason equal original"));
        const uint64 CancelEpoch = Source->GetGameplayObservationEpoch();
        const bool Restarted = Source->NavigationReadiness->Start(Source->LastGeometry.GeometrySeconds);
        Rows += FString::Printf(TEXT("PublicStartReturned=%d\n"), Restarted);
        Check(TEXT("Public Start revokes before its navigation pending writes"), Source->GetGameplayObservationEpoch() != CancelEpoch);
        RejectRefresh(*Source, TEXT("NavigationPending cannot publish"));
        Source->NavigationReadiness->Cancel();
        TArray<FOccupancyEvidence> Occupancy;
        int32 Blocking = 0, LeasedDormant = 0;
        for (const auto& Edge : Source->Batch.Edges)
        {
            const auto Evidence = RecordOccupancy(*Source, Edge, TEXT("OriginalOccupiedG2BeforeBatchRevalidate"));
            Occupancy.Add(Evidence); Blocking += Evidence.BlockingHits; LeasedDormant += Evidence.LeasedDormantHits;
        }
        Check(TEXT("Occupied batch negative has actual physical blocking hits"), Blocking > 0);
        Check(TEXT("Actual blockers include Dormant Enemy with a valid pre-revalidation G1A lease"), LeasedDormant > 0);
        const uint64 BeforeRevalidate = Source->GetGameplayObservationEpoch();
        const int32 Revision = Source->CommittedLoops.GeometryRevision;
        FString Why;
        const bool Revalidated = FRogueliteRuntimeLoopAssembler::RevalidateConfigured(Source, Source->Batch, Source->Settings, Source->CommittedLoops, Why);
        Check(TEXT("Original complete C2 batch rejects actual occupied room boxes"), !Revalidated);
        Check(TEXT("Real independent batch call advances revision, without restoring a failed batch"), Source->CommittedLoops.GeometryRevision == Revision+1);
        Check(TEXT("Controlled C2 calls revoke before independent batch result can be consumed"), Source->GetGameplayObservationEpoch() != BeforeRevalidate);
        for (int32 Index=0; Index<Source->Batch.Edges.Num(); ++Index)
        {
            const auto& Edge = Source->Batch.Edges[Index];
            const auto& Result = Edge.Validation;
            Check(TEXT("Actual edge result agrees with its unchanged occupancy query"), Result.bSucceeded == (Occupancy[Index].BlockingHits == 0));
            Check(TEXT("Occupied negative retains original seam and Pawn capsule probes"), Result.bGeometryPassed && Result.bCapsulePassed);
            Rows += FString::Printf(TEXT("OccupiedBatchEdge Edge=%d Child=%d Connection=%s BlockingHits=%d PreLeaseDormantHits=%d Result=%d Reason=%s\n"),
                Edge.EdgeId, Edge.ChildNode, *Edge.ConnectionId.ToString(), Occupancy[Index].BlockingHits, Occupancy[Index].LeasedDormantHits, Result.bSucceeded, *Result.Message);
        }
        Rows += TEXT("IndependentOccupiedBatchRevalidateReason=")+Why+TEXT("\n");
        Revoked(*Source, Epoch, TEXT("Rejected actual occupied batch cannot retain old observed authorization"));
        RejectRefresh(*Source, TEXT("Configured batch rejection cannot silently restore strict navigation Ready"));
        Differential(*Source, TEXT("Independent occupied batch rejection preserves original Snapshot and Reason"));
        if (!bGood) return Finish(Source, TEXT("Occupied fixture evidence failed; do not attempt positive"));
        CheckIndependentEmptyBatch(*Source);
        return Finish(nullptr, TEXT("Occupied rejection and independent complete unoccupied batch separately verified"));
    }
    else if (Case == TEXT("ControlledConnectionsAndBridge"))
    {
        auto* Registry = Source->Batch.Registry.Get();
        const auto* FinalEdge = Source->Batch.Edges.FindByPredicate([&](const auto& Edge)
        {
            const auto* Child = Source->Batch.Rooms.FindByPredicate([&](const auto& Room) { return Room.NodeId == Edge.ChildNode; });
            return Child && Child->RoomType == ERogueliteSeedRoomType::Final;
        });
        Check(TEXT("Single-connection positive selects actual Final child instead of occupied Combat"), FinalEdge != nullptr);
        if (!FinalEdge) return Finish(Source, TEXT("No legal Final edge"));
        const FGuid Connection = FinalEdge->ConnectionId;
        Check(TEXT("Selected Final edge has no actual C2 occupancy blockers"), RecordOccupancy(*Source, *FinalEdge, TEXT("SingleConnectionPositive")).BlockingHits == 0);
        const auto Revalidation = Registry->RevalidateConfiguredConnection(Connection);
        Check(TEXT("Actual configured C2 revalidation retains original probes"), Revalidation.bSucceeded && Revalidation.bCapsulePassed && Revalidation.bGeometryPassed);
        Revoked(*Source, Epoch, TEXT("Direct C2 revalidation revokes old observation"));
        Publish(*Source, TEXT("Successful full Ready is required after C2 transaction"));
        FString Why;
        ARogueliteSliceGate* Gate = nullptr;
        for (TActorIterator<ARogueliteSliceGate> It(World); It; ++It)
            if (It->GetOwner() == Source && It->CanClose(Why)) { Gate = *It; break; }
        Check(TEXT("Actual existing room gate can close safely"), Gate != nullptr);
        if (Gate)
        {
            const uint64 BeforeOpenUnregister = Source->GetGameplayObservationEpoch();
            Check(TEXT("Direct G1A unregister operates on actual existing open gate lease"),
                FRogueliteDynamicGameplayBridge::UnregisterComponent(Source, Gate->GetBlocker()));
            Revoked(*Source, BeforeOpenUnregister, TEXT("Direct open-gate unregister revokes before lease writes"));
            Check(TEXT("Direct G1A registration restores same real NoCollision gate lease"),
                FRogueliteDynamicGameplayBridge::RegisterComponent(Source, Source->Batch.BatchId, Gate,
                    Gate->GetBlocker(), ERogueliteDynamicParticipant::Gate, Why));
            Publish(*Source, TEXT("Restored actual G1A lease requires fresh strong success"));
            Check(TEXT("Controlled gate closes through existing G1A path"), Gate->SetClosed(true, Why));
            Check(TEXT("Closed registered gate remains an isolated dynamic participant"),
                FRogueliteDynamicGameplayBridge::IsComponentIsolated(Source, Source->Batch.BatchId, Gate->GetBlocker()));
            Publish(*Source, TEXT("Fresh full Ready publishes while registered controlled gate is closed"));
            const uint64 BeforeUnregister = Source->GetGameplayObservationEpoch();
            Check(TEXT("Direct G1A unregister changes the actual colliding exclusion set"),
                FRogueliteDynamicGameplayBridge::UnregisterComponent(Source, Gate->GetBlocker()));
            Revoked(*Source, BeforeUnregister, TEXT("Actual G1A lease removal invalidates observation synchronously"));
            Check(TEXT("Removed colliding gate is no longer GeometryDigest-excluded"),
                !FRogueliteDynamicGameplayBridge::IsComponentIsolated(Source, Source->Batch.BatchId, Gate->GetBlocker()));
            RejectRefresh(*Source, TEXT("Unleased colliding gate cannot republish"));
            Differential(*Source, TEXT("G1A exclusion change retains original strong Snapshot failure"));
        }
        const uint64 BeforeRelease = Source->GetGameplayObservationEpoch();
        Check(TEXT("Actual C2 release remains owned batch-only"), Registry->ReleaseRuntimePlacedConnection(Connection, Source->Batch.BatchId));
        Check(TEXT("C2 release notification advances epoch even with observation already revoked"), Source->GetGameplayObservationEpoch() != BeforeRelease);
        RejectRefresh(*Source, TEXT("Missing authoritative connection cannot publish"));
        Rows += TEXT("C2ActualTransformCallbackCoverage=Not covered here; configured path deliberately performs no Static movement\n");
    }
    else if (Case == TEXT("FormalSourceLifecycle"))
    {
        Callback.Reset(NewObject<URogueliteTrackedGameplayCallback>());
        Callback->Source = Source;
        Callback->PreviousEpoch = Epoch;
        for (const auto& Enemy : Source->Encounter->GetEnemies())
            if (Enemy.IsValid()) Enemy->OnDestroyed.AddDynamic(Callback.Get(), &URogueliteTrackedGameplayCallback::ActorDestroyed);
        SavedBatch = Source->Batch.BatchId;
        SavedEpoch = Epoch;
        Check(TEXT("Actual virtual Rig Clear succeeds"), Source->ClearRuntime());
        CheckCallback(TEXT("Virtual Rig cleanup destroys real encounter enemies"));
        Check(TEXT("Formal Clear resets gameplay and source batch identities"), !Source->bPlayerBridgeReady && !Source->GameplayBatchId.IsValid() && Source->Batch.OwnedActors.IsEmpty());
        RejectRefresh(*Source, TEXT("Cleared incomplete G2 domain cannot publish"));
        int32 PreSpawnCalls = 0;
        bool RevokedBeforeSpawn = true, NoPublishDuringSpawn = true;
        const uint64 BeforeGenerate = Source->GetGameplayObservationEpoch();
        const auto Handle = World->AddOnActorPreSpawnInitialization(FOnActorSpawned::FDelegate::CreateLambda([&](AActor* Actor)
        {
            ++PreSpawnCalls;
            const uint64 CallbackEpoch = Source->GetGameplayObservationEpoch();
            const bool IsPlayerSpawn = Actor && Actor->IsA<ARogueliteSlicePlayer>();
            FString StatusReason, Reason;
            RevokedBeforeSpawn &= !Source->IsGameplayObservationEpochCurrent(SavedEpoch);
            NoPublishDuringSpawn &= Source->GetGameplayObservationStatus(StatusReason) != ERogueliteGameplayObservationStatus::ValidObservation;
            NoPublishDuringSpawn &= !Source->RefreshGameplayObservation(Reason);
            if (!IsPlayerSpawn) RevokedBeforeSpawn &= CallbackEpoch != BeforeGenerate;
            Rows += FString::Printf(TEXT("PreSpawn=%s PlayerBeforeBaseScope=%d OriginalPublished=%llu PostClear=%llu CallbackEpoch=%llu StatusReason=%s RefreshReason=%s\n"),
                *GetNameSafe(Actor), IsPlayerSpawn, SavedEpoch, BeforeGenerate, CallbackEpoch, *StatusReason, *Reason);
        }));
        const bool Generated = Source->GenerateRuntime();
        World->RemoveOnActorPreSpawnInitialization(Handle);
        Check(TEXT("Formal Source regeneration uses original planning"), Generated);
        Check(TEXT("Real pre-initialization spawn callbacks observed"), PreSpawnCalls > 0);
        Check(TEXT("Old published authorization stays revoked before every replacement spawn"), RevokedBeforeSpawn);
        Check(TEXT("No callback publishes a partially spawned replacement batch"), NoPublishDuringSpawn);
        if (!Generated) return Finish(Source, TEXT("Formal regeneration failure"));
        Stage = 1;
        return false;
    }
    else if (Case == TEXT("IndependentWritersAndWatch"))
    {
        FString Why;
        auto* Independent = ARogueliteRoomTemplate::CreateTemplateInstance(Source, Source->Batch.Rooms[0].TemplateClass,
            FTransform(FVector(100000, 100000, 0)), Why);
        Check(TEXT("Real independent public factory creates a valid owned-content instance"), IsValid(Independent));
        Rows += TEXT("IndependentFactoryReason=") + Why + TEXT("\n");
        if (Independent)
        {
            FString StatusReason;
            Rows += FString::Printf(TEXT("UncontrolledFactoryImmediateStatus=%d Epoch=%llu Current=%d Reason=%s\n"),
                int32(Source->GetGameplayObservationStatus(StatusReason)), Source->GetGameplayObservationEpoch(),
                Source->IsGameplayObservationEpochCurrent(Epoch), *StatusReason);
            // The revised contract permits a Watch/critical-strong window for this independent legal API.
            RejectRefresh(*Source, TEXT("Next original strong validation detects independent factory geometry"));
            Revoked(*Source, Epoch, TEXT("Strong rejection revokes previous independent-factory observation"));
            Independent->Destroy();
            if (!Source->IsDungeonReady())
            {
                Stage = 4;
                NavigationRecoveryStarted = FPlatformTime::Seconds();
                RecordNavigationState(*Source, TEXT("IndependentFactoryRemovedNavigationPending"));
                RejectRefresh(*Source, TEXT("Factory cleanup cannot publish before actual strict navigation Ready"));
                return false;
            }
            Publish(*Source, TEXT("Factory test-owned instance removed before fresh full Ready publication"));
        }
        return BeginWatchObstacleChecks(*Source);
    }
    else if (Case == TEXT("ConservativeAdmission"))
    {
        auto* Other = SpawnInactiveSource(*World, ARogueliteTrackedGameplayUnknownSource::StaticClass());
        Check(TEXT("Independent unknown native Source is a real actor"), IsValid(Other));
        if (Other)
        {
            Other->SetGameplayReadMode(ERogueliteGameplayReadMode::TrackedGameplay);
            RejectRefresh(*Other, TEXT("Unknown native Source stays strict"));
            RejectRefresh(*Source, TEXT("Multiple Sources cannot publish single-authority observation"));
            auto* OriginalReadiness = Source->NavigationReadiness.Get();
            Source->NavigationReadiness = Other->NavigationReadiness;
            RejectRefresh(*Source, TEXT("Readiness component owned by another Source cannot publish"));
            Differential(*Source, TEXT("Foreign readiness owner preserves original strict Snapshot failure"));
            Source->NavigationReadiness = OriginalReadiness;
            Differential(*Other, TEXT("Unknown native Source preserves original full failure Reason"));
            Other->Destroy();
        }
        Publish(*Source, TEXT("Single exact native source can republish only after fresh strong validation"));
        auto* Blueprint = FKismetEditorUtilities::CreateBlueprint(ARogueliteRuntimeLoopAssembly::StaticClass(), GetTransientPackage(),
            MakeUniqueObjectName(GetTransientPackage(), UBlueprint::StaticClass(), TEXT("PERF21B_UnknownSource")),
            BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass());
        Check(TEXT("Transient unknown Blueprint Source class created without asset save"), Blueprint && Blueprint->GeneratedClass);
        if (Blueprint && Blueprint->GeneratedClass)
        {
            auto* BlueprintSource = SpawnInactiveSource(*World, Blueprint->GeneratedClass);
            Check(TEXT("Real Blueprint-derived source spawned"), IsValid(BlueprintSource));
            if (BlueprintSource)
            {
                BlueprintSource->SetGameplayReadMode(ERogueliteGameplayReadMode::TrackedGameplay);
                RejectRefresh(*BlueprintSource, TEXT("Unknown Blueprint Source cannot inherit observation admission"));
                Differential(*BlueprintSource, TEXT("Unknown Blueprint complete strict Snapshot semantics"));
                BlueprintSource->Destroy();
            }
        }
        Publish(*Source, TEXT("Unknown source cleanup restores original exact native domain"));
        const bool OriginalBridge = Source->bPlayerBridgeReady;
        const uint64 BeforeBridgeChange = Source->GetGameplayObservationEpoch();
        Source->bPlayerBridgeReady = false;
        Revoked(*Source, BeforeBridgeChange, TEXT("Missing real PlayerBridgeReady prevents observation consumption"));
        RejectRefresh(*Source, TEXT("Incomplete Player bridge cannot publish"));
        Source->bPlayerBridgeReady = OriginalBridge;
        Publish(*Source, TEXT("Restored complete bridge requires fresh strong publication"));
        Source->SecondEncounter->Shutdown(TEXT("Test actual direct Cancelled encounter admission"));
        RejectRefresh(*Source, TEXT("Actual cancelled Encounter refuses publication even before Rig Clear"));
    }
    else if (Case == TEXT("WriteScopeAndReentry"))
    {
        {
            FRogueliteGameplayObservationWrite Outer(World, TEXT("PERF21B real outer write interval"));
            Revoked(*Source, Epoch, TEXT("World-scoped mutation revokes synchronously at entry"));
            RejectRefresh(*Source, TEXT("Outer controlled interval forbids republish"));
            {
                FRogueliteGameplayObservationWrite Nested(World, TEXT("PERF21B nested write interval"));
                RejectRefresh(*Source, TEXT("Nested controlled interval forbids republish"));
            }
            RejectRefresh(*Source, TEXT("Ending inner interval does not release outer protection"));
        }
        Revoked(*Source, Epoch, TEXT("Leaving scope cannot reauthorize old epoch by itself"));
        Publish(*Source, TEXT("Only explicit fresh strong publication restores observation"));
        {
            FRogueliteGameplayObservationWrite UnknownWorld(nullptr, TEXT("PERF21B unknown World scope"));
            RejectRefresh(*Source, TEXT("Unknown World impact conservatively forbids observation publication"));
        }
        Check(TEXT("Unknown World remains DeveloperStrict after the scope exits"),
            Source->GetGameplayReadMode() == ERogueliteGameplayReadMode::DeveloperStrict);
        Source->SetGameplayReadMode(ERogueliteGameplayReadMode::TrackedGameplay);
        Publish(*Source, TEXT("Explicit mode re-entry still requires fresh strong publication"));
        auto* Actor = World->SpawnActor<AActor>();
        Check(TEXT("Test-owned virtual-getter actor created"), Actor != nullptr);
        if (Actor)
        {
            auto* Getter = NewObject<URogueliteTrackedGameplayGetter>(Actor);
            Actor->AddInstanceComponent(Getter);
            Actor->SetRootComponent(Getter);
            Getter->SetMobility(EComponentMobility::Movable);
            Getter->SetCanEverAffectNavigation(false);
            Getter->SetCollisionEnabled(ECollisionEnabled::NoCollision);
            Getter->RegisterComponent();
            Getter->Source = Source;
            Getter->bAttemptRefresh = true;
            const float SavedLength = Source->Batch.Rooms[0].Room->RoomLength;
            Getter->bChangeRoomLength = true;
            Source->SetGameplayReadMode(ERogueliteGameplayReadMode::DeveloperStrict);
            // Component registration is fixture setup, not part of either measured Snapshot.
            Getter->Calls = Getter->ReentryCalls = 0;
            Getter->bChanged = false;
            Getter->bEveryNestedRefreshRejected = true;
            const auto Expected = OriginalSnapshot(*Source);
            const int32 ExpectedCalls = Getter->Calls;
            const float ExpectedLength = Source->Batch.Rooms[0].Room->RoomLength;
            Source->Batch.Rooms[0].Room->RoomLength = SavedLength;
            Getter->Calls = Getter->ReentryCalls = 0;
            Getter->bChanged = false;
            Getter->bEveryNestedRefreshRejected = true;
            Source->SetGameplayReadMode(ERogueliteGameplayReadMode::TrackedGameplay);
            const auto Actual = Source->GetTraversalSnapshot();
            Check(TEXT("Unknown getter fallback retains every strict Snapshot field and Reason"),
                FRogueliteLoopTraversalSnapshot::StaticStruct()->CompareScriptStruct(&Expected, &Actual, 0));
            Check(TEXT("Unknown getter fallback retains original virtual query count"), Getter->Calls == ExpectedCalls);
            Check(TEXT("Unknown getter fallback retains actual original mutation result"), Source->Batch.Rooms[0].Room->RoomLength == ExpectedLength);
            Rows += FString::Printf(TEXT("GetterExpectedCalls=%d ActualCalls=%d Expected=%s\nActual=%s\n"),
                ExpectedCalls, Getter->Calls, *SnapshotText(Expected), *SnapshotText(Actual));
            Check(TEXT("Original virtual getter really ran"), Getter->Calls > 0);
            Check(TEXT("Getter produced its real parameter side effect"), Getter->bChanged && Source->Batch.Rooms[0].Room->RoomLength != SavedLength);
            Check(TEXT("Real virtual getter attempted synchronous reentry"), Getter->ReentryCalls > 0);
            Check(TEXT("Every real nested publication attempt rejected"), Getter->bEveryNestedRefreshRejected);
            RejectRefresh(*Source, TEXT("Unknown getter and side-effect geometry cannot publish"));
            Getter->bAttemptRefresh = Getter->bChangeRoomLength = false;
            Source->Batch.Rooms[0].Room->RoomLength = SavedLength;
            Actor->Destroy();
        }
        Publish(*Source, TEXT("Getter fixture cleanup requires original full Ready success"));
    }
    else if (Case == TEXT("WorldEnd"))
    {
        Callback.Reset(NewObject<URogueliteTrackedGameplayCallback>());
        Callback->Source = Source;
        Callback->PreviousEpoch = Epoch;
        Source->OnEndPlay.AddDynamic(Callback.Get(), &URogueliteTrackedGameplayCallback::ActorEnded);
        GEditor->RequestEndPlayMap();
        Stage = 1;
        return false;
    }
    else
    {
        Check(TEXT("Known independent Phase1 Source case"), false);
    }
    return Finish(Source, TEXT("Independent Phase1 Source contract checks finished"));
}
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FRogueliteTrackedGameplaySourcePIETest,
    "ProjectVerification.PERF21B.SourceObservation", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

void FRogueliteTrackedGameplaySourcePIETest::GetTests(TArray<FString>& Names, TArray<FString>& Commands) const
{
    Names = {TEXT("StrictAndStrongSemantics"), TEXT("ControlledNavigation"), TEXT("ControlledConnectionsAndBridge"),
        TEXT("FormalSourceLifecycle"), TEXT("IndependentWritersAndWatch"), TEXT("ConservativeAdmission"),
        TEXT("WriteScopeAndReentry"), TEXT("WorldEnd")};
    Commands = Names;
}

bool FRogueliteTrackedGameplaySourcePIETest::RunTest(const FString& Parameters)
{
    if (!GEditor || GEditor->PlayWorld)
    {
        AddError(TEXT("Refuse to replace an existing user PIE session"));
        return false;
    }
    FAutomationEditorCommonUtils::LoadMap(TrackedGameplaySourceTests::Map);
    ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
    FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<TrackedGameplaySourceTests::FRun>(this, Parameters));
    ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRogueliteTrackedOccupancyFixtureTest,
    "ProjectVerification.PERF21B.FIX02.OccupancyFixtures", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FRogueliteTrackedOccupancyFixtureTest::RunTest(const FString&)
{
    if (!GEditor || GEditor->PlayWorld) { AddError(TEXT("Refuse to replace existing PIE")); return false; }
    FAutomationEditorCommonUtils::LoadMap(TrackedGameplaySourceTests::Map);
    ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
    FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<TrackedGameplaySourceTests::FRun>(this, TEXT("FIX02OccupancyFixtures")));
    ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
    return true;
}

#endif
