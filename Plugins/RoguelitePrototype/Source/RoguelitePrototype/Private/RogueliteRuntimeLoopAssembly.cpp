#include "RogueliteRuntimeLoopAssembly.h"
#include "Gameplay/RogueliteGameplayObservation.h"
#include "Gameplay/RogueliteCombatSliceEncounter.h"
#include "Gameplay/RogueliteCombatSlicePlayer.h"
#include "HAL/IConsoleManager.h"
#include "UObject/UObjectIterator.h"
#include <atomic>

/* PERF1A_BEGIN */
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "ProfilingDebugging/MiscTrace.h"
/* PERF1A_END */
#include "RogueliteSeedAssemblyInternal.h"
#include "RoguelitePrototype.h"
#include "RogueliteFloorProfile.h"
#include "RogueliteReadyQueryProof.h"
#include "Tests/RogueliteCombatSliceRig.h"
#include "ProfilingDebugging/CountersTrace.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "EngineUtils.h"
#include "Components/PrimitiveComponent.h"
#include "Components/StaticMeshComponent.h"
#include "RogueliteRoomTemplate.h"
#include "RogueliteRuntimeNavigationSystem.h"
#include "NavigationSystem.h"
#include "UObject/TopLevelAssetPath.h"

TRACE_DECLARE_INT_COUNTER(PerfReadyProofProduced,TEXT("PERF1B.ProofProduced"));
TRACE_DECLARE_INT_COUNTER(PerfReadyProofConsumed,TEXT("PERF1B.ProofConsumed"));
TRACE_DECLARE_INT_COUNTER(PerfReadyProofRejected,TEXT("PERF1B.ProofRejected"));
TRACE_DECLARE_INT_COUNTER(PerfReadyLegacySource,TEXT("PERF1B.Legacy.UnknownSource"));
TRACE_DECLARE_INT_COUNTER(PerfReadyLegacyComponent,TEXT("PERF1B.Legacy.UnknownReadiness"));
TRACE_DECLARE_INT_COUNTER(PerfReadyLegacyNavigation,TEXT("PERF1B.Legacy.UnknownNavigation"));
TRACE_DECLARE_INT_COUNTER(PerfTraversalProduced,TEXT("PERF1D1.TraversalProduced"));
TRACE_DECLARE_INT_COUNTER(PerfTraversalConsumed,TEXT("PERF1D1.TraversalConsumed"));
TRACE_DECLARE_INT_COUNTER(PerfTraversalRejected,TEXT("PERF1D1.TraversalRejected"));
TRACE_DECLARE_INT_COUNTER(PerfTraversalLegacy,TEXT("PERF1D1.TraversalLegacy"));

namespace RogueliteObservedGameplay
{
TAutoConsoleVariable<int32> RequestedMode(TEXT("roguelite.PERF21B.Mode"),0,
    TEXT("Prototype only. 0 Strict, 1 Tracked. Applied to newly started PIE Sources; not saved."),ECVF_Default);
TMap<FObjectKey,int32> Writes;
int32 UnknownWorldWrites=0;
std::atomic<uint64> UnsupportedSerial{0};
std::atomic<int32> UnsupportedWrites{0};
bool Writing(UWorld* World)
{
    return UnknownWorldWrites>0||UnsupportedWrites.load()>0||Writes.FindRef(FObjectKey(World))>0;
}
}
TRACE_DECLARE_INT_COUNTER(PerfObservedPublished,TEXT("PERF21B.Published"));
TRACE_DECLARE_INT_COUNTER(PerfObservedRevoked,TEXT("PERF21B.Revoked"));
TRACE_DECLARE_INT_COUNTER(PerfObservedHit,TEXT("PERF21B.ReadSuccess"));
TRACE_DECLARE_INT_COUNTER(PerfObservedMiss,TEXT("PERF21B.ReadInvalid"));
TRACE_DECLARE_INT_COUNTER(PerfObservedFallback,TEXT("PERF21B.ReadRequiresStrong"));
TRACE_DECLARE_INT_COUNTER(PerfObservedRefresh,TEXT("PERF21B.StrongRefresh"));
TRACE_DECLARE_INT_COUNTER(PerfObservedBegin,TEXT("PERF21B.TransactionBegin"));
TRACE_DECLARE_INT_COUNTER(PerfObservedEnd,TEXT("PERF21B.TransactionEnd"));

void FRogueliteGameplayObservationDeleter::operator()(FRogueliteGameplayObservationState* State) const
{
    delete State;
}

