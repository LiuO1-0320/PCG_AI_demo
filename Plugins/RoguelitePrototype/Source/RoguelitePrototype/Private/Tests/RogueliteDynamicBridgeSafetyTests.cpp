#include "Tests/RogueliteDynamicBridgeTestRig.h"
#include "RogueliteDynamicGameplayBridge.h"
#include "RogueliteCompactCorridorContract.h"
#include "RogueliteFloorProfile.h"
#include "RogueliteDungeonMap.h"
#include "RoguelitePrototype.h"
#include "Components/CapsuleComponent.h"
#include "Components/BoxComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "AIController.h"
#include "Misc/AutomationTest.h"
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Editor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "Tests/AutomationEditorCommon.h"
#include "Misc/FileHelper.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "TimerManager.h"

namespace G1ASafety
{
using Bridge=FRogueliteDynamicGameplayBridge;
const TCHAR* Map=TEXT("/Game/Roguelite/Tests/P04G1A/L_Roguelite_DynamicGameplayBridge_Dev");
ARogueliteDynamicBridgeTestRig* Rig(UWorld* W){if(W)for(TActorIterator<ARogueliteDynamicBridgeTestRig>I(W);I;++I)if(!I->IsActorBeingDestroyed())return *I;return nullptr;}
int32 Count(UWorld* W){int32 N=0;for(TActorIterator<AActor>I(W);I;++I)N+=!I->IsActorBeingDestroyed();return N;}
FString Dir(){FString D;FParse::Value(FCommandLine::Get(),TEXT("G1AEvidenceDir="),D);return D;}
void Save(const FString& Name,const FString& Text)
{const auto D=Dir();if(D.IsEmpty())return;IFileManager::Get().MakeDirectory(*D,true);FString File=D/Name;if(IFileManager::Get().FileExists(*File))File+=TEXT(".")+FDateTime::UtcNow().ToString(TEXT("%Y%m%d%H%M%S"))+TEXT(".rerun");FFileHelper::SaveStringToFile(Text,*File,FFileHelper::EEncodingOptions::ForceUTF8);}
struct FRun:public TSharedFromThis<FRun>
{
    TWeakObjectPtr<ARogueliteDynamicBridgeTestRig> Source;
    TArray<TWeakObjectPtr<ARogueliteDynamicBridgeTestCharacter>> Pawns;
    TArray<TWeakObjectPtr<AAIController>> Controllers;
    TWeakObjectPtr<ARogueliteDynamicBridgeTestGate> Gate;
    TWeakObjectPtr<AActor> External;
    TWeakObjectPtr<UBoxComponent> Guard;
    FTimerHandle Timer;
    FString Case,Rows,Digest,Epoch;
    FGuid OriginalBatch,Connection;
    FVector PawnStart[2],StartFeet,EndFeet,Normal;
    double Began=0,Action=0;
    int32 Step=0,BeforeCount=0,Samples=0;
    bool Good=true,NeedsNavigation=false;
    void Check(bool Value,const FString& Name){Good&=Value;Rows+=FString::Printf(TEXT("Check=%s\tPass=%d\n"),*Name,Value);}
    ARogueliteDynamicBridgeTestCharacter* SpawnPawn(bool Register,bool Controller=false,FGuid Expected={})
    {
        auto* A=Source.Get();if(!A)return nullptr;const FTransform Pose(FVector(10000+Pawns.Num()*600,10000,400));
        auto* P=A->GetWorld()->SpawnActorDeferred<ARogueliteDynamicBridgeTestCharacter>(ARogueliteDynamicBridgeTestCharacter::StaticClass(),Pose,A,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        if(!P)return nullptr;P->GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);P->FinishSpawning(Pose);
        P->GetCharacterMovement()->SetMovementMode(MOVE_Flying);Pawns.Add(P);
        if(Controller){FActorSpawnParameters Params;Params.Owner=A;Params.ObjectFlags|=RF_Transient;auto* C=A->GetWorld()->SpawnActor<AAIController>(Params);Controllers.Add(C);C->Possess(P);}
        FString Why;if(Register)Check(Bridge::RegisterComponent(A,Expected.IsValid()?Expected:A->Batch.BatchId,P,P->GetCapsuleComponent(),ERogueliteDynamicParticipant::Pawn,Why),TEXT("Register exact pawn capsule: ")+Why);
        P->GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);return P;
    }
    void Cleanup()
    {
        if(auto* A=Source.Get())Bridge::Disable(A);
        for(auto P:Pawns)if(P.IsValid())P->Destroy();for(auto C:Controllers)if(C.IsValid())C->Destroy();
        if(Gate.IsValid())Gate->Destroy();if(External.IsValid())External->Destroy();
        if(auto* A=Source.Get())if(!A->Batch.OwnedActors.IsEmpty())A->ClearRuntime();
    }
    void Finish(const FString& Why)
    {
        auto* A=Source.Get();if(!A)return;A->GetWorld()->GetTimerManager().ClearTimer(Timer);
        Rows+=TEXT("FinalReason=")+Why+TEXT("\n");
        Rows+=Bridge::RecoveryEvidence(A);Cleanup();
        Check(Count(A->GetWorld())==BeforeCount,TEXT("Only task-created actors cleaned; manual world actors retained"));
        Check(Bridge::ActiveComponentCount(A)==0,TEXT("No active component lease after cleanup"));
        A->bScenarioPassed=Good;A->bScenarioFinished=true;A->ScenarioEvidence=Rows;
        Save(TEXT("safety-")+Case+TEXT(".txt"),Rows+FString::Printf(TEXT("Passed=%d\n"),Good));
        UE_LOG(LogRoguelitePrototype,Display,TEXT("G1A_SAFETY Case=%s Passed=%d %s"),*Case,Good,*Why);
    }
    bool SelectConnection()
    {
        auto* A=Source.Get();if(!A)return false;
        ERogueliteRoomSide Side;ARogueliteParametricRoom* RA=nullptr;ARogueliteParametricRoom* RB=nullptr;
        if(Case==TEXT("LoopPassage"))
        {
            if(A->CommittedLoops.Edges.IsEmpty())return false;const auto& E=A->CommittedLoops.Edges[0];Connection=E.ConnectionId;Side=E.SocketA;
            const auto* R=A->Batch.Rooms.FindByPredicate([&](const auto& X){return X.NodeId==E.NodeA;});RA=R?R->Room.Get():nullptr;
        }
        else {const auto& E=A->Batch.Edges[0];Connection=E.ConnectionId;Side=E.ParentSocket;const auto* R=A->Batch.Rooms.FindByPredicate([&](const auto& X){return X.NodeId==E.ParentNode;});RA=R?R->Room.Get():nullptr;}
        FRogueliteDoorSocket S;if(!RA||!RA->TryGetDoorSocket(Side,S))return false;
        const auto Records=A->Batch.Registry->GetConnectionRecords();const auto* R=Records.FindByPredicate([&](const auto& X){return X.Id==Connection&&X.bActive;});
        if(!R||!R->RoomB)return false;RB=R->RoomB;FRogueliteDoorSocket B;if(!RB->TryGetDoorSocket(R->SocketB,B))return false;
        StartFeet=S.WorldPosition-S.WorldOutwardDirection*300;EndFeet=B.WorldPosition-B.WorldOutwardDirection*300;Normal=S.WorldOutwardDirection;
        FActorSpawnParameters Params;Params.Owner=A;Params.ObjectFlags|=RF_Transient;Gate=A->GetWorld()->SpawnActor<ARogueliteDynamicBridgeTestGate>(Params);
        Gate->PlaceAtSocket(S);FString Why;Check(Bridge::RegisterComponent(A,A->Batch.BatchId,Gate.Get(),Gate->Blocker,ERogueliteDynamicParticipant::Gate,Why),TEXT("Register gate's exact box: ")+Why);
        Rows+=FString::Printf(TEXT("Connection=%s\nGateSocket=%d\nGateThreshold=%s\n"),*Connection.ToString(),int32(Side),*S.WorldPosition.ToString());return true;
    }
    void Start()
    {
        auto* A=Source.Get();if(!A)return;BeforeCount=Count(A->GetWorld());Began=FPlatformTime::Seconds();
        A->bScenarioFinished=A->bScenarioPassed=false;A->Scenario=Case;
        NeedsNavigation=Case==TEXT("LegalDynamics")||Case==TEXT("LoopPassage");A->bNavigationEnabled=NeedsNavigation;
        if(!A->GenerateRuntime()){Check(false,TEXT("Actual runtime geometry generation"));Finish(A->LastGeometry.Reason);return;}
        Rows=FString::Printf(TEXT("Case=%s\nWorld=%s\nSeed=%d\nBatch=%s\nGraphDigest=%s\nLayoutDigest=%s\nRooms=%d\nTree=%d\nLoop=%d\nNavigationRequested=%d\n"),*Case,*A->GetWorld()->GetPathName(),A->Seed,*A->Batch.BatchId.ToString(),*A->Batch.GraphDigest,*A->Batch.LayoutDigest,A->Batch.Rooms.Num(),A->Batch.Edges.Num(),A->CommittedLoops.Edges.Num(),NeedsNavigation);
        TArray<FRogueliteCompactCorridorMeasurement> Lengths;FString Why;
        Check(FRogueliteCompactCorridorContract::Actual(A->Batch,A->CommittedLoops,Lengths,Why),TEXT("Actual unchanged 1200-1500cm length contract: ")+Why);
        for(const auto& E:A->Batch.Edges){Check(E.Validation.bSucceeded&&E.Validation.bCapsulePassed&&E.Validation.bGeometryPassed,TEXT("Original Tree C2 validation"));Rows+=FString::Printf(TEXT("C2 Tree Edge=%d Probes=%d Length=%.2f\n"),E.EdgeId,E.Validation.Probes.Num(),E.CorridorLength);}
        for(const auto& E:A->CommittedLoops.Edges){Check(E.Validation.bSucceeded&&E.Validation.bCapsulePassed&&E.Validation.bGeometryPassed,TEXT("Original Loop C2 validation"));Rows+=FString::Printf(TEXT("C2 Loop Edge=%d Probes=%d\n"),E.EdgeId,E.Validation.Probes.Num());}
        for(const auto& R:A->Batch.Rooms){Check(R.Room&&R.Room->RoomHeight==500&&R.Room->GetActorScale3D().Equals(FVector::OneVector),TEXT("Original 500cm unit-scale room instance"));Rows+=FString::Printf(TEXT("Room Node=%d Type=%d Template=%s Pose=%s\n"),R.NodeId,int32(R.RoomType),*R.TemplateId.ToString(),*R.PlacedTransform.ToHumanReadableString());}
        TWeakPtr<FRun> Weak=AsShared();A->GetWorld()->GetTimerManager().SetTimer(Timer,FTimerDelegate::CreateWeakLambda(A,[Weak](){if(auto R=Weak.Pin())R->Poll();}),.1,true);
    }
    void Poll()
    {
        auto* A=Source.Get();if(!A)return;
        if(FPlatformTime::Seconds()-Began>240){Check(false,TEXT("Bounded scenario timeout"));Finish(TEXT("Timeout"));return;}
        if(Step==0)
        {
            if(NeedsNavigation&&!A->IsDungeonReady())
            {const auto State=A->NavigationReadiness->Report.State;if(State.Contains(TEXT("Failed"))||State.Contains(TEXT("Unavailable"))||State.Contains(TEXT("TimedOut"))){Check(false,TEXT("Strict navigation failed"));Finish(State);}return;}
            OriginalBatch=A->Batch.BatchId;Digest=A->CommittedLoops.GeometryDigest;Epoch=A->GetNavigationEpoch();FString Why;
            Check(A->GetTraversalSnapshot().bCurrent,TEXT("Initial actual structural Current"));
            Check(Bridge::Enable(A,OriginalBatch,Why),TEXT("Explicit current Source/Batch opt-in: ")+Why);
            if(NeedsNavigation)
            {
                const auto& N=A->NavigationReadiness->Report;Rows+=FString::Printf(TEXT("InitialStrictReady=%d\nPaths=%d\nMoves=%d\n"),A->IsDungeonReady(),N.Paths.Num(),N.Moves.Num());
                Check(N.bReady&&!N.Paths.IsEmpty()&&!N.Moves.IsEmpty(),TEXT("Original strict paths and real Character MoveTo completed"));
                for(const auto& P:N.Paths)Check(P.bSucceeded&&!P.bPartial,TEXT("Original full non-partial path"));for(const auto& M:N.Moves)Check(M.bSucceeded,TEXT("Original MoveTo success"));
                FString Text;FRogueliteNavigationReadinessEvidence::StaticStruct()->ExportText(Text,&N,nullptr,nullptr,PPF_None,nullptr);Save(TEXT("safety-")+Case+TEXT("-navigation.txt"),Text);
                if(!SelectConnection()){Check(false,TEXT("Real connection lookup"));Finish(TEXT("No actual connection"));return;}
                SpawnPawn(true,true);SpawnPawn(true);Check(Bridge::ActiveComponentCount(A)==3,TEXT("Two exact root capsules plus one gate box"));
                for(int32 I=0;I<2;++I){PawnStart[I]=Pawns[I]->GetActorLocation();Pawns[I]->AddMovementInput(I?FVector::RightVector:FVector::ForwardVector,1,true);}
            }
            else if(Case==TEXT("WrongBatch"))
            {auto* P=SpawnPawn(false);P->Tags.Add(TEXT("Gameplay"));Check(!Bridge::RegisterComponent(A,FGuid::NewGuid(),P,P->GetCapsuleComponent(),ERogueliteDynamicParticipant::Pawn,Why),TEXT("Wrong-batch/tag spoof refused: ")+Why);}
            else if(Case==TEXT("UnregisteredPawn"))SpawnPawn(false);
            else if(Case==TEXT("ExternalBox"))
            {auto* E=A->GetWorld()->SpawnActor<AActor>();External=E;auto* B=NewObject<UBoxComponent>(E);E->SetRootComponent(B);B->SetMobility(EComponentMobility::Movable);B->SetBoxExtent(FVector(50));B->SetCollisionResponseToAllChannels(ECR_Block);B->SetCanEverAffectNavigation(false);B->SetWorldLocation(FVector(11000,11000,200));B->RegisterComponent();}
            else if(Case==TEXT("StaticRoom"))A->Batch.Rooms[0].Room->RoomLength+=100;
            else if(Case==TEXT("StaticCollision"))
            {
                TInlineComponentArray<UPrimitiveComponent*> Parts;A->Batch.Rooms[0].Room->GetComponents(Parts);bool Changed=false;
                for(auto* P:Parts)if(P->GetFName()==TEXT("Floor")&&P->Mobility==EComponentMobility::Static)
                {P->SetCollisionEnabled(ECollisionEnabled::NoCollision);Changed=true;Rows+=TEXT("Intentional negative only: actual registered Static floor collision changed.\n");break;}
                Check(Changed,TEXT("Actual Static shell component selected via real room record"));
            }
            else
            {
                auto* P=SpawnPawn(true);PawnStart[0]=P->GetActorLocation();
                if(Case==TEXT("OwnerChanged"))P->SetOwner(nullptr);
                if(Case==TEXT("SameActorComponent"))
                {
                    auto* B=NewObject<UBoxComponent>(P);P->AddInstanceComponent(B);B->SetMobility(EComponentMobility::Static);B->SetBoxExtent(FVector(50));B->SetCanEverAffectNavigation(false);B->SetCollisionEnabled(ECollisionEnabled::NoCollision);
                    B->SetCollisionResponseToAllChannels(ECR_Block);B->SetRelativeLocation(FVector(12000,12000,400));B->RegisterComponent();Guard=B;
                    Check(B->GetAttachParent()==nullptr&&B->Mobility==EComponentMobility::Static,TEXT("Independent same-Actor Static guard (no Static movement)"));
                    Check(!Bridge::IsComponentIsolated(A,OriginalBatch,B),TEXT("Other component on registered Actor has no exemption"));B->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
                }
                if(Case==TEXT("ActorDestroy")){P->Destroy();Check(Bridge::ActiveComponentCount(A)==0,TEXT("Destroyed Actor's component lease immediately inactive"));}
                if(Case==TEXT("DuplicateLifecycle"))
                {
                    Check(Bridge::RegisterComponent(A,OriginalBatch,P,P->GetCapsuleComponent(),ERogueliteDynamicParticipant::Pawn,Why)&&Bridge::ActiveComponentCount(A)==1,TEXT("Duplicate registration idempotent"));
                    Check(!Bridge::RegisterComponent(A,OriginalBatch,P,P->GetCapsuleComponent(),ERogueliteDynamicParticipant::Gate,Why),TEXT("Wrong participant kind rejected"));
                    Check(!Bridge::RegisterComponent(A,OriginalBatch,P,P->GetCapsuleComponent(),static_cast<ERogueliteDynamicParticipant>(255),Why),TEXT("Invalid participant enum rejected"));
                    P->GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
                    Bridge::UnregisterComponent(A,P->GetCapsuleComponent());Bridge::UnregisterComponent(A,P->GetCapsuleComponent());
                    Check(Bridge::ActiveComponentCount(A)==0,TEXT("Repeated unregister idempotent"));
                }
                if(Case==TEXT("StaleBatch"))
                {
                    Check(A->ClearRuntime(),TEXT("Actual Source Clear succeeds"));Check(Bridge::ActiveComponentCount(A)==0,TEXT("Source Clear invalidates leases immediately"));
                    Check(A->GenerateRuntime()&&A->Batch.BatchId!=OriginalBatch,TEXT("New batch has distinct actual identity"));
                    Check(!Bridge::RegisterComponent(A,OriginalBatch,P,P->GetCapsuleComponent(),ERogueliteDynamicParticipant::Pawn,Why),TEXT("Old batch lease cannot transfer"));
                    Rows+=TEXT("ReplacementBatch=")+A->Batch.BatchId.ToString()+TEXT("\n");P->AddMovementInput(FVector::ForwardVector,1,true);
                }
                if(Case==TEXT("SourceClear")){Check(A->ClearRuntime(),TEXT("Explicit source clear"));Check(Bridge::ActiveComponentCount(A)==0,TEXT("All old component grants revoked"));Check(Bridge::QueryConnection(A,FGuid::NewGuid()).State==ERogueliteGameplayPassage::Unavailable,TEXT("No stale successful passage after Clear"));}
            }
            Step=1;Action=FPlatformTime::Seconds();return;
        }
        if(NeedsNavigation)
        {
            if(Step==1)
            {
                for(int32 I=0;I<2;++I)Pawns[I]->AddMovementInput(I?FVector::RightVector:FVector::ForwardVector,1,true);
                const bool Current=A->GetTraversalSnapshot().bCurrent;Check(Current&&A->bGeometryValidated&&A->Batch.BatchId==OriginalBatch,TEXT("Registered movement survives WatchGeometry"));++Samples;
                Rows+=FString::Printf(TEXT("Motion t=%.3f Pawn1=%s Pawn2=%s Current=%d Digest=%s\n"),FPlatformTime::Seconds()-Action,*Pawns[0]->GetActorLocation().ToString(),*Pawns[1]->GetActorLocation().ToString(),Current,*FRogueliteLoopCommitQueries::GeometryDigest(A,A->Batch,A->CommittedLoops));
                if(FPlatformTime::Seconds()-Action<1.3)return;
                Check(Samples>=10&&FVector::Dist(Pawns[0]->GetActorLocation(),PawnStart[0])>100&&FVector::Dist(Pawns[1]->GetActorLocation(),PawnStart[1])>100,TEXT("Two independent actual CharacterMovement displacements across multiple Watch periods"));
                Check(Digest==FRogueliteLoopCommitQueries::GeometryDigest(A,A->Batch,A->CommittedLoops),TEXT("Exact structure digest unchanged after movement"));
                Gate->SetClosed(true);Step=2;Action=FPlatformTime::Seconds();return;
            }
            if(Step==2)
            {
                const auto Q=Bridge::QueryConnection(A,Connection,Pawns[0].Get());
                Check(Q.State==ERogueliteGameplayPassage::GameplayBlocked&&!Q.bForwardCapsule&&!Q.bReverseCapsule,TEXT("Closed gate physically rejects two-way capsule sweeps"));
                Check(Q.bStructuralCurrent&&Q.bBaseNavigationReady&&A->Batch.BatchId==OriginalBatch,TEXT("Legal gate closure preserves structure and original navigation report"));
                Rows+=FString::Printf(TEXT("Closed t=%.3f State=%d BaseReady=%d Forward=%d Reverse=%d StaticPath=%d\n"),FPlatformTime::Seconds()-Action,int32(Q.State),Q.bBaseNavigationReady,Q.bForwardCapsule,Q.bReverseCapsule,Q.bFullPath);
                FHitResult Hit;Check(A->GetWorld()->LineTraceSingleByChannel(Hit,Gate->GetActorLocation()-Normal*100,Gate->GetActorLocation()+Normal*100,ECC_Visibility)&&Hit.GetComponent()==Gate->Blocker,TEXT("Closed gate real Visibility collision"));
                if(FPlatformTime::Seconds()-Action<1.2)return;
                Gate->SetClosed(false);const auto Open=Bridge::QueryConnection(A,Connection,Pawns[0].Get());
                Check(Open.State==ERogueliteGameplayPassage::Recovering&&Open.bForwardCapsule&&Open.bReverseCapsule&&Open.bFullPath&&!Open.bMoveVerified,TEXT("Open state requires fresh recovery; not old NavPath success"));
                auto* P=Pawns[0].Get();P->GetCharacterMovement()->StopMovementImmediately();P->SetActorLocation(StartFeet+FVector(0,0,90));
                P->GetCharacterMovement()->SetMovementMode(MOVE_Flying);FString FlyingWhy;
                Check(!Bridge::BeginRecovery(A,Connection,P,Controllers[0].Get(),FlyingWhy),TEXT("Flying pawn cannot substitute standing recovery proof: ")+FlyingWhy);
                P->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
                Rows+=TEXT("Recovery setup placement (not movement proof)=")+P->GetActorLocation().ToString()+TEXT("\n");
                FString Why;Check(Bridge::BeginRecovery(A,Connection,P,Controllers[0].Get(),Why),TEXT("New actual post-open MoveTo: ")+Why);Step=3;Action=FPlatformTime::Seconds();return;
            }
            const auto Q=Bridge::QueryConnection(A,Connection,Pawns[0].Get());
            if(Q.State!=ERogueliteGameplayPassage::Traversable&&FPlatformTime::Seconds()-Action<35)return;
            Check(Q.State==ERogueliteGameplayPassage::Traversable&&Q.bMoveVerified&&Q.bFullPath&&Q.bForwardCapsule&&Q.bReverseCapsule,TEXT("Real post-open full path, actual corridor MoveTo and two-way collision recovery"));
            Check(A->GetTraversalSnapshot().bCurrent&&A->IsDungeonReady()&&A->GetNavigationEpoch()==Epoch,TEXT("Original strict navigation epoch unchanged; no silent regeneration"));
            Finish(TEXT("Registered two-Pawn movement, closed-gate physical blocking, independent GameplayBlocked and actual recovery."));return;
        }
        if(Case==TEXT("StaleBatch")&&!Pawns.IsEmpty()&&Pawns[0].IsValid())Pawns[0]->AddMovementInput(FVector::ForwardVector,1,true);
        const bool ExpectClear=Case!=TEXT("ActorDestroy")&&Case!=TEXT("DuplicateLifecycle")&&Case!=TEXT("SourceClear");
        Rows+=FString::Printf(TEXT("WatchSample t=%.3f Current=%d Geometry=%d Owned=%d ActiveComponents=%d\n"),FPlatformTime::Seconds()-Action,A->GetTraversalSnapshot().bCurrent,A->bGeometryValidated,A->Batch.OwnedActors.Num(),Bridge::ActiveComponentCount(A));
        if(FPlatformTime::Seconds()-Action<1.2)return;
        if(ExpectClear)Check(!A->bGeometryValidated&&A->Batch.OwnedActors.IsEmpty()&&A->Stage==ERogueliteDungeonBuildStage::NavigationFailed,TEXT("Unapproved geometry still invokes original Watch rejection and owned cleanup"));
        else if(Case!=TEXT("SourceClear"))Check(A->bGeometryValidated&&A->GetTraversalSnapshot().bCurrent,TEXT("Safe lifecycle does not invalidate existing structure"));
        if(External.IsValid())Check(!External->IsActorBeingDestroyed(),TEXT("Foreign external obstacle was not deleted by batch cleanup"));
        Finish(TEXT("Bounded component identity/lifecycle/negative safety case."));
    }
};
TMap<TWeakObjectPtr<ARogueliteDynamicBridgeTestRig>,TSharedPtr<FRun>> Runs;
class FObserve:public IAutomationLatentCommand
{
    FAutomationTestBase* Test;FString Case;double Started=0;bool Invoked=false,Legacy=false;
public:
    FObserve(FAutomationTestBase* T,FString C,bool Old=false):Test(T),Case(MoveTemp(C)),Legacy(Old){}
    bool Update()override
    {
        auto* W=GEditor?GEditor->PlayWorld.Get():nullptr;if(!W)return false;
        if(!Started){Started=FPlatformTime::Seconds();return false;}if(FPlatformTime::Seconds()-Started<1)return false;
        auto* A=Rig(W);if(!A){Test->AddError(TEXT("Actual G1A GameWorld missing"));return true;}
        if(!Invoked){Invoked=true;if(!A->StartScenario(Case)){Test->AddError(TEXT("Advanced safety case did not start"));return true;}}
        if(!A->bScenarioFinished&&FPlatformTime::Seconds()-Started<255)return false;
        Test->TestTrue(TEXT("Safety case actual assertions and cleanup"),A->bScenarioFinished&&A->bScenarioPassed&&A->Batch.OwnedActors.IsEmpty());
        if(Legacy){Test->TestEqual(TEXT("Legacy runtime retains three physical connections"),A->LastGeometry.Connections.Num(),3);for(const auto& E:A->LastGeometry.Connections)Test->TestTrue(TEXT("Original Legacy 59 probes unchanged"),E.bSucceeded&&E.bCapsulePassed&&E.Probes.Num()==59);}return true;
    }
};
struct FEditorState
{TWeakObjectPtr<ARogueliteDynamicBridgeTestRig> Rig;TStrongObjectPtr<URogueliteFloorProfile> Profile;FRoguelitePreparedTemplateCatalog Catalog;ERogueliteLoopTestPreset Preset=ERogueliteLoopTestPreset::EightRoomSingleLoop;bool Auto=false;int32 Count=0;};
class FRestore:public IAutomationLatentCommand
{
    TSharedPtr<FEditorState> Before;FAutomationTestBase* Test;
public:FRestore(TSharedPtr<FEditorState> B,FAutomationTestBase* T):Before(B),Test(T){}
    bool Update()override{if(GEditor->PlayWorld)return false;if(auto* A=Before->Rig.Get()){A->FloorProfile=Before->Profile.Get();A->PreparedTemplates=Before->Catalog;A->Preset=Before->Preset;A->bAutoRunScenario=Before->Auto;Test->TestEqual(TEXT("No PIE actors written to Editor map"),Count(A->GetWorld()),Before->Count);}return true;}
};
}

