#include "Tests/RoguelitePlayerTrackedTestTypes.h"

ECollisionEnabled::Type URoguelitePlayerTrackedReentryMesh::GetCollisionEnabled() const
{
    ++Calls;
    if(Player.IsValid()&&!bInCallback)
    {
        TGuardValue<bool> Guard(bInCallback,true);
        ++ReentryCalls;
        bEveryReentryRejected&=!Player->ReadOrdinaryGameplayBinding(ERoguelitePlayerReadOperation::Move);
    }
    return Super::GetCollisionEnabled();
}

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Gameplay/RogueliteGameplayObservation.h"
#include "Gameplay/RogueliteCombatSliceActors.h"
#include "Gameplay/RogueliteCombatSliceEncounter.h"
#include "RogueliteDynamicGameplayBridge.h"
#include "RogueliteNavigationReadinessComponent.h"
#include "RogueliteFloorProfile.h"
#include "RogueliteParametricRoom.h"
#include "RogueliteRoomAssembly.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Editor.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "InputActionValue.h"
#include "InputKeyEventArgs.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "NavigationPath.h"
#include "NavigationSystem.h"
#include "Tests/AutomationEditorCommon.h"
#include "UObject/StrongObjectPtr.h"

namespace PlayerTrackedVerification
{
const TCHAR* Map=TEXT("/Game/Roguelite/Tests/P04G2/L_Roguelite_TwoCombatRooms_Dev");
using Operation=ERoguelitePlayerReadOperation;

UEnhancedInputLocalPlayerSubsystem* Input(ARogueliteSlicePlayerController* Controller)
{
    return Controller&&Controller->GetLocalPlayer()
        ?Controller->GetLocalPlayer()->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>():nullptr;
}

void Inject(ARogueliteSlicePlayerController* Controller,FVector2D Move,FVector2D Look)
{
    if(auto* Subsystem=Input(Controller))
    {
        Subsystem->InjectInputForAction(Controller->GetMoveAction(),FInputActionValue(Move),{},{});
        Subsystem->InjectInputForAction(Controller->GetLookAction(),FInputActionValue(Look),{},{});
    }
}

AActor* MakeBlocker(UWorld& World)
{
    auto* Actor=World.SpawnActor<AActor>();
    if(!Actor)return nullptr;
    auto* Box=NewObject<UBoxComponent>(Actor);
    Actor->SetRootComponent(Box);Actor->AddInstanceComponent(Box);
    Box->SetMobility(EComponentMobility::Movable);Box->SetCanEverAffectNavigation(false);
    Box->SetBoxExtent(FVector(50));Box->SetCollisionProfileName(TEXT("BlockAll"));
    Box->RegisterComponent();Actor->SetActorLocation(FVector(11000,11000,200));
    return Actor;
}

/** The original G2 walk algorithm, confined to this test's real entry route. */
struct FEntryWalk
{
    TArray<FVector> Points;
    FVector Goal=FVector::ZeroVector,LastProgress=FVector::ZeroVector;
    int32 Index=1;
    double Started=0,ProgressAt=0,NextSample=0;

    bool Begin(ARogueliteSlicePlayer& Player,const FVector& Requested,FString& Rows)
    {
        auto* Path=UNavigationSystemV1::FindPathToLocationSynchronously(Player.GetWorld(),Player.GetActorLocation(),Requested,&Player);
        if(!Path||!Path->IsValid()||Path->IsPartial()||Path->PathPoints.Num()<2)return false;
        Points=Path->PathPoints;Goal=Requested;LastProgress=Player.GetActorLocation();
        Started=ProgressAt=NextSample=FPlatformTime::Seconds();
        Rows+=FString::Printf(TEXT("CriticalWalkBegin Player=%s From=%s Goal=%s FullPathPoints=%d\n"),
            *Player.GetPathName(),*LastProgress.ToString(),*Goal.ToString(),Points.Num());
        return true;
    }

    int32 Tick(ARogueliteSlicePlayer& Player,ARogueliteSlicePlayerController& Controller,FString& Rows)
    {
        if(!Player.HasLiveBinding())return -1;
        const double Now=FPlatformTime::Seconds();
        const FVector Location=Player.GetActorLocation();
        if(FVector::Dist2D(Location,Goal)<=35)
        {
            Inject(&Controller,FVector2D::ZeroVector,FVector2D::ZeroVector);
            Rows+=TEXT("CriticalWalkArrived=")+Location.ToString()+TEXT("\n");
            return 1;
        }
        if(Now-Started>45)return -1;
        if(FVector::Dist2D(Location,LastProgress)>20){LastProgress=Location;ProgressAt=Now;}
        if(Now-ProgressAt>8)return -1;
        while(Index<Points.Num()-1&&FVector::Dist2D(Location,Points[Index])<48)++Index;
        FVector Direction=(Points.IsValidIndex(Index)?Points[Index]:Goal)-Location;Direction.Z=0;
        Controller.SetControlRotation(Direction.Rotation());
        Inject(&Controller,FVector2D(0,FMath::Clamp(float(Direction.Size2D()/100.),.22f,1.f)),FVector2D::ZeroVector);
        if(Now>=NextSample)
        {
            NextSample=Now+.2;
            Rows+=FString::Printf(TEXT("CriticalWalkSample Wall=%.6f Position=%s Speed=%.3f\n"),
                Now-Started,*Location.ToString(),Player.GetVelocity().Size2D());
        }
        return 0;
    }
};

struct FRun final:IAutomationLatentCommand
{
    FAutomationTestBase* Test;
    FString Case,Rows;
    bool bGood=true;
    int32 Stage=0;
    double Started=FPlatformTime::Seconds(),Action=0;
    TWeakObjectPtr<ARogueliteCombatSliceRig> Source;
    TWeakObjectPtr<AActor> Obstacle;
    FGuid SavedBatch;
    uint64 SavedEpoch=0;
    FVector Position;
    FRotator Rotation;
    FRoguelitePlayerReadCounters Before;
    FRogueliteLoopTraversalSnapshot InitialSnapshot;
    double StrictDistance=0,TrackedDistance=0;
    FEntryWalk EntryWalk;
    TWeakObjectPtr<ARogueliteSlicePlayer> CriticalPlayer;

    FRun(FAutomationTestBase* InTest,FString InCase):Test(InTest),Case(MoveTemp(InCase)){}

    void Check(const TCHAR* Name,bool Passed)
    {
        bGood&=Passed;Test->TestTrue(Name,Passed);
        Rows+=FString::Printf(TEXT("Check=%s Pass=%d\n"),Name,Passed);
    }

    void Record(ARogueliteSlicePlayer& Player,const TCHAR* Label)
    {
        const auto Counts=Player.GetOrdinaryReadCounters();
        FString Reason;
        const auto Status=Source.IsValid()?Source->GetGameplayObservationStatus(Reason):ERogueliteGameplayObservationStatus::InvalidRevoked;
        Rows+=FString::Printf(TEXT("State=%s World=%s GameTime=%.6f Frame=%llu Player=%s Batch=%s SourceBatch=%s Epoch=%llu Status=%d HP=%.2f CapsuleCollision=%d Lease=%d Position=%s Tracked=%llu,%llu,%llu Strong=%llu,%llu,%llu Rejected=%llu,%llu,%llu Refresh=%llu/%llu StrongCalls=%llu Reentry=%llu Reason=%s\n"),
            Label,*Player.GetWorld()->GetPathName(),Player.GetWorld()->GetTimeSeconds(),static_cast<unsigned long long>(GFrameCounter),*Player.GetPathName(),*Player.GetBoundBatch().ToString(),
            Source.IsValid()?*Source->Batch.BatchId.ToString():TEXT("None"),Source.IsValid()?Source->GetGameplayObservationEpoch():0,int32(Status),
            Player.HealthComponent->GetHealth(),int32(Player.GetCapsuleComponent()->GetCollisionEnabled()),
            Source.IsValid()&&FRogueliteDynamicGameplayBridge::IsComponentIsolated(Source.Get(),Player.GetBoundBatch(),Player.GetCapsuleComponent()),*Player.GetActorLocation().ToString(),
            Counts.Tracked[0],Counts.Tracked[1],Counts.Tracked[2],Counts.Strong[0],Counts.Strong[1],Counts.Strong[2],Counts.Rejected[0],Counts.Rejected[1],Counts.Rejected[2],
            Counts.RefreshSucceeded,Counts.RefreshAttempts,Counts.StrongBindingCalls,Counts.ReentryRejected,*Reason);
    }