ARogueliteRuntimeLoopAssembly::ARogueliteRuntimeLoopAssembly()
{
    GameplayObservation.Reset(new FRogueliteGameplayObservationState());
    bGenerateOnBeginPlay=false;bEnableRuntimeNavigation=false;bRunRuntimeChecks=false;
    Tags.Add(TEXT("RogueliteRuntimeLoopTestOnly"));Planning.bEnableLoopAwarePlanning=true;
    ConfigureSource();
}
void ARogueliteRuntimeLoopAssembly::ConfigureSource()
{
    if(IsValid(FloorProfile))
    {
        const auto C=FloorProfile->ResolvePreview(Seed);
        if(C.bValid){Settings=C.Source;Planning=C.Planning;}
        else {RuntimeStatus=TEXT("ProfileRejected");LastFailureReason=C.ErrorCode+TEXT(": ")+C.Message;}
        return;
    }
    Settings=FRogueliteRuntimeLoopAssembler::SourcePreset(Seed,Preset==ERogueliteLoopTestPreset::FourRoomTree);
    Planning.bEnableLoopAwarePlanning=true;
    Planning.MinLoops=Planning.MaxLoops=Preset==ERogueliteLoopTestPreset::FourRoomTree?0:1;
}
void ARogueliteRuntimeLoopAssembly::PrepareTemplatesInEditor()
{
    if(!GetWorld()||GetWorld()->WorldType!=EWorldType::Editor
        ||(GetWorld()->GetOutermost()->GetName()!=TEXT("/Game/Roguelite/Maps/L_Roguelite_RuntimeLoopNavigation_Dev")
            &&!(IsValid(FloorProfile)&&GetWorld()->GetOutermost()->GetName()==TEXT("/Game/Roguelite/Maps/L_Roguelite_OpenPassageIntegration_Dev"))))return;
    if(IsValid(FloorProfile)&&!FloorProfile->ResolvePreview(Seed).bValid){RuntimeStatus=TEXT("ProfileRejected");return;}
    ConfigureSource();Modify();FString Reason;
    const bool Good=FRoguelitePreparedTemplateCatalog::CaptureEditor(this,Settings,PreparedTemplates,Reason);
    RuntimeStatus=Good?TEXT("模板数据已准备；PIE时安全生成"):TEXT("模板数据准备失败");LastFailureReason=Good?FString():Reason;
    UE_LOG(LogRoguelitePrototype,Display,TEXT("B4B3_EDITOR_CATALOG Ready=%d Digest=%s Rows=%d Reason=%s"),Good,*PreparedTemplates.Digest,PreparedTemplates.Rows.Num(),*Reason);
}
bool ARogueliteRuntimeLoopAssembly::GenerateRuntime()
{
    FRogueliteGameplayObservationWrite ObservationWrite(AActor::GetWorld(),TEXT("Source GenerateRuntime()"));
/* PERF1A_BEGIN */
    TRACE_CPUPROFILER_EVENT_SCOPE(PERF1A_Source_GenerateRuntime);
/* PERF1A_END */

    if(!GetWorld()||!GetWorld()->IsGameWorld()||!Batch.OwnedActors.IsEmpty())return false;
    ConfigureSource();NavigationReadiness->Cancel();bGeometryValidated=bDungeonReady=false;
    RuntimeStatus=TEXT("Planning");LastFailureReason.Reset();StageHistory.Reset();
    BatchSummary.Reset();NavigationSummary.Reset();MoveToSummary.Reset();
    bool Good=false;
    if(IsValid(FloorProfile))
    {
        const auto Config=FloorProfile->ResolvePreview(Seed);
        if(Config.bValid)Good=FRogueliteFloorProfileResolver::GenerateRuntime(this,Config,ERogueliteFloorValidationPurpose::DeveloperValidation,PreparedTemplates,Batch,CommittedLoops,LastGeometry);
        else {LastGeometry={};LastGeometry.Stage=Config.ErrorCode;LastGeometry.Reason=Config.Message;}
    }
    else Good=FRogueliteRuntimeLoopAssembler::Generate(this,Settings,Planning,PreparedTemplates,Batch,CommittedLoops,LastGeometry);
    if(!Good){RuntimeStatus=TEXT("Failed");LastFailureReason=LastGeometry.Reason;Stage=ERogueliteDungeonBuildStage::Failed;return false;}
    StageHistory={ERogueliteDungeonBuildStage::GraphGenerated,ERogueliteDungeonBuildStage::SpatialPlanValid,
        ERogueliteDungeonBuildStage::ActorsSpawned,ERogueliteDungeonBuildStage::GeometryValidated,ERogueliteDungeonBuildStage::NavigationPending};
    bGeometryValidated=true;Stage=ERogueliteDungeonBuildStage::NavigationPending;RuntimeStatus=LastGeometry.Stage;
    BatchSummary=FString::Printf(TEXT("Seed %d | %d房 / %d Tree廊 + %d Loop廊 | 环数%d | Batch %s"),Seed,Batch.Rooms.Num(),Batch.Edges.Num(),CommittedLoops.Edges.Num(),LastGeometry.Traversal.IndependentCycles,*Batch.BatchId.ToString());
    if(bNavigationEnabled)
    {
        RuntimeStatus=TEXT("NavigationPending");NavigationSummary=TEXT("NavigationPending：正在构建导航并执行路径与真实移动验证");NavigationReadiness->Start(LastGeometry.GeometrySeconds);
    }
    else NavigationSummary=TEXT("NavigationPending：本次仅检查几何，未发布DungeonReady");
    GetWorld()->GetTimerManager().SetTimer(WatchTimer,this,&ARogueliteRuntimeLoopAssembly::WatchGeometry,.25,true);
    return true;
}
bool ARogueliteRuntimeLoopAssembly::ClearRuntime()
{
    FRogueliteGameplayObservationWrite ObservationWrite(AActor::GetWorld(),TEXT("Source ClearRuntime()"));
/* PERF1A_BEGIN */
    TRACE_CPUPROFILER_EVENT_SCOPE(PERF1A_Source_ClearRuntime);
/* PERF1A_END */

/* PERF1A_BEGIN */
    TRACE_BOOKMARK(TEXT("PERF1A SourceClearBegin"));
/* PERF1A_END */

    if(!GetWorld()||!GetWorld()->IsGameWorld())return false;
    GetWorld()->GetTimerManager().ClearTimer(WatchTimer);NavigationReadiness->Cancel();bDungeonReady=false;
    FString Reason;const bool Good=FRogueliteRuntimeLoopAssembler::Clear(this,Batch,CommittedLoops,Reason);
    if(Good){bGeometryValidated=false;Stage=ERogueliteDungeonBuildStage::Idle;RuntimeStatus=TEXT("Cancelled / 自有批次已清理");}
    else LastFailureReason=Reason;
    UE_LOG(LogRoguelitePrototype,Display,TEXT("B4B3_CLEAR Success=%d Owned=%d Loop=%d Reason=%s"),Good,Batch.OwnedActors.Num(),CommittedLoops.Edges.Num(),*Reason);return Good;
}
bool ARogueliteRuntimeLoopAssembly::IsDungeonReady() const
{
    const auto OriginalReady=[&]() -> bool
    {
/* PERF1A_BEGIN */
    TRACE_CPUPROFILER_EVENT_SCOPE(PERF1A_Source_IsDungeonReady);
/* PERF1A_END */
    if(!IsInGameThread())return false;
    if(bReadyQueryInProgress)
    {
        if(ActiveTraversalRequest)ActiveTraversalRequest->bTraversalInvalidated=true;
        return false;
    }
    TGuardValue<bool> QueryGuard(bReadyQueryInProgress,true);
    if(!bDungeonReady||!IsValid(NavigationReadiness))return false;
    UWorld* World=GetWorld();
    // This applies to Legacy too: another Source's Ready component is not evidence for this Source.
    if(!IsValid(World)||!World->IsGameWorld()||World->bIsTearingDown
        ||NavigationReadiness->GetOwner()!=this||NavigationReadiness->GetWorld()!=World
        ||!Batch.BatchId.IsValid()||!IsValid(Batch.Registry)||Batch.Registry->GetWorld()!=World)return false;
    if(IsNativeReadyProofClass(GetClass()))
    {
        if(NavigationReadiness->GetClass()==URogueliteNavigationReadinessComponent::StaticClass())
        {
            FRogueliteReadyQueryProof Proof;
            const auto Admission=NavigationReadiness->PrepareReadyQuery(*this,Proof);
            if(Admission==ERogueliteReadyProofAdmission::Rejected){TRACE_COUNTER_INCREMENT(PerfReadyProofRejected);return false;}
            if(Admission==ERogueliteReadyProofAdmission::Native)
            {
                TGuardValue<const FRogueliteReadyQueryProof*> ProofGuard(ActiveReadyQueryProof,&Proof);
                if(!ProduceReadyQueryProof(Proof)){TRACE_COUNTER_INCREMENT(PerfReadyProofRejected);return false;}
                const bool Ready=NavigationReadiness->ConsumeReadyQuery(*this,Proof);
                if(Ready){TRACE_COUNTER_INCREMENT(PerfReadyProofConsumed);}else{TRACE_COUNTER_INCREMENT(PerfReadyProofRejected);}
                auto* Request=Proof.TraversalRequest;
                if(Ready&&Request&&ActiveTraversalRequest==Request&&Request->Source==this
                    &&Request->bTraversalAdmitted&&!Request->bTraversalInvalidated&&!Request->bProduced
                    &&!Request->bConsumed&&Proof.Traversal.IsSet())
                {
                    TRACE_CPUPROFILER_EVENT_SCOPE(PERF1D1_ProofTransfer);
                    // Move the consumed proof's binding and value into its caller's
                    // current stack. Nothing is retained on the Actor after RAII.
                    Request->SourceKey=Proof.SourceKey;Request->Readiness=Proof.Readiness;
                    Request->ReadinessKey=Proof.ReadinessKey;Request->World=Proof.World;
                    Request->WorldKey=Proof.WorldKey;Request->Registry=Proof.Registry;Request->RegistryKey=Proof.RegistryKey;
                    Request->NavData=Proof.NavData;Request->NavDataKey=Proof.NavDataKey;
                    Request->Navigation=Proof.Navigation;Request->NavigationKey=Proof.NavigationKey;
                    Request->NavigationDataKeys=MoveTemp(Proof.NavigationDataKeys);
                    Request->Batch=Proof.Batch;Request->Settings=Proof.Settings;Request->Loops=Proof.Loops;
                    Request->Report=Proof.Report;Request->BatchId=Proof.BatchId;
                    Request->GraphRevision=Proof.GraphRevision;Request->GeometryRevision=Proof.GeometryRevision;
                    Request->ConfigurationDigest=MoveTemp(Proof.ConfigurationDigest);
                    Request->GraphDigest=MoveTemp(Proof.GraphDigest);Request->LayoutDigest=MoveTemp(Proof.LayoutDigest);
                    Request->GeometryDigest=MoveTemp(Proof.GeometryDigest);Request->Epoch=MoveTemp(Proof.Epoch);
                    Request->WorldPath=MoveTemp(Proof.WorldPath);Request->SourceTransform=Proof.SourceTransform;
                    Request->Traversal=MoveTemp(Proof.Traversal);Request->bProduced=true;
                    Request->bTraversalDelivered=true;TRACE_COUNTER_INCREMENT(PerfTraversalProduced);
                }
                return Ready;
            }
            TRACE_COUNTER_INCREMENT(PerfReadyLegacyNavigation);
        }
        else {TRACE_COUNTER_INCREMENT(PerfReadyLegacyComponent);}
    }
    else {TRACE_COUNTER_INCREMENT(PerfReadyLegacySource);}
    TRACE_CPUPROFILER_EVENT_SCOPE(PERF1B_LegacyReady);
    // Original public/virtual path and both original strong call sites remain intact.
return bDungeonReady&&NavigationReadiness&&
/* PERF1A_BEGIN */
([&]() { TRACE_CPUPROFILER_EVENT_SCOPE(PERF1A_Call_IsCurrentBatchReady); return 
/* PERF1A_END */
NavigationReadiness->IsCurrentBatchReady()
/* PERF1A_BEGIN */
; }())
/* PERF1A_END */
&&
/* PERF1A_BEGIN */
([&]() { TRACE_CPUPROFILER_EVENT_SCOPE(PERF1A_Call_Current_From_IsDungeonReady); return 
/* PERF1A_END */
FRogueliteRuntimeLoopAssembler::Current(const_cast<ARogueliteRuntimeLoopAssembly*>(this),Batch,Settings,CommittedLoops)
/* PERF1A_BEGIN */
; }())
/* PERF1A_END */
;
    };
    const bool Ready=OriginalReady();
    ObserveGameplayStrongResult(Ready);
    return Ready;
}
TArray<FRogueliteSeedEdgeRecord> ARogueliteRuntimeLoopAssembly::GetNavigationExtraEdges() const
{
    TArray<FRogueliteSeedEdgeRecord> Result;
    for(const auto& L:CommittedLoops.Edges)
    {
        auto& R=Result.AddDefaulted_GetRef();R.EdgeId=L.EdgeId;R.ParentNode=L.NodeA;R.ChildNode=L.NodeB;R.Corridor=L.Corridor;R.ConnectionId=L.ConnectionId;
        R.ParentSocket=L.SocketA;R.ChildSocket=L.SocketB;R.CorridorProfile=L.ProfileId;R.CorridorLength=IsValid(L.Corridor)?L.Corridor->CorridorLength:0;R.Validation=L.Validation;
    }
    return Result;
}
FString ARogueliteRuntimeLoopAssembly::GetNavigationEpoch() const
{
/* PERF1A_BEGIN */
    TRACE_CPUPROFILER_EVENT_SCOPE(PERF1A_Source_GetNavigationEpoch);
/* PERF1A_END */

    if(!
/* PERF1A_BEGIN */
([&]() { TRACE_CPUPROFILER_EVENT_SCOPE(PERF1A_Call_Current_From_GetNavigationEpoch); return 
/* PERF1A_END */
FRogueliteRuntimeLoopAssembler::Current(const_cast<ARogueliteRuntimeLoopAssembly*>(this),Batch,Settings,CommittedLoops)
/* PERF1A_BEGIN */
; }())
/* PERF1A_END */
)return TEXT("InvalidRuntimeGeometry");
    return FormatCurrentNavigationEpoch(GetActorTransform());
}
FString ARogueliteRuntimeLoopAssembly::FormatCurrentNavigationEpoch(const FTransform& Transform) const
{
    return Batch.BatchId.ToString()+FString::Printf(TEXT("|GraphRevision=%d|GeometryRevision=%d|"),CommittedLoops.GraphRevision,CommittedLoops.GeometryRevision)
        +Batch.GraphDigest+Batch.LayoutDigest+CommittedLoops.GeometryDigest+Transform.ToString();
}
bool ARogueliteRuntimeLoopAssembly::IsNativeReadyProofClass(const UClass* Class)
{
    // Deliberately exact: native and Blueprint descendants do not inherit admission.
    return Class==ARogueliteRuntimeLoopAssembly::StaticClass()||Class==ARogueliteCombatSliceRig::StaticClass();
}
bool ARogueliteRuntimeLoopAssembly::ProduceReadyQueryProof(FRogueliteReadyQueryProof& Proof) const
{
    TRACE_CPUPROFILER_EVENT_SCOPE(PERF1B_ProofProduce);
    if(!IsInGameThread()||!bReadyQueryInProgress||ActiveReadyQueryProof!=&Proof||Proof.bProduced||Proof.bConsumed
        ||!IsNativeReadyProofClass(GetClass())||Proof.Readiness!=NavigationReadiness.Get())return false;
    Proof.Source=this;Proof.SourceKey=FObjectKey(this);Proof.Registry=Batch.Registry.Get();Proof.RegistryKey=FObjectKey(Proof.Registry);
    Proof.Batch=&Batch;Proof.Settings=&Settings;Proof.Loops=&CommittedLoops;Proof.BatchId=Batch.BatchId;
    Proof.GraphRevision=CommittedLoops.GraphRevision;Proof.GeometryRevision=CommittedLoops.GeometryRevision;
    Proof.ConfigurationDigest=Batch.ConfigurationDigest;Proof.GraphDigest=Batch.GraphDigest;
    Proof.LayoutDigest=Batch.LayoutDigest;Proof.GeometryDigest=CommittedLoops.GeometryDigest;Proof.SourceTransform=GetActorTransform();
    auto* Request=ActiveTraversalRequest;
    const bool Carry=Request&&Request->Source==this&&Request->bTraversalAdmitted
        &&!Request->bTraversalInvalidated&&!Request->bProduced&&!Request->bConsumed;
    if(Carry)
    {
        TRACE_CPUPROFILER_EVENT_SCOPE(PERF1D1_PayloadPrepare);
        Proof.TraversalRequest=Request;Proof.Traversal.Emplace();
    }
    {
        TRACE_CPUPROFILER_EVENT_SCOPE(PERF1B_Current_FromProof);
        const bool Current=Carry
            ?FRogueliteRuntimeLoopAssembler::CurrentWithTraversal(
                const_cast<ARogueliteRuntimeLoopAssembly*>(this),Batch,Settings,CommittedLoops,&Proof.Traversal.GetValue())
            :FRogueliteRuntimeLoopAssembler::Current(const_cast<ARogueliteRuntimeLoopAssembly*>(this),Batch,Settings,CommittedLoops);
        if(!Current)
        {Proof.Failure=ERogueliteReadyProofFailure::Structure;return false;}
    }
    // No virtual Epoch call: this formatter is the same native implementation used above.
    Proof.Epoch=FormatCurrentNavigationEpoch(Proof.SourceTransform);
    Proof.bProduced=true;TRACE_COUNTER_INCREMENT(PerfReadyProofProduced);return true;
}
bool ARogueliteRuntimeLoopAssembly::RevalidateRuntimeGeometry()
{
    FRogueliteGameplayObservationWrite ObservationWrite(AActor::GetWorld(),TEXT("Source Revalidate"));
    FString Reason;return IsValid(FloorProfile)?FRogueliteRuntimeLoopAssembler::RevalidateConfigured(this,Batch,Settings,CommittedLoops,Reason):FRogueliteRuntimeLoopAssembler::Revalidate(this,Batch,Settings,CommittedLoops,Reason);
}