bool StartG1AAdvancedScenario(ARogueliteDynamicBridgeTestRig* A,const FString& Name)
{
    using namespace G1ASafety;const TArray<FString> Allowed={TEXT("LegalDynamics"),TEXT("LoopPassage"),TEXT("UnregisteredPawn"),TEXT("ExternalBox"),TEXT("WrongBatch"),TEXT("StaleBatch"),TEXT("StaticRoom"),TEXT("StaticCollision"),TEXT("SameActorComponent"),TEXT("ActorDestroy"),TEXT("OwnerChanged"),TEXT("DuplicateLifecycle"),TEXT("SourceClear")};
    if(!IsValid(A)||!A->GetWorld()->IsGameWorld()||!A->Batch.OwnedActors.IsEmpty()||!Allowed.Contains(Name))return false;
    for(auto It=Runs.CreateIterator();It;++It)if(!It.Key().IsValid()||!It.Key()->GetWorld()||It.Key()->GetWorld()->bIsTearingDown)It.RemoveCurrent();
    auto C=MakeShared<FRun>();C->Source=A;C->Case=Name;Runs.Add(A,C);C->Start();return true;
}
IMPLEMENT_COMPLEX_AUTOMATION_TEST(FG1ASafety,"ProjectVerification.G1A.Safety",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
void FG1ASafety::GetTests(TArray<FString>& Names,TArray<FString>& Commands)const
{Names={TEXT("LegalDynamics"),TEXT("LoopPassage"),TEXT("UnregisteredPawn"),TEXT("ExternalBox"),TEXT("WrongBatch"),TEXT("StaleBatch"),TEXT("StaticRoom"),TEXT("StaticCollision"),TEXT("SameActorComponent"),TEXT("ActorDestroy"),TEXT("OwnerChanged"),TEXT("DuplicateLifecycle"),TEXT("SourceClear")};Commands=Names;}
bool FG1ASafety::RunTest(const FString& Case)
{
    using namespace G1ASafety;if(!GEditor||GEditor->PlayWorld)return false;auto* A=Rig(GEditor->GetEditorWorldContext().World());
    if(!A||A->GetWorld()->GetOutermost()->GetName()!=Map)return false;
    auto B=MakeShared<FEditorState>();B->Rig=A;B->Profile=TStrongObjectPtr<URogueliteFloorProfile>(A->FloorProfile.Get());B->Catalog=A->PreparedTemplates;B->Preset=A->Preset;B->Auto=A->bAutoRunScenario;B->Count=Count(A->GetWorld());A->bAutoRunScenario=false;
    const TCHAR* P=Case==TEXT("LoopPassage")?TEXT("/Game/Roguelite/ProductionCandidates/P03M1/Profiles/DA_M1_Floor2_Candidate.DA_M1_Floor2_Candidate"):TEXT("/Game/Roguelite/ProductionCandidates/P03M1/Profiles/DA_M1_Floor1_Candidate.DA_M1_Floor1_Candidate");
    A->FloorProfile=LoadObject<URogueliteFloorProfile>(nullptr,P);if(!A->FloorProfile)return false;
    const auto Config=A->FloorProfile->ResolvePreview(0);FString Why;if(!Config.bValid||!FRoguelitePreparedTemplateCatalog::CaptureEditor(A,Config.Source,A->PreparedTemplates,Why))return false;
    auto& F=FAutomationTestFramework::Get();F.EnqueueLatentCommand(MakeShared<FStartPIECommand>(false));F.EnqueueLatentCommand(MakeShared<FObserve>(this,Case));F.EnqueueLatentCommand(MakeShared<FEndPlayMapCommand>());F.EnqueueLatentCommand(MakeShared<FRestore>(B,this));return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FG1AEditorDenied,"ProjectVerification.G1A.EditorWorldDenied",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FG1AEditorDenied::RunTest(const FString&)
{
    using namespace G1ASafety;if(!GEditor||GEditor->PlayWorld)return false;auto* A=Rig(GEditor->GetEditorWorldContext().World());if(!A)return false;FString Why;
    return TestFalse(TEXT("EditorWorld cannot grant a Gameplay component lease"),Bridge::Enable(A,FGuid::NewGuid(),Why))
        &&TestTrue(TEXT("EditorWorld cannot return current gameplay traversal"),Bridge::QueryConnection(A,FGuid::NewGuid()).State==ERogueliteGameplayPassage::Unavailable);
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FG1ALegacy,"ProjectVerification.G1A.LegacyRuntimeNoOptIn",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FG1ALegacy::RunTest(const FString&)
{
    using namespace G1ASafety;if(!GEditor||GEditor->PlayWorld)return false;auto* A=Rig(GEditor->GetEditorWorldContext().World());if(!A||A->GetWorld()->GetOutermost()->GetName()!=Map)return false;
    auto B=MakeShared<FEditorState>();B->Rig=A;B->Profile=TStrongObjectPtr<URogueliteFloorProfile>(A->FloorProfile.Get());B->Catalog=A->PreparedTemplates;B->Preset=A->Preset;B->Auto=A->bAutoRunScenario;B->Count=Count(A->GetWorld());
    A->Preset=ERogueliteLoopTestPreset::FourRoomTree;A->FloorProfile=nullptr;A->bAutoRunScenario=false;
    const auto Settings=FRogueliteRuntimeLoopAssembler::SourcePreset(0,true);FString Why;
    if(!FRoguelitePreparedTemplateCatalog::CaptureEditor(A,Settings,A->PreparedTemplates,Why)){A->Preset=B->Preset;A->FloorProfile=B->Profile.Get();A->PreparedTemplates=B->Catalog;A->bAutoRunScenario=B->Auto;AddError(Why);return false;}
    auto& F=FAutomationTestFramework::Get();F.EnqueueLatentCommand(MakeShared<FStartPIECommand>(false));F.EnqueueLatentCommand(MakeShared<FObserve>(this,TEXT("BaselinePawn"),true));F.EnqueueLatentCommand(MakeShared<FEndPlayMapCommand>());F.EnqueueLatentCommand(MakeShared<FRestore>(B,this));return true;
}
#endif
