#include "Tests/RogueliteProductionCandidateRig.h"
#include "RogueliteFloorProfile.h"
#include "Engine/World.h"
#include "UObject/Package.h"
namespace M1Rig {
bool Own(const AActor* A) { return A && A->GetWorld() && A->GetWorld()->WorldType==EWorldType::Editor && A->GetWorld()->GetOutermost()->GetName()==TEXT("/Game/Roguelite/Tests/P03M1/L_Roguelite_ProductionReadiness_Dev"); }
}
ARogueliteProductionCandidateRig::ARogueliteProductionCandidateRig() { bAutoGenerate=false; }
bool ARogueliteProductionCandidateRig::M1TreeFallback() const { return GetWorld()&&GetWorld()->IsGameWorld()?LastGeometry.Plan.bUsedOptionalTreeFallback:M1EditorEvidence.bUsedOptionalTreeFallback; }
void ARogueliteProductionCandidateRig::M1GeneratePreview()
{
#if WITH_EDITOR
    if(!M1Rig::Own(this)||!IsValid(FloorProfile)||!Batch.OwnedActors.IsEmpty()||!CommittedLoops.Edges.IsEmpty())return;
    const auto C=FloorProfile->ResolvePreview(Seed);
    if(!C.bValid){RuntimeStatus=TEXT("ProfileRejected");LastFailureReason=C.ErrorCode+TEXT(": ")+C.Message;return;}
    Modify();Settings=C.Source;Planning=C.Planning;bGeometryValidated=bDungeonReady=false;M1RoleSummary.Reset();
    const auto P=FRogueliteLoopAwarePlanner::Plan(this,Settings,Planning,GetActorTransform());
    M1EditorEvidence=P.Evidence;
    const bool Good=P.Tree.bValid&&FRogueliteLoopAwarePlanner::MaterializeEditor(this,Settings,Planning,P,Batch,CommittedLoops,M1EditorEvidence);
    bGeometryValidated=Good;RuntimeStatus=Good?TEXT("M1A GeometryValidated / Navigation Not Tested in Editor"):TEXT("M1A Rejected");LastFailureReason=M1EditorEvidence.Reason;
    for(const auto& R:Batch.Rooms)M1RoleSummary+=FString::Printf(TEXT("Node%d %s -> %s\n"),R.NodeId,R.NodeId==P.Tree.Graph.StartNodeId?TEXT("StartSafehouse"):R.NodeId==P.Tree.Graph.FinalNodeId?(FloorProfile->Rules.FloorIndex==3?TEXT("FinalBoss"):TEXT("FloorExit")):TEXT("NormalRoom"),*R.TemplateId.ToString());
    BatchSummary=FString::Printf(TEXT("%d Rooms / %d Tree / %d CommittedLoop / TreeFallback=%d"),Batch.Rooms.Num(),Batch.Edges.Num(),CommittedLoops.Edges.Num(),M1EditorEvidence.bUsedOptionalTreeFallback);
#endif
}
void ARogueliteProductionCandidateRig::M1ClearPreview()
{
#if WITH_EDITOR
    if(!M1Rig::Own(this))return;
    Modify();FString Why;const bool Good=FRogueliteLoopAwarePlanner::Clear(this,Batch,CommittedLoops,Why);
    if(Good){bGeometryValidated=bDungeonReady=false;M1EditorEvidence={};M1RoleSummary.Reset();BatchSummary.Reset();}
    RuntimeStatus=Good?TEXT("M1A Owned Preview Cleared"):TEXT("M1A Cleanup Rejected");LastFailureReason=Why;
#endif
}
void ARogueliteProductionCandidateRig::M1PrepareTemplates()
{
#if WITH_EDITOR
    if(!M1Rig::Own(this)||!IsValid(FloorProfile)||!Batch.OwnedActors.IsEmpty())return;
    const auto C=FloorProfile->ResolvePreview(Seed);if(!C.bValid){LastFailureReason=C.ErrorCode+TEXT(": ")+C.Message;return;}
    Modify();Settings=C.Source;Planning=C.Planning;FString Why;const bool Good=FRoguelitePreparedTemplateCatalog::CaptureEditor(this,Settings,PreparedTemplates,Why);
    RuntimeStatus=Good?TEXT("M1A Current Profile Prepared; enable Auto Generate for PIE"):TEXT("M1A Catalog Rejected");LastFailureReason=Why;
#endif
}

bool ARogueliteProductionCandidateRig::GenerateRuntime(){const bool Good=Super::GenerateRuntime();M1RoleSummary.Reset();if(Good&&IsValid(FloorProfile)){const auto G=FRogueliteRoomGraphGenerator::Generate(Settings.Seed,Settings.GraphSettings());for(const auto& R:Batch.Rooms)M1RoleSummary+=FString::Printf(TEXT("Node%d %s -> %s\n"),R.NodeId,R.NodeId==G.Graph.StartNodeId?TEXT("StartSafehouse"):R.NodeId==G.Graph.FinalNodeId?(FloorProfile->Rules.FloorIndex==3?TEXT("FinalBoss"):TEXT("FloorExit")):TEXT("NormalRoom"),*R.TemplateId.ToString());}return Good;}