    bool Publish(ARogueliteCombatSliceRig& Rig)
    {
        FString Reason;const bool Published=Rig.RefreshGameplayObservation(Reason);
        Check(TEXT("Fresh original strict Ready publishes Source-owned observation"),Published);
        Rows+=TEXT("PublishReason=")+Reason+TEXT("\n");return Published;
    }

    void Preserved(ARogueliteSlicePlayer& Player,FGuid Batch,const TCHAR* Label)
    {
        Check(Label,Player.GetBoundBatch()==Batch&&Player.GetCapsuleComponent()->GetCollisionEnabled()==ECollisionEnabled::QueryAndPhysics);
    }

    bool Finish(const TCHAR* Reason)
    {
        if(Source.IsValid())if(auto* Player=Source->GetSlicePlayer())
            Inject(Cast<ARogueliteSlicePlayerController>(Player->GetController()),FVector2D::ZeroVector,FVector2D::ZeroVector);
        if(Obstacle.IsValid())Obstacle->Destroy();
        if(Source.IsValid()&&!Source->Batch.OwnedActors.IsEmpty())
            Check(TEXT("Test clears only its generated Source batch"),Source->ClearRuntime());
        Rows+=FString::Printf(TEXT("Passed=%d\nReason=%s\n"),bGood,Reason);
        FString Directory;
        if(FParse::Value(FCommandLine::Get(),TEXT("PlayerTrackedEvidenceDir="),Directory))
        {
            IFileManager::Get().MakeDirectory(*Directory,true);
            const FString File=Directory/(Case+TEXT("-")+FGuid::NewGuid().ToString(EGuidFormats::Digits)+TEXT(".txt"));
            if(!FFileHelper::SaveStringToFile(Rows,*File))Test->AddError(TEXT("Could not save Player tracked evidence: ")+File);
        }
        return true;
    }