bool ARogueliteRuntimeLoopAssembly::CanReuseNativeTraversal(FString* Rejection) const
{
    TRACE_CPUPROFILER_EVENT_SCOPE(PERF1D1_TraversalAdmission);
    const auto Reject=[&](const TCHAR* Why){if(Rejection)*Rejection=Why;return false;};
    if(!IsInGameThread()||bReadyQueryInProgress||ActiveTraversalRequest||!IsNativeReadyProofClass(GetClass()))
        return Reject(TEXT("Unknown Source or nested query"));
    UWorld* World=AActor::GetWorld();const auto* Ready=NavigationReadiness.Get();
    if(!IsValid(World)||World->GetClass()!=UWorld::StaticClass()||!World->IsGameWorld()||World->bIsTearingDown
        ||!bDungeonReady||!bGeometryValidated||!Batch.BatchId.IsValid()||!IsValid(Ready)
        ||Ready->GetClass()!=URogueliteNavigationReadinessComponent::StaticClass()||Ready->GetOwner()!=this
        ||Ready->UActorComponent::GetWorld()!=World||!Ready->Report.bReady||Ready->Report.State!=TEXT("NavigationReady")
        ||!IsValid(Batch.Registry)||Batch.Registry->GetClass()!=ARogueliteRoomAssembly::StaticClass()
        ||Batch.Registry->AActor::GetWorld()!=World||Batch.Registry->GetOwner()!=this)
        return Reject(TEXT("World, readiness or authority not admitted"));
    const auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
    if(!IsValid(Nav)||Nav->GetClass()!=URogueliteRuntimeNavigationSystem::StaticClass()||Nav->NavDataSet.IsEmpty())
        return Reject(TEXT("Unknown navigation system"));
    const auto* ReportData=Ready->Data.Get();
    if(!IsValid(ReportData)||ReportData->GetClass()!=ARogueliteRuntimeRecastNavMesh::StaticClass()
        ||ReportData->AActor::GetWorld()!=World||!Nav->NavDataSet.Contains(ReportData))
        return Reject(TEXT("Unknown readiness navigation data"));
    for(const ANavigationData* Data:Nav->NavDataSet)
        if(!IsValid(Data)||Data->GetClass()!=ARogueliteRuntimeRecastNavMesh::StaticClass()||Data->AActor::GetWorld()!=World)
            return Reject(TEXT("Unknown navigation data"));
    // This immutable metadata is not a World/frame/result cache. No unreviewed
    // collision/material/IsEditorOnly/GetWorld getter runs during admission.
    static const TSet<FTopLevelAssetPath> Actors={
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
    static const TSet<FTopLevelAssetPath> Primitives={
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

    static const TSet<FTopLevelAssetPath> BlueprintActors={
        FTopLevelAssetPath(TEXT("/Game/Roguelite/ProductionCandidates/P03M1/Templates/BP_M1_Safehouse.BP_M1_Safehouse_C")),
        FTopLevelAssetPath(TEXT("/Game/Roguelite/ProductionCandidates/P03M1/Templates/BP_M1_Normal_Straight.BP_M1_Normal_Straight_C")),
        FTopLevelAssetPath(TEXT("/Game/Roguelite/ProductionCandidates/P03M1/Templates/BP_M1_Normal_Corner.BP_M1_Normal_Corner_C")),
        FTopLevelAssetPath(TEXT("/Game/Roguelite/ProductionCandidates/P03M1/Templates/BP_M1_Normal_Junction.BP_M1_Normal_Junction_C")),
        FTopLevelAssetPath(TEXT("/Game/Roguelite/ProductionCandidates/P03M1/Templates/BP_M1_FloorExit.BP_M1_FloorExit_C")),
        FTopLevelAssetPath(TEXT("/Game/Roguelite/ProductionCandidates/P03M1/Templates/BP_M1_FinalBoss.BP_M1_FinalBoss_C"))
    };
    const auto ActorAdmitted=[&](const AActor* Actor)
    {
        if(!IsValid(Actor)||Actor->AActor::GetWorld()!=World)return false;
        const UClass* Actual=Actor->GetClass();const UClass* Native=Actual;
        while(Native&&!Native->HasAnyClassFlags(CLASS_Native))Native=Native->GetSuperClass();
        if(!Native||!Actors.Contains(Native->GetClassPathName()))return false;
        return Actual==Native||BlueprintActors.Contains(Actual->GetClassPathName());
    };
    // Also cover record references before following any virtual member getter,
    // including malformed references to actors outside the iterated World.
    for(const auto& Room:Batch.Rooms)if(!ActorAdmitted(Room.Room))return Reject(TEXT("Unknown room record"));
    for(const auto& Edge:Batch.Edges)if(!ActorAdmitted(Edge.Corridor))return Reject(TEXT("Unknown Tree corridor"));
    for(const auto& Edge:CommittedLoops.Edges)if(!ActorAdmitted(Edge.Corridor))return Reject(TEXT("Unknown Loop corridor"));
    for(TActorIterator<AActor> It(World);It;++It)
    {
        if(!ActorAdmitted(*It))
        {if(Rejection)*Rejection=TEXT("Unreviewed Actor implementation: ")+It->GetClass()->GetPathName();return false;}
        TInlineComponentArray<UPrimitiveComponent*> Components;It->GetComponents(Components);
        for(const auto* Component:Components)
            if(!Component->GetClass()->HasAnyClassFlags(CLASS_Native)
                ||!Primitives.Contains(Component->GetClass()->GetClassPathName()))
            {if(Rejection)*Rejection=TEXT("Unreviewed virtual primitive getters: ")+Component->GetClass()->GetPathName();return false;}
    }
    // MembersText additionally calls IsEditorOnly on template components. A
    // nonprimitive subclass is not implicitly safe just because it cannot block.
    for(const auto& Room:Batch.Rooms)
        for(const auto* Component:Room.Room->GetComponents())
        {
            if(!Component||(!Cast<UPrimitiveComponent>(Component)&&Component->GetClass()!=USceneComponent::StaticClass()))
                return Reject(TEXT("Unreviewed template member getter"));
            if(const auto* Mesh=Cast<UStaticMeshComponent>(Component);Mesh&&Mesh->GetStaticMesh()
                &&Mesh->GetStaticMesh()->GetClass()!=UStaticMesh::StaticClass())
                return Reject(TEXT("Unreviewed static mesh material implementation"));
        }
    return true;
}

bool ARogueliteRuntimeLoopAssembly::TakeCurrentTraversal(FRogueliteReadyQueryProof& Request,
    FRogueliteLoopTraversalSnapshot& Out) const
{
    TRACE_CPUPROFILER_EVENT_SCOPE(PERF1D1_TraversalConsume);
    if(!IsInGameThread()||bReadyQueryInProgress||ActiveReadyQueryProof||ActiveTraversalRequest!=&Request
        ||!Request.bTraversalAdmitted||Request.bTraversalInvalidated||!Request.bTraversalDelivered
        ||!Request.bProduced||Request.bConsumed||!Request.Traversal.IsSet())return false;
    Request.bConsumed=true; // One attempt, even if an identity was invalidated.
    const auto* Ready=NavigationReadiness.Get();UWorld* World=Request.World;
    if(Request.Source!=this||Request.SourceKey!=FObjectKey(this)||!IsValid(World)||Request.WorldKey!=FObjectKey(World)
        ||AActor::GetWorld()!=World||World->GetClass()!=UWorld::StaticClass()||World->bIsTearingDown||!World->IsGameWorld()
        ||!IsValid(Ready)||Ready!=Request.Readiness||Ready->GetClass()!=URogueliteNavigationReadinessComponent::StaticClass()
        ||Request.ReadinessKey!=FObjectKey(Ready)||Ready->GetOwner()!=this||Ready->UActorComponent::GetWorld()!=World
        ||!bDungeonReady||!bGeometryValidated||Request.Batch!=&Batch||Request.Settings!=&Settings||Request.Loops!=&CommittedLoops
        ||!Request.BatchId.IsValid()||Request.BatchId!=Batch.BatchId||Request.BatchId!=Ready->Report.BatchId
        ||!IsValid(Batch.Registry)||Request.Registry!=Batch.Registry||Request.RegistryKey!=FObjectKey(Batch.Registry)
        ||Batch.Registry->AActor::GetWorld()!=World||Request.GraphRevision!=CommittedLoops.GraphRevision
        ||Request.GeometryRevision!=CommittedLoops.GeometryRevision||Request.ConfigurationDigest!=Batch.ConfigurationDigest
        ||Request.GraphDigest!=Batch.GraphDigest||Request.LayoutDigest!=Batch.LayoutDigest||Request.GeometryDigest!=CommittedLoops.GeometryDigest
        ||!Request.SourceTransform.Equals(GetActorTransform(),0)||Request.Report!=&Ready->Report||!Ready->Report.bReady
        ||Ready->Report.State!=TEXT("NavigationReady")||Ready->Report.World!=Request.WorldPath||Request.Epoch.IsEmpty()
        ||Request.Epoch==TEXT("InvalidRuntimeGeometry")||Request.Epoch!=Ready->Report.Epoch
        ||!IsValid(Request.Navigation)||Request.NavigationKey!=FObjectKey(Request.Navigation)
        ||FNavigationSystem::GetCurrent<UNavigationSystemV1>(World)!=Request.Navigation||!IsValid(Request.NavData)
        ||Request.NavDataKey!=FObjectKey(Request.NavData)||Request.NavData->AActor::GetWorld()!=World
        ||Request.NavData->GetPathName()!=Ready->Report.NavData
        ||Request.NavigationDataKeys.Num()!=Request.Navigation->NavDataSet.Num())return false;
    for(int32 I=0;I<Request.NavigationDataKeys.Num();++I)
        if(Request.NavigationDataKeys[I]!=FObjectKey(Request.Navigation->NavDataSet[I]))return false;
    if(UNavigationSystemV1::IsNavigationBeingBuiltOrLocked(World))return false;
    const auto& Value=Request.Traversal.GetValue();
    if(!Value.bCurrent||!Value.bGeometryValidated||Value.BatchId!=Batch.BatchId||Value.Seed!=Batch.Seed
        ||Value.GraphRevision!=CommittedLoops.GraphRevision||Value.GeometryRevision!=CommittedLoops.GeometryRevision)return false;
    {TRACE_CPUPROFILER_EVENT_SCOPE(PERF1D1_TraversalMoveOut);Out=MoveTemp(Request.Traversal.GetValue());}
    return true;
}

FRogueliteLoopTraversalSnapshot ARogueliteRuntimeLoopAssembly::GetTraversalSnapshot() const
{
/* PERF1A_BEGIN */
    TRACE_CPUPROFILER_EVENT_SCOPE(PERF1A_Source_GetTraversalSnapshot);
/* PERF1A_END */

    if(IsInGameThread()&&ActiveTraversalRequest)
        ActiveTraversalRequest->bTraversalInvalidated=true;
    else if(CanReuseNativeTraversal())
    {
        FRogueliteReadyQueryProof Request;Request.Source=this;Request.bTraversalAdmitted=true;
        TGuardValue<FRogueliteReadyQueryProof*> RequestGuard(ActiveTraversalRequest,&Request);
        // Preserve the public Ready implementation and all its existing timing
        // scopes. Only an audited pure interval may reverse Capture/Ready order.
        const bool Ready=IsDungeonReady();FRogueliteLoopTraversalSnapshot Reused;
        if(Ready&&TakeCurrentTraversal(Request,Reused))
        {
            Reused.bNavigationReady=true;Reused.NavigationState=TEXT("NavigationReady");
            TRACE_COUNTER_INCREMENT(PerfTraversalConsumed);ObserveGameplayStrongResult(Reused.bCurrent&&Reused.bGeometryValidated&&Reused.bNavigationReady);return Reused;
        }
        TRACE_COUNTER_INCREMENT(PerfTraversalRejected);
    }
    TRACE_COUNTER_INCREMENT(PerfTraversalLegacy);

/* PERF1A_BEGIN */
    TRACE_CPUPROFILER_EVENT_MANUAL_START(TEXT("PERF1A_Call_Capture_FromSnapshot"));
/* PERF1A_END */
    auto Result=FRogueliteLoopCommitQueries::Capture(const_cast<ARogueliteRuntimeLoopAssembly*>(this),Batch,Settings,CommittedLoops);
/* PERF1A_BEGIN */
    TRACE_CPUPROFILER_EVENT_MANUAL_END();
/* PERF1A_END */

    Result.bNavigationReady=Result.bCurrent&&IsDungeonReady();Result.NavigationState=Result.bNavigationReady?TEXT("NavigationReady"):TEXT("NavigationPending");ObserveGameplayStrongResult(Result.bCurrent&&Result.bGeometryValidated&&Result.bNavigationReady);return Result;
}
void ARogueliteRuntimeLoopAssembly::ReadinessChanged(FGuid Id,ERogueliteDungeonBuildStage Value,bool Ready,const FString& Reason)
{
    if(Id!=Batch.BatchId)return;
/* PERF1A_BEGIN */
    TRACE_BOOKMARK(TEXT("PERF1A ReadinessChanged Stage=%d Ready=%d"), static_cast<int32>(Value), Ready);
/* PERF1A_END */

    NavigationSummary=NavigationReadiness->Report.State+TEXT(": ")+Reason;
    int32 Success=0,Loop=0;for(const auto& M:NavigationReadiness->Report.Moves){Success+=M.bSucceeded;Loop+=M.bLoopRoute&&M.bSucceeded&&M.bTraversedInterior;}
    MoveToSummary=FString::Printf(TEXT("真实Character：%d/%d移动通过；回环方向验证%d"),Success,NavigationReadiness->Report.Moves.Num(),Loop);
    if(Ready){RuntimeStatus=TEXT("NavigationReady / DungeonReady（技术验收，非Gameplay）");return;}
    if(Value!=ERogueliteDungeonBuildStage::NavigationPending)
    {
        LastFailureReason=Reason;const FString Saved=NavigationSummary;ClearRuntime();Stage=Value;RuntimeStatus=TEXT("Failed");NavigationSummary=Saved;
        LastFailureReason=Reason;
    }
}
void ARogueliteRuntimeLoopAssembly::WatchGeometry()
{
/* PERF1A_BEGIN */
    TRACE_CPUPROFILER_EVENT_SCOPE(PERF1A_Source_WatchGeometry);
/* PERF1A_END */

    if(bGeometryValidated&&!
/* PERF1A_BEGIN */
([&]() { TRACE_CPUPROFILER_EVENT_SCOPE(PERF1A_Call_Current_From_WatchGeometry); return 
/* PERF1A_END */
FRogueliteRuntimeLoopAssembler::Current(this,Batch,Settings,CommittedLoops)
/* PERF1A_BEGIN */
; }())
/* PERF1A_END */
)
    {
        const FString Why=TEXT("Current runtime Graph/Geometry changed; Ready revoked and only this batch cleared.");
        ClearRuntime();Stage=ERogueliteDungeonBuildStage::NavigationFailed;RuntimeStatus=TEXT("Failed");LastFailureReason=Why;
    }
}
void ARogueliteRuntimeLoopAssembly::BeginPlay()
{
    SetGameplayReadMode(RogueliteObservedGameplay::RequestedMode.GetValueOnGameThread()==1
        ?ERogueliteGameplayReadMode::TrackedGameplay:ERogueliteGameplayReadMode::DeveloperStrict);
    bGenerateOnBeginPlay=false;bRunRuntimeChecks=false;Super::BeginPlay();
    OnReadinessChanged.AddUniqueDynamic(this,&ARogueliteRuntimeLoopAssembly::ReadinessChanged);
    UE_LOG(LogRoguelitePrototype,Display,TEXT("B4B3_BEGIN RuntimeActor=%s GameWorld=%d SavedOwned=%d"),*GetPathName(),GetWorld()->IsGameWorld(),Batch.OwnedActors.Num());
    // Test-only baseline must follow delayed PlayerManager/debugger startup (UE tick interval 0.5s).
    if(bRunValidationSuite)GetWorld()->GetTimerManager().SetTimer(SuiteTimer,this,&ARogueliteRuntimeLoopAssembly::RunSuite,1.f,false);
    else if(bAutoGenerate)GetWorld()->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(this,[this](){GenerateRuntime();}));
}
void ARogueliteRuntimeLoopAssembly::EndPlay(const EEndPlayReason::Type Reason)
{
    FRogueliteGameplayObservationWrite ObservationWrite(AActor::GetWorld(),TEXT("Source EndPlay"));
    GetWorld()->GetTimerManager().ClearTimer(SuiteTimer);GetWorld()->GetTimerManager().ClearTimer(WatchTimer);
    OnReadinessChanged.RemoveDynamic(this,&ARogueliteRuntimeLoopAssembly::ReadinessChanged);Super::EndPlay(Reason);
}

ARogueliteRuntimeLoopAssembly::~ARogueliteRuntimeLoopAssembly() = default;

void ARogueliteRuntimeLoopAssembly::SetGameplayReadMode(ERogueliteGameplayReadMode Mode)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(PERF21B_ModeSwitch);
    if(!IsInGameThread()||!GameplayObservation)return;
    if(GameplayObservation->Mode==Mode)return;
    RevokeGameplayObservation(TEXT("Explicit experimental mode switch"));
    GameplayObservation->Mode=Mode;
    GameplayObservation->UnsupportedSerial=RogueliteObservedGameplay::UnsupportedSerial.load();
}

