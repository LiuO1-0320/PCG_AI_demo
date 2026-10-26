#include "RogueliteRuntimeLoopAssembly.h"
#include "RogueliteSeedAssemblyInternal.h"
#include "RoguelitePrototype.h"
#include "RogueliteFloorProfile.h"
#include "Engine/World.h"
#include "TimerManager.h"

ARogueliteRuntimeLoopAssembly::ARogueliteRuntimeLoopAssembly()
{
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
    if(!GetWorld()||!GetWorld()->IsGameWorld())return false;
    GetWorld()->GetTimerManager().ClearTimer(WatchTimer);NavigationReadiness->Cancel();bDungeonReady=false;
    FString Reason;const bool Good=FRogueliteRuntimeLoopAssembler::Clear(this,Batch,CommittedLoops,Reason);
    if(Good){bGeometryValidated=false;Stage=ERogueliteDungeonBuildStage::Idle;RuntimeStatus=TEXT("Cancelled / 自有批次已清理");}
    else LastFailureReason=Reason;
    UE_LOG(LogRoguelitePrototype,Display,TEXT("B4B3_CLEAR Success=%d Owned=%d Loop=%d Reason=%s"),Good,Batch.OwnedActors.Num(),CommittedLoops.Edges.Num(),*Reason);return Good;
}
bool ARogueliteRuntimeLoopAssembly::IsDungeonReady() const
{return bDungeonReady&&NavigationReadiness&&NavigationReadiness->IsCurrentBatchReady()&&FRogueliteRuntimeLoopAssembler::Current(const_cast<ARogueliteRuntimeLoopAssembly*>(this),Batch,Settings,CommittedLoops);}
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
    if(!FRogueliteRuntimeLoopAssembler::Current(const_cast<ARogueliteRuntimeLoopAssembly*>(this),Batch,Settings,CommittedLoops))return TEXT("InvalidRuntimeGeometry");
    return Batch.BatchId.ToString()+FString::Printf(TEXT("|GraphRevision=%d|GeometryRevision=%d|"),CommittedLoops.GraphRevision,CommittedLoops.GeometryRevision)
        +Batch.GraphDigest+Batch.LayoutDigest+CommittedLoops.GeometryDigest+GetActorTransform().ToString();
}
bool ARogueliteRuntimeLoopAssembly::RevalidateRuntimeGeometry()
{FString Reason;return IsValid(FloorProfile)?FRogueliteRuntimeLoopAssembler::RevalidateConfigured(this,Batch,Settings,CommittedLoops,Reason):FRogueliteRuntimeLoopAssembler::Revalidate(this,Batch,Settings,CommittedLoops,Reason);}
FRogueliteLoopTraversalSnapshot ARogueliteRuntimeLoopAssembly::GetTraversalSnapshot() const
{
    auto Result=FRogueliteLoopCommitQueries::Capture(const_cast<ARogueliteRuntimeLoopAssembly*>(this),Batch,Settings,CommittedLoops);
    Result.bNavigationReady=Result.bCurrent&&IsDungeonReady();Result.NavigationState=Result.bNavigationReady?TEXT("NavigationReady"):TEXT("NavigationPending");return Result;
}
void ARogueliteRuntimeLoopAssembly::ReadinessChanged(FGuid Id,ERogueliteDungeonBuildStage Value,bool Ready,const FString& Reason)
{
    if(Id!=Batch.BatchId)return;
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
    if(bGeometryValidated&&!FRogueliteRuntimeLoopAssembler::Current(this,Batch,Settings,CommittedLoops))
    {
        const FString Why=TEXT("Current runtime Graph/Geometry changed; Ready revoked and only this batch cleared.");
        ClearRuntime();Stage=ERogueliteDungeonBuildStage::NavigationFailed;RuntimeStatus=TEXT("Failed");LastFailureReason=Why;
    }
}
void ARogueliteRuntimeLoopAssembly::BeginPlay()
{
    bGenerateOnBeginPlay=false;bRunRuntimeChecks=false;Super::BeginPlay();
    OnReadinessChanged.AddUniqueDynamic(this,&ARogueliteRuntimeLoopAssembly::ReadinessChanged);
    UE_LOG(LogRoguelitePrototype,Display,TEXT("B4B3_BEGIN RuntimeActor=%s GameWorld=%d SavedOwned=%d"),*GetPathName(),GetWorld()->IsGameWorld(),Batch.OwnedActors.Num());
    // Test-only baseline must follow delayed PlayerManager/debugger startup (UE tick interval 0.5s).
    if(bRunValidationSuite)GetWorld()->GetTimerManager().SetTimer(SuiteTimer,this,&ARogueliteRuntimeLoopAssembly::RunSuite,1.f,false);
    else if(bAutoGenerate)GetWorld()->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(this,[this](){GenerateRuntime();}));
}
void ARogueliteRuntimeLoopAssembly::EndPlay(const EEndPlayReason::Type Reason)
{
    GetWorld()->GetTimerManager().ClearTimer(SuiteTimer);GetWorld()->GetTimerManager().ClearTimer(WatchTimer);
    OnReadinessChanged.RemoveDynamic(this,&ARogueliteRuntimeLoopAssembly::ReadinessChanged);Super::EndPlay(Reason);
}
