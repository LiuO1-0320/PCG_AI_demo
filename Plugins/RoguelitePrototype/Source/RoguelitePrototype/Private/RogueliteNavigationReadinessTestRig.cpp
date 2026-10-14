#include "RogueliteNavigationReadinessTestRig.h"
#include "RoguelitePrototype.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "TimerManager.h"
#include "RogueliteNavigationTestCharacter.h"
#include "GameFramework/CharacterMovementComponent.h"

ARogueliteNavigationReadinessTestRig::ARogueliteNavigationReadinessTestRig()
{bEnableRuntimeNavigation=true;Settings.BranchTypeWeights={0,1,0};}
void ARogueliteNavigationReadinessTestRig::BeginPlay()
{
    if(bRunNavigationSuite){bGenerateOnBeginPlay=false;bRunRuntimeChecks=false;}
    Super::BeginPlay();
    if(bRunNavigationSuite && GetWorld()->GetOutermost()->GetName().EndsWith(TEXT("L_Roguelite_RuntimeNavigation_Dev")))
        GetWorldTimerManager().SetTimerForNextTick(this,&ARogueliteNavigationReadinessTestRig::StartSuite);
}
void ARogueliteNavigationReadinessTestRig::AddCheck(const FString& Name,bool Passed)
{bAllPassed&=Passed;SuiteEvidence.Add((Passed?TEXT("PASS "):TEXT("FAIL "))+Name);UE_LOG(LogRoguelitePrototype,Display,TEXT("B3B_CHECK %s"),*SuiteEvidence.Last());}
void ARogueliteNavigationReadinessTestRig::RunB2EditorRegression()
{
    const auto Original=Settings;Settings=GetDefault<ARogueliteSeedAssembly>()->Settings;
    RunEditorRegression();Settings=Original;
}
void ARogueliteNavigationReadinessTestRig::StartSuite()
{
    SmallSettings=Settings;SmallTransform=GetActorTransform();
    bEnableRuntimeNavigation=false;Settings=GetDefault<ARogueliteSeedAssembly>()->Settings;RunRuntimeChecks();
    AddCheck(TEXT("B3A original 63-check runtime/Static/POI/rollback/20-seed regression"),bRuntimeChecksPassed);
    AddCheck(TEXT("B3A cleanup before navigation suite"),ClearRuntime());
    Settings=SmallSettings;bEnableRuntimeNavigation=true;CaseIndex=0;StartCase();
    GetWorldTimerManager().SetTimer(SuiteTimer,this,&ARogueliteNavigationReadinessTestRig::CheckCase,.5,true);
}
void ARogueliteNavigationReadinessTestRig::StartCase()
{
    Settings=SmallSettings;NavigationReadiness->BuildTimeoutSeconds=30;NavigationReadiness->MoveTimeoutSeconds=120;
    // Each case is a fresh GameWorld batch. Seed/candidate algorithm is unchanged.
    if(CaseIndex<=3)
    {Settings.Seed=CaseIndex<2?2:CaseIndex-2;SetActorRotation(SmallTransform.Rotator());CurrentCase=FString::Printf(TEXT("Small_Seed%d_Repeat%d"),Settings.Seed,CaseIndex);}
    else if(CaseIndex<=6)
    {
        const int32 Seeds[]={2,7,11};Settings.Seed=Seeds[CaseIndex-4];
        Settings.MinMainNodes=6;Settings.MaxMainNodes=6;Settings.MinBranches=2;Settings.MaxBranches=2;Settings.MaxNodes=8;
        Settings.MaxCandidatesPerEdge=128;
        SetActorRotation(FRotator(0,(CaseIndex-3)*90,0));CurrentCase=FString::Printf(TEXT("EightRooms_Seed%d_Yaw%d"),Settings.Seed,(CaseIndex-3)*90);
    }
    else
    {
        Settings.Seed=2;SetActorRotation(SmallTransform.Rotator());
        CurrentCase=CaseIndex==7?TEXT("ForcedBuildTimeout"):CaseIndex==8?TEXT("BlockedCorridorNavPath"):
            CaseIndex==9?TEXT("ActualMovementInterrupted"):TEXT("CancelOldBatchAndRetry");
        if(CaseIndex==7)NavigationReadiness->BuildTimeoutSeconds=.01f;
        if(CaseIndex==9)NavigationReadiness->MoveTimeoutSeconds=1;
    }
    UE_LOG(LogRoguelitePrototype,Display,TEXT("B3B_CASE_BEGIN Index=%d Name=%s"),CaseIndex,*CurrentCase);
    const bool Generated=GenerateRuntime();
    if(!Generated)
    {
        FRogueliteNavigationReadinessEvidence E;E.Seed=Settings.Seed;E.BatchId=Batch.BatchId;E.State=TEXT("GeometryFailed");E.Reason=Batch.Reason;
        E.Diagnostics.Add(CurrentCase);Cases.Add(E);AddCheck(CurrentCase+TEXT(" geometry generated"),false);return;
    }
    PublishNavigationState(Batch.BatchId,ERogueliteDungeonBuildStage::NavigationReady,TEXT("Negative test: Ready before paths/MoveTo must be refused."));
    AddCheck(TEXT("Current batch cannot publish Ready before actual paths/MoveTo"),!bDungeonReady && Stage==ERogueliteDungeonBuildStage::NavigationPending);
    if(CaseIndex==8)
    {
        // Independent explicit fixture AFTER C2 validation, BEFORE dynamic navigation.
        // It remains outside the batch's ownership; batch cleanup must preserve it.
        FActorSpawnParameters P;P.ObjectFlags|=RF_Transient;
        Blocker=GetWorld()->SpawnActor<AActor>(AActor::StaticClass(),Batch.Edges.Last().Corridor->GetActorTransform(),P);
        auto* Mesh=NewObject<UStaticMeshComponent>(Blocker,TEXT("B3B_IndependentFullWidthBlocker"));Blocker->AddInstanceComponent(Mesh);Blocker->SetRootComponent(Mesh);
        Mesh->SetMobility(EComponentMobility::Movable);Mesh->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));
        Mesh->SetCollisionProfileName(TEXT("BlockAll"));Mesh->SetCanEverAffectNavigation(true);
        Mesh->SetWorldTransform(FTransform(Batch.Edges.Last().Corridor->GetActorRotation(),Batch.Edges.Last().Corridor->GetActorTransform().TransformPosition(FVector(0,0,200)),FVector(1,6,4)));
        Mesh->RegisterComponent();BlockerTransform=Blocker->GetActorTransform();
    }
    if(CaseIndex==10)
    {
        CancelledBatch=Batch.BatchId;AddCheck(TEXT("Clear pending batch and revoke Ready"),ClearRuntime() && !bDungeonReady && NavigationReadiness->Report.bCleanupComplete);
        AddCheck(TEXT("Regenerate after pending cancel"),GenerateRuntime() && Batch.BatchId!=CancelledBatch);
        PublishNavigationState(CancelledBatch,ERogueliteDungeonBuildStage::NavigationReady,TEXT("Injected stale callback must be ignored."));
        AddCheck(TEXT("Old BatchId cannot publish Ready for replacement"),!bDungeonReady && Stage==ERogueliteDungeonBuildStage::NavigationPending);
    }
}
void ARogueliteNavigationReadinessTestRig::CheckCase()
{
    if(Stage==ERogueliteDungeonBuildStage::NavigationPending)
    {
        if(CaseIndex==9 && !NavigationReadiness->Report.Moves.IsEmpty() && NavigationReadiness->Report.Moves[0].RequestId!=0)
            for(TActorIterator<ARogueliteNavigationTestCharacter> It(GetWorld());It;++It)
                if(It->CreatedByVerifier.Get()==NavigationReadiness && It->CreationBatch==Batch.BatchId)It->GetCharacterMovement()->DisableMovement();
        return;
    }
    if(Stage!=ERogueliteDungeonBuildStage::Failed)
    {
        auto E=NavigationReadiness->Report;E.Diagnostics.Add(TEXT("Case=")+CurrentCase);Cases.Add(E);
        const bool Expected=CaseIndex==7?Stage==ERogueliteDungeonBuildStage::NavigationTimedOut:
            CaseIndex==9?(Stage==ERogueliteDungeonBuildStage::NavigationTimedOut || Stage==ERogueliteDungeonBuildStage::NavigationFailed):
            CaseIndex==8?Stage==ERogueliteDungeonBuildStage::NavigationFailed:IsDungeonReady();
        AddCheck(CurrentCase+TEXT(" expected final state"),Expected);
        if(CaseIndex==1)AddCheck(TEXT("Repeat Seed2 retains ordered spatial layout digest"),Cases[0].LayoutDigest==E.LayoutDigest);
        if(CaseIndex==4 || CaseIndex==5 || CaseIndex==6)AddCheck(TEXT("Expanded graph has eight actual rooms/seven corridors"),E.Rooms==8 && E.Corridors==7);
    }
    if(CaseIndex>=10)
    {
        GetWorldTimerManager().ClearTimer(SuiteTimer);bSuiteFinished=true;bSuitePassed=bAllPassed;
        UE_LOG(LogRoguelitePrototype,Display,TEXT("B3B_SUITE_FINISHED Passed=%d Cases=%d LastReady=%d"),bSuitePassed,Cases.Num(),IsDungeonReady());return;
    }
    const FGuid PreviousId=Batch.BatchId;
    AddCheck(TEXT("Current navigation+geometry batch clear"),ClearRuntime() && Batch.OwnedActors.IsEmpty() && !bDungeonReady);
    int32 RemainingNPCs=0,RemainingControllers=0;
    for(TActorIterator<ARogueliteNavigationTestCharacter> It(GetWorld());It;++It)
        if(!It->IsActorBeingDestroyed() && It->CreatedByVerifier.Get()==NavigationReadiness)++RemainingNPCs;
    for(TActorIterator<ARogueliteNavigationTestController> It(GetWorld());It;++It)
        if(!It->IsActorBeingDestroyed() && It->GetOwner()==this)++RemainingControllers;
    AddCheck(TEXT("Possessed test NPC/controller cleanup leaves zero owned actors"),RemainingNPCs==0 && RemainingControllers==0);
    if(CaseIndex==7 || CaseIndex==8 || CaseIndex==9)
        AddCheck(TEXT("Failure diagnostics survive cleanup"),NavigationReadiness->LastFailure.BatchId==PreviousId && !NavigationReadiness->LastFailure.bReady
            && NavigationReadiness->LastFailure.bCleanupComplete && !NavigationReadiness->LastFailure.Reason.IsEmpty());
    if(Blocker)AddCheck(TEXT("Independent navigation blocker preserved by batch cleanup"),IsValid(Blocker) && Blocker->GetActorTransform().Equals(BlockerTransform,.0001));
    RemoveFixture();++CaseIndex;StartCase();
}
void ARogueliteNavigationReadinessTestRig::RemoveFixture(){if(IsValid(Blocker))Blocker->Destroy();Blocker=nullptr;}