ERogueliteGameplayReadMode ARogueliteRuntimeLoopAssembly::GetGameplayReadMode() const
{
    if(!IsInGameThread()||!GameplayObservation)return ERogueliteGameplayReadMode::DeveloperStrict;
    if(GameplayObservation->UnsupportedSerial!=RogueliteObservedGameplay::UnsupportedSerial.load())
    {
        const_cast<ARogueliteRuntimeLoopAssembly*>(this)->RevokeGameplayObservation(TEXT("Unsupported write thread; Strict fallback"));
        GameplayObservation->UnsupportedSerial=RogueliteObservedGameplay::UnsupportedSerial.load();
        GameplayObservation->Mode=ERogueliteGameplayReadMode::DeveloperStrict;
    }
    return GameplayObservation->Mode;
}

void ARogueliteRuntimeLoopAssembly::RevokeGameplayObservation(const TCHAR* Reason)
{
    if(!IsInGameThread()||!GameplayObservation)return;
    TRACE_CPUPROFILER_EVENT_SCOPE(PERF21B_Revoke);
    ++GameplayObservation->Epoch;
    GameplayObservation->Published.Reset();
    GameplayObservation->Reason=Reason;
    TRACE_COUNTER_INCREMENT(PerfObservedRevoked);
}

uint64 ARogueliteRuntimeLoopAssembly::GetGameplayObservationEpoch() const
{return IsInGameThread()&&GameplayObservation?GameplayObservation->Epoch:0;}

