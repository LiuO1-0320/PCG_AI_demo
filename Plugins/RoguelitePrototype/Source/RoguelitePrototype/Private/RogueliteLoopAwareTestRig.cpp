#include "RogueliteLoopAwareTestRig.h"
#include "RogueliteBranchingTestSettings.h"
#include "RogueliteSeedAssemblyInternal.h"
#include "RoguelitePrototype.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"

namespace
{
using G=FRogueliteRoomGraphGenerator;
using namespace RogueliteSeedAssemblyInternal;
FString Scene(UWorld* W)
{
    TArray<FString> Rows;
    for(TActorIterator<AActor> It(W);It;++It)if(!It->IsActorBeingDestroyed())
    {
        Rows.Add(It->GetPathName()+It->GetActorTransform().ToString());TInlineComponentArray<USceneComponent*> Cs;It->GetComponents(Cs);
        for(const auto* C:Cs){FString T=C->GetPathName()+C->GetRelativeTransform().ToString();if(const auto* M=Cast<UStaticMeshComponent>(C))T+=GetPathNameSafe(M->GetStaticMesh())+FString::FromInt(uint8(M->GetCollisionEnabled()));Rows.Add(T);}
        if(const auto* R=Cast<ARogueliteParametricRoom>(*It)){FString D;for(const auto* V:{&R->NorthDoor,&R->SouthDoor,&R->EastDoor,&R->WestDoor})FRogueliteDoorConfig::StaticStruct()->ExportText(D,V,nullptr,nullptr,PPF_None,nullptr);Rows.Add(R->GetPathName()+D);}
        if(const auto* R=Cast<ARogueliteRoomAssembly>(*It))for(const auto& C:R->GetConnectionRecords()){FString D;FRogueliteRoomConnection::StaticStruct()->ExportText(D,&C,nullptr,nullptr,PPF_None,nullptr);Rows.Add(D);}
    }
    Rows.Sort();FString T;for(const auto& R:Rows)T+=R+TEXT("\n");return G::StableTextDigest(T);
}
FRogueliteLoopAwareSettings Enabled(){FRogueliteLoopAwareSettings S;S.bEnableLoopAwarePlanning=true;return S;}
FRogueliteSeedLayoutSettings Small(int32 Count)
{
    auto S=RogueliteBranchingTestSettings();S.MinMainNodes=S.MaxMainNodes=3;S.MinBranches=S.MaxBranches=Count-3;S.MinBranchDepth=S.MaxBranchDepth=1;S.MinNodes=S.MaxNodes=Count;S.MinDecisionNodes=S.MaxDecisionNodes=Count-3;S.MinNestedDecisionNodes=0;
    for(auto& R:S.TypeAssignment.Rules){R.MinCount=0;R.MaxCount=0;R.Weight=0;if(R.Type==ERogueliteSeedRoomType::Combat){R.MinCount=2;R.MaxCount=256;R.Weight=1;}if(R.Type==ERogueliteSeedRoomType::Final){R.MinCount=R.MaxCount=1;R.Weight=1;}}return S;
}
bool Actual(AActor* Owner,const FRogueliteSeedLayoutSettings& S,const FRogueliteSeedBatch& B,const FRogueliteCommittedLoopBatch& L,FRogueliteLoopAwareCase& C)
{
    if(!B.bSucceeded||!B.bGeometryValid||!IsValid(B.Registry))return false;bool Good=true;
    for(const auto& R:B.Rooms)
    {
        Good&=IsValid(R.Room)&&R.Room->GetActorTransform().Equals(R.PlacedTransform,.0001)&&R.Room->GetActorScale3D().Equals(FVector::OneVector,.0001)&&MembersText(R.Room)==R.MemberSignature;
        if(IsValid(R.Room))C.ActualGeometry.Add(FString::Printf(TEXT("RoomNode=%d Actor=%s Template=%s Pose=%s Members=%s"),R.NodeId,*R.Room->GetPathName(),*R.TemplateId.ToString(),*R.Room->GetActorTransform().ToString(),*MembersText(R.Room)));
    }
    const auto Records=B.Registry->GetConnectionRecords();TSet<FGuid> Ids;TSet<FString> Ports;
    for(const auto& R:Records)
    {Good&=R.bActive&&R.bGeometryValid&&R.ValidationStatus==ERogueliteConnectionValidation::Passed&&!Ids.Contains(R.Id);Ids.Add(R.Id);C.ActualConnectionIds.Add(R.Id);for(const auto& Pair:{TPair<ARogueliteParametricRoom*,ERogueliteRoomSide>(R.RoomA,R.SocketA),TPair<ARogueliteParametricRoom*,ERogueliteRoomSide>(R.RoomB,R.SocketB)}){const auto Key=GetPathNameSafe(Pair.Key)+FString::FromInt(uint8(Pair.Value));Good&=!Ports.Contains(Key);Ports.Add(Key);}}
    for(const auto& E:B.Edges){C.TreeValidations.Add(E.Validation);Good&=IsValid(E.Corridor)&&E.Validation.bSucceeded&&E.Validation.Probes.Num()==59;if(IsValid(E.Corridor))C.ActualGeometry.Add(FString::Printf(TEXT("TreeEdge=%d Corridor=%s Length=%.3f Pose=%s"),E.EdgeId,*E.Corridor->GetPathName(),E.Corridor->CorridorLength,*E.Corridor->GetActorTransform().ToString()));}
    for(const auto& E:L.Edges){Good&=E.Validation.bSucceeded&&E.Validation.Probes.Num()==59;if(IsValid(E.Corridor))C.ActualGeometry.Add(FString::Printf(TEXT("LoopEdge=%d Corridor=%s Length=%.3f Pose=%s"),E.EdgeId,*E.Corridor->GetPathName(),E.Corridor->CorridorLength,*E.Corridor->GetActorTransform().ToString()));}
    const auto V=FRogueliteLoopCommitQueries::Capture(Owner,B,S,L);Good&=V.bCurrent&&V.bGeometryValidated&&!V.bNavigationReady&&V.IndependentCycles==L.Edges.Num()&&V.Nodes.Num()==B.Rooms.Num()&&V.Edges.Num()==B.Edges.Num()+L.Edges.Num()&&Records.Num()==V.Edges.Num();return Good;
}
}