    bool Update() override
    {
        UWorld* World=GEditor?GEditor->PlayWorld.Get():nullptr;
        if(FPlatformTime::Seconds()-Started>210)
        {
            Check(TEXT("Bounded Player test fixture"),false);
            return Finish(TEXT("PIE or strict Ready timed out"));
        }
        if(!World)return false;
        auto* Rig=Source.Get();
        if(!Rig)
        {
            for(TActorIterator<ARogueliteCombatSliceRig> It(World);It;++It)
                if(!It->IsActorBeingDestroyed()){Rig=*It;Source=Rig;break;}
        }
        if(!Rig)return false;
        if(Case==TEXT("ExternalBlockerWatch")&&Stage==1)
        {
            if(Rig->Batch.bSucceeded)return false;
            Check(TEXT("Original Watch clears invalid batch and retains foreign blocker"),Rig->Batch.OwnedActors.IsEmpty()&&Obstacle.IsValid());
            Check(TEXT("Watch never restores original epoch"),!Rig->IsGameplayObservationEpochCurrent(SavedEpoch));
            Rows+=FString::Printf(TEXT("WatchClearWallSeconds=%.6f DetectionMayIncludeExistingStrongReaders=1\n"),FPlatformTime::Seconds()-Action);
            return Finish(TEXT("Original Watch/lifecycle window observed"));
        }
        if(!Rig->SliceFailure.IsEmpty())
        {
            Check(TEXT("Original G2 fixture completes"),false);
            Rows+=TEXT("FixtureFailure=")+Rig->SliceFailure+TEXT("\n");
            return Finish(TEXT("Original fixture failed"));
        }
        if(Case==TEXT("UnknownSource")&&Stage==100&&Rig->Stage==ERogueliteDungeonBuildStage::Failed)
        {
            Check(TEXT("Replacement Source reaches a successful runtime stage"),false);
            Rows+=FString::Printf(TEXT("ReplacementFailure RuntimeStatus=%s Reason=%s GeometryStage=%s GeometryReason=%s\n"),
                *Rig->RuntimeStatus,*Rig->LastFailureReason,*Rig->LastGeometry.Stage,*Rig->LastGeometry.Reason);
            return Finish(TEXT("Replacement Source failed before player binding"));
        }
        if(Case==TEXT("CriticalActions")&&Stage>=4
            &&(!CriticalPlayer.IsValid()||Rig->GetSlicePlayer()!=CriticalPlayer.Get()||!CriticalPlayer->HealthComponent->IsAlive()))
        {
            Check(TEXT("Critical Player remains alive until explicit damage exercise"),false);
            return Finish(TEXT("Critical Player lifecycle ended before the intended damage boundary"));
        }
        if(!Rig->bPlayerBridgeReady)return false;
        auto* Player=Rig->GetSlicePlayer();if(!Player)return false;
        auto* Controller=Cast<ARogueliteSlicePlayerController>(Player->GetController());if(!Controller)return false;

        if(Stage==100)
        {
            if(!Rig->IsDungeonReady())return false;
            Rig->SetGameplayReadMode(ERogueliteGameplayReadMode::TrackedGameplay);
            Before=Player->GetOrdinaryReadCounters();
            const bool Expected=Player->HasLiveBinding();
            const bool Actual=Player->ReadOrdinaryGameplayBinding(Operation::Tick);
            const auto After=Player->GetOrdinaryReadCounters();
            Check(TEXT("Unknown actual native Source retains strict binding result"),Actual==Expected&&Expected);
            Check(TEXT("Unknown Source never takes Player tracked path"),After.Tracked[0]==Before.Tracked[0]&&After.Strong[0]>Before.Strong[0]);
            Record(*Player,TEXT("UnknownSourceStrongFallback"));
            return Finish(TEXT("Unknown native Rig generated and bound through original APIs"));
        }
        if(Stage==101)
        {
            if(!Rig->IsDungeonReady())return false;
            Check(TEXT("Actual replacement batch has a different identity"),Rig->Batch.BatchId.IsValid()&&Rig->Batch.BatchId!=SavedBatch);
            Check(TEXT("Old epoch cannot authorize replacement batch"),!Rig->IsGameplayObservationEpochCurrent(SavedEpoch));
            Rig->SetGameplayReadMode(ERogueliteGameplayReadMode::TrackedGameplay);
            Check(TEXT("Replacement Player requires fresh valid observation"),Player->ReadOrdinaryGameplayBinding(Operation::Tick));
            Record(*Player,TEXT("ActualReplacementBatch"));
            return Finish(TEXT("Controlled Clear and actual regeneration completed"));
        }
        if(Case==TEXT("NativeReadAndInputs")&&(Stage==1||Stage==3))
        {
            const double Time=World->GetTimeSeconds();
            Inject(Controller,FVector2D(0,.25),FVector2D(.12,0));
            if(Time-Action<.4)return false;
            Inject(Controller,FVector2D::ZeroVector,FVector2D::ZeroVector);
            const double Distance=FVector::Dist2D(Position,Player->GetActorLocation());
            const float Yaw=FMath::Abs(FRotator::NormalizeAxis(Controller->GetControlRotation().Yaw-Rotation.Yaw));
            Check(TEXT("Actual Enhanced Input moves possessed Player"),Distance>10);
            Check(TEXT("Actual Enhanced Input changes control yaw"),Yaw>.1f);
            const auto After=Player->GetOrdinaryReadCounters();
            if(Stage==1)
            {
                StrictDistance=Distance;
                Check(TEXT("Strict input retains strong Move and Look calls"),After.Strong[1]>Before.Strong[1]&&After.Strong[2]>Before.Strong[2]);
                Rig->SetGameplayReadMode(ERogueliteGameplayReadMode::TrackedGameplay);
                if(!Publish(*Rig))return Finish(TEXT("Tracked input publication rejected"));
                Player->GetCharacterMovement()->StopMovementImmediately();Player->ConsumeMovementInputVector();
                Position=Player->GetActorLocation();Rotation=Controller->GetControlRotation();Before=Player->GetOrdinaryReadCounters();
                Action=Time;Stage=3;return false;
            }
            TrackedDistance=Distance;
            Check(TEXT("Tracked input takes Move and Look light path"),After.Tracked[1]>Before.Tracked[1]&&After.Tracked[2]>Before.Tracked[2]);
            Check(TEXT("Real Player Tick consumes valid observation"),After.Tracked[0]>Before.Tracked[0]);
            const auto Snapshot=Rig->GetTraversalSnapshot();
            Check(TEXT("Both ordinary action modes retain every Snapshot field"),
                FRogueliteLoopTraversalSnapshot::StaticStruct()->CompareScriptStruct(&InitialSnapshot,&Snapshot,0));
            Rows+=FString::Printf(TEXT("StrictDistance=%.6f TrackedDistance=%.6f InputDurationWorldSeconds=0.4 TimingIsNotFrameLocked=1\n"),StrictDistance,TrackedDistance);
            Record(*Player,TEXT("ActualInputModesCompleted"));
            return Finish(TEXT("Actual input and structural semantic comparison complete"));
        }
        if(Case==TEXT("CriticalActions")&&Stage==2)
        {
            // InputKey queues Enhanced Input processing for the next Player tick.
            if(World->GetTimeSeconds()-Action<.12)return false;
            Controller->InputKey(FInputKeyEventArgs(nullptr,FInputDeviceId::CreateFromInternalId(0),EKeys::R,IE_Released,FPlatformTime::Cycles64()));
            Action=World->GetTimeSeconds();Stage=3;return false;
        }
        if(Case==TEXT("CriticalActions")&&Stage==3)
        {
            if(!Player->IsReloading()&&World->GetTimeSeconds()-Action<.35)return false;
            Check(TEXT("Actual R starts original reload with strong validation"),Player->IsReloading()&&Player->GetOrdinaryReadCounters().StrongBindingCalls>Before.StrongBindingCalls);
            Check(TEXT("Original Enhanced Input R delegate actually ran"),Controller->ReloadInputEvents>0);
            Check(TEXT("Reload timer retains its positive original duration"),Player->ReloadDurationSeconds==1.5f
                &&Player->GetReloadRemainingSeconds()>0&&Player->GetReloadRemainingSeconds()<=Player->ReloadDurationSeconds);
            if(!bGood)return Finish(TEXT("Actual R did not start original reload within bounded input processing"));
            Before=Player->GetOrdinaryReadCounters();Action=World->GetTimeSeconds();Stage=1;return false;
        }
        if(Case==TEXT("CriticalActions")&&Stage==1)
        {
            if(Player->IsReloading())return false;
            Check(TEXT("Original reload callback replenishes magazine"),Player->Ammo==Player->MagazineCapacity&&Player->ReloadCompletedCount==1);
            Check(TEXT("Reload callback retains strong binding query"),Player->GetOrdinaryReadCounters().StrongBindingCalls>Before.StrongBindingCalls);
            const uint64 Strong=Player->GetOrdinaryReadCounters().StrongBindingCalls;
            Check(TEXT("Unauthorized damage still rejects"),Player->ReceiveTestDamage(2,FGuid::NewGuid(),nullptr)==0);
            Check(TEXT("Damage entry retains strong binding query"),Player->GetOrdinaryReadCounters().StrongBindingCalls>Strong);
            if(!bGood)return Finish(TEXT("Original reload or damage rejection contract failed"));
            const auto* TargetRoom=Rig->Batch.Rooms.FindByPredicate([&](const auto& Room){return Room.NodeId==Rig->Encounter->GetTargetNodeId();});
            Check(TEXT("Existing first Combat room is present"),TargetRoom&&TargetRoom->Room);
            if(!TargetRoom||!TargetRoom->Room)return Finish(TEXT("No original Combat room for real entry"));
            const auto Records=Rig->Batch.Registry->GetConnectionRecords();
            bool Found=false;FVector Landing=FVector::ZeroVector;
            for(const auto& Edge:InitialSnapshot.Edges)
            {
                if(!((Edge.NodeA==Rig->PlayerStartNode&&Edge.NodeB==TargetRoom->NodeId)
                    ||(Edge.NodeB==Rig->PlayerStartNode&&Edge.NodeA==TargetRoom->NodeId)))continue;
                const auto* Connection=Records.FindByPredicate([&](const auto& Record){return Record.Id==Edge.ConnectionId&&Record.bActive&&Record.bGeometryValid;});
                if(!Connection)continue;
                FRogueliteDoorSocket Door;
                Found=Connection->RoomA==TargetRoom->Room?Connection->RoomA->TryGetDoorSocket(Connection->SocketA,Door)
                    :(Connection->RoomB==TargetRoom->Room&&Connection->RoomB->TryGetDoorSocket(Connection->SocketB,Door));
                if(Found){Landing=Door.WorldPosition-Door.WorldOutwardDirection*250+FVector(0,0,10);break;}
            }
            Check(TEXT("Entry route uses an actual committed Start-to-Combat connection"),Found);
            if(!Found)return Finish(TEXT("No authoritative committed entry route"));
            const bool Walking=EntryWalk.Begin(*Player,Landing,Rows);
            Check(TEXT("Original navigation provides a full real entry path"),Walking);
            if(!Walking)return Finish(TEXT("Actual Combat entry walk could not start"));
            Action=World->GetTimeSeconds();Stage=4;return false;
        }
        if(Case==TEXT("CriticalActions")&&Stage==4)
        {
            const int32 Walking=EntryWalk.Tick(*Player,*Controller,Rows);
            if(Walking<0){Check(TEXT("Real Combat entry walking succeeds without teleport"),false);return Finish(TEXT("Actual entry walk failed or stalled"));}
            if(!Walking)return false;
            Action=World->GetTimeSeconds();Stage=5;return false;
        }
        if(Case==TEXT("CriticalActions")&&Stage==5)
        {
            if(Rig->Encounter->GetState()!=ERogueliteSliceEncounterState::Preparation&&World->GetTimeSeconds()-Action<3)return false;
            Check(TEXT("Real full entry reaches original Preparation"),Rig->Encounter->GetState()==ERogueliteSliceEncounterState::Preparation);
            bool AllClosed=!Rig->Encounter->GetIncidentConnectionIds().IsEmpty();
            for(const auto Id:Rig->Encounter->GetIncidentConnectionIds())
            {
                const auto* Gate=Rig->Encounter->GetGate(Id);AllClosed&=Gate&&Gate->IsClosed();
            }
            Check(TEXT("Every actual incident gate is closed before Combat"),AllClosed);
            if(!bGood)return Finish(TEXT("Original Preparation admission failed"));
            Controller->SetControlRotation(FRotator(85,0,0));
            Action=World->GetTimeSeconds();Stage=7;return false;
        }
        if(Case==TEXT("CriticalActions")&&Stage==7)
        {
            if(World->GetTimeSeconds()-Action<.12)return false;
            Check(TEXT("Actual camera has processed the intended upward miss aim"),Player->FirstPersonCamera->GetForwardVector().Z>.9);
            if(!bGood)return Finish(TEXT("Actual camera did not process the intended miss aim"));
            const uint64 Strong=Player->GetOrdinaryReadCounters().StrongBindingCalls;
            Check(TEXT("Original first miss is accepted after real entry"),Player->TryFire());
            Check(TEXT("Combat miss retains strong validation and original activation"),Player->GetOrdinaryReadCounters().StrongBindingCalls>Strong
                &&Player->LastAppliedDamage==0&&Rig->Encounter->GetState()==ERogueliteSliceEncounterState::CombatActive);
            if(!bGood)return Finish(TEXT("Original first-shot activation failed"));
            Action=World->GetTimeSeconds();Stage=6;return false;
        }
        if(Case==TEXT("CriticalActions")&&Stage==6)
        {
            ARogueliteSliceEnemy* Attacker=nullptr;
            for(const auto& Enemy:Rig->Encounter->GetEnemies())if(Enemy.IsValid()&&Enemy->IsActive()){Attacker=Enemy.Get();break;}
            Check(TEXT("Actual Encounter has a living active same-batch Enemy"),Attacker&&Attacker->GetDungeonSource()==Rig&&Attacker->GetBoundBatch()==SavedBatch);
            if(!Attacker)return Finish(TEXT("No actual active Enemy for original damage entry"));
            const float Damage=Attacker->AttackDamage;
            Check(TEXT("Damage exercise uses the unchanged positive Enemy AttackDamage"),FMath::IsFinite(Damage)&&Damage>0);
            if(!bGood)return Finish(TEXT("Original attacker identity or damage configuration rejected"));
            if(!Publish(*Rig))return Finish(TEXT("Active Encounter did not pass original fresh strong validation"));

            const FGuid Batch=Rig->Batch.BatchId;
            const uint64 BeforeFaultEpoch=Rig->GetGameplayObservationEpoch();
            const auto OldShot=Player->GetLastShot().Id;
            const int32 OldShots=Player->ShotAcceptedCount,OldAmmo=Player->Ammo;
            const uint64 InvalidStrong=Player->GetOrdinaryReadCounters().StrongBindingCalls;
            Rig->Batch.BatchId=FGuid::NewGuid();
            const bool FireRejected=!Player->TryFire();
            const bool ReloadRejected=!Player->TryReload();
            Check(TEXT("Real Source Batch identity fault rejects Fire and Reload without old Observation authorization"),FireRejected&&ReloadRejected
                &&Player->ShotAcceptedCount==OldShots&&Player->Ammo==OldAmmo&&Player->GetLastShot().Id==OldShot
                &&Player->GetOrdinaryReadCounters().StrongBindingCalls>=InvalidStrong+2);
            Preserved(*Player,SavedBatch,TEXT("Batch identity failure preserves physical collision evidence"));
            Check(TEXT("Old Observation cannot authorize the mismatched Batch"),!Rig->IsGameplayObservationEpochCurrent(BeforeFaultEpoch));
            Rows+=TEXT("Fault=Unregistered same-Tick Source Batch identity change; separate from genuine Clear/regeneration case\n");
            Rig->Batch.BatchId=Batch;
            if(!bGood)return Finish(TEXT("Strong critical actions failed the Source identity negative"));
            if(!Publish(*Rig))return Finish(TEXT("Restored Source identity did not pass fresh strong publication"));
            Check(TEXT("Restored identity retains the original real Capsule lease"),FRogueliteDynamicGameplayBridge::IsComponentIsolated(Rig,SavedBatch,Player->GetCapsuleComponent()));
            if(!bGood)return Finish(TEXT("Original Capsule lease was lost during a rejected critical action"));

            Record(*Player,TEXT("CriticalBeforeActualDeath"));
            const uint64 BeforeDeathEpoch=Rig->GetGameplayObservationEpoch();
            const FString AttackerPath=Attacker->GetPathName();
            const int32 Deaths=Player->PlayerDeathCount;
            const int32 Limit=FMath::CeilToInt(Player->HealthComponent->GetHealth()/Damage)+1;
            Rows+=TEXT("DamageApiExercise=1 NaturalEnemyShotCadence=0 NoEnemyPropertyChanges=1\n");
            for(int32 Index=0;Index<Limit&&Player->HealthComponent->IsAlive();++Index)
            {
                const float Health=Player->HealthComponent->GetHealth();
                const uint64 Strong=Player->GetOrdinaryReadCounters().StrongBindingCalls;
                const FGuid Event=FGuid::NewGuid();
                const float Applied=Player->ReceiveTestDamage(Damage,Event,Attacker);
                Rows+=FString::Printf(TEXT("CriticalDamage Index=%d Event=%s Enemy=%s Amount=%.6f Before=%.6f Applied=%.6f After=%.6f StrongBefore=%llu StrongAfter=%llu\n"),
                    Index,*Event.ToString(),*AttackerPath,Damage,Health,Applied,Player->HealthComponent->GetHealth(),
                    Strong,Player->GetOrdinaryReadCounters().StrongBindingCalls);
                Check(TEXT("Actual legal damage changes Health through the strong Player entry"),Applied==FMath::Min(Health,Damage)
                    &&Player->HealthComponent->GetHealth()==Health-Applied&&Player->GetOrdinaryReadCounters().StrongBindingCalls>Strong);
                if(!bGood)return Finish(TEXT("Actual strong damage entry failed"));
            }
            Check(TEXT("Actual death executes original cleanup"),!Player->HealthComponent->IsAlive()&&Player->PlayerDeathCount==Deaths+1
                &&!Player->GetBoundBatch().IsValid()&&Player->GetCapsuleComponent()->GetCollisionEnabled()==ECollisionEnabled::NoCollision
                &&!FRogueliteDynamicGameplayBridge::IsComponentIsolated(Rig,SavedBatch,Player->GetCapsuleComponent()));
            const auto DeadShot=Player->GetLastShot().Id;
            const int32 DeadShots=Player->ShotAcceptedCount,DeadAmmo=Player->Ammo;
            Check(TEXT("Death rejects further dangerous actions without creating a ShotId or reload"),!Player->TryFire()&&!Player->TryReload()
                &&Player->ShotAcceptedCount==DeadShots&&Player->Ammo==DeadAmmo&&Player->GetLastShot().Id==DeadShot&&!Player->IsReloading());
            Check(TEXT("Death cannot reuse the prior Source observation epoch"),!Rig->IsGameplayObservationEpochCurrent(BeforeDeathEpoch));
            return Finish(TEXT("Critical Fire, actual R, callback, real entry, strong damage and death contracts retained"));
        }
        if(Stage!=0)return false;
        if(!Rig->IsDungeonReady())return false;
        Check(TEXT("Original exact G2 fixture"),Rig->GetClass()==ARogueliteCombatSliceRig::StaticClass()&&Rig->bEnableTwoCombatRooms&&Rig->Encounter&&Rig->SecondEncounter);
        Check(TEXT("Real local native Player bound"),Player->GetClass()==ARogueliteSlicePlayer::StaticClass()&&Player->HasLiveBinding()&&Input(Controller));
        if(!bGood)return Finish(TEXT("Incomplete fixture"));
        Rows+=FString::Printf(TEXT("Case=%s World=%s Seed=%d Batch=%s Player=%s\n"),*Case,*World->GetPathName(),Rig->Seed,*Rig->Batch.BatchId.ToString(),*Player->GetPathName());
        SavedBatch=Rig->Batch.BatchId;InitialSnapshot=Rig->GetTraversalSnapshot();
        Check(TEXT("New Source begins in default Strict"),Rig->GetGameplayReadMode()==ERogueliteGameplayReadMode::DeveloperStrict);
        if(Case==TEXT("NativeReadAndInputs"))
        {
            for(const auto Op:{Operation::Tick,Operation::Move,Operation::Look})
                Check(TEXT("Strict ordinary read matches legal original binding"),Player->ReadOrdinaryGameplayBinding(Op));
            Before=Player->GetOrdinaryReadCounters();Position=Player->GetActorLocation();Rotation=Controller->GetControlRotation();
            Action=World->GetTimeSeconds();Stage=1;return false;
        }
        Rig->SetGameplayReadMode(ERogueliteGameplayReadMode::TrackedGameplay);
        if(!Publish(*Rig))return Finish(TEXT("Initial tracked publication unavailable"));
        SavedEpoch=Rig->GetGameplayObservationEpoch();Before=Player->GetOrdinaryReadCounters();
        Check(TEXT("Fresh observation authorizes idle Player"),Player->ReadOrdinaryGameplayBinding(Operation::Tick));
        Check(TEXT("Tracked idle query avoids HasLiveBinding"),Player->GetOrdinaryReadCounters().StrongBindingCalls==Before.StrongBindingCalls);
        if(!bGood)return Finish(TEXT("Initial tracked Player admission failed"));
        Record(*Player,TEXT("InitialTracked"));

        if(Case==TEXT("LeaseRevocation"))
        {
            Check(TEXT("Actual registered capsule lease removed"),FRogueliteDynamicGameplayBridge::UnregisterComponent(Rig,Player->GetCapsuleComponent()));
            Check(TEXT("Lease mutation revokes old epoch synchronously"),Rig->GetGameplayObservationEpoch()!=SavedEpoch);
            Check(TEXT("No movement authorization without actual lease"),!Player->ReadOrdinaryGameplayBinding(Operation::Move));
            Preserved(*Player,SavedBatch,TEXT("Lease loss preserves collision and binding evidence"));
            Check(TEXT("Helper does not restore lease by itself"),!FRogueliteDynamicGameplayBridge::IsComponentIsolated(Rig,SavedBatch,Player->GetCapsuleComponent()));
        }
        else if(Case==TEXT("NavCancel"))
        {
            Rig->NavigationReadiness->Cancel();
            Check(TEXT("Cancel revokes old observation"),Rig->GetGameplayObservationEpoch()!=SavedEpoch);
            Check(TEXT("Cancelled navigation rejects Move and Look"),!Player->ReadOrdinaryGameplayBinding(Operation::Move)&&!Player->ReadOrdinaryGameplayBinding(Operation::Look));
            Preserved(*Player,SavedBatch,TEXT("Nav rejection preserves physical evidence"));
            Check(TEXT("Nav rejection does not unregister capsule"),FRogueliteDynamicGameplayBridge::IsComponentIsolated(Rig,SavedBatch,Player->GetCapsuleComponent()));
        }
        else if(Case==TEXT("GateAndTransaction"))
        {
            {
                FRogueliteGameplayObservationWrite Outer(World,TEXT("Player test outer controlled write"));
                Check(TEXT("Write entry invalidates old epoch"),!Rig->IsGameplayObservationEpochCurrent(SavedEpoch));
                Check(TEXT("No ordinary operation during Source write"),!Player->ReadOrdinaryGameplayBinding(Operation::Move)&&!Player->ReadOrdinaryGameplayBinding(Operation::Look));
                Preserved(*Player,SavedBatch,TEXT("Write rejection retains collision and binding"));
                {
                    FRogueliteGameplayObservationWrite Inner(World,TEXT("Player test nested controlled write"));
                    Check(TEXT("Nested write cannot authorize Tick"),!Player->ReadOrdinaryGameplayBinding(Operation::Tick));
                }
                Check(TEXT("Inner exit cannot authorize outer interval"),!Player->ReadOrdinaryGameplayBinding(Operation::Move));
            }
            Check(TEXT("Fresh read validates after controlled interval"),Player->ReadOrdinaryGameplayBinding(Operation::Tick));
            FString Why;ARogueliteSliceGate* Gate=nullptr;
            for(TActorIterator<ARogueliteSliceGate> It(World);It;++It)if(It->GetOwner()==Rig&&It->CanClose(Why)){Gate=*It;break;}
            Check(TEXT("Actual safely closable gate exists"),Gate!=nullptr);
            if(Gate)
            {
                const uint64 Epoch=Rig->GetGameplayObservationEpoch();
                Check(TEXT("Existing gate closes through safe public path"),Gate->SetClosed(true,Why));
                Check(TEXT("Gate transition revokes old epoch"),Rig->GetGameplayObservationEpoch()!=Epoch);
                Check(TEXT("New Player read validates closed registered gate"),Player->ReadOrdinaryGameplayBinding(Operation::Tick));
                Check(TEXT("Physical gate remains closed"),Gate->IsClosed());
            }
        }
        else if(Case==TEXT("ExternalBlockerWatch"))
        {
            Obstacle=MakeBlocker(*World);Check(TEXT("Real external BlockAll blocker created"),Obstacle.IsValid());
            Record(*Player,TEXT("UncontrolledObstacleBeforeDetection"));
            Rows+=FString::Printf(TEXT("OrdinaryReadBeforeExistingWatch=%d OriginalWatchOwnsDetectionWindow=1\n"),Player->ReadOrdinaryGameplayBinding(Operation::Move));
            Action=FPlatformTime::Seconds();Stage=1;return false;
        }

        else if(Case==TEXT("StaticCollision")||Case==TEXT("StaticTransform")||Case==TEXT("RawParameterSemantics"))
        {
            auto* Room=Rig->Batch.Rooms[0].Room.Get();
            Check(TEXT("Actual generated room exists"),Room!=nullptr);
            if(Room)
            {
                const float Length=Room->RoomLength;const FVector Scale=Room->GetActorScale3D();
                UStaticMeshComponent* Floor=nullptr;TInlineComponentArray<UStaticMeshComponent*> Parts;Room->GetComponents(Parts);
                for(auto* Part:Parts)if(Part->GetFName()==FName(TEXT("Floor")))Floor=Part;
                Check(TEXT("Registered Static floor exists"),Floor&&Floor->IsRegistered()&&Floor->Mobility==EComponentMobility::Static);
                if(!Floor)return Finish(TEXT("Missing actual floor"));
                const auto Collision=Floor->GetCollisionEnabled();
                if(Case==TEXT("StaticCollision"))Floor->SetCollisionEnabled(ECollisionEnabled::NoCollision);
                if(Case==TEXT("StaticTransform"))
                {
                    // Deliberate illegal test fault, never a valid room-generation technique.
                    Room->SetActorScale3D(FVector(1.01,1,1));
                    Check(TEXT("Real room transform fault was applied"),!Room->GetActorScale3D().Equals(Scale));
                }
                if(Case==TEXT("RawParameterSemantics"))Room->RoomLength+=10;
                const auto Strong=Rig->GetTraversalSnapshot();const bool Legacy=Player->HasLiveBinding();
                const auto Counts=Player->GetOrdinaryReadCounters();
                const bool Actual=Player->ReadOrdinaryGameplayBinding(Operation::Move);
                Check(TEXT("Fault cannot consume old observation"),Player->GetOrdinaryReadCounters().Tracked[1]==Counts.Tracked[1]);
                if(Case==TEXT("RawParameterSemantics"))
                {
                    Check(TEXT("Known raw-parameter Current/Ready boundary retained"),Strong.bCurrent&&!Strong.bNavigationReady);
                    Check(TEXT("Raw parameter strong fallback retains old Player semantics"),Actual==Legacy);
                }
                else Check(TEXT("Actual Static fault rejects Player and original strong binding"),!Actual&&!Legacy&&!Strong.bCurrent);
                Preserved(*Player,SavedBatch,TEXT("Geometry failure preserves capsule collision and binding"));
                Record(*Player,TEXT("StaticFaultAfterStrong"));
                if(Case==TEXT("StaticCollision"))Floor->SetCollisionEnabled(Collision);
                if(Case==TEXT("StaticTransform"))Room->SetActorScale3D(Scale);
                if(Case==TEXT("RawParameterSemantics"))Room->RoomLength=Length;
                Rows+=TEXT("Fault restored before yielding; no later positive uses this navigation fixture.\n");
            }
        }
        else if(Case==TEXT("SourceClearAndBatch"))
        {
            Check(TEXT("Legal Source Clear uses owned cleanup"),Rig->ClearRuntime());
            Check(TEXT("Clear removes old gameplay identity"),!Rig->GameplayBatchId.IsValid()&&Rig->Batch.OwnedActors.IsEmpty());
            Check(TEXT("Clear never reauthorizes old epoch"),!Rig->IsGameplayObservationEpochCurrent(SavedEpoch));
            Check(TEXT("Actual original runtime regeneration starts"),Rig->GenerateRuntime());
            Stage=101;return false;
        }
        else if(Case==TEXT("UnknownSource"))
        {
            TStrongObjectPtr<URogueliteFloorProfile> Profile(Rig->FloorProfile.Get());const int32 Seed=Rig->Seed;
            const auto Catalog=Rig->PreparedTemplates;
            FString CatalogReason;
            const bool OriginalCurrent=Catalog.IsCurrent(Rig->Settings,CatalogReason);
            Check(TEXT("Original Source has a current real sealed PreparedTemplates catalog"),OriginalCurrent);
            Check(TEXT("Original catalog digest and candidate count match actual templates"),Catalog.Digest==Catalog.ComputeDigest()
                &&Catalog.Rows.Num()==Rig->Settings.Templates.Num()&&!Catalog.Rows.IsEmpty());
            Rows+=FString::Printf(TEXT("CatalogOriginal Source=%s Digest=%s Rows=%d CandidateCount=%d Version=%d CatalogVersion=%d Current=%d Reason=%s\n"),
                *Rig->GetPathName(),*Catalog.Digest,Catalog.Rows.Num(),Rig->Settings.Templates.Num(),Catalog.Version,Catalog.TemplateCatalogVersion,OriginalCurrent,*CatalogReason);
            Check(TEXT("Replacement preserves the actual original FloorProfile"),Profile.IsValid());
            if(!bGood)return Finish(TEXT("Original sealed template metadata was unavailable"));
            const auto Configuration=Profile->ResolvePreview(Seed);
            const bool ResolvedCurrent=Configuration.bValid&&Catalog.IsCurrent(Configuration.Source,CatalogReason);
            Check(TEXT("Value-copied catalog matches the replacement Source's resolved profile"),ResolvedCurrent);
            if(!bGood)return Finish(TEXT("Replacement profile does not match the original catalog"));
            Check(TEXT("Known Source clears before unknown Source fixture"),Rig->ClearRuntime());
            Check(TEXT("Known Source retires before replacement"),Rig->Destroy());
            if(!bGood)return Finish(TEXT("Old Source did not complete its original cleanup and retirement"));
            auto* Other=World->SpawnActorDeferred<ARoguelitePlayerTrackedUnknownRig>(ARoguelitePlayerTrackedUnknownRig::StaticClass(),FTransform::Identity,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
            Check(TEXT("Real unknown Source allocated"),Other!=nullptr);
            if(!Other)return Finish(TEXT("Unknown Source allocation failed"));
            Other->FloorProfile=Profile.Get();Other->PreparedTemplates=Catalog;Other->Seed=Seed;
            Other->bEnableTwoCombatRooms=true;Other->bEnableCombatEncounter=true;
            Check(TEXT("Replacement is the actual unknown native Source class"),Other->GetClass()==ARoguelitePlayerTrackedUnknownRig::StaticClass()
                &&Other->GetClass()!=ARogueliteCombatSliceRig::StaticClass());
            Check(TEXT("Catalog value and digest reach replacement before FinishSpawning"),Other->PreparedTemplates.Digest==Catalog.Digest
                &&Other->PreparedTemplates.ComputeDigest()==Catalog.Digest&&Other->PreparedTemplates.IsCurrent(Configuration.Source,CatalogReason));
            if(!bGood){Source=Other;return Finish(TEXT("Replacement metadata assignment failed before spawning completion"));}
            Other->FinishSpawning(FTransform::Identity);Source=Other;
            const bool Generated=Other->GenerateRuntime();
            Check(TEXT("Unknown Source uses original actual runtime generator"),Generated);
            const bool NewCurrent=Other->PreparedTemplates.IsCurrent(Other->Settings,CatalogReason);
            Check(TEXT("Actually configured new Source retains a current matching catalog"),NewCurrent&&Other->PreparedTemplates.Digest==Catalog.Digest);
            Rows+=FString::Printf(TEXT("CatalogReplacement Source=%s Class=%s Digest=%s Rows=%d Current=%d Generated=%d RuntimeStatus=%s Reason=%s GeometryStage=%s GeometryReason=%s CatalogReason=%s\n"),
                *Other->GetPathName(),*Other->GetClass()->GetPathName(),*Other->PreparedTemplates.Digest,Other->PreparedTemplates.Rows.Num(),NewCurrent,Generated,
                *Other->RuntimeStatus,*Other->LastFailureReason,*Other->LastGeometry.Stage,*Other->LastGeometry.Reason,*CatalogReason);
            if(!Generated||!bGood)return Finish(TEXT("Unknown Source runtime generation rejected; original reasons recorded without waiting for a nonexistent bridge"));
            Stage=100;return false;
        }
        else if(Case==TEXT("UnknownTypesAndReentry"))
        {
            const FTransform Pose=Player->GetActorTransform();
            auto* Other=World->SpawnActorDeferred<ARoguelitePlayerTrackedUnknownPlayer>(ARoguelitePlayerTrackedUnknownPlayer::StaticClass(),Pose,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
            Check(TEXT("Unknown native Player allocated"),Other!=nullptr);
            if(!Other)return Finish(TEXT("Unknown Player allocation failed"));
            Other->FinishSpawning(Pose);Controller->Possess(Other);FString Why;
            Check(TEXT("Unknown Player really binds through original public API"),Other->BindToDungeon(Rig,Pose,Why));
            const bool Expected=Other->HasLiveBinding();const auto Counts=Other->GetOrdinaryReadCounters();
            Check(TEXT("Unknown Player uses original strict result"),Other->ReadOrdinaryGameplayBinding(Operation::Move)==Expected&&Expected);
            Check(TEXT("Unknown Player cannot take tracked read"),Other->GetOrdinaryReadCounters().Tracked[1]==Counts.Tracked[1]&&Other->GetOrdinaryReadCounters().Strong[1]>Counts.Strong[1]);
            Controller->Possess(Player);
            Check(TEXT("Original real Player restored through binding API"),Player->BindToDungeon(Rig,Pose,Why));
            Other->Destroy();
            auto* Camera=NewObject<URoguelitePlayerTrackedUnknownCamera>(Player);
            Player->AddInstanceComponent(Camera);Camera->SetupAttachment(Player->GetCapsuleComponent());Camera->RegisterComponent();
            auto* OriginalCamera=Player->FirstPersonCamera.Get();Player->FirstPersonCamera=Camera;
            const bool CameraExpected=Player->HasLiveBinding();const auto CameraCounts=Player->GetOrdinaryReadCounters();
            Check(TEXT("Unknown real Camera uses strict result"),Player->ReadOrdinaryGameplayBinding(Operation::Look)==CameraExpected);
            Check(TEXT("Unknown component excludes tracked admission"),Player->GetOrdinaryReadCounters().Tracked[2]==CameraCounts.Tracked[2]&&Player->GetOrdinaryReadCounters().Strong[2]>CameraCounts.Strong[2]);
            Player->FirstPersonCamera=OriginalCamera;Camera->DestroyComponent();
            auto* Actor=World->SpawnActor<AActor>();Obstacle=Actor;
            Check(TEXT("Reentry getter owner allocated"),Actor!=nullptr);
            if(!Actor)return Finish(TEXT("Getter owner allocation failed"));
            auto* Getter=NewObject<URoguelitePlayerTrackedReentryMesh>(Actor);
            Actor->SetRootComponent(Getter);Actor->AddInstanceComponent(Getter);
            Getter->SetMobility(EComponentMobility::Movable);Getter->SetCanEverAffectNavigation(false);
            Getter->SetCollisionEnabled(ECollisionEnabled::NoCollision);Getter->RegisterComponent();Getter->Player=Player;
            Rig->RevokeGameplayObservation(TEXT("Test requests genuine strong validation over unknown getter"));
            const auto ReentryBefore=Player->GetOrdinaryReadCounters();
            Player->ReadOrdinaryGameplayBinding(Operation::Move);
            Check(TEXT("Actual virtual getter attempted synchronous Player reentry"),Getter->ReentryCalls>0);
            Check(TEXT("All nested Player reads reject reentry"),Getter->bEveryReentryRejected);
            Check(TEXT("Player records real reentry rejection"),Player->GetOrdinaryReadCounters().ReentryRejected>ReentryBefore.ReentryRejected);
            Preserved(*Player,SavedBatch,TEXT("Reentry failure preserves physical binding"));
            Rows+=FString::Printf(TEXT("GetterCalls=%d ReentryCalls=%d AllRejected=%d\n"),Getter->Calls,Getter->ReentryCalls,Getter->bEveryReentryRejected);
        }
        else if(Case==TEXT("CriticalActions"))
        {
            CriticalPlayer=Player;
            Controller->SetControlRotation(FRotator(85,0,0));
            const uint64 StrongBefore=Player->GetOrdinaryReadCounters().StrongBindingCalls;
            Check(TEXT("Original actual Fire accepted inside safehouse"),Player->TryFire());
            Check(TEXT("Fire retains current strong query"),Player->GetOrdinaryReadCounters().StrongBindingCalls>StrongBefore);
            Before=Player->GetOrdinaryReadCounters();
            Controller->InputKey(FInputKeyEventArgs(nullptr,FInputDeviceId::CreateFromInternalId(0),EKeys::R,IE_Pressed,FPlatformTime::Cycles64()));
            Action=World->GetTimeSeconds();Stage=2;return false;
        }
        else Check(TEXT("Known independent Player scenario"),false);

        Record(*Player,TEXT("CaseEndBeforeOwnedCleanup"));
        return Finish(TEXT("Independent Player tracked contract complete"));
    }
};

struct FAfter final:IAutomationLatentCommand
{
    FAutomationTestBase* Test;
    explicit FAfter(FAutomationTestBase* InTest):Test(InTest){}
    bool Update() override
    {
        if(GEditor&&GEditor->PlayWorld)return false;
        const auto* Mode=IConsoleManager::Get().FindConsoleVariable(TEXT("roguelite.PERF21B.Mode"));
        Test->TestTrue(TEXT("Permanent global mode remains Strict"),Mode&&Mode->GetInt()==0);
        return true;
    }
};
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FRoguelitePlayerTrackedPIETest,
    "ProjectVerification.PERF21B.PlayerTracked",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)

void FRoguelitePlayerTrackedPIETest::GetTests(TArray<FString>& Names,TArray<FString>& Commands) const
{
    Names={TEXT("NativeReadAndInputs"),TEXT("LeaseRevocation"),TEXT("NavCancel"),TEXT("GateAndTransaction"),
        TEXT("ExternalBlockerWatch"),TEXT("StaticCollision"),TEXT("StaticTransform"),TEXT("RawParameterSemantics"),
        TEXT("SourceClearAndBatch"),TEXT("UnknownTypesAndReentry"),TEXT("UnknownSource"),TEXT("CriticalActions")};
    Commands=Names;
}
bool FRoguelitePlayerTrackedPIETest::RunTest(const FString& Parameters)
{
    if(!GEditor||GEditor->PlayWorld){AddError(TEXT("Refuse to replace existing PIE"));return false;}
    FAutomationEditorCommonUtils::LoadMap(PlayerTrackedVerification::Map);
    ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
    FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<PlayerTrackedVerification::FRun>(this,Parameters));
    ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
    FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<PlayerTrackedVerification::FAfter>(this));
    return true;
}
#endif