void ARogueliteRuntimeLoopAssembly::BeginGameplayObservationWrite(UWorld* World,const TCHAR* Reason)
{
    if(!IsInGameThread())
    {
        RogueliteObservedGameplay::UnsupportedWrites.fetch_add(1);
        RogueliteObservedGameplay::UnsupportedSerial.fetch_add(1);
        return;
    }
    TRACE_CPUPROFILER_EVENT_SCOPE(PERF21B_TransactionBegin);
    TRACE_COUNTER_INCREMENT(PerfObservedBegin);
    if(!World)
    {
        ++RogueliteObservedGameplay::UnknownWorldWrites;
        for(TObjectIterator<ARogueliteRuntimeLoopAssembly> It;It;++It)
            if(!It->HasAnyFlags(RF_ClassDefaultObject|RF_ArchetypeObject)&&It->GameplayObservation)
            {
                It->RevokeGameplayObservation(Reason);
                It->GameplayObservation->Mode=ERogueliteGameplayReadMode::DeveloperStrict;
            }
        return;
    }
    ++RogueliteObservedGameplay::Writes.FindOrAdd(FObjectKey(World));
    for(TActorIterator<ARogueliteRuntimeLoopAssembly> It(World);It;++It)It->RevokeGameplayObservation(Reason);
}

