#include "RogueliteRuntimeLoopAssembly.h"
#include "RogueliteSeedAssemblyInternal.h"
#include "RogueliteNavigationTestCharacter.h"
#include "RoguelitePrototype.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "Components/BoxComponent.h"
#include "NavModifierComponent.h"
#include "NavAreas/NavArea_Null.h"
#include "NavigationData.h"

namespace
{
TArray<FString> SceneRows(UWorld* W)
{
    // Recast may create one shared GameWorld NavData on the first build and reuse it.
    // It is engine navigation infrastructure, never an owned dungeon/NPC actor.
    TArray<FString> Rows;for(TActorIterator<AActor> I(W);I;++I)
        if(!I->IsActorBeingDestroyed()&&!I->IsA<ANavigationData>())Rows.Add(I->GetPathName()+I->GetActorTransform().ToString());
    Rows.Sort();return Rows;
}
FString Scene(UWorld* W){return FRogueliteRoomGraphGenerator::StableTextDigest(FString::Join(SceneRows(W),TEXT("\n")));}
FRogueliteLoopAwareSettings Enabled(){FRogueliteLoopAwareSettings S;S.bEnableLoopAwarePlanning=true;return S;}
}

void ARogueliteRuntimeLoopAssembly::RunSuite()
{
    if(!GetWorld()->IsGameWorld()||!Batch.OwnedActors.IsEmpty())return;
    Cases.Reset();SuiteEvidence.Reset();bSuitePassed=true;bSuiteFinished=false;InitialScene=Scene(GetWorld());
    InitialSceneRows=SceneRows(GetWorld());
    for(const auto& Row:InitialSceneRows)SuiteEvidence.Add(TEXT("Settled GameWorld startup baseline: ")+Row);
    const auto Check=[&](const FString& Name,bool Pass){bSuitePassed&=Pass;SuiteEvidence.Add((Pass?TEXT("PASS "):TEXT("FAIL "))+Name);UE_LOG(LogRoguelitePrototype,Display,TEXT("B4B3_CHECK %s"),*SuiteEvidence.Last());};
    const auto Run=[&](const FString& Name,const FRogueliteSeedLayoutSettings& S,const FRogueliteLoopAwareSettings& P,
        const FRoguelitePreparedTemplateCatalog& Catalog,ERogueliteRuntimeLoopFault Fault,bool Expected)
    {
        FRogueliteRuntimeLoopCase C;C.Name=Name;const auto Before=Scene(GetWorld());FRogueliteSeedBatch B;FRogueliteCommittedLoopBatch L;
        const bool Result=FRogueliteRuntimeLoopAssembler::Generate(this,S,P,Catalog,B,L,C.Geometry,Fault);
        bool Good=Result==Expected;
        if(Result)
        {
            TSet<FGuid> Unique;for(const auto& Id:C.Geometry.ConnectionIds)Unique.Add(Id);
            Good&=C.Geometry.bProbeFreePlanning&&C.Geometry.bRegistrationPassed&&B.Rooms.Num()==S.MinNodes
                &&B.Edges.Num()==B.Rooms.Num()-1&&L.Edges.Num()>=P.MinLoops&&Unique.Num()==B.Edges.Num()+L.Edges.Num();
            for(const auto& V:C.Geometry.Connections)Good&=V.bSucceeded&&V.bCapsulePassed&&V.bGeometryPassed&&V.Probes.Num()==59;
            FRogueliteRuntimeLoopResult Duplicate;const auto Existing=Scene(GetWorld());
            Good&=!FRogueliteRuntimeLoopAssembler::Generate(this,S,P,Catalog,B,L,Duplicate)&&Scene(GetWorld())==Existing;
        }
        else Good&=C.Geometry.bCleanupComplete&&B.OwnedActors.IsEmpty()&&L.Edges.IsEmpty();
        if(IsValid(C.Geometry.TestObstacle))
        {
            Good&=Fault==ERogueliteRuntimeLoopFault::RejectLoopC2&&C.Geometry.Stage==TEXT("LoopC2Rejected");
            Check(TEXT("Independent physical blocker survives failed batch cleanup"),IsValid(C.Geometry.TestObstacle));
            C.Geometry.TestObstacle->Destroy();C.Geometry.TestObstacle=nullptr;
        }
        FString Why;C.Geometry.bCleanupComplete=FRogueliteRuntimeLoopAssembler::Clear(this,B,L,Why);
        C.bSceneRestored=Scene(GetWorld())==Before;C.bPassed=Good&&C.Geometry.bCleanupComplete&&C.bSceneRestored;
        C.Reason=C.Geometry.Reason;Check(Name,C.bPassed);Cases.Add(C);return C.Geometry;
    };
    int32 Success=0;
    for(int32 I=0;I<20;++I)Success+=Run(FString::Printf(TEXT("Runtime Geometry Seed%d"),I),FRogueliteRuntimeLoopAssembler::SourcePreset(I),Enabled(),PreparedTemplates,ERogueliteRuntimeLoopFault::None,true).bSucceeded;
    SuiteEvidence.Add(FString::Printf(TEXT("Observed Runtime Tree+Loop geometry successes=%d/20. All raw failures retained."),Success));
    const auto A=Run(TEXT("Runtime Determinism A"),FRogueliteRuntimeLoopAssembler::SourcePreset(0),Enabled(),PreparedTemplates,ERogueliteRuntimeLoopFault::None,true);
    const auto B=Run(TEXT("Runtime Determinism B"),FRogueliteRuntimeLoopAssembler::SourcePreset(0),Enabled(),PreparedTemplates,ERogueliteRuntimeLoopFault::None,true);
    Check(TEXT("Runtime same Seed graph/type/plan/actual transforms/layout/traversal stable excluding GUID"),A.bSucceeded&&B.bSucceeded
        &&A.Plan.GraphDigest==B.Plan.GraphDigest&&A.Plan.RoomTypeDigest==B.Plan.RoomTypeDigest&&A.Plan.StablePlanText==B.Plan.StablePlanText
        &&A.ActualTransforms==B.ActualTransforms&&A.Plan.LayoutDigest==B.Plan.LayoutDigest&&A.Traversal.StableDigest==B.Traversal.StableDigest);
    for(const auto Fault:{ERogueliteRuntimeLoopFault::CancelLoopSpawn,ERogueliteRuntimeLoopFault::RejectLoopC2,ERogueliteRuntimeLoopFault::CancelAfterCommit})
        Run(FString::Printf(TEXT("Runtime controlled cancellation/C2 fault%d"),uint8(Fault)),FRogueliteRuntimeLoopAssembler::SourcePreset(0),Enabled(),PreparedTemplates,Fault,false);
    auto Small=FRogueliteRuntimeLoopAssembler::SourcePreset(0,true);Small.MinBranches=Small.MaxBranches=0;Small.MinNodes=Small.MaxNodes=3;Small.MinDecisionNodes=Small.MaxDecisionNodes=0;
    const auto NoLoop=Run(TEXT("Hard MinLoops1 unsatisfied by three-room topology"),Small,Enabled(),PreparedTemplates,ERogueliteRuntimeLoopFault::None,false);
    Check(TEXT("No-loop negative reaches genuine topology refusal"),NoLoop.Plan.Stage==TEXT("LoopIntentUnsatisfied"));
    auto Capacity=FRogueliteRuntimeLoopAssembler::SourcePreset(0);Capacity.Templates.RemoveAll([](const auto& T){return T.Id!=TEXT("Warehouse");});
    auto Catalog=PreparedTemplates;Catalog.Rows.RemoveAll([](const auto& T){return T.Id!=TEXT("Warehouse");});Catalog.Digest=Catalog.ComputeDigest();
    Run(TEXT("Actual Warehouse capacity cannot serve degree3"),Capacity,Enabled(),Catalog,ERogueliteRuntimeLoopFault::None,false);
    auto Budget=Enabled();Budget.MaxSpatialChecks=1;Run(TEXT("Finite spatial search budget"),FRogueliteRuntimeLoopAssembler::SourcePreset(0),Budget,PreparedTemplates,ERogueliteRuntimeLoopFault::None,false);
    auto Zero=Enabled();Zero.MinLoops=Zero.MaxLoops=0;
    Run(TEXT("Independent four-room Runtime Tree geometry"),FRogueliteRuntimeLoopAssembler::SourcePreset(0,true),Zero,PreparedTemplates,ERogueliteRuntimeLoopFault::None,true);
    // A second independently owned actual Tree is present during this batch's lifecycle.
    FActorSpawnParameters Args;Args.ObjectFlags|=RF_Transient;
    auto* Foreign=GetWorld()->SpawnActor<ARogueliteSeedAssembly>(ARogueliteSeedAssembly::StaticClass(),FTransform(FVector(150000,150000,0)),Args);
    FRogueliteSeedBatch Other;FRogueliteCommittedLoopBatch OtherLoops;FRogueliteRuntimeLoopResult OtherResult;
    const auto ForeignSettings=FRogueliteRuntimeLoopAssembler::SourcePreset(17,true);
    const bool ForeignBuilt=Foreign&&FRogueliteRuntimeLoopAssembler::Generate(Foreign,ForeignSettings,Zero,PreparedTemplates,Other,OtherLoops,OtherResult);
    Check(TEXT("Non-owned Runtime Tree actually exists"),ForeignBuilt);
    if(ForeignBuilt)
    {
        Run(TEXT("Independent foreign Tree/leases/actors protected"),FRogueliteRuntimeLoopAssembler::SourcePreset(0),Enabled(),PreparedTemplates,ERogueliteRuntimeLoopFault::None,true);
        Check(TEXT("Foreign Tree still current after own batch cleanup"),FRogueliteRuntimeLoopAssembler::Current(Foreign,Other,ForeignSettings,OtherLoops));
    }
    FString Why;FRogueliteRuntimeLoopAssembler::Clear(Foreign,Other,OtherLoops,Why);if(Foreign)Foreign->Destroy();
    // Actual pending navigation is cancelled before replacement; no previous epoch may be accepted.
    Seed=0;Preset=ERogueliteLoopTestPreset::EightRoomSingleLoop;bNavigationEnabled=false;
    const bool Old=GenerateRuntime();if(Old)NavigationReadiness->Start(LastGeometry.GeometrySeconds);
    const auto OldId=Batch.BatchId;const auto OldEpoch=GetNavigationEpoch();const bool Pending=NavigationReadiness->IsExpectedEpoch(OldId,OldEpoch);
    ClearRuntime();Seed=1;const bool New=GenerateRuntime();
    Check(TEXT("New batch rejects old pending BatchId/epoch and cannot inherit Ready"),Old&&New&&Pending&&OldId!=Batch.BatchId&&!NavigationReadiness->IsExpectedEpoch(OldId,OldEpoch)&&!IsDungeonReady());
    ClearRuntime();Check(TEXT("Original complete GameWorld preserved after geometry/rollback suite"),Scene(GetWorld())==InitialScene);
    SuiteIndex=0;BeginNavigationCase();
    GetWorld()->GetTimerManager().SetTimer(SuiteTimer,this,&ARogueliteRuntimeLoopAssembly::PollSuite,.25,true);
}

