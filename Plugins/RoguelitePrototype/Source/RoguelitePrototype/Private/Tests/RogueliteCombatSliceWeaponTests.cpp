#include "Tests/RogueliteCombatSliceRig.h"
#include "Gameplay/RogueliteCombatSlicePlayer.h"
#include "Gameplay/RogueliteCombatSliceActors.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "Misc/AutomationTest.h"
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Editor.h"
#include "Tests/AutomationEditorCommon.h"
#include "Engine/LocalPlayer.h"
#include "EngineUtils.h"
#include "EnhancedInputSubsystems.h"
#include "InputActionValue.h"
#include "Misc/FileHelper.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "HAL/FileManager.h"

namespace CombatSliceWeaponVerification
{
struct FOrderProof {bool bObserved=false,bWasDormant=false,bActivated=false;float HealthBefore=0;int32 Deaths=0;};
class FRestoreMode:public IAutomationLatentCommand
{
    TWeakObjectPtr<ARogueliteCombatSliceRig> Rig;bool Previous;
public:
    FRestoreMode(ARogueliteCombatSliceRig* R,bool Value):Rig(R),Previous(Value){}
    bool Update()override {if(Rig.IsValid())Rig->bEnableCombatEncounter=Previous;return true;}
};
class FExercise:public IAutomationLatentCommand
{
    FAutomationTestBase* Test;FString Scenario,Rows;double Started=0,Action=0;int32 Step=0;
    TWeakObjectPtr<ARogueliteCombatSliceRig> Rig;TWeakObjectPtr<ARogueliteSliceEnemy> Enemy,Outside;
    TSharedPtr<FOrderProof> Proof=MakeShared<FOrderProof>();FDelegateHandle AcceptedHandle;
    FGuid FirstShot;float PlayerHealthBefore=0;FVector DormantPosition;
    bool Check(bool Good,const FString& What)
    {Rows+=FString::Printf(TEXT("Check=%s\tPass=%d\n"),*What,Good);if(!Good)Test->AddError(What);return Good;}
    bool Finish(bool Good,const FString& Why)
    {
        if(!Good)Test->AddError(Why);
        if(auto* R=Rig.Get())if(auto* FPS=R->GetSlicePlayer())FPS->OnShotAccepted.Remove(AcceptedHandle);
        if(Enemy.IsValid()){Enemy->Quiesce();Enemy->Destroy();}if(Outside.IsValid()){Outside->Quiesce();Outside->Destroy();}
        Rows+=FString::Printf(TEXT("Passed=%d\nReason=%s\n"),Good,*Why);
        FString Dir;if(FParse::Value(FCommandLine::Get(),TEXT("G1BEvidenceDir="),Dir))
        {
            IFileManager::Get().MakeDirectory(*Dir,true);FString File=Dir/TEXT("stage1-")+Scenario+TEXT(".txt");
            if(IFileManager::Get().FileExists(*File))File+=TEXT(".")+FDateTime::UtcNow().ToString(TEXT("%Y%m%d%H%M%S"))+TEXT(".rerun");
            FFileHelper::SaveStringToFile(Rows,*File);
        }
        return true;
    }
    bool Fixtures(UWorld* W,ARogueliteCombatSliceRig* R,ARogueliteSlicePlayer* FPS)
    {
        const auto Snapshot=R->GetTraversalSnapshot();const FRogueliteSeedRoomRecord* Room=nullptr;const FRogueliteSeedRoomRecord* Other=nullptr;
        for(const auto& N:Snapshot.Nodes)if(N.Role!=TEXT("Start")&&N.Role!=TEXT("Final")&&N.RoomType==ERogueliteSeedRoomType::Combat)
        {
            auto* Found=R->Batch.Rooms.FindByPredicate([&](const auto& V){return V.NodeId==N.NodeId;});
            if(Found&&R->Settings.RoomTemplateRoles.NormalTemplateIds.Contains(Found->TemplateId)){if(!Room)Room=Found;else if(!Other)Other=Found;}
        }
        if(!Room||!Other)return false;
        auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(W);if(!Nav)return false;
        FCollisionQueryParams Q(SCENE_QUERY_STAT(G1B_WeaponFixture),false,FPS);FVector PPos,EPos;bool FoundPair=false;
        const auto StandingOnFloor=[&](const FRogueliteSeedRoomRecord& Record,const FNavLocation& NavPoint,FVector& Center)
        {
            FHitResult Ground;
            if(!W->LineTraceSingleByChannel(Ground,NavPoint.Location+FVector(0,0,150),NavPoint.Location-FVector(0,0,300),ECC_Visibility,Q)
                ||Ground.GetActor()!=Record.Room.Get()||Ground.ImpactNormal.Z<0.9f
                ||FMath::Abs(Record.PlacedTransform.InverseTransformPosition(Ground.ImpactPoint).Z)>1)return false;
            Center=Ground.ImpactPoint+FVector(0,0,90);
            Rows+=FString::Printf(TEXT("PhysicalFloor Node=%d NavZ=%.3f Impact=%s StandingCenter=%s\n"),Record.NodeId,NavPoint.Location.Z,*Ground.ImpactPoint.ToString(),*Center.ToString());return true;
        };
        for(const auto& Pair:TArray<TPair<FVector,FVector>>{{FVector(-500,-400,10),FVector(100,-400,10)},{FVector(-400,-500,10),FVector(-400,100,10)},{FVector(-500,400,10),FVector(100,400,10)},{FVector(300,-500,10),FVector(300,100,10)}})
        {
            FNavLocation A,B;
            if(!Nav->ProjectPointToNavigation(Room->PlacedTransform.TransformPosition(Pair.Key),A,FVector(60,60,80))||!Nav->ProjectPointToNavigation(Room->PlacedTransform.TransformPosition(Pair.Value),B,FVector(60,60,80)))continue;
            if(!StandingOnFloor(*Room,A,PPos)||!StandingOnFloor(*Room,B,EPos))continue;
            FHitResult Hit;
            if(W->OverlapBlockingTestByChannel(PPos,FQuat::Identity,ECC_Pawn,FCollisionShape::MakeCapsule(34,88),Q)||W->OverlapBlockingTestByChannel(EPos,FQuat::Identity,ECC_Pawn,FCollisionShape::MakeCapsule(34,88),Q))continue;
            if(W->LineTraceSingleByChannel(Hit,PPos+FVector(0,0,64),EPos,ECC_Visibility,Q))continue;
            FoundPair=true;break;
        }
        if(!FoundPair)return false;
        // Explicit fixture placement, not a claim of room-entry or corridor movement proof.
        FPS->SetActorLocation(PPos,false,nullptr,ETeleportType::TeleportPhysics);FPS->GetCharacterMovement()->StopMovementImmediately();
        FActorSpawnParameters Params;Params.Owner=R;Params.ObjectFlags|=RF_Transient;
        auto* E=W->SpawnActor<ARogueliteSliceEnemy>(Params);if(!E)return false;Enemy=E;FString Why;
        if(!E->Configure(R,R->Batch.BatchId,Room->NodeId,FGuid::NewGuid(),FTransform(FRotator::ZeroRotator,EPos),Why)){Rows+=Why;return false;}
        auto* O=W->SpawnActor<ARogueliteSliceEnemy>(Params);if(!O)return false;Outside=O;bool PlacedOther=false;
        for(const auto& Local:TArray<FVector>{{0,-100,10},{0,100,10},{-400,-400,10},{400,-400,10}})
        {
            FNavLocation N;if(!Nav->ProjectPointToNavigation(Other->PlacedTransform.TransformPosition(Local),N,FVector(60,60,80)))continue;
            FVector Standing;if(!StandingOnFloor(*Other,N,Standing))continue;
            if(O->Configure(R,R->Batch.BatchId,Other->NodeId,FGuid::NewGuid(),FTransform(FRotator::ZeroRotator,Standing),Why)){PlacedOther=true;break;}
        }
        if(!PlacedOther){Rows+=Why;return false;}
        const auto WeakEnemy=Enemy;const auto SharedProof=Proof;
        AcceptedHandle=FPS->OnShotAccepted.AddLambda([WeakEnemy,SharedProof](const FRogueliteSliceShot& Shot)
        {
            if(auto* Target=WeakEnemy.Get())
            {
                SharedProof->bObserved=true;SharedProof->bWasDormant=Target->IsDormant();SharedProof->HealthBefore=Target->Health->GetHealth();
                SharedProof->bActivated=Target->Activate(Shot.Shooter.Get());
            }
        });
        E->Health->OnKilled.AddLambda([SharedProof](URogueliteSliceHealth*){++SharedProof->Deaths;});
        DormantPosition=E->GetActorLocation();PlayerHealthBefore=FPS->HealthComponent->GetHealth();
        Rows+=FString::Printf(TEXT("Batch=%s\nNormalNode=%d\nOtherNormalNode=%d\nFixturePlayer=%s\nFixtureEnemy=%s\nSetupPlacementIsNotEntryProof=1\n"),*R->Batch.BatchId.ToString(),Room->NodeId,Other->NodeId,*PPos.ToString(),*EPos.ToString());
        return true;
    }
public:
    FExercise(FAutomationTestBase* T,FString S):Test(T),Scenario(MoveTemp(S)){}
    bool Update() override
    {
        auto* W=GEditor?GEditor->PlayWorld.Get():nullptr;if(!W)return false;
        if(!Started){Started=FPlatformTime::Seconds();Rows=TEXT("World=")+W->GetPathName()+TEXT("\n");}
        const double Now=FPlatformTime::Seconds();
        if(!Rig.IsValid())for(TActorIterator<ARogueliteCombatSliceRig>I(W);I;++I){Rig=*I;break;}
        auto* R=Rig.Get();if(!R)return Finish(false,TEXT("Independent Rig missing"));
        if(Now-Started>220)return Finish(false,TEXT("Weapon test timeout: ")+R->SliceStatus);
        if(!R->SliceFailure.IsEmpty())return Finish(false,R->SliceFailure);
        auto* FPS=R->GetSlicePlayer();if(!R->bPlayerBridgeReady||!FPS)return false;
        if(!FPS->HasLiveBinding())return Finish(false,TEXT("Actual player binding became invalid"));
        auto* PC=Cast<ARogueliteSlicePlayerController>(FPS->GetController());
        auto* Input=PC&&PC->GetLocalPlayer()?PC->GetLocalPlayer()->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>():nullptr;
        if(!Input||!PC->GetFireAction())return Finish(false,TEXT("Actual fire input action unavailable"));
        if(Step==0)
        {
            if(!Fixtures(W,R,FPS))return Finish(false,TEXT("Safe real Normal Combat weapon fixture rejected"));
            Action=Now;Step=1;return false;
        }
        auto* E=Enemy.Get();if(!E)return Finish(false,TEXT("Main enemy unexpectedly absent before controlled destruction"));
        if(Step==1)
        {
            if(Now-Action<0.8)return false;
            if(!Check(E->IsDormant()&&Outside->IsDormant()&&FVector::Dist(E->GetActorLocation(),DormantPosition)<1&&FPS->HealthComponent->GetHealth()==PlayerHealthBefore,TEXT("Dormant enemies neither move nor attack before accepted shot")))return Finish(false,TEXT("Dormant contract failed"));
            FVector Eye;FRotator View;FPS->GetActorEyesViewPoint(Eye,View);
            PC->SetControlRotation(Scenario==TEXT("AcceptedMissAndAttack")?FRotator(85,0,0):(E->GetActorLocation()-Eye).Rotation());
            if(Scenario==TEXT("RejectedAndDestroy"))FPS->Ammo=0;
            Action=Now;Step=2;return false;
        }
        if(Step==2)
        {
            if(Now-Action<0.15)return false;
            Input->InjectInputForAction(PC->GetFireAction(),FInputActionValue(true),{},{});Action=Now;Step=3;return false;
        }
        if(Step==3)
        {
            Input->InjectInputForAction(PC->GetFireAction(),FInputActionValue(false),{},{});
            if(Now-Action<0.1)return false;
            if(Scenario==TEXT("RejectedAndDestroy"))
            {
                if(!Check(FPS->ShotAcceptedCount==0&&FPS->ShotRejectedCount>0&&E->IsDormant()&&!Proof->bObserved,TEXT("Actual empty-ammo FireInputPressed is rejected and emits no activation or damage")))return Finish(false,TEXT("Rejected shot activated enemy"));
                E->Destroy();Enemy.Reset();
                return Finish(Check(Proof->Deaths==0&&R->GetTraversalSnapshot().bCurrent,TEXT("Abnormal Actor Destroy is not a valid Health death; no structural false invalidation")),TEXT("Rejected input and abnormal removal checked; Encounter victory semantics are a later stage"));
            }
            if(!Check(FPS->ShotAcceptedCount==1&&Proof->bObserved&&Proof->bWasDormant&&Proof->bActivated&&Outside->IsDormant(),TEXT("Only main fixture enemy activated from actual accepted shot before hit damage; other room stays dormant")))return Finish(false,TEXT("Accepted-shot ordering/scope failed"));
            FirstShot=FPS->GetLastShot().Id;Rows+=TEXT("FirstShot=")+FirstShot.ToString()+TEXT("\n");
            if(Scenario==TEXT("AcceptedMissAndAttack"))
            {
                if(!Check(!FPS->GetLastShot().bHit&&FPS->HitConfirmedCount==0&&E->Health->GetHealth()==Proof->HealthBefore,TEXT("Real accepted miss still emits activation but no hit damage")))return Finish(false,TEXT("Miss classification failed"));
                Action=Now;Step=5;return false;
            }
            if(!Check(FPS->HitConfirmedCount==1&&FPS->GetLastShot().Hit.GetActor()==E&&E->Health->GetHealth()==Proof->HealthBefore-FPS->ShotDamage,TEXT("First actual hit activates first, then applies real damage without swallowing it")))return Finish(false,TEXT("First-hit damage failed"));
            const float Before=E->Health->GetHealth();
            if(!Check(E->ReceiveShot(FPS,FirstShot)==0&&E->Health->GetHealth()==Before,TEXT("Replayed ShotId cannot damage twice")))return Finish(false,TEXT("Duplicate damage accepted"));
            // A native second real request inside the configured interval must also be rejected.
            if(!Check(!FPS->TryFire()&&FPS->ShotAcceptedCount==1,TEXT("Fire rate rejects a real immediate second request")))return Finish(false,TEXT("Rate limit failed"));
            Action=Now;Step=4;return false;
        }
        if(Step==4)
        {
            if(Now-Action<FPS->FireIntervalSeconds+0.1)return false;
            Input->InjectInputForAction(PC->GetFireAction(),FInputActionValue(true),{},{});Action=Now;Step=6;return false;
        }
        if(Step==5)
        {
            if(Now-Action<2.5)return false;
            return Finish(Check(FPS->HealthComponent->GetHealth()<PlayerHealthBefore&&FPS->ShotAcceptedCount==1&&Outside->IsDormant()&&R->GetTraversalSnapshot().bCurrent,TEXT("Active stationary test enemy performs real obstructable attack; enemy fire emits no player shot")),TEXT("Accepted miss and minimal active attack verified, not formal BT/EQS"));
        }
        if(Step==6)
        {
            Input->InjectInputForAction(PC->GetFireAction(),FInputActionValue(false),{},{});if(Now-Action<0.1)return false;
            Rows+=FString::Printf(TEXT("Accepted=%d HitConfirmed=%d DamageTotal=%.2f KilledEvents=%d\n"),FPS->ShotAcceptedCount,FPS->HitConfirmedCount,FPS->TotalDamageDealt,Proof->Deaths);
            return Finish(Check(!E->Health->IsAlive()&&Proof->Deaths==1&&FPS->ShotAcceptedCount==2&&FPS->GetLastShot().Id!=FirstShot&&FPS->HitConfirmedCount==2&&R->GetTraversalSnapshot().bCurrent,TEXT("Unique second shot kills once; exact Health death and current structure preserved")),TEXT("Native actual fire/hit/health ordering and dedup verified"));
        }
        return false;
    }
};
}
IMPLEMENT_COMPLEX_AUTOMATION_TEST(FG1BWeaponTests,"ProjectVerification.G1B.Stage1",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
void FG1BWeaponTests::GetTests(TArray<FString>& Names,TArray<FString>& Commands)const
{Names={TEXT("FirstHitOrdering"),TEXT("AcceptedMissAndAttack"),TEXT("RejectedAndDestroy")};Commands=Names;}
bool FG1BWeaponTests::RunTest(const FString& Scenario)
{
    if(!GEditor||GEditor->PlayWorld||!GEditor->GetEditorWorldContext().World()||GEditor->GetEditorWorldContext().World()->GetOutermost()->GetName()!=TEXT("/Game/Roguelite/Tests/P04G1B/L_Roguelite_SingleCombatSlice_Dev"))return false;
    ARogueliteCombatSliceRig* EditorRig=nullptr;for(TActorIterator<ARogueliteCombatSliceRig>I(GEditor->GetEditorWorldContext().World());I;++I){EditorRig=*I;break;}if(!EditorRig)return false;
    const bool Previous=EditorRig->bEnableCombatEncounter;EditorRig->bEnableCombatEncounter=false;
    auto& Framework=FAutomationTestFramework::Get();Framework.EnqueueLatentCommand(MakeShared<FStartPIECommand>(false));
    Framework.EnqueueLatentCommand(MakeShared<CombatSliceWeaponVerification::FExercise>(this,Scenario));Framework.EnqueueLatentCommand(MakeShared<FEndPlayMapCommand>());
    Framework.EnqueueLatentCommand(MakeShared<CombatSliceWeaponVerification::FRestoreMode>(EditorRig,Previous));return true;
}
#endif