void ARogueliteRuntimeLoopAssembly::EndGameplayObservationWrite(FObjectKey World,bool bGameThread)
{
    if(!bGameThread){RogueliteObservedGameplay::UnsupportedWrites.fetch_sub(1);return;}
    TRACE_CPUPROFILER_EVENT_SCOPE(PERF21B_TransactionEnd);
    TRACE_COUNTER_INCREMENT(PerfObservedEnd);
    if(World==FObjectKey())
    {
        if(RogueliteObservedGameplay::UnknownWorldWrites>0)--RogueliteObservedGameplay::UnknownWorldWrites;
        return;
    }
    if(auto* Depth=RogueliteObservedGameplay::Writes.Find(World))
        if(--*Depth==0)RogueliteObservedGameplay::Writes.Remove(World);
}

ERogueliteGameplayObservationStatus ARogueliteRuntimeLoopAssembly::CheckGameplayObservationDomain(FString& Reason) const
{
    using Status=ERogueliteGameplayObservationStatus;
    const auto Reject=[&](Status Result,const TCHAR* Why){Reason=Why;return Result;};
    if(!IsInGameThread()||GetClass()!=ARogueliteCombatSliceRig::StaticClass())
        return Reject(Status::RequiresStrongValidation,TEXT("Unknown Source implementation uses Strict"));
    auto* World=AActor::GetWorld();
    if(!IsValid(World)||World->GetClass()!=UWorld::StaticClass()||!World->IsGameWorld()
        ||World->bIsTearingDown||IsActorBeingDestroyed()||!Batch.BatchId.IsValid())
        return Reject(Status::InvalidRevoked,TEXT("Source/World/Batch ended"));
    if(RogueliteObservedGameplay::Writing(World))return Reject(Status::InvalidRevoked,TEXT("Controlled transaction is still writing"));
    const auto* Rig=static_cast<const ARogueliteCombatSliceRig*>(this);
    if(!Rig->bPlayerBridgeReady||Rig->GameplayBatchId!=Batch.BatchId||!IsValid(Rig->Encounter)||!IsValid(Rig->SecondEncounter))
        return Reject(Status::InvalidRevoked,TEXT("Formal G2 binding is incomplete or clearing"));
    for(const auto* Encounter:{Rig->Encounter.Get(),Rig->SecondEncounter.Get()})
    {
        if(Encounter->GetClass()!=URogueliteSliceEncounter::StaticClass()
            ||Encounter->GetOwner()!=this||Encounter->UActorComponent::GetWorld()!=World)
            return Reject(Status::RequiresStrongValidation,TEXT("Unknown Encounter uses Strict"));
        const auto State=Encounter->GetState();
        if(State==ERogueliteSliceEncounterState::Cancelled||State==ERogueliteSliceEncounterState::Uninitialized)
            return Reject(Status::InvalidRevoked,TEXT("Encounter lifecycle is not observable"));
        // Frozen Encounter owns recovery Spawn/Destroy. Keep that entire domain
        // strict, including parked-probe retirement, instead of borrowing a DTO.
        if(State==ERogueliteSliceEncounterState::Cleared||Encounter->NeedsRecoveryService()||Encounter->IsRecoveryBusy())
            return Reject(Status::RequiresStrongValidation,TEXT("Cleared/recovery domain uses fresh strong queries"));
    }
    int32 Sources=0,Registries=0;
    for(TActorIterator<ARogueliteRuntimeLoopAssembly> It(World);It;++It)++Sources;
    for(TActorIterator<ARogueliteRoomAssembly> It(World);It;++It)
    {
        ++Registries;
        if(*It!=Batch.Registry)return Reject(Status::RequiresStrongValidation,TEXT("Foreign Registry domain uses Strict"));
    }
    if(Sources!=1||Registries!=1)return Reject(Status::RequiresStrongValidation,TEXT("Multiple/unknown structural domain uses Strict"));
    const auto* Ready=NavigationReadiness.Get();
    if(!IsValid(Ready)||Ready->GetClass()!=URogueliteNavigationReadinessComponent::StaticClass()
        ||!IsValid(Batch.Registry)||Batch.Registry->GetClass()!=ARogueliteRoomAssembly::StaticClass()
        ||Batch.Registry->AActor::GetWorld()!=World||Batch.Registry->GetOwner()!=this)
        return Reject(Status::RequiresStrongValidation,TEXT("Unknown authority implementation uses Strict"));
    if(!bDungeonReady||!bGeometryValidated||!Batch.bSucceeded||!Batch.bGeometryValid
        ||Ready->GetOwner()!=this||Ready->UActorComponent::GetWorld()!=World||!Ready->Report.bReady
        ||Ready->Report.State!=TEXT("NavigationReady")||Ready->Report.BatchId!=Batch.BatchId
        ||Ready->Report.Seed!=Batch.Seed||Ready->Report.World!=World->GetPathName()
        ||Ready->Report.LayoutDigest!=Batch.LayoutDigest||Ready->Report.Epoch.IsEmpty()
        ||Ready->Report.Epoch==TEXT("InvalidRuntimeGeometry")
        ||Ready->ActiveWorld.Get()!=World||Ready->ActiveBatch!=Batch.BatchId
        ||Ready->ActiveEpoch!=Ready->Report.Epoch)
        return Reject(Status::InvalidRevoked,TEXT("Live geometry/navigation evidence is unavailable"));
    const auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
    if(!IsValid(Nav)||Nav->GetClass()!=URogueliteRuntimeNavigationSystem::StaticClass())
        return Reject(Status::RequiresStrongValidation,TEXT("Unknown navigation implementation uses Strict"));
    if(Ready->Navigation.Get()!=Nav||!IsValid(Ready->Data)||Ready->Data->GetClass()!=ARogueliteRuntimeRecastNavMesh::StaticClass()
        ||Ready->Data->AActor::GetWorld()!=World||Ready->Data->GetPathName()!=Ready->Report.NavData
        ||!Nav->NavDataSet.Contains(Ready->Data.Get())||UNavigationSystemV1::IsNavigationBeingBuiltOrLocked(World))
        return Reject(Status::InvalidRevoked,TEXT("Live navigation identity/build state changed"));
    for(const ANavigationData* Data:Nav->NavDataSet)
        if(!IsValid(Data)||Data->GetClass()!=ARogueliteRuntimeRecastNavMesh::StaticClass()||Data->AActor::GetWorld()!=World)
            return Reject(Status::RequiresStrongValidation,TEXT("Unknown navigation data uses Strict"));
    Reason.Reset();return Status::ValidObservation;
}