void ARogueliteRuntimeLoopAssembly::BeginNavigationCase()
{
    static const TCHAR* Names[]={TEXT("Strict Loop Navigation Seed0"),TEXT("Strict Loop Navigation Seed1"),TEXT("Strict Loop Navigation Seed2"),
        TEXT("Four-room zero-loop strict navigation"),TEXT("Bounded real navigation timeout"),TEXT("Independent NavArea_Null loop interruption"),
        TEXT("Actual Character MoveTo interrupted"),TEXT("Old GraphRevision rejected"),TEXT("Old GeometryRevision rejected")};
    if(SuiteIndex>=UE_ARRAY_COUNT(Names))return;
    CurrentCase=Names[SuiteIndex];CaseInitialScene=Scene(GetWorld());CaseInitialSceneRows=SceneRows(GetWorld());CaseStarted=FPlatformTime::Seconds();bInterruptIssued=false;
    Seed=SuiteIndex<3?SuiteIndex:0;Preset=SuiteIndex==3?ERogueliteLoopTestPreset::FourRoomTree:ERogueliteLoopTestPreset::EightRoomSingleLoop;
    NavigationReadiness->BuildTimeoutSeconds=SuiteIndex==4?.001f:30.f;NavigationReadiness->MoveTimeoutSeconds=120.f;
    bNavigationEnabled=SuiteIndex!=5;GenerateRuntime();
    if(SuiteIndex==5&&bGeometryValidated&&!CommittedLoops.Edges.IsEmpty())
    {
        auto* Obstacle=GetWorld()->SpawnActor<AActor>();auto* Box=NewObject<UBoxComponent>(Obstacle);Obstacle->SetRootComponent(Box);
        Box->SetBoxExtent(FVector(1200,1200,600));Box->SetCollisionEnabled(ECollisionEnabled::NoCollision);Box->SetCanEverAffectNavigation(false);
        Obstacle->SetActorLocation(CommittedLoops.Edges[0].Corridor->GetActorLocation()+FVector(0,0,150));Box->RegisterComponent();
        auto* Modifier=NewObject<UNavModifierComponent>(Obstacle);Modifier->FailsafeExtent=FVector(1200,1200,600);Modifier->SetAreaClass(UNavArea_Null::StaticClass());Modifier->RegisterComponent();
        SuiteObstacle=Obstacle;NavigationReadiness->Start(LastGeometry.GeometrySeconds);
    }
    if(SuiteIndex==7||SuiteIndex==8)
    {
        const auto OldId=Batch.BatchId;const auto OldEpoch=GetNavigationEpoch();
        if(SuiteIndex==7)++CommittedLoops.GraphRevision;else ++CommittedLoops.GeometryRevision;
        const bool Rejected=!NavigationReadiness->IsExpectedEpoch(OldId,OldEpoch)&&!IsDungeonReady();
        bSuitePassed&=Rejected;SuiteEvidence.Add((Rejected?TEXT("PASS "):TEXT("FAIL "))+CurrentCase+TEXT(" before any Ready publication"));
    }
    UE_LOG(LogRoguelitePrototype,Display,TEXT("B4B3_NAV_CASE_BEGIN Index=%d Name=%s Seed=%d"),SuiteIndex,*CurrentCase,Seed);
}

