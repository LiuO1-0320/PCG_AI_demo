#include "RogueliteDesignerWorkbench.h"
#include "RogueliteCompactCorridorContract.h"
#include "Editor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Subsystems/EditorAssetSubsystem.h"
#include "Misc/ScopedSlowTask.h"
namespace
{
bool EditorWorld(UWorld* W){return W&&W->WorldType==EWorldType::Editor&&GEditor&&!GEditor->PlayWorld&&!W->bIsTearingDown;}
FDesignerWorkbenchResult Fail(const FString& Raw,const FString& Message=FString())
{FDesignerWorkbenchResult R;R.Status=TEXT("未通过");R.RawReason=Raw;R.Message=Message.IsEmpty()?FRogueliteFloorProfileResolver::FriendlyFailure(Raw):Message;return R;}
FDesignerWorkbenchResult Read(ARogueliteWorkbenchPreview* A)
{
    FDesignerWorkbenchResult R;R.Source=A;R.Map=URogueliteDungeonMapQuery::Capture(A);R.bSucceeded=R.Map.bValid&&R.Map.bGeometryValidated;
    R.Status=R.bSucceeded?TEXT("Geometry Validated / Navigation Not Tested"):TEXT("当前批次需重新验证");R.RawReason=R.Map.Reason;
    R.Message=R.bSucceeded?TEXT("房间、真实连接和碰撞已验证。编辑器预览未测试Runtime导航，不代表DungeonReady。"):FRogueliteFloorProfileResolver::FriendlyFailure(R.Map.Reason);
    if(A&&A->AppliedRules.FinalRolePolicy==ERogueliteFloorFinalPolicy::BossFinal)R.Message+=TEXT(" BossFinal仅预留：安全屋、最远路线与Boss通关尚未实现/验证。");return R;
}
}
TArray<FString> FDesignerWorkbench::ProfilePaths()
{return {TEXT("/Game/Roguelite/FloorProfiles/DA_Floor1_Intro.DA_Floor1_Intro"),TEXT("/Game/Roguelite/FloorProfiles/DA_Floor2_Expansion.DA_Floor2_Expansion"),TEXT("/Game/Roguelite/FloorProfiles/DA_Floor3_Final.DA_Floor3_Final")};}
ARogueliteWorkbenchPreview* FDesignerWorkbench::Find(UWorld* W,FString& Why)
{
    ARogueliteWorkbenchPreview* Found=nullptr;if(!W)return nullptr;
    for(TActorIterator<ARogueliteWorkbenchPreview> It(W);It;++It)if(!It->IsActorBeingDestroyed()&&It->WorkbenchId.IsValid()&&It->ActorHasTag(TEXT("RogueliteDesignerWorkbench")))
    {if(Found){Why=TEXT("MultipleWorkbenchSources");return nullptr;}Found=*It;}
    Why=Found?TEXT("CurrentWorkbenchOwner"):TEXT("NoWorkbenchBatch");return Found;
}
FDesignerWorkbenchResult FDesignerWorkbench::Generate(UWorld* W,URogueliteFloorProfile* P,int32 Seed)
{
    if(!EditorWorld(W))return Fail(TEXT("EditorWorldRequired"),TEXT("Generate仅用于编辑器设计预览；请先停止PIE。"));
    if(!IsValid(P))return Fail(TEXT("ProfileMissing"),TEXT("楼层Profile资产未加载，请确认本轮三个预设已安装。"));
    const auto C=P->ResolvePreview(Seed);if(!C.bValid)return Fail(C.ErrorCode,C.Message);
    FString Why;auto* A=Find(W,Why);if(Why==TEXT("MultipleWorkbenchSources"))return Fail(Why,TEXT("场景有多个工作台所有者，已拒绝猜测或清理；请检查来源。"));
    if(A&&(!A->Batch.OwnedActors.IsEmpty()||!A->CommittedLoops.Edges.IsEmpty()))return Fail(TEXT("ExistingBatchProtected"));
    for(TActorIterator<AActor> It(W);It;++It)if(*It!=A&&URogueliteDungeonMapQuery::IsSupportedSource(*It)&&URogueliteDungeonMapQuery::Capture(*It).bValid)return Fail(TEXT("ForeignBatchProtected"),TEXT("当前地图已有其他生成器的有效批次；请先由原工具清理，不会叠加或替你清除。"));
    if(!A){FActorSpawnParameters Params;Params.ObjectFlags=RF_Transactional;A=W->SpawnActor<ARogueliteWorkbenchPreview>(Params);if(!A)return Fail(TEXT("OwnerAllocationFailed"));A->WorkbenchId=FGuid::NewGuid();A->SetActorLabel(TEXT("Roguelite_Workbench_OwnedBatch"));}
    if(!FRogueliteCompactCorridorContract::Configuration(C.Source,C.Planning,Why)){A->Destroy();return Fail(Why);}
    A->ApplyProfile(P,C);
    const auto Plan=FRogueliteLoopAwarePlanner::Plan(A,C.Source,C.Planning,A->GetActorTransform());
    if(!Plan.Tree.bValid)
    {const FString Raw=FString::Printf(TEXT("%s: %s | Attempts=%d Backtracks=%d Checks=%d"),*Plan.Evidence.Stage,*Plan.Evidence.Reason,Plan.Evidence.LayoutAttempts,Plan.Evidence.BacktrackStates,Plan.Evidence.SpatialChecks);A->Destroy();return Fail(Raw);}
    if(!FRogueliteCompactCorridorContract::Plan(Plan,Why)){A->Destroy();return Fail(Why);}
    const bool Good=FRogueliteLoopAwarePlanner::MaterializeEditor(A,C.Source,C.Planning,Plan,A->Batch,A->CommittedLoops,A->Latest);
    if(!Good)
    {const auto Raw=A->Latest.Stage+TEXT(": ")+A->Latest.Reason;if(A->Batch.OwnedActors.IsEmpty()&&A->CommittedLoops.Edges.IsEmpty())A->Destroy();return Fail(Raw);}
    TArray<FRogueliteCompactCorridorMeasurement> Lengths;if(!FRogueliteCompactCorridorContract::Actual(A->Batch,A->CommittedLoops,Lengths,Why)){FString Cleanup;const bool Cleared=FRogueliteLoopAwarePlanner::Clear(A,A->Batch,A->CommittedLoops,Cleanup);if(Cleared)A->Destroy();return Fail(Why+TEXT(" | ")+Cleanup);}
    auto R=Validate(W);UE_LOG(LogTemp,Display,TEXT("E2_GENERATE Profile=%s Seed=%d Rooms=%d Tree=%d Loop=%d Graph=%s Layout=%s Batch=%s Passed=%d Reason=%s"),*P->Rules.ProfileId.ToString(),Seed,A->Batch.Rooms.Num(),A->Batch.Edges.Num(),A->CommittedLoops.Edges.Num(),*A->Batch.GraphDigest,*A->Batch.LayoutDigest,*A->Batch.BatchId.ToString(),R.bSucceeded,*R.RawReason);return R;
}
FDesignerWorkbenchResult FDesignerWorkbench::Clear(UWorld* W)
{
    if(!EditorWorld(W))return Fail(TEXT("EditorWorldRequired"),TEXT("请先停止PIE；不会跨World清理。"));FString Why;auto* A=Find(W,Why);
    if(!A){if(Why==TEXT("MultipleWorkbenchSources"))return Fail(Why);FDesignerWorkbenchResult R;R.bSucceeded=true;R.Status=TEXT("已清理 / 无自有批次");R.Message=TEXT("当前地图没有工作台自有地牢，其他Actor未处理。");return R;}
    TArray<AActor*> Attached;A->GetAttachedActors(Attached);if(!Attached.IsEmpty())return Fail(TEXT("ForeignAttachmentProtected"),TEXT("工作台所有者附有外部Actor，已拒绝清理，避免影响手工内容。"));
    if(!FRogueliteLoopAwarePlanner::Clear(A,A->Batch,A->CommittedLoops,Why))return Fail(Why);
    if(!A->Batch.OwnedActors.IsEmpty()||!A->CommittedLoops.Edges.IsEmpty())return Fail(TEXT("IncompleteOwnedCleanup"));
    A->Destroy();FDesignerWorkbenchResult R;R.bSucceeded=true;R.Status=TEXT("已清理");R.Message=TEXT("只清除了工作台自有连接、房间和走廊；灯光、PlayerStart、手工Actor及其他批次保留。");UE_LOG(LogTemp,Display,TEXT("E2_CLEAR Passed=1 OwnOnly=1"));return R;
}
FDesignerWorkbenchResult FDesignerWorkbench::Validate(UWorld* W)
{
    if(!EditorWorld(W))return Fail(TEXT("EditorWorldRequired"),TEXT("这是Editor几何验证入口；Runtime严格导航仍由原B4-B3执行。"));
    FString Why;auto* A=Find(W,Why);if(!A)return Fail(Why,TEXT("当前地图没有工作台批次，请重新生成。"));
    if(!A->IsProfileCurrent(Why))return Fail(Why);
    TArray<FRogueliteCompactCorridorMeasurement> Lengths;if(!FRogueliteCompactCorridorContract::Actual(A->Batch,A->CommittedLoops,Lengths,Why))return Fail(Why);
    if(!FRogueliteLoopCommitQueries::Revalidate(A,A->Batch,A->CommittedLoops,Why))return Fail(Why);
    const auto Graph=FRogueliteRoomGraphGenerator::Generate(A->Settings.Seed,A->Settings.GraphSettings());const auto Traversal=FRogueliteLoopCommitQueries::Capture(A,A->Batch,A->Settings,A->CommittedLoops);
    const auto& D=A->AppliedRules;int32 Start=0,Final=0;for(const auto& N:Graph.Graph.Nodes){Start+=N.Role==ERogueliteGraphNodeRole::Start;Final+=N.Role==ERogueliteGraphNodeRole::Final;}
    bool Good=Graph.bSucceeded&&Traversal.bCurrent&&Traversal.bGeometryValidated&&Start==1&&Final==1&&A->Batch.Rooms.Num()>=D.MinRoomCount&&A->Batch.Rooms.Num()<=D.MaxRoomCount&&A->Batch.Edges.Num()==A->Batch.Rooms.Num()-1&&A->CommittedLoops.Edges.Num()>=D.MinLoops&&A->CommittedLoops.Edges.Num()<=D.MaxLoops&&Traversal.IndependentCycles==A->CommittedLoops.Edges.Num();
    for(const auto& Rule:D.RoomTypes.Rules){int32 Count=0;for(const auto& N:A->Batch.Rooms)Count+=N.RoomType==Rule.Type;Good&=Count>=Rule.MinCount&&Count<=Rule.MaxCount;}
    if(!Good)return Fail(TEXT("ActualProfileTraversalMismatch: ")+Traversal.Reason);
    auto R=Read(A);R.RawReason=Why;UE_LOG(LogTemp,Display,TEXT("E2_VALIDATE Passed=%d Batch=%s GraphRevision=%d GeometryRevision=%d NavigationNotTested=1"),R.bSucceeded,*A->Batch.BatchId.ToString(),A->CommittedLoops.GraphRevision,A->CommittedLoops.GeometryRevision);return R;
}
FString FDesignerWorkbench::TemplateInfo(UWorld* W,URogueliteFloorProfile* P,int32 Seed)
{
    if(!EditorWorld(W)||!IsValid(P))return TEXT("模板信息仅在Editor有效。");const auto C=P->ResolvePreview(Seed);if(!C.bValid)return C.Message;
    auto* Owner=W->SpawnActor<AActor>();if(!Owner)return TEXT("无法创建临时描述所有者。");FRoguelitePreparedTemplateCatalog Catalog;FString Why;
    const bool Good=FRoguelitePreparedTemplateCatalog::CaptureEditor(Owner,C.Source,Catalog,Why);Owner->Destroy();if(!Good)return FRogueliteFloorProfileResolver::FriendlyFailure(Why)+TEXT("\n")+Why;
    FString Text;for(const auto& Row:Catalog.Rows)
    {
        const auto* Option=C.Source.Templates.FindByPredicate([&](const auto& T){return T.Id==Row.Id;});FString Types;if(Option)for(auto Type:Option->AllowedTypes)Types+=StaticEnum<ERogueliteSeedRoomType>()->GetNameStringByValue(int64(Type))+TEXT(" ");
        Text+=FString::Printf(TEXT("%s | %s | 有效Socket %d | 长宽高 %s cm\n%s\n"),*Row.Id.ToString(),*Types,Row.Sockets.Num(),*Row.Descriptor.Dimensions.ToString(),*Row.TemplateClass.ToSoftObjectPath().ToString());
        FString Failure;auto* Instance=ARogueliteRoomTemplate::CreateTemplateInstance(W,Row.TemplateClass.LoadSynchronous(),FTransform(FVector(10000000,10000000,0)),Failure);
        if(Instance){const auto Members=Instance->GetTemplateMemberStates();Text+=FString::Printf(TEXT("TemplateContentRoot自有成员 %d（组件POI；原模板编辑体系）：\n"),Members.Num());for(const auto& M:Members)Text+=FString::Printf(TEXT("  %s | Parent %s | Local %s | Mesh %s\n"),*M.Name.ToString(),*M.ParentName.ToString(),*M.ParentLocalTransform.ToString(),*M.StaticMeshPath);Instance->Destroy();}
        else Text+=TEXT("成员读取未通过：")+Failure+TEXT("\n");

    }
    return Text;
}
bool FDesignerWorkbench::SaveProfile(URogueliteFloorProfile* P,int32 Seed,FString& Why)
{
    if(!IsValid(P)||!ProfilePaths().Contains(P->GetPathName())){Why=TEXT("只保存本轮三个工作台Profile，不触碰模板或其他资产。");return false;}
    if(!P->ResolvePreview(Seed).bValid){Why=TEXT("Profile配置不合法，请修正后保存；原始输入保留。");return false;}
    const bool Saved=GEditor&&GEditor->GetEditorSubsystem<UEditorAssetSubsystem>()->SaveLoadedAsset(P,false);Why=Saved?TEXT("当前Profile规则已保存到资产。"):TEXT("Profile保存失败。");return Saved;
}