bool ARogueliteRuntimeLoopAssembly::CaptureGameplayObservationIdentity(FRogueliteGameplayObservationValue& V) const
{
    auto* World=AActor::GetWorld();const auto* Ready=NavigationReadiness.Get();
    const auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
    if(!Ready||!Nav)return false;
    V.Source=FObjectKey(this);V.World=FObjectKey(World);V.Registry=FObjectKey(Batch.Registry.Get());
    V.Readiness=FObjectKey(Ready);V.Navigation=FObjectKey(Nav);V.NavData=FObjectKey(Ready->Data.Get());
    for(const ANavigationData* Data:Nav->NavDataSet)V.NavigationData.Add(FObjectKey(Data));
    V.Batch=Batch.BatchId;V.SourceSeed=Seed;V.Seed=Batch.Seed;
    V.GraphRevision=CommittedLoops.GraphRevision;V.GeometryRevision=CommittedLoops.GeometryRevision;
    V.ConfigurationDigest=Batch.ConfigurationDigest;V.GraphDigest=Batch.GraphDigest;V.LayoutDigest=Batch.LayoutDigest;
    V.GeometryDigest=CommittedLoops.GeometryDigest;V.NavigationEpoch=Ready->Report.Epoch;
    V.SourceTransform=GetActorTransform();return true;
}