ARogueliteLoopAwareTestRig::ARogueliteLoopAwareTestRig()
{PrimaryActorTick.bCanEverTick=false;SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("LoopAwareRoot")));Tags.Add(TEXT("RogueliteLoopAwareTestOnly"));Settings=RogueliteBranchingTestSettings();Planning=Enabled();}
bool ARogueliteLoopAwareTestRig::OwnMap() const
{return GetWorld()&&GetWorld()->WorldType==EWorldType::Editor&&GetWorld()->GetOutermost()->GetName()==TEXT("/Game/Roguelite/Maps/L_Roguelite_LoopAwarePlanning_Dev");}
void ARogueliteLoopAwareTestRig::AnalyzeSpatialPlan()
{
    if(!OwnMap()||!Batch.OwnedActors.IsEmpty()){Summary=TEXT("Only independent Editor map with empty owned batch; existing preview protected.");return;}
    PendingPlan=MakeUnique<FRogueliteLoopAwareSpatialPlan>(FRogueliteLoopAwarePlanner::Plan(this,Settings,Planning,GetActorTransform()));Latest=PendingPlan->Evidence;Summary=Latest.Stage+TEXT(": ")+Latest.Reason;
}
void ARogueliteLoopAwareTestRig::GenerateDungeon()
{
    if(!OwnMap()||!Batch.OwnedActors.IsEmpty()){Summary=TEXT("Clear Owned Batch explicitly before generating; never stacks previews.");return;}Modify();AnalyzeSpatialPlan();if(!PendingPlan||!PendingPlan->Tree.bValid)return;
    FRogueliteLoopAwarePlanner::MaterializeEditor(this,Settings,Planning,*PendingPlan,Batch,CommittedLoops,Latest);Traversal=FRogueliteLoopCommitQueries::Capture(this,Batch,Settings,CommittedLoops);Summary=Latest.Stage+TEXT(": ")+Latest.Reason;
}
void ARogueliteLoopAwareTestRig::ValidateCommittedLoops()
{if(OwnMap()){Modify();FRogueliteLoopCommitQueries::Revalidate(this,Batch,CommittedLoops,Summary);Traversal=FRogueliteLoopCommitQueries::Capture(this,Batch,Settings,CommittedLoops);Latest.bSucceeded=Traversal.bCurrent;Latest.bC2Validated=Traversal.bGeometryValidated;Latest.bNavigationReady=false;}}
void ARogueliteLoopAwareTestRig::ClearOwnedBatch()
{if(OwnMap()){Modify();FRogueliteLoopAwarePlanner::Clear(this,Batch,CommittedLoops,Summary);PendingPlan.Reset();Traversal=FRogueliteLoopCommitQueries::Capture(this,Batch,Settings,CommittedLoops);Latest.bSucceeded=false;Latest.bC2Validated=false;Latest.CommittedLoops=Latest.IndependentCycles=0;Latest.TraversalDigest.Reset();}}
void ARogueliteLoopAwareTestRig::BeginPlay()
{
    Super::BeginPlay();if(GetWorld()->IsGameWorld()){const auto Before=Scene(GetWorld());FRogueliteLoopAwareSpatialPlan Empty;FRogueliteLoopAwareEvidence R;const bool Result=FRogueliteLoopAwarePlanner::MaterializeEditor(this,Settings,Planning,Empty,Batch,CommittedLoops,R);bRuntimeMutationRefused=!Result&&Scene(GetWorld())==Before;UE_LOG(LogRoguelitePrototype,Display,TEXT("LOOP_AWARE_PIE_GUARD Actor=%s Refused=%d"),*GetPathName(),bRuntimeMutationRefused);}
}
void ARogueliteLoopAwareTestRig::RunBaseline()
{
    if(!OwnMap()||!Batch.OwnedActors.IsEmpty())return;BaselineCases.Reset();bBaselinePassed=true;int32 Trees=0,Straights=0;const auto Initial=Scene(GetWorld());
    for(int32 Seed=0;Seed<20;++Seed)
    {
        FRogueliteLoopAwareCase C;C.Name=FString::Printf(TEXT("Legacy Seed%d"),Seed);C.Result.Seed=Seed;const auto S=RogueliteBranchingTestSettings(Seed);const auto Before=Scene(GetWorld());auto B=FRogueliteSeedLayoutAssembler::Generate(this,S,GetActorTransform());C.Result.GraphDigest=B.GraphDigest;C.Result.LayoutDigest=B.LayoutDigest;C.Result.bTreeBuilt=B.bSucceeded;C.Result.Reason=B.Reason;
        if(B.bSucceeded){++Trees;FRogueliteLoopPlanningSettings L;const auto P=FRogueliteLoopPlanner::Analyze(this,B,S,L);C.Result.PrecheckedCount=P.StraightPassedPairs;Straights+=P.StraightPassedPairs>0;}
        FString Reason;C.Result.bCleanupComplete=FRogueliteSeedLayoutAssembler::Clear(this,B,Reason);C.bSceneRestored=Scene(GetWorld())==Before;C.bPassed=C.bSceneRestored&&C.Result.bCleanupComplete;bBaselinePassed&=C.bPassed;BaselineCases.Add(C);
    }
    bBaselinePassed&=Scene(GetWorld())==Initial;Summary=FString::Printf(TEXT("Legacy actual20Seed Trees=%d/20 Straight=%d/%d Protected=%d; historical15/20 and0/15 not rewritten."),Trees,Straights,Trees,bBaselinePassed);UE_LOG(LogRoguelitePrototype,Display,TEXT("LOOP_AWARE_BASELINE %s"),*Summary);
}
void ARogueliteLoopAwareTestRig::RunChecks()
{
    if(!OwnMap()||!Batch.OwnedActors.IsEmpty()){Summary=TEXT("Refused: existing preview/user layout protected.");return;}
    Cases.Reset();Evidence.Reset();bChecksPassed=true;const auto Initial=Scene(GetWorld());
    const auto Check=[&](const FString& Name,bool Pass){bChecksPassed&=Pass;Evidence.Add((Pass?TEXT("PASS "):TEXT("FAIL "))+Name);UE_LOG(LogRoguelitePrototype,Display,TEXT("LOOP_AWARE_CHECK %s"),*Evidence.Last());};
    const auto Run=[&](const FString& Name,const FRogueliteSeedLayoutSettings& Source,const FRogueliteLoopAwareSettings& S,ERogueliteLoopAwareTestFault Fault,int32 Required)
    {
        FRogueliteLoopAwareCase C;C.Name=Name;const auto Before=Scene(GetWorld());auto P=FRogueliteLoopAwarePlanner::Plan(this,Source,S,GetActorTransform());FRogueliteSeedBatch B;FRogueliteCommittedLoopBatch L;C.Result=P.Evidence;bool Consistent=true;
        if(P.Tree.bValid)
        {
            TArray<FString> ShapeRows;
            for(const auto& R:P.Tree.Rooms)ShapeRows.Add(TEXT("RoomGeometry|")+R.Record.Dimensions.ToString()+TEXT("|")+R.Record.PlacedTransform.ToString());
            for(const auto& R:P.Tree.Edges)ShapeRows.Add(FString::Printf(TEXT("CorridorGeometry|%.3f,%.3f,%.3f|%s"),R.Record.CorridorLength,R.Profile.Width,R.Profile.Height,*R.Transform.ToString()));
            for(const auto& R:P.Evidence.Intents)if(R.bSpatiallyFeasible)ShapeRows.Add(FString::Printf(TEXT("LoopGeometry|%.3f|%s"),R.CorridorLength,*R.CorridorTransform.ToString()));
            ShapeRows.Sort();P.Evidence.Attempts.Add(TEXT("PhysicalShapeDigest=")+G::StableTextDigest(FString::Join(ShapeRows,TEXT("\n"))));
            FRogueliteLoopAwarePlanner::MaterializeEditor(this,Source,S,P,B,L,C.Result,Fault);
            if(C.Result.bSucceeded)Consistent=Actual(this,Source,B,L,C)&&C.Result.CommittedLoops>=S.MinLoops&&B.Edges.Num()==B.Rooms.Num()-1&&L.Edges.Num()==C.Result.CommittedLoops;
            const auto Existing=Scene(GetWorld());FRogueliteLoopAwareEvidence Repeat;const bool Duplicate=FRogueliteLoopAwarePlanner::MaterializeEditor(this,Source,S,P,B,L,Repeat);Consistent&=!Duplicate&&Scene(GetWorld())==Existing;
            FString Why;C.Result.bCleanupComplete=FRogueliteLoopAwarePlanner::Clear(this,B,L,Why);FRogueliteLoopAwareEvidence Stale;const auto After=Scene(GetWorld());const bool Replay=FRogueliteLoopAwarePlanner::MaterializeEditor(this,Source,S,P,B,L,Stale);Consistent&=!Replay&&Scene(GetWorld())==After&&B.OwnedActors.IsEmpty()&&L.Edges.IsEmpty();
        }
        C.bSceneRestored=Scene(GetWorld())==Before;C.bPassed=Consistent&&C.bSceneRestored&&C.Result.bCleanupComplete&&(Required==0||(Required>0?C.Result.bSucceeded:!C.Result.bSucceeded));Check(Name,C.bPassed);Cases.Add(C);return C.Result;
    };
    int32 Success=0,Tree=0;TSet<FString> Shapes;TArray<int32> SuccessfulSeeds;
    for(int32 Seed=0;Seed<20;++Seed)
    {const auto R=Run(FString::Printf(TEXT("Random V2 Seed%d"),Seed),RogueliteBranchingTestSettings(Seed),Enabled(),ERogueliteLoopAwareTestFault::None,0);Success+=R.bSucceeded;Tree+=R.bTreeBuilt;if(R.bSucceeded){SuccessfulSeeds.Add(Seed);const auto* Shape=R.Attempts.FindByPredicate([](const auto& T){return T.StartsWith(TEXT("PhysicalShapeDigest="));});if(Shape)Shapes.Add(*Shape);}}
    Check(TEXT("At least one normal randomized V2 graph physically commits a Loop"),Success>0);Check(TEXT("Different successful seeds yield distinct complete placement data"),Shapes.Num()>1);
    if(!SuccessfulSeeds.IsEmpty())
    {
        const int32 Seed=SuccessfulSeeds[0];const auto A=Run(TEXT("Determinism repeat A"),RogueliteBranchingTestSettings(Seed),Enabled(),ERogueliteLoopAwareTestFault::None,1),B=Run(TEXT("Determinism repeat B"),RogueliteBranchingTestSettings(Seed),Enabled(),ERogueliteLoopAwareTestFault::None,1);
        Check(TEXT("Graph/type/template/pose/ports/corridor/intent/committed traversal stable without GUID/name"),A.GraphDigest==B.GraphDigest&&A.RoomTypeDigest==B.RoomTypeDigest&&A.StablePlanText==B.StablePlanText&&A.StablePlanDigest==B.StablePlanDigest&&A.LayoutDigest==B.LayoutDigest&&A.TraversalDigest==B.TraversalDigest);
        for(const auto Fault:{ERogueliteLoopAwareTestFault::FailLoopCommit,ERogueliteLoopAwareTestFault::CancelAfterTree,ERogueliteLoopAwareTestFault::CancelAfterLoop})Run(FString::Printf(TEXT("Controlled whole-batch rollback fault%d"),uint8(Fault)),RogueliteBranchingTestSettings(Seed),Enabled(),Fault,-1);
    }
    Run(TEXT("No legal loop topology and hard MinLoops1"),Small(3),Enabled(),ERogueliteLoopAwareTestFault::None,-1);
    auto Capacity=RogueliteBranchingTestSettings();Capacity.Templates.RemoveAll([](const auto& T){return T.Id!=TEXT("Warehouse");});Run(TEXT("Real Warehouse capacity cannot serve degree3"),Capacity,Enabled(),ERogueliteLoopAwareTestFault::None,-1);
    auto Mismatch=RogueliteBranchingTestSettings();Mismatch.Corridors[0].DoorWidth=180;Run(TEXT("Actual authored door specification cannot align to180cm profile"),Mismatch,Enabled(),ERogueliteLoopAwareTestFault::None,-1);
    auto Budget=Enabled();Budget.MaxSpatialChecks=1;Run(TEXT("Explicit finite spatial budget exhaustion"),RogueliteBranchingTestSettings(),Budget,ERogueliteLoopAwareTestFault::None,-1);
    auto Zero=Enabled();Zero.MinLoops=Zero.MaxLoops=0;Run(TEXT("Independent four-room zero-loop legality, not a formal FloorProfile"),Small(4),Zero,ERogueliteLoopAwareTestFault::None,1);
    auto Optional=Enabled();Optional.MinLoops=0;Run(TEXT("MinLoops0 permits truthful zero when topology offers no loop"),Small(3),Optional,ERogueliteLoopAwareTestFault::None,1);
    if(!SuccessfulSeeds.IsEmpty())
    {
        const auto S=RogueliteBranchingTestSettings(SuccessfulSeeds[0]);const auto V=Enabled();auto P=FRogueliteLoopAwarePlanner::Plan(this,S,V,GetActorTransform());
        if(P.Tree.bValid&&!P.Evidence.Intents.IsEmpty())
        {
            const auto& Intent=P.Evidence.Intents[0];FActorSpawnParameters Args;Args.bDeferConstruction=true;Args.ObjectFlags|=RF_Transient;Args.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
            Args.CustomPreSpawnInitialization=[](AActor* Actor){auto* R=CastChecked<ARogueliteParametricRoom>(Actor);R->RoomLength=R->RoomWidth=1000;R->RoomHeight=400;};
            const FTransform Pose(Intent.CorridorTransform.GetLocation());auto* External=GetWorld()->SpawnActor<ARogueliteParametricRoom>(ARogueliteParametricRoom::StaticClass(),Pose,Args);
            if(External){External->FinishSpawning(Pose);const auto Before=Scene(GetWorld());FRogueliteSeedBatch B;FRogueliteCommittedLoopBatch L;FRogueliteLoopAwareEvidence R;
                const bool Result=FRogueliteLoopAwarePlanner::MaterializeEditor(this,S,V,P,B,L,R);Check(TEXT("Real non-owned parameter room blocks reserved loop; stale plan refused before spawn"),!Result&&R.Stage==TEXT("StalePlan")&&Scene(GetWorld())==Before&&B.OwnedActors.IsEmpty()&&External->GetActorTransform().Equals(Pose,.0001));External->Destroy();}
            else Check(TEXT("Independent blocking-room fixture allocation"),false);
        }
        auto* Foreign=GetWorld()->SpawnActor<ARogueliteSeedAssembly>(ARogueliteSeedAssembly::StaticClass(),FTransform(FVector(150000,150000,0)));
        if(Foreign)
        {
            auto Other=FRogueliteSeedLayoutAssembler::Generate(Foreign,Small(4),Foreign->GetActorTransform());Check(TEXT("Independent foreign Tree fixture truly exists"),Other.bSucceeded);
            if(Other.bSucceeded)Run(TEXT("Original non-owned Tree connections/rooms untouched by new batch clear"),S,V,ERogueliteLoopAwareTestFault::None,1);
            FString Why;const bool Cleared=FRogueliteSeedLayoutAssembler::Clear(Foreign,Other,Why);Check(TEXT("Only test harness cleans its own independent foreign Tree"),Cleared);Foreign->Destroy();
        }
    }
    Check(TEXT("Original complete Editor actor/component/connection scene preserved"),Scene(GetWorld())==Initial);
    Summary=FString::Printf(TEXT("B2B checks=%s normal V2 committedLoops=%d/20 treeBuilt=%d/20; improvement target10/20=%s; cases=%d. Editor only, loop NavigationPending."),bChecksPassed?TEXT("PASS"):TEXT("FAIL"),Success,Tree,Success>=10?TEXT("met"):TEXT("NOT MET"),Cases.Num());UE_LOG(LogRoguelitePrototype,Display,TEXT("LOOP_AWARE_SUITE_FINISHED %s"),*Summary);
}