// Default-off test telemetry for Player Tracked validation.
// Reads existing values and buffers evidence until WorldCleanup.
// Enable only with -PlayerTrackedTelemetryDir="<this Run>/Telemetry".
// Source publication/revocation totals are private. Export the existing
// PERF21B.Published / Revoked / StrongRefresh counters from the separate Trace.
// Status has no pure-read interface and is deliberately marked NotSampled.
// The observer never calls GetGameplayObservationStatus, Snapshot, Current,
// IsDungeonReady, Refresh or a gameplay write API. Enabled observer cost is
// visible under PERF21B_TestTelemetry, including its per-tick gating.

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Camera/PlayerCameraManager.h"
#include "Engine/GameViewportClient.h"
#include "Misc/DateTime.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "UnrealClient.h"

namespace RoguelitePlayerTrackedTelemetry
{
const FString& OutputDirectory()
{
    static const FString Directory=[]()
    {
        FString Value;
        FParse::Value(FCommandLine::Get(),TEXT("PlayerTrackedTelemetryDir="),Value);
        return Value;
    }();
    return Directory;
}

FString TextField(FString Value)
{
    Value.ReplaceInline(TEXT("\t"),TEXT(" "));
    Value.ReplaceInline(TEXT("\r"),TEXT(" "));
    Value.ReplaceInline(TEXT("\n"),TEXT(" "));
    return Value;
}

const TCHAR* EncounterState(const URogueliteSliceEncounter* Encounter)
{
    if(!IsValid(Encounter))return TEXT("Absent");
    switch(Encounter->GetState())
    {
    case ERogueliteSliceEncounterState::AwaitingEntry:return TEXT("AwaitingEntry");
    case ERogueliteSliceEncounterState::Preparation:return TEXT("Preparation");
    case ERogueliteSliceEncounterState::CombatActive:return TEXT("CombatActive");
    case ERogueliteSliceEncounterState::Cleared:return TEXT("Cleared");
    case ERogueliteSliceEncounterState::Cancelled:return TEXT("Cancelled");
    default:return TEXT("Uninitialized");
    }
}

struct FWorldRows
{
    FString Filename;
    FString Rows=TEXT("Utc\tWallSeconds\tWorldSeconds\tEngineFrame\tWorld\tBatch\tSource\tSourceClass\tAdmission\tSourceMode\tRequestedCVar\tEpoch\tObservationStatus\tObservationReason\tPrimary\tSecondary\tRecoveryPrimary\tRecoverySecondary\tPlayer\tPlayerClass\tBoundBatch\tPositionX\tPositionY\tPositionZ\tVelocityX\tVelocityY\tVelocityZ\tControlPitch\tControlYaw\tControlRoll\tCameraX\tCameraY\tCameraZ\tCameraPitch\tCameraYaw\tCameraRoll\tFovDegrees\tViewportPixelsX\tViewportPixelsY\tTrackedTick\tTrackedMove\tTrackedLook\tStrongTick\tStrongMove\tStrongLook\tRejectedTick\tRejectedMove\tRejectedLook\tPlayerRefreshAttempts\tPlayerRefreshSuccess\tPlayerReentryRejected\tPlayerHasLiveBindingCalls\n");
    double NextSample=0;
    uint32 Samples=0;
};

class FObserver
{
public:
    FObserver()
    {
        TickHandle=FWorldDelegates::OnWorldTickStart.AddRaw(this,&FObserver::Tick);
        CleanupHandle=FWorldDelegates::OnWorldCleanup.AddRaw(this,&FObserver::Cleanup);
    }
    ~FObserver()
    {
        FWorldDelegates::OnWorldTickStart.Remove(TickHandle);
        FWorldDelegates::OnWorldCleanup.Remove(CleanupHandle);
        for(const auto& Entry:Worlds)Save(Entry.Value,TEXT("ObserverShutdown"));
    }
private:
    void Save(const FWorldRows& Data,const TCHAR* Reason) const
    {
        if(Data.Filename.IsEmpty())return;
        IFileManager::Get().MakeDirectory(*OutputDirectory(),true);
        const FString Text=Data.Rows+FString::Printf(TEXT("# End=%s Samples=%u Sampling=WorldTickStart OneSecondWallInterval=1 SourceCounterTotals=IndependentTrace\n"),Reason,Data.Samples);
        if(!FFileHelper::SaveStringToFile(Text,*Data.Filename,FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
            UE_LOG(LogTemp,Error,TEXT("Player telemetry could not save %s"),*Data.Filename);
    }
    void Cleanup(UWorld* World,bool,bool)
    {
        const TWeakObjectPtr<UWorld> Key(World);
        if(const auto* Data=Worlds.Find(Key))Save(*Data,TEXT("WorldCleanup"));
        Worlds.Remove(Key);
    }
    void Tick(UWorld* World,ELevelTick,float)
    {
        if(OutputDirectory().IsEmpty()||!IsInGameThread()||!World||!World->IsGameWorld()||World->bIsTearingDown)return;
        TRACE_CPUPROFILER_EVENT_SCOPE(PERF21B_TestTelemetry);
        const FString Package=World->GetOutermost()->GetName();
        if(!Package.StartsWith(TEXT("/Game/Roguelite/Tests/P04G2/"))
            ||!Package.EndsWith(TEXT("L_Roguelite_TwoCombatRooms_Dev")))return;
        auto& Data=Worlds.FindOrAdd(TWeakObjectPtr<UWorld>(World));
        const double Now=FPlatformTime::Seconds();
        if(Now<Data.NextSample||Data.Samples>=600)return;
        Data.NextSample=Now+1.0;
        ++Data.Samples;
        if(Data.Filename.IsEmpty())
            Data.Filename=OutputDirectory()/(World->GetName()+TEXT("-")+FGuid::NewGuid().ToString(EGuidFormats::Digits)+TEXT(".tsv"));
        ARogueliteCombatSliceRig* Rig=nullptr;
        uint32 SourceCount=0;
        for(TActorIterator<ARogueliteCombatSliceRig> It(World);It;++It)
            if(!It->IsActorBeingDestroyed()){Rig=*It;++SourceCount;}
        FString Row=FString::Printf(TEXT("%s\t%.9f\t%.9f\t%llu\t%s\t"),
            *FDateTime::UtcNow().ToIso8601(),Now,World->GetTimeSeconds(),
            static_cast<unsigned long long>(GFrameCounter),*TextField(World->GetPathName()));
        if(SourceCount!=1||!Rig||Rig->GetClass()!=ARogueliteCombatSliceRig::StaticClass())
        {
            // Intentionally short diagnostic row. No unknown Source getters are called.
            Data.Rows+=Row+TEXT("N/A\tN/A\tN/A\tUnknownOrAmbiguousSource\n");
            return;
        }
        const auto* Requested=IConsoleManager::Get().FindConsoleVariable(TEXT("roguelite.PERF21B.Mode"));
        const auto ActualMode=Rig->GetGameplayReadMode();
        const uint64 Epoch=Rig->GetGameplayObservationEpoch();
        Row+=FString::Printf(TEXT("%s\t%s\t%s\tExactNativeSource\t%d\t%d\t%llu\tNotSampled\tNoPureReadInterface\t%s\t%s\t%d\t%d\t"),
            *Rig->Batch.BatchId.ToString(EGuidFormats::Digits),*TextField(Rig->GetPathName()),*TextField(Rig->GetClass()->GetPathName()),
            int32(ActualMode),Requested?Requested->GetInt():-1,Epoch,
            EncounterState(Rig->Encounter),EncounterState(Rig->SecondEncounter),
            Rig->Encounter?Rig->Encounter->IsRecoveryBusy():false,Rig->SecondEncounter?Rig->SecondEncounter->IsRecoveryBusy():false);
        auto* Player=Rig->GetSlicePlayer();
        if(!IsValid(Player)||Player->GetClass()!=ARogueliteSlicePlayer::StaticClass())
        {
            Data.Rows+=Row+TEXT("N/A\tN/A\tPlayerUnavailableOrUnknown\n");
            return;
        }
        const auto* Controller=Cast<ARogueliteSlicePlayerController>(Player->GetController());
        FVector CameraPosition=FVector::ZeroVector;
        FRotator CameraRotation=FRotator::ZeroRotator;
        float Fov=-1;
        if(Controller&&Controller->GetClass()==ARogueliteSlicePlayerController::StaticClass())
        {
            Controller->GetPlayerViewPoint(CameraPosition,CameraRotation);
            if(Controller->PlayerCameraManager)Fov=Controller->PlayerCameraManager->GetFOVAngle();
        }
        FIntPoint Pixels(-1,-1);
        if(const auto* ViewportClient=World->GetGameViewport())
            if(ViewportClient->Viewport)Pixels=ViewportClient->Viewport->GetSizeXY();
        const FVector Position=Player->GetActorLocation();
        const FVector Velocity=Player->GetVelocity();
        const FRotator Control=Controller?Controller->GetControlRotation():FRotator::ZeroRotator;
        const auto Counts=Player->GetOrdinaryReadCounters();
        Row+=FString::Printf(TEXT("%s\t%s\t%s\t%.6f\t%.6f\t%.6f\t%.6f\t%.6f\t%.6f\t%.6f\t%.6f\t%.6f\t"),
            *TextField(Player->GetPathName()),*TextField(Player->GetClass()->GetPathName()),*Player->GetBoundBatch().ToString(EGuidFormats::Digits),
            Position.X,Position.Y,Position.Z,Velocity.X,Velocity.Y,Velocity.Z,Control.Pitch,Control.Yaw,Control.Roll);
        Row+=FString::Printf(TEXT("%.6f\t%.6f\t%.6f\t%.6f\t%.6f\t%.6f\t%.3f\t%d\t%d\t"),
            CameraPosition.X,CameraPosition.Y,CameraPosition.Z,CameraRotation.Pitch,CameraRotation.Yaw,CameraRotation.Roll,Fov,Pixels.X,Pixels.Y);
        Row+=FString::Printf(TEXT("%llu\t%llu\t%llu\t%llu\t%llu\t%llu\t%llu\t%llu\t%llu\t%llu\t%llu\t%llu\t%llu\n"),
            Counts.Tracked[0],Counts.Tracked[1],Counts.Tracked[2],Counts.Strong[0],Counts.Strong[1],Counts.Strong[2],
            Counts.Rejected[0],Counts.Rejected[1],Counts.Rejected[2],Counts.RefreshAttempts,Counts.RefreshSucceeded,Counts.ReentryRejected,Counts.StrongBindingCalls);
        Data.Rows+=Row;
    }
    FDelegateHandle TickHandle,CleanupHandle;
    TMap<TWeakObjectPtr<UWorld>,FWorldRows> Worlds;
};
FObserver Observer;
}
#endif