ERogueliteGameplayObservationStatus ARogueliteRuntimeLoopAssembly::GetGameplayObservationStatus(FString& Reason) const
{
    TRACE_CPUPROFILER_EVENT_SCOPE(PERF21B_ReadObservation);
    using Status=ERogueliteGameplayObservationStatus;
    if(GetGameplayReadMode()!=ERogueliteGameplayReadMode::TrackedGameplay)
    {Reason=TEXT("DeveloperStrict requested");TRACE_COUNTER_INCREMENT(PerfObservedFallback);return Status::RequiresStrongValidation;}
    if(GameplayObservation->bRefreshing)
    {
        Reason=TEXT("Strong publication is in progress; observation cannot be consumed");
        TRACE_COUNTER_INCREMENT(PerfObservedFallback);
        return Status::RequiresStrongValidation;
    }
    const auto Domain=CheckGameplayObservationDomain(Reason);
    if(Domain!=Status::ValidObservation)
    {
        if(GameplayObservation->Published.IsSet())const_cast<ARogueliteRuntimeLoopAssembly*>(this)->RevokeGameplayObservation(*Reason);
        if(Domain==Status::InvalidRevoked){TRACE_COUNTER_INCREMENT(PerfObservedMiss);}
        else {TRACE_COUNTER_INCREMENT(PerfObservedFallback);}
        return Domain;
    }
    if(!GameplayObservation->Published.IsSet())
    {Reason=GameplayObservation->Reason;TRACE_COUNTER_INCREMENT(PerfObservedFallback);return Status::RequiresStrongValidation;}
    FRogueliteGameplayObservationValue Now;CaptureGameplayObservationIdentity(Now);
    const auto& Old=GameplayObservation->Published.GetValue();
    const bool Same=Old.Epoch==GameplayObservation->Epoch&&Old.Source==Now.Source&&Old.World==Now.World
        &&Old.Registry==Now.Registry&&Old.Readiness==Now.Readiness&&Old.Navigation==Now.Navigation&&Old.NavData==Now.NavData
        &&Old.NavigationData==Now.NavigationData&&Old.Batch==Now.Batch&&Old.SourceSeed==Now.SourceSeed&&Old.Seed==Now.Seed
        &&Old.GraphRevision==Now.GraphRevision&&Old.GeometryRevision==Now.GeometryRevision
        &&Old.ConfigurationDigest==Now.ConfigurationDigest&&Old.GraphDigest==Now.GraphDigest
        &&Old.LayoutDigest==Now.LayoutDigest&&Old.GeometryDigest==Now.GeometryDigest&&Old.NavigationEpoch==Now.NavigationEpoch
        &&Old.SourceTransform.Equals(Now.SourceTransform,.0001);
    if(!Same)
    {
        Reason=TEXT("Observed identity changed");
        const_cast<ARogueliteRuntimeLoopAssembly*>(this)->RevokeGameplayObservation(*Reason);
        TRACE_COUNTER_INCREMENT(PerfObservedMiss);return Status::InvalidRevoked;
    }
    Reason=TEXT("Valid last-audited observation; no fresh structure scan");
    TRACE_COUNTER_INCREMENT(PerfObservedHit);return Status::ValidObservation;
}

bool ARogueliteRuntimeLoopAssembly::IsGameplayObservationEpochCurrent(uint64 Epoch) const
{
    FString Reason;
    return Epoch!=0&&Epoch==GetGameplayObservationEpoch()
        &&GetGameplayObservationStatus(Reason)==ERogueliteGameplayObservationStatus::ValidObservation;
}

bool ARogueliteRuntimeLoopAssembly::RefreshGameplayObservation(FString& Reason)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(PERF21B_StrongRefresh);
    if(GetGameplayReadMode()!=ERogueliteGameplayReadMode::TrackedGameplay)
    {Reason=TEXT("DeveloperStrict requested");return false;}
    if(GameplayObservation->bRefreshing)
    {
        Reason=TEXT("Strong publication reentry rejected");
        RevokeGameplayObservation(*Reason);
        return false;
    }
    if(CheckGameplayObservationDomain(Reason)!=ERogueliteGameplayObservationStatus::ValidObservation)return false;
    // Reject unknown virtual implementations before a strong query can invoke them.
    // Their original caller retains the original strict query order and side effects.
    if(!CanReuseNativeTraversal(&Reason))
    {
        RevokeGameplayObservation(TEXT("Unknown read implementation uses original strict path"));
        return false;
    }
    const uint64 Before=GameplayObservation->Epoch;
    TGuardValue<bool> Guard(GameplayObservation->bRefreshing,true);
    TRACE_COUNTER_INCREMENT(PerfObservedRefresh);
    const auto Strong=GetTraversalSnapshot();
    if(!Strong.bCurrent||!Strong.bGeometryValidated||!Strong.bNavigationReady
        ||Before!=GameplayObservation->Epoch
        ||CheckGameplayObservationDomain(Reason)!=ERogueliteGameplayObservationStatus::ValidObservation
        ||!CanReuseNativeTraversal(&Reason)||Before!=GameplayObservation->Epoch)
    {
        if(Reason.IsEmpty())Reason=Strong.Reason.IsEmpty()?TEXT("Complete fresh strong validation rejected publication"):Strong.Reason;
        RevokeGameplayObservation(TEXT("Complete strong publication refused"));return false;
    }
    FRogueliteGameplayObservationValue Value;
    CaptureGameplayObservationIdentity(Value);Value.Epoch=Before;
    for(const auto& Room:Batch.Rooms)Value.Rooms.Add({Room.NodeId,Room.PlacedTransform,Room.Dimensions});
    GameplayObservation->Published.Emplace(MoveTemp(Value));
    GameplayObservation->Reason=TEXT("Published after complete fresh strong validation");
    Reason=GameplayObservation->Reason;TRACE_COUNTER_INCREMENT(PerfObservedPublished);return true;
}

bool ARogueliteRuntimeLoopAssembly::GetGameplayObservedRoom(int32 NodeId,FTransform& Transform,FVector& Dimensions,uint64& Epoch) const
{
    FString Reason;if(GetGameplayObservationStatus(Reason)!=ERogueliteGameplayObservationStatus::ValidObservation)return false;
    const auto* Room=GameplayObservation->Published->Rooms.FindByPredicate([&](const auto& R){return R.NodeId==NodeId;});
    if(!Room)return false;
    Transform=Room->Transform;Dimensions=Room->Dimensions;Epoch=GameplayObservation->Epoch;return true;
}

void ARogueliteRuntimeLoopAssembly::ObserveGameplayStrongResult(bool Ready) const
{
    if(!Ready&&GetGameplayReadMode()==ERogueliteGameplayReadMode::TrackedGameplay)
        const_cast<ARogueliteRuntimeLoopAssembly*>(this)->RevokeGameplayObservation(TEXT("Original strong query rejected"));
}