void ARogueliteRuntimeLoopAssembly::PollSuite()
{
    const auto& N=NavigationReadiness->Report;
    if(SuiteIndex==6&&!bInterruptIssued&&!N.Moves.IsEmpty()&&N.Moves[0].RequestId>0&&!N.Moves[0].bSucceeded)
    {
        for(TActorIterator<ARogueliteNavigationTestController> I(GetWorld());I;++I)
            if(I->GetOwner()==this&&I->VerificationBatch==Batch.BatchId){bInterruptIssued=true;I->StopMovement();break;}
    }
    const bool Failed=RuntimeStatus==TEXT("Failed")||N.State==TEXT("NavigationFailed")||N.State==TEXT("NavigationTimedOut")||N.State==TEXT("NavigationUnavailable");
    const bool Ready=IsDungeonReady();
    if(!Ready&&!Failed&&FPlatformTime::Seconds()-CaseStarted<900)return;
    FRogueliteRuntimeLoopCase C;C.Name=CurrentCase;C.Geometry=LastGeometry;C.Navigation=N;C.Reason=Ready?N.Reason:LastFailureReason;
    bool Pass=SuiteIndex<4?Ready:SuiteIndex==4?N.State==TEXT("NavigationTimedOut"):Failed;
    if(SuiteIndex<3)
    {
        int32 LoopRoutes=0;for(const auto& M:N.Moves)LoopRoutes+=M.bLoopRoute&&M.bSucceeded&&M.bTraversedInterior
            &&M.ActualCheckpoints.Num()==M.RequiredCheckpoints.Num()&&M.LegRequestIds.Num()==M.RequiredCheckpoints.Num();
        Pass&=N.bCoverageComplete&&N.Corridors==8&&LoopRoutes==2&&GetTraversalSnapshot().IndependentCycles==1;
    }
    if(SuiteIndex==6)Pass&=bInterruptIssued;
    if(SuiteObstacle.IsValid()){Pass&=IsValid(SuiteObstacle.Get());SuiteObstacle->Destroy();SuiteObstacle.Reset();}
    const bool Cleared=ClearRuntime();C.Geometry.bCleanupComplete=Cleared;C.bSceneRestored=Scene(GetWorld())==CaseInitialScene;
    if(!C.bSceneRestored)
    {
        C.Reason+=TEXT(" | Independent GameWorld actor identity/pose differs after cleanup; see actual scene difference rows.");
        const auto After=SceneRows(GetWorld());
        for(const auto& Row:After)if(!CaseInitialSceneRows.Contains(Row))SuiteEvidence.Add(TEXT("Unexpected added/changed scene row: ")+Row);
        for(const auto& Row:CaseInitialSceneRows)if(!After.Contains(Row))SuiteEvidence.Add(TEXT("Missing/changed original scene row: ")+Row);
    }
    C.bPassed=Pass&&Cleared&&C.bSceneRestored;bSuitePassed&=C.bPassed;Cases.Add(C);
    SuiteEvidence.Add((C.bPassed?TEXT("PASS "):TEXT("FAIL "))+CurrentCase+TEXT(" State=")+N.State);
    UE_LOG(LogRoguelitePrototype,Display,TEXT("B4B3_NAV_CASE_END Index=%d Passed=%d State=%s Geometry=%.3f Build=%.3f Paths=%.3f Move=%.3f Total=%.3f Cleanup=%d"),
        SuiteIndex,C.bPassed,*C.Navigation.State,C.Geometry.GeometrySeconds,C.Navigation.BuildSeconds,C.Navigation.PathSeconds,C.Navigation.MoveSeconds,
        C.Geometry.GeometrySeconds+C.Navigation.TotalSeconds,Cleared);
    for(TActorIterator<ANavigationData> I(GetWorld());I;++I)
        SuiteEvidence.Add(TEXT("Engine NavData retained outside OwnedBatch: ")+I->GetPathName());
    if(++SuiteIndex<9){BeginNavigationCase();return;}
    GetWorld()->GetTimerManager().ClearTimer(SuiteTimer);bSuiteFinished=true;bSuitePassed&=Scene(GetWorld())==InitialScene;
    RuntimeStatus=bSuitePassed?TEXT("验证完成：全部通过（已清理批次）"):TEXT("验证完成：有失败，查看原始记录");
    UE_LOG(LogRoguelitePrototype,Display,TEXT("B4B3_SUITE_FINISHED Passed=%d Cases=%d SceneRestored=%d"),bSuitePassed,Cases.Num(),Scene(GetWorld())==InitialScene);
}
