#include "Tests/RogueliteCombatSliceRig.h"
#include "Gameplay/RogueliteCombatSlicePlayer.h"
#include "Gameplay/RogueliteCombatSliceActors.h"
#include "Gameplay/RogueliteCombatSliceEncounter.h"
#include "RogueliteDynamicGameplayBridge.h"
#include "Components/CapsuleComponent.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "NavigationPath.h"
#include "NavigationSystem.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/AutomationTest.h"
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Editor.h"
#include "Tests/AutomationEditorCommon.h"
#include "Engine/LocalPlayer.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "EnhancedInputSubsystems.h"
#include "InputActionValue.h"
#include "InputMappingContext.h"
#include "InputKeyEventArgs.h"
#include "Engine/Level.h"
#include "UObject/ObjectKey.h"
#include <limits>
#include "Misc/FileHelper.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "HAL/FileManager.h"

namespace CombatSliceEncounterVerification
{
const TCHAR* Map=TEXT("/Game/Roguelite/Tests/P04G1B/L_Roguelite_SingleCombatSlice_Dev");
ARogueliteCombatSliceRig* FindRig(UWorld* W)
{if(W)for(TActorIterator<ARogueliteCombatSliceRig>I(W);I;++I)if(!I->IsActorBeingDestroyed())return *I;return nullptr;}
bool FullRoom(const FRogueliteSeedRoomRecord& R,const FVector& Center)
{
    const FVector L=R.PlacedTransform.InverseTransformPosition(Center);
    return FMath::Abs(L.X)+34<=R.Dimensions.X*.5&&FMath::Abs(L.Y)+34<=R.Dimensions.Y*.5&&L.Z>=85&&L.Z+88<=R.Dimensions.Z+3;
}
UEnhancedInputLocalPlayerSubsystem* Input(ARogueliteSlicePlayer* FPS)
{
    auto* PC=FPS?Cast<ARogueliteSlicePlayerController>(FPS->GetController()):nullptr;
    return PC&&PC->GetLocalPlayer()?PC->GetLocalPlayer()->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>():nullptr;
}
void MoveInput(ARogueliteSlicePlayer* FPS,const FVector2D& Value)
{
    auto* PC=FPS?Cast<ARogueliteSlicePlayerController>(FPS->GetController()):nullptr;
    if(auto* Sub=Input(FPS);Sub&&PC)Sub->InjectInputForAction(PC->GetMoveAction(),FInputActionValue(Value),{},{});
}
void FireInput(ARogueliteSlicePlayer* FPS,bool Pressed)
{
    auto* PC=FPS?Cast<ARogueliteSlicePlayerController>(FPS->GetController()):nullptr;
    if(auto* Sub=Input(FPS);Sub&&PC)Sub->InjectInputForAction(PC->GetFireAction(),FInputActionValue(Pressed),{},{});
}
void ReloadKey(ARogueliteSlicePlayer* FPS,bool Pressed)
{
    if(auto* PC=FPS?Cast<ARogueliteSlicePlayerController>(FPS->GetController()):nullptr)
        PC->InputKey(FInputKeyEventArgs(nullptr,FInputDeviceId::CreateFromInternalId(0),EKeys::R,Pressed?IE_Pressed:IE_Released,FPlatformTime::Cycles64()));
}
/** Drives the actual possessed player through Enhanced Input; never writes actor transforms. */
struct FWalk
{
    TArray<FVector> Points;FVector Goal=FVector::ZeroVector,LastProgress=FVector::ZeroVector;
    int32 Index=0;double Started=0,ProgressAt=0,NextSample=0;bool bActive=false;float ArrivalRadius=42;
    bool Begin(ARogueliteSlicePlayer* FPS,const FVector& Requested,FString& Rows,FString& Why,const FRogueliteSeedRoomRecord* Constrain=nullptr,float Tolerance=42)
    {
        if(!FPS||!FPS->HasLiveBinding()){Why=TEXT("Walker requires the current real local player");return false;}
        auto* Path=UNavigationSystemV1::FindPathToLocationSynchronously(FPS->GetWorld(),FPS->GetActorLocation(),Requested,FPS);
        if(!Path||!Path->IsValid()||Path->IsPartial()||Path->PathPoints.Num()<2){Why=TEXT("Actual player route is unavailable or partial");return false;}
        if(Constrain)for(const auto& Point:Path->PathPoints)
        {
            const FVector L=Constrain->PlacedTransform.InverseTransformPosition(Point);
            if(FMath::Abs(L.X)>Constrain->Dimensions.X*.5-30||FMath::Abs(L.Y)>Constrain->Dimensions.Y*.5-30)
            {Why=TEXT("Indoor firing route would leave the closed encounter room");return false;}
        }
        Points=Path->PathPoints;Index=1;Goal=Requested;ArrivalRadius=Tolerance;Started=ProgressAt=FPlatformTime::Seconds();NextSample=Started;
        LastProgress=FPS->GetActorLocation();bActive=true;
        Rows+=FString::Printf(TEXT("WalkBegin ActualPlayer=%s From=%s Goal=%s FullPathPoints=%d Indoor=%d\n"),*FPS->GetPathName(),*LastProgress.ToString(),*Goal.ToString(),Points.Num(),Constrain!=nullptr);return true;
    }
    // -1 failure, 0 in progress, 1 physically arrived.
    int32 Tick(ARogueliteSlicePlayer* FPS,FString& Rows,FString& Why)
    {
        if(!bActive||!FPS||!FPS->HasLiveBinding()){Why=TEXT("Actual player walk identity expired");return -1;}
        const double Now=FPlatformTime::Seconds();const FVector Position=FPS->GetActorLocation();
        if(FVector::Dist2D(Position,Goal)<=ArrivalRadius)
        {MoveInput(FPS,FVector2D::ZeroVector);bActive=false;Rows+=TEXT("WalkArrived=")+Position.ToString()+TEXT("\n");
            Rows+=FString::Printf(TEXT("FIX04Walking World=%s WorldTime=%.6f GameFrame=%llu ReadWall=%.6f Sampling=ArrivalReadback Goal=%s Actual=%s Velocity=%s Distance2D=%.6f Tolerance=%.6f\n"),*FPS->GetWorld()->GetPathName(),FPS->GetWorld()->GetTimeSeconds(),static_cast<unsigned long long>(GFrameCounter),FPlatformTime::Seconds(),*Goal.ToString(),*Position.ToString(),*FPS->GetVelocity().ToString(),FVector::Dist2D(Position,Goal),ArrivalRadius);return 1;}
        if(Now-Started>45){Why=TEXT("Actual player walk timed out; no teleport fallback");return -1;}
        if(FVector::Dist2D(Position,LastProgress)>20){LastProgress=Position;ProgressAt=Now;}
        if(Now-ProgressAt>8){Why=TEXT("Actual player is physically blocked/stalled on the requested route");return -1;}
        while(Index<Points.Num()-1&&FVector::Dist2D(Position,Points[Index])<48)++Index;
        const FVector Aim=Points.IsValidIndex(Index)?Points[Index]:Goal;FVector Delta=Aim-Position;Delta.Z=0;
        if(Delta.Size2D()<5&&Index<Points.Num()-1){++Index;return 0;}
        auto* PC=Cast<ARogueliteSlicePlayerController>(FPS->GetController());if(!PC){Why=TEXT("Actual FPS PlayerController lost");return -1;}
        PC->SetControlRotation(Delta.Rotation());
        MoveInput(FPS,FVector2D(0,FMath::Clamp(float(Delta.Size2D()/100.),.22f,1.f)));
        if(Now>=NextSample){NextSample=Now+.2;Rows+=FString::Printf(TEXT("WalkSample t=%.3f Position=%s Speed=%.1f\n"),Now-Started,*Position.ToString(),FPS->GetVelocity().Size2D());}
        return 0;
    }
};

struct FReloadLifetimeObservation
{
    bool bObserved=false,bReloading=true;int32 Ammo=-1,Completes=-1,Cancels=-1;float Remaining=-1;
    FDelegateHandle WorldCleanupHandle;
};
void ObserveReloadTeardown(ARogueliteSlicePlayer* FPS,const TSharedPtr<FReloadLifetimeObservation>& Proof)
{
    if(!FPS)return;Proof->bObserved=true;Proof->Ammo=FPS->Ammo;Proof->Completes=FPS->ReloadCompletedCount;
    Proof->Cancels=FPS->ReloadCancelledCount;Proof->bReloading=FPS->IsReloading();Proof->Remaining=FPS->GetReloadRemainingSeconds();
}
class FExercise:public IAutomationLatentCommand
{
    FAutomationTestBase* Test;FString Scenario,Rows;int32 Step=0;double Started=0,Action=0,LastFire=0;
    TWeakObjectPtr<ARogueliteCombatSliceRig> Rig;TWeakObjectPtr<ARogueliteRoomAssembly> Registry;
    TWeakObjectPtr<AActor> Manual;TWeakObjectPtr<ARogueliteSliceEnemy> AimedEnemy;
    FGuid ExpectedEntry,WalkConnection;FRogueliteSliceShot SafehouseShot;FWalk Walker;
    FVector EntryLanding=FVector::ZeroVector,OtherLanding=FVector::ZeroVector,TargetReturn=FVector::ZeroVector;
    FVector RecoveryStart=FVector::ZeroVector,RecoveryGoal=FVector::ZeroVector,RecoveryWaiting=FVector::ZeroVector;
    int32 OtherNode=INDEX_NONE,AcceptedBefore=0,RejectedBefore=0,HitsBefore=0,CorridorAcceptedBefore=0;float HealthBefore=0;
    bool bFaultArmed=false,bCorridorShot=false,bReleaseCorridorFire=false,bFiringWalk=false,bCancellationObserved=false;
    bool bPreparationFaultRestored=false;FGuid FaultConnection,PushConnection;
    TWeakObjectPtr<ACharacter> DoorOccupant;FTransform OccupantBefore;
    ECollisionResponse FaultVisibility=ECR_Block;
    TArray<FGuid> BlockedExits;int32 ExitIndex=0,ShotsBeforeExitTrials=0;
    TSharedPtr<FGuid> BypassAcceptedShot;
    bool bAmmoPulse=false;
    int32 ConsecutiveAimMisses=0;
    // FIX04 read-only test evidence; timestamps describe the actual readback.
    FGuid DiagnosticObservedShot;
    int32 DiagnosticAimSerial=0,DiagnosticRejected=-1;
    FVector DiagnosticSelectedPosition=FVector::ZeroVector;
    bool bDiagnosticSelectedPosition=false;
    FString DiagnosticStamp(UWorld* World) const
    {
        return FString::Printf(TEXT("World=%s WorldTime=%.6f GameFrame=%llu ReadWall=%.6f ReadUTC=%s Step=%d AimSerial=%d"),
            *GetPathNameSafe(World),World?World->GetTimeSeconds():-1.,static_cast<unsigned long long>(GFrameCounter),FPlatformTime::Seconds(),*FDateTime::UtcNow().ToIso8601(),Step,DiagnosticAimSerial);
    }
    void DiagnosticHit(const TCHAR* Label,UWorld* World,bool Blocked,const FHitResult& Hit)
    {
        const auto* Actor=Hit.GetActor();const auto* Component=Hit.GetComponent();
        Rows+=FString::Printf(TEXT("FIX04Hit Label=%s %s Blocked=%d Start=%s End=%s Impact=%s Distance=%.6f Actor=%s ActorClass=%s ActorId=%u Component=%s ComponentClass=%s ComponentId=%u Owner=%s Collision=%d ObjectType=%d Visibility=%d Mobility=%d\n"),
            Label,*DiagnosticStamp(World),Blocked,*Hit.TraceStart.ToString(),*Hit.TraceEnd.ToString(),*Hit.ImpactPoint.ToString(),Hit.Distance,
            *GetPathNameSafe(Actor),*GetPathNameSafe(Actor?Actor->GetClass():nullptr),Actor?Actor->GetUniqueID():0,
            *GetPathNameSafe(Component),*GetPathNameSafe(Component?Component->GetClass():nullptr),Component?Component->GetUniqueID():0,*GetPathNameSafe(Component?Component->GetOwner():nullptr),
            Component?int32(Component->GetCollisionEnabled()):-1,Component?int32(Component->GetCollisionObjectType()):-1,Component?int32(Component->GetCollisionResponseToChannel(ECC_Visibility)):-1,Component?int32(Component->Mobility):-1);
    }
    void DiagnosticState(const TCHAR* Label,ARogueliteSlicePlayer* FPS)
    {
        const auto* Target=AimedEnemy.Get();const auto* Camera=FPS->FirstPersonCamera.Get();const auto* Controller=FPS->GetController();
        Rows+=FString::Printf(TEXT("FIX04State Label=%s %s Sampling=Readback Batch=%s Player=%s Position=%s CameraOrigin=%s CameraRotation=%s ControlRotation=%s Target=%s TargetPos=%s TargetHealth=%.6f TargetAlive=%d Ammo=%d Reloading=%d ReloadRemaining=%.6f FireInterval=%.6f Range=%.6f Accepted=%d Rejected=%d LastShot=%s LastDamage=%.6f Reason=%s Outcome=%s Selected=%d SelectedPosition=%s\n"),
            Label,*DiagnosticStamp(FPS->GetWorld()),*FPS->GetBoundBatch().ToString(),*FPS->GetPathName(),*FPS->GetActorLocation().ToString(),
            Camera?*Camera->GetComponentLocation().ToString():TEXT("None"),Camera?*Camera->GetComponentRotation().ToString():TEXT("None"),Controller?*Controller->GetControlRotation().ToString():TEXT("None"),
            *GetPathNameSafe(Target),Target?*Target->GetActorLocation().ToString():TEXT("None"),Target&&Target->Health?Target->Health->GetHealth():-1.f,Target&&Target->Health&&Target->Health->IsAlive(),
            FPS->Ammo,FPS->IsReloading(),FPS->GetReloadRemainingSeconds(),FPS->FireIntervalSeconds,FPS->WeaponRangeCm,FPS->ShotAcceptedCount,FPS->ShotRejectedCount,*FPS->GetLastShot().Id.ToString(),FPS->LastAppliedDamage,*FPS->LastRejectedReason,*FPS->LastFireOutcome,bDiagnosticSelectedPosition,*DiagnosticSelectedPosition.ToString());
    }
    void DiagnosticCompletedShots(ARogueliteSlicePlayer* FPS)
    {
        const auto& Shot=FPS->GetLastShot();
        if(Shot.Id.IsValid()&&Shot.Id!=DiagnosticObservedShot)
        {
            DiagnosticObservedShot=Shot.Id;
            Rows+=FString::Printf(TEXT("FIX04Shot Id=%s Batch=%s %s Sampling=CompletedShotReadback PawnAtShot=%s Origin=%s Direction=%s Hit=%d DamageReadback=%.6f\n"),
                *Shot.Id.ToString(),*Shot.BatchId.ToString(),*DiagnosticStamp(FPS->GetWorld()),*Shot.PawnLocation.ToString(),*Shot.Origin.ToString(),*Shot.Direction.ToString(),Shot.bHit,FPS->LastAppliedDamage);
            DiagnosticHit(TEXT("CompletedShot"),FPS->GetWorld(),Shot.bHit,Shot.Hit);
            DiagnosticState(TEXT("CompletedShotReadback"),FPS);
        }
        if(DiagnosticRejected!=FPS->ShotRejectedCount)
        {
            DiagnosticRejected=FPS->ShotRejectedCount;
            DiagnosticState(TEXT("RejectedCountReadback"),FPS);
        }
    }
    bool EmptyEntryCase() const { return Scenario==TEXT("ReloadEmptyEntry"); }
    bool ReloadCase() const { return Scenario.StartsWith(TEXT("Reload")); }
    bool FirstHitCase() const { return Scenario==TEXT("FirstHitSeed1")||Scenario==TEXT("ReloadPartialFullSeed1"); }
    bool bReloadExercised=false,bReloadKeyDown=true;
    TWeakObjectPtr<ARogueliteSlicePlayer> ReloadSubject;
    TSharedPtr<FReloadLifetimeObservation> ReloadLifetime;
    FDelegateHandle ReloadDestroyedHandle;
    int32 ReloadResume=0,ReloadAmmo=0,ReloadStarts=0,ReloadCompletes=0,ReloadShots=0,ReloadHits=0,ReloadRejects=0,ReloadAttacks=0;
    float ReloadDamage=0,ReloadHealth=0,ReloadEnemyHealth=0,ReloadMaxZ=0,ReloadLastRemaining=0,OriginalFireInterval=.25f;
    double ReloadAt=0,NextReloadKey=0;
    FGuid ReloadLastShot;FVector ReloadPosition;FRotator ReloadRotation;
    ERogueliteSliceEncounterState ReloadEncounterState=ERogueliteSliceEncounterState::AwaitingEntry;
    void BeginReload(ARogueliteSlicePlayer* FPS,URogueliteSliceEncounter* E,double Now,int32 Resume)
    {
        ReloadSubject=FPS;
        const FObjectKey PlayerKey(FPS);const auto Proof=ReloadLifetime;
        ReloadDestroyedHandle=FPS->GetWorld()->AddOnActorDestroyedHandler(FOnActorDestroyed::FDelegate::CreateLambda([PlayerKey,Proof](AActor* Actor)
        {if(FObjectKey(Actor)==PlayerKey)ObserveReloadTeardown(Cast<ARogueliteSlicePlayer>(Actor),Proof);}));
        if(Scenario==TEXT("ReloadLifeStopPIE"))
        {
            const FObjectKey WorldKey(FPS->GetWorld());
            Proof->WorldCleanupHandle=FWorldDelegates::OnWorldCleanup.AddLambda([PlayerKey,WorldKey,Proof](UWorld* World,bool,bool)
            {
                if(FObjectKey(World)!=WorldKey||!World->PersistentLevel)return;
                for(AActor* Actor:World->PersistentLevel->Actors)if(Actor&&FObjectKey(Actor)==PlayerKey){ObserveReloadTeardown(Cast<ARogueliteSlicePlayer>(Actor),Proof);break;}
            });
        }
        ReloadResume=Resume;ReloadAmmo=FPS->Ammo;ReloadStarts=FPS->ReloadStartedCount;ReloadCompletes=FPS->ReloadCompletedCount;
        ReloadShots=FPS->ShotAcceptedCount;ReloadHits=FPS->HitConfirmedCount;ReloadRejects=FPS->ShotRejectedCount;ReloadDamage=FPS->TotalDamageDealt;
        ReloadHealth=FPS->HealthComponent->GetHealth();ReloadLastShot=FPS->GetLastShot().Id;ReloadPosition=FPS->GetActorLocation();ReloadMaxZ=ReloadPosition.Z;
        ReloadRotation=FPS->GetController()->GetControlRotation();ReloadEnemyHealth=0;ReloadAttacks=0;
        for(const auto& Weak:E->GetEnemies())if(Weak.IsValid()){ReloadEnemyHealth+=Weak->Health->GetHealth();ReloadAttacks+=Weak->AttackCount;}
        ReloadEncounterState=E->GetState();ReloadAt=Now;NextReloadKey=Now+.25;bReloadKeyDown=true;
        ReloadKey(FPS,true);Action=Now;Step=103;
        Rows+=FString::Printf(TEXT("ReloadActualR Begin Ammo=%d Shots=%d HP=%.2f Phase=%d Batch=%s\n"),ReloadAmmo,ReloadShots,ReloadHealth,int32(ReloadEncounterState),*FPS->GetBoundBatch().ToString());
    }
    FVector PushStart=FVector::ZeroVector,PushNormal=FVector::ZeroVector,PushDoor=FVector::ZeroVector;
    bool Check(bool Good,const FString& What)
    {Rows+=FString::Printf(TEXT("Check=%s\tPass=%d\n"),*What,Good);if(!Good)Test->AddError(What);return Good;}
    bool Finish(bool Good,const FString& Why)
    {
        if(!Good)Test->AddError(Why);
        if(auto* R=Rig.Get())
        {
            if(Scenario!=TEXT("ReloadLifeStopPIE")&&ReloadDestroyedHandle.IsValid())R->GetWorld()->RemoveOnActorDestroyedHandler(ReloadDestroyedHandle);
            if(auto* Occupant=DoorOccupant.Get();Occupant&&Occupant->GetOwner()==R&&Occupant->GetWorld()==R->GetWorld())
            {
                Occupant->GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
                FRogueliteDynamicGameplayBridge::UnregisterComponent(R,Occupant->GetCapsuleComponent());Occupant->Destroy();
            }
            if(Scenario==TEXT("PartialRollback")&&!bPreparationFaultRestored&&R->Encounter)
                if(auto* Gate=R->Encounter->GetGate(FaultConnection))Gate->GetBlocker()->SetCollisionResponseToChannel(ECC_Visibility,FaultVisibility);
            if(auto* FPS=R->GetSlicePlayer())
            {
                MoveInput(FPS,FVector2D::ZeroVector);FireInput(FPS,false);if(ReloadCase())ReloadKey(FPS,false);
                Rows+=TEXT("FinalActualPlayerPosition=")+FPS->GetActorLocation().ToString()+TEXT("\n");
                if(R->Encounter&&R->Encounter->GetExitConnectionId().IsValid())
                {
                    const auto Passage=FRogueliteDynamicGameplayBridge::QueryConnection(R,R->Encounter->GetExitConnectionId(),FPS);
                    Rows+=FString::Printf(TEXT("FinalExitQuery State=%d StructuralCurrent=%d BaseReady=%d Forward=%d Reverse=%d FullPath=%d MoveVerified=%d Reason=%s\n"),
                        int32(Passage.State),Passage.bStructuralCurrent,Passage.bBaseNavigationReady,Passage.bForwardCapsule,Passage.bReverseCapsule,Passage.bFullPath,Passage.bMoveVerified,*Passage.Reason);
                }
            }
            Rows+=FString::Printf(TEXT("RecoveryCanonicalStart=%s\nRecoveryCanonicalGoal=%s\nRecoveryWaitingPoint=%s\nRecoveryEvidenceBegin\n%s\nRecoveryEvidenceEnd\n"),
                *RecoveryStart.ToString(),*RecoveryGoal.ToString(),*RecoveryWaiting.ToString(),*FRogueliteDynamicGameplayBridge::RecoveryEvidence(R));
        }
        Rows+=FString::Printf(TEXT("Passed=%d\nReason=%s\n"),Good,*Why);
        FString Dir;if(FParse::Value(FCommandLine::Get(),TEXT("G1BEvidenceDir="),Dir))
        {
            IFileManager::Get().MakeDirectory(*Dir,true);FString File=Dir/TEXT("encounter-")+Scenario+TEXT(".txt");
            if(IFileManager::Get().FileExists(*File))File+=TEXT(".")+FDateTime::UtcNow().ToString(TEXT("%Y%m%d%H%M%S"))+TEXT(".rerun");
            FFileHelper::SaveStringToFile(Rows,*File);
        }
        return true;
    }
    bool ReadConnection(ARogueliteCombatSliceRig* R,FGuid Id,FRogueliteRoomConnection& Out)
    {
        if(!R||!R->Batch.Registry)return false;for(const auto& C:R->Batch.Registry->GetConnectionRecords())
            if(C.Id==Id&&C.bActive&&C.bGeometryValid&&C.ValidationStatus==ERogueliteConnectionValidation::Passed){Out=C;return true;}return false;
    }
    bool DoorForNode(ARogueliteCombatSliceRig* R,const FRogueliteRoomConnection& C,int32 Node,FRogueliteDoorSocket& Door,int32* Neighbor=nullptr)
    {
        const auto* Room=R->Batch.Rooms.FindByPredicate([&](const auto& X){return X.NodeId==Node;});if(!Room)return false;
        ARogueliteParametricRoom* Other=nullptr;bool Good=false;
        if(C.RoomA==Room->Room){Good=C.RoomA->TryGetDoorSocket(C.SocketA,Door);Other=C.RoomB;}
        else if(C.RoomB==Room->Room){Good=C.RoomB->TryGetDoorSocket(C.SocketB,Door);Other=C.RoomA;}
        if(Neighbor){const auto* N=R->Batch.Rooms.FindByPredicate([&](const auto& X){return X.Room==Other;});*Neighbor=N?N->NodeId:INDEX_NONE;}
        return Good;
    }
    bool AllDormant(URogueliteSliceEncounter* E) const
    {if(E->GetEnemies().Num()!=2)return false;for(const auto& Weak:E->GetEnemies())if(!Weak.IsValid()||!Weak->IsDormant())return false;return true;}
    bool CheckClosed(ARogueliteCombatSliceRig* R,URogueliteSliceEncounter* E,ARogueliteSlicePlayer* FPS,FGuid Id)
    {
        auto* Gate=E->GetGate(Id);const auto Q=FRogueliteDynamicGameplayBridge::QueryConnection(R,Id,FPS);
        bool ForwardRay=false,ReverseRay=false;
        if(Gate&&Gate->GetBlocker())
        {
            const FVector Center=Gate->GetBlocker()->GetComponentLocation(),Normal=Gate->GetBlocker()->GetForwardVector();
            FCollisionQueryParams Params(SCENE_QUERY_STAT(G1B_ClosedGateRay),false,FPS);FHitResult Forward,Reverse;
            ForwardRay=R->GetWorld()->LineTraceSingleByChannel(Forward,Center-Normal*100,Center+Normal*100,ECC_Visibility,Params)&&Forward.GetComponent()==Gate->GetBlocker();
            ReverseRay=R->GetWorld()->LineTraceSingleByChannel(Reverse,Center+Normal*100,Center-Normal*100,ECC_Visibility,Params)&&Reverse.GetComponent()==Gate->GetBlocker();
        }
        Rows+=FString::Printf(TEXT("Closed Connection=%s EncounterState=%d State=%d BaseReady=%d Fwd=%d Rev=%d StaticFullPath=%d ForwardRay=%d ReverseRay=%d\n"),*Id.ToString(),int32(E->GetState()),int32(Q.State),Q.bBaseNavigationReady,Q.bForwardCapsule,Q.bReverseCapsule,Q.bFullPath,ForwardRay,ReverseRay);
        return Gate&&Gate->IsClosed()&&Gate->GetBlocker()->GetCollisionEnabled()!=ECollisionEnabled::NoCollision&&ForwardRay&&ReverseRay
            &&Q.State==ERogueliteGameplayPassage::GameplayBlocked&&!Q.bForwardCapsule&&!Q.bReverseCapsule&&Q.bStructuralCurrent&&Q.bBaseNavigationReady;
    }
    bool FindFiringPosition(ARogueliteCombatSliceRig* R,ARogueliteSlicePlayer* FPS,ARogueliteSliceEnemy* Enemy,FVector& Out)
    {
        const auto* Room=R->Batch.Rooms.FindByPredicate([&](const auto& X){return X.NodeId==R->Encounter->GetTargetNodeId();});if(!Room)return false;
        ++DiagnosticAimSerial;bDiagnosticSelectedPosition=false;
        auto* W=R->GetWorld();FCollisionQueryParams Q(SCENE_QUERY_STAT(G1B_FiringPosition),false,FPS);
        const auto CanHit=[&](const FVector& Center)
        {
            FHitResult Hit;const bool Blocked=W->LineTraceSingleByChannel(Hit,Center+FVector(0,0,64),Enemy->GetActorLocation(),ECC_Visibility,Q);
            Rows+=FString::Printf(TEXT("FIX04Candidate %s Center=%s QueryStart=%s QueryEnd=%s Channel=Visibility TraceComplex=0 Ignore=%s Target=%s TargetHealth=%.6f TargetAlive=%d\n"),
                *DiagnosticStamp(W),*Center.ToString(),*(Center+FVector(0,0,64)).ToString(),*Enemy->GetActorLocation().ToString(),*FPS->GetPathName(),*Enemy->GetPathName(),Enemy->Health->GetHealth(),Enemy->Health->IsAlive());
            DiagnosticHit(TEXT("OriginalPreAim"),W,Blocked,Hit);
            if(!Blocked||Hit.GetActor()!=Enemy)return false;
            if(Scenario==TEXT("ReloadCombatEmpty"))
            {
                FCollisionQueryParams Reverse(SCENE_QUERY_STAT(G1B_ReloadEnemyLOS),false,Enemy);Reverse.AddIgnoredActor(FPS);
                if(W->LineTraceTestByChannel(Enemy->GetActorLocation()+FVector(0,0,45),Center+FVector(0,0,35),ECC_Visibility,Reverse))return false;
            }
            return true;
        };
        if(CanHit(FPS->GetActorLocation())){Out=FPS->GetActorLocation();DiagnosticSelectedPosition=Out;bDiagnosticSelectedPosition=true;return true;}
        auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(W);if(!Nav)return false;
        for(const auto& Local:TArray<FVector>{{-500,-500,10},{0,-500,10},{500,-500,10},{-500,0,10},{500,0,10},{-500,500,10},{0,500,10},{500,500,10},{0,0,10}})
        {
            FNavLocation N;if(!Nav->ProjectPointToNavigation(Room->PlacedTransform.TransformPosition(Local),N,FVector(80,80,100)))continue;
            FHitResult Ground;if(!W->LineTraceSingleByChannel(Ground,N.Location+FVector(0,0,150),N.Location-FVector(0,0,300),ECC_Visibility,Q)
                ||Ground.GetActor()!=Room->Room.Get()||FMath::Abs(Room->PlacedTransform.InverseTransformPosition(Ground.ImpactPoint).Z)>1)continue;
            const FVector Center=Ground.ImpactPoint+FVector(0,0,90);
            if(!FullRoom(*Room,Center)||W->OverlapBlockingTestByChannel(Center,FQuat::Identity,ECC_Pawn,FCollisionShape::MakeCapsule(34,88),Q)||!CanHit(Center))continue;
            auto* Path=UNavigationSystemV1::FindPathToLocationSynchronously(W,FPS->GetActorLocation(),Ground.ImpactPoint+FVector(0,0,10),FPS);
            if(!Path||!Path->IsValid()||Path->IsPartial())continue;bool Inside=true;
            for(const auto& P:Path->PathPoints){const FVector L=Room->PlacedTransform.InverseTransformPosition(P);Inside&=FMath::Abs(L.X)<Room->Dimensions.X*.5-30&&FMath::Abs(L.Y)<Room->Dimensions.Y*.5-30;}
            if(Inside){Out=Ground.ImpactPoint+FVector(0,0,10);DiagnosticSelectedPosition=Out;bDiagnosticSelectedPosition=true;return true;}
        }
        return false;
    }
    bool FindRecoveryWaitingPosition(ARogueliteCombatSliceRig* R,ARogueliteSlicePlayer* FPS,FVector& Out,FString& Why)
    {
        const auto* Room=R->Batch.Rooms.FindByPredicate([&](const auto& Row){return Row.NodeId==R->Encounter->GetTargetNodeId();});
        FRogueliteRoomConnection Connection;FRogueliteDoorSocket A,B;
        if(!Room||!ReadConnection(R,R->Encounter->GetExitConnectionId(),Connection)
            ||!Connection.RoomA->TryGetDoorSocket(Connection.SocketA,A)||!Connection.RoomB->TryGetDoorSocket(Connection.SocketB,B))
        {Why=TEXT("Post-clear waiting position requires the actual current C2 exit pair");return false;}
        // Same physical doorway-to-room anchors queried by G1A; this is read-only geometry.
        RecoveryStart=A.WorldPosition-A.WorldOutwardDirection*300;
        RecoveryGoal=B.WorldPosition-B.WorldOutwardDirection*300;
        Rows+=FString::Printf(TEXT("PostClearPlayer=%s RecoveryStart=%s RecoveryGoal=%s\n"),*FPS->GetActorLocation().ToString(),*RecoveryStart.ToString(),*RecoveryGoal.ToString());
        auto* W=R->GetWorld();auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(W);if(!Nav){Why=TEXT("No navigation for an actual in-room waiting walk");return false;}
        FCollisionQueryParams Query(SCENE_QUERY_STAT(G1B_RecoveryWaiting),false,FPS);
        TArray<FVector> Candidates;
        for(float X:TArray<float>{-700,-450,0,450,700})for(float Y:TArray<float>{-700,-450,0,450,700})
            Candidates.Add(Room->PlacedTransform.TransformPosition(FVector(X,Y,10)));
        Candidates.StableSort([&](const FVector& L,const FVector& P){return FVector::DistSquared2D(L,FPS->GetActorLocation())<FVector::DistSquared2D(P,FPS->GetActorLocation());});
        for(const auto& Candidate:Candidates)
        {
            FNavLocation NavPoint;if(!Nav->ProjectPointToNavigation(Candidate,NavPoint,FVector(60,60,100)))continue;
            FHitResult Ground;if(!W->LineTraceSingleByChannel(Ground,NavPoint.Location+FVector(0,0,150),NavPoint.Location-FVector(0,0,300),ECC_Visibility,Query)
                ||Ground.GetActor()!=Room->Room.Get()||Ground.ImpactNormal.Z<.9f||FMath::Abs(Room->PlacedTransform.InverseTransformPosition(Ground.ImpactPoint).Z)>1)continue;
            const FVector Center=Ground.ImpactPoint+FVector(0,0,90);
            // 220cm planned margin still exceeds 160cm after the driver's <=42cm arrival tolerance.
            if(!FullRoom(*Room,Center)||FMath::PointDistToSegment(Ground.ImpactPoint,RecoveryStart,RecoveryGoal)<220
                ||W->OverlapBlockingTestByChannel(Center,FQuat::Identity,ECC_Pawn,FCollisionShape::MakeCapsule(34,88),Query))continue;
            auto* Path=UNavigationSystemV1::FindPathToLocationSynchronously(W,FPS->GetActorLocation(),Ground.ImpactPoint+FVector(0,0,10),FPS);
            if(!Path||!Path->IsValid()||Path->IsPartial()||Path->PathPoints.Num()<2)continue;
            bool Inside=true;for(const auto& Point:Path->PathPoints)
            {const FVector L=Room->PlacedTransform.InverseTransformPosition(Point);Inside&=FMath::Abs(L.X)<Room->Dimensions.X*.5-30&&FMath::Abs(L.Y)<Room->Dimensions.Y*.5-30;}
            if(!Inside)continue;
            Out=Ground.ImpactPoint+FVector(0,0,10);RecoveryWaiting=Out;
            Rows+=FString::Printf(TEXT("SelectedWaitingPoint=%s SupportedFloor=%s Clearance=%.2f PathPoints=%d\n"),*Out.ToString(),*Ground.ImpactPoint.ToString(),FMath::PointDistToSegment(Ground.ImpactPoint,RecoveryStart,RecoveryGoal),Path->PathPoints.Num());return true;
        }
        Why=TEXT("No bounded capsule-safe, floor-supported waiting point with a full in-room path away from the actual recovery segment");return false;
    }
    void Aim(ARogueliteSlicePlayer* FPS,const FVector& Target)
    {FPS->GetController()->SetControlRotation((Target-FPS->FirstPersonCamera->GetComponentLocation()).Rotation());}
    bool IsCancellationCase() const
    {return Scenario==TEXT("PlayerDeath")||Scenario==TEXT("SourceClear")||Scenario==TEXT("SeedChange")||Scenario==TEXT("EnemyDestroy")||Scenario==TEXT("RegistrationFailure")||Scenario==TEXT("IllegalStaticCollision");}
    bool ArmPreparationFault(ARogueliteCombatSliceRig* R,URogueliteSliceEncounter* E,FString& Why)
    {
        if(Scenario!=TEXT("DoorOccupied")&&Scenario!=TEXT("PartialRollback"))return true;
        const auto& Ids=E->GetIncidentConnectionIds();
        for(int32 I=Ids.Num()-1;I>=0;--I)if(Ids[I]!=ExpectedEntry){FaultConnection=Ids[I];break;}
        auto* Gate=E->GetGate(FaultConnection);if(!Gate){Why=TEXT("No actual non-entry door for lock fault");return false;}
        if(Scenario==TEXT("PartialRollback"))
        {
            FaultVisibility=Gate->GetBlocker()->GetCollisionResponseToChannel(ECC_Visibility);
            Gate->GetBlocker()->SetCollisionResponseToChannel(ECC_Visibility,ECR_Ignore);
            Rows+=TEXT("Fault=Only this generated gameplay gate's Visibility response, requiring real commit validation and rollback; Connection=")+FaultConnection.ToString()+TEXT("\n");
            return true;
        }
        FRogueliteRoomConnection Record;FRogueliteDoorSocket Door;
        if(!ReadConnection(R,FaultConnection,Record)||!DoorForNode(R,Record,E->GetTargetNodeId(),Door))return false;
        FActorSpawnParameters Params;Params.Owner=R;Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        auto* Occupant=R->GetWorld()->SpawnActor<ACharacter>(ACharacter::StaticClass(),Door.WorldPosition+FVector(0,0,90),FRotator::ZeroRotator,Params);
        if(!Occupant){Why=TEXT("Could not create independent doorway occupant fixture");return false;}
        DoorOccupant=Occupant;OccupantBefore=Occupant->GetActorTransform();
        Occupant->GetCharacterMovement()->SetMovementMode(MOVE_None);
        TArray<UPrimitiveComponent*> Parts;Occupant->GetComponents(Parts);
        for(auto* Part:Parts){Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);Part->SetCanEverAffectNavigation(false);}
        auto* Capsule=Occupant->GetCapsuleComponent();Capsule->SetCapsuleSize(34,88);Capsule->SetCollisionObjectType(ECC_Pawn);
        Capsule->SetCollisionResponseToAllChannels(ECR_Block);
        if(!FRogueliteDynamicGameplayBridge::RegisterComponent(R,R->Batch.BatchId,Occupant,Capsule,ERogueliteDynamicParticipant::Pawn,Why))return false;
        Capsule->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
        Rows+=TEXT("Fault=Legitimately leased external-to-Encounter Pawn occupies actual non-entry doorway; Connection=")+FaultConnection.ToString()+TEXT(" Actor=")+Occupant->GetPathName()+TEXT("\n");
        return true;
    }
public:
    FExercise(FAutomationTestBase* T,FString S,TSharedPtr<FReloadLifetimeObservation> Proof):Test(T),Scenario(MoveTemp(S)),ReloadLifetime(MoveTemp(Proof)){}
    bool Update() override
    {
        auto* W=GEditor?GEditor->PlayWorld.Get():nullptr;if(!W)return false;
        const double Now=FPlatformTime::Seconds();
        if(!Started){Started=Now;Rows=TEXT("World=")+W->GetPathName()+TEXT("\n");}
        if(!Rig.IsValid())Rig=FindRig(W);auto* R=Rig.Get();if(!R||!R->Encounter)return Finish(false,TEXT("Independent encounter Rig/component missing"));
        auto* E=R->Encounter.Get();
        if(!Registry.IsValid()&&R->Batch.Registry)Registry=R->Batch.Registry;
        if(!Manual.IsValid())for(TActorIterator<AActor>I(W);I;++I)if(I->GetActorLabel()==TEXT("G1B_ProtectedManualMarker")){Manual=*I;break;}
        if(Scenario==TEXT("RegistrationFailure")&&!bFaultArmed){E->SetSpawnFailureForValidation(true);bFaultArmed=true;Rows+=TEXT("Fault=Omit second required member before Initialize\n");}
        if(Step==111)
        {
            if(Now-Action<2.0)return false;
            auto* Subject=ReloadSubject.Get();
            const bool LiveCancelled=Subject&&!Subject->IsReloading()&&Subject->GetReloadRemainingSeconds()==0&&Subject->Ammo==ReloadAmmo&&Subject->ReloadCompletedCount==ReloadCompletes&&Subject->ReloadCancelledCount==1;
            const bool DestroyedCancelled=!Subject&&ReloadLifetime->bObserved&&!ReloadLifetime->bReloading&&ReloadLifetime->Remaining==0&&ReloadLifetime->Ammo==ReloadAmmo&&ReloadLifetime->Completes==ReloadCompletes&&ReloadLifetime->Cancels==1;
            bool Good=Check(LiveCancelled||DestroyedCancelled,TEXT("After original reload deadline, a surviving or legitimately destroyed old player has a cancelled timer and no replenishment"));
            Rows+=FString::Printf(TEXT("ReloadDestroyObservation Observed=%d Reloading=%d Remaining=%.3f Ammo=%d Completed=%d Cancelled=%d SubjectStillValid=%d\n"),ReloadLifetime->bObserved,ReloadLifetime->bReloading,ReloadLifetime->Remaining,ReloadLifetime->Ammo,ReloadLifetime->Completes,ReloadLifetime->Cancels,Subject!=nullptr);
            if(Subject){const int32 Cancels=Subject->ReloadCancelledCount;Subject->CancelReload(TEXT("Repeated validation cancellation"));Good&=Check(Subject->ReloadCancelledCount==Cancels,TEXT("Reload cancellation is idempotent and affects no other timers"));}
            int32 Gameplay=0,Reservations=0;for(TActorIterator<AActor>I(W);I;++I)if(!I->IsActorBeingDestroyed())Gameplay+=I->IsA<ARogueliteSliceEnemy>()||I->IsA<ARogueliteSliceGate>()||I->IsA<ARogueliteSliceRecoveryProbe>();
            for(TActorIterator<ARogueliteRoomAssembly>I(W);I;++I)if(!I->IsActorBeingDestroyed())for(const auto& C:I->GetConnectionRecords())Reservations+=C.bActive;
            Good&=Check(E->GetState()==ERogueliteSliceEncounterState::Cancelled&&E->GetClearCount()==0&&Gameplay==0&&Reservations==0&&R->Batch.OwnedActors.IsEmpty()&&FRogueliteDynamicGameplayBridge::ActiveComponentCount(R)==0&&Manual.IsValid(),TEXT("Reload lifecycle fault preserves controlled cancellation, zero owned batch/leases/reservations and the protected manual marker"));
            Rows+=FString::Printf(TEXT("ReloadLifecycle Ammo=%d Completed=%d Cancelled=%d Gameplay=%d Reservations=%d\n"),Subject?Subject->Ammo:-1,Subject?Subject->ReloadCompletedCount:-1,Subject?Subject->ReloadCancelledCount:-1,Gameplay,Reservations);
            return Finish(Good,TEXT("Original reload cancelled before its deadline; no old-batch replenishment after deadline"));
        }
        if(Now-Started>240)return Finish(false,TEXT("Encounter test timeout: ")+R->SliceStatus+TEXT(" / ")+E->Describe());
        if((Step==90||Scenario==TEXT("RegistrationFailure"))&&E->GetState()==ERogueliteSliceEncounterState::Cancelled)
        {
            if(!bCancellationObserved){bCancellationObserved=true;Action=Now;}
            int32 LiveEnemies=0,LiveGates=0,LiveProbes=0,ActiveReservations=0;
            for(TActorIterator<AActor>I(W);I;++I)if(!I->IsActorBeingDestroyed())
            {LiveEnemies+=I->IsA<ARogueliteSliceEnemy>();LiveGates+=I->IsA<ARogueliteSliceGate>();LiveProbes+=I->IsA<ARogueliteSliceRecoveryProbe>();}
            for(TActorIterator<ARogueliteRoomAssembly>I(W);I;++I)if(!I->IsActorBeingDestroyed())for(const auto& C:I->GetConnectionRecords())ActiveReservations+=C.bActive;
            if((!R->Batch.OwnedActors.IsEmpty()||LiveEnemies||LiveGates||LiveProbes||ActiveReservations)&&Now-Action<5)return false;
            Rows+=TEXT("Cancellation=")+E->GetLastReason()+TEXT("\nOriginalFailure=")+R->LastFailureReason+TEXT("\n");
            Rows+=FString::Printf(TEXT("ActiveWorldCleanup Enemies=%d Gates=%d RecoveryProbes=%d Reservations=%d BridgeComponents=%d\n"),LiveEnemies,LiveGates,LiveProbes,ActiveReservations,FRogueliteDynamicGameplayBridge::ActiveComponentCount(R));
            const int32 ExpectedClears=Scenario==TEXT("RecoveredSourceClear")?1:0;
            bool Good=Check(E->GetClearCount()==ExpectedClears&&E->GetGameplayActorCount()==0&&R->Batch.OwnedActors.IsEmpty()&&LiveEnemies==0&&LiveGates==0&&LiveProbes==0&&ActiveReservations==0&&FRogueliteDynamicGameplayBridge::ActiveComponentCount(R)==0&&Manual.IsValid(),TEXT("Still-active GameWorld has zero owned enemies, gates, controller-owned recovery probes, layout, reservations and leases; manual marker retained"));
            if(Scenario==TEXT("RegistrationFailure"))Good&=Check(E->GetSpawnedEnemyCount()<2,TEXT("Incomplete roster never auto-clears from zero remaining count"));
            if(Scenario==TEXT("IllegalStaticCollision"))Good&=Check(R->Stage==ERogueliteDungeonBuildStage::NavigationFailed&&R->LastFailureReason.Contains(TEXT("Current runtime Graph/Geometry changed")),TEXT("Actual Static collision modification rejected by original Watch reason"));
            if(Scenario==TEXT("ReopenedBeforeShot"))
            {
                Rows+=TEXT("BypassActualShotAccepted=")+(BypassAcceptedShot.IsValid()?BypassAcceptedShot->ToString():TEXT("None"))+TEXT("\n");
                Good&=Check(BypassAcceptedShot.IsValid()&&BypassAcceptedShot->IsValid()&&E->GetActivationCount()==0&&E->GetLastReason().Contains(FaultConnection.ToString())&&E->GetLastReason().Contains(TEXT("Preparation lock")),TEXT("Actual accepted shot refuses an explicitly reopened gate instead of repairing it or activating enemies"));
            }
            return Finish(Good,TEXT("Controlled cancellation path verified"));
        }
        if(Step==90){if(Now-Action>5)return Finish(false,TEXT("Expected cancellation/owned cleanup did not occur"));return false;}
        if(Scenario==TEXT("RegistrationFailure"))return false;
        if(!R->SliceFailure.IsEmpty())return Finish(false,TEXT("Rig refused gameplay: ")+R->SliceFailure);
        if(!R->bPlayerBridgeReady)return false;auto* FPS=R->GetSlicePlayer();if(!FPS||!FPS->HasLiveBinding())return Finish(false,TEXT("Actual local player binding lost"));
        auto* PC=Cast<ARogueliteSlicePlayerController>(FPS->GetController());if(!PC||!Input(FPS))return Finish(false,TEXT("Actual local EnhancedInput player unavailable"));
        DiagnosticCompletedShots(FPS);
        if(!R->GetTraversalSnapshot().bCurrent)return Finish(false,TEXT("Legal gameplay invalidated original structural Current"));
        if(Step==0)
        {
            if(E->GetState()!=ERogueliteSliceEncounterState::AwaitingEntry)return false;
            Registry=R->Batch.Registry;
            bool Good=Check(R->Batch.Rooms.Num()==4&&R->Batch.Edges.Num()==3&&R->CommittedLoops.Edges.IsEmpty()&&R->IsDungeonReady(),TEXT("Actual randomized M1 Floor1 four-room three-corridor tree and strict Ready"));
            const auto* Start=R->Batch.Rooms.FindByPredicate([&](const auto& Room){return Room.NodeId==R->PlayerStartNode;});
            Good&=Check(Start&&R->Settings.RoomTemplateRoles.bEnabled&&R->Settings.RoomTemplateRoles.StartTemplateIds.Contains(Start->TemplateId),TEXT("Existing Start node uses the actual G1 StartSafehouse template qualification"));
            Good&=Check(E->GetCurrentRoomNodeId()==R->PlayerStartNode&&E->GetTargetNodeId()!=R->PlayerStartNode&&E->GetSpawnedEnemyCount()==2&&E->GetRemainingEnemyCount()==2&&AllDormant(E),TEXT("Actual StartSafehouse player and pre-registered dormant Normal Combat roster"));
            for(const auto& Id:E->GetIncidentConnectionIds())Good&=Check(E->GetGate(Id)&&!E->GetGate(Id)->IsClosed(),TEXT("Actual incident committed connection starts open"));
            if(!Good)return Finish(false,TEXT("Initial encounter state invalid"));
            Rows+=FString::Printf(TEXT("PreviewSeed=%d Batch=%s Graph=%s Layout=%s Start=%d Target=%d Incident=%d\n"),R->Seed,*R->Batch.BatchId.ToString(),*R->Batch.GraphDigest,*R->Batch.LayoutDigest,R->PlayerStartNode,E->GetTargetNodeId(),E->GetIncidentConnectionIds().Num());
            TSet<FGuid> ActiveIds;int32 ActiveRecords=0;
            for(const auto& C:R->Batch.Registry->GetConnectionRecords())if(C.bActive){++ActiveRecords;ActiveIds.Add(C.Id);}
            if(!Check(ActiveRecords==3&&ActiveIds.Num()==3,TEXT("Only original C2 owns exactly three unique active connection records")))return Finish(false,TEXT("Initial reservation identity invalid"));
            const auto Traversal=R->GetTraversalSnapshot();
            for(const auto& Room:R->Batch.Rooms)
            {
                const auto* Node=Traversal.Nodes.FindByPredicate([&](const auto& N){return N.NodeId==Room.NodeId;});
                Rows+=FString::Printf(TEXT("Room Node=%d Role=%s RoomType=%d Template=%s Pose=%s\n"),Room.NodeId,Node?*Node->Role:TEXT("Unknown"),int32(Room.RoomType),*Room.TemplateId.ToString(),*Room.PlacedTransform.ToHumanReadableString());
            }
            for(const auto& Edge:R->Batch.Edges)Rows+=FString::Printf(TEXT("C2 Edge=%d Connection=%s Probes=%d Length=%.2f Passed=%d\n"),Edge.EdgeId,*Edge.ConnectionId.ToString(),Edge.Validation.Probes.Num(),Edge.CorridorLength,Edge.Validation.bSucceeded);
            if(IsCancellationCase())
            {
                if(Scenario==TEXT("PlayerDeath")){Rows+=TEXT("Fault=Direct Health damage; not counted as an enemy shooting kill\n");FPS->HealthComponent->ApplyDamageOnce(10000,FGuid::NewGuid(),FPS);}
                else if(Scenario==TEXT("SourceClear"))R->ClearRuntime();
                else if(Scenario==TEXT("SeedChange"))++R->Seed;
                else if(Scenario==TEXT("EnemyDestroy")){const auto Members=E->GetEnemies();if(!Members.IsEmpty()&&Members[0].IsValid())Members[0]->Destroy();else return Finish(false,TEXT("No actual member for destroy fault"));}
                else if(Scenario==TEXT("IllegalStaticCollision"))
                {
                    UStaticMeshComponent* Floor=nullptr;for(const auto& Room:R->Batch.Rooms)if(Room.NodeId==R->PlayerStartNode&&Room.Room)
                    {TArray<UStaticMeshComponent*> Meshes;Room.Room->GetComponents(Meshes);for(auto* M:Meshes)if(M&&M->GetFName()==TEXT("Floor")&&M->Mobility==EComponentMobility::Static){Floor=M;break;}}
                    if(!Floor)return Finish(false,TEXT("Actual runtime Static Floor component not found"));
                    Rows+=TEXT("Fault=Only current generated Static floor collision; no transform or asset mutation\n");Floor->SetCollisionEnabled(ECollisionEnabled::NoCollision);
                }
                Action=Now;Step=90;return false;
            }
            for(const auto& C:R->Batch.Registry->GetConnectionRecords())
            {
                FRogueliteDoorSocket Door;int32 Neighbor=INDEX_NONE;
                if(C.bActive&&DoorForNode(R,C,E->GetTargetNodeId(),Door,&Neighbor)&&Neighbor==R->PlayerStartNode)
                {ExpectedEntry=C.Id;EntryLanding=Door.WorldPosition-Door.WorldOutwardDirection*250+FVector(0,0,10);WalkConnection=C.Id;break;}
            }
            if(!ExpectedEntry.IsValid())return Finish(false,TEXT("No authoritative direct Start-to-target connection for actual player route"));
            FString FaultWhy;if(!ArmPreparationFault(R,E,FaultWhy))return Finish(false,TEXT("Could not arm bounded Preparation fault: ")+FaultWhy);
            if(ReloadCase()&&!Check(FPS->Ammo==FPS->MagazineCapacity&&FPS->MagazineCapacity==30&&FPS->ReloadStartedCount==0&&FPS->ReloadCompletedCount==0,TEXT("New PIE player has a fresh default magazine and no reload state from a previous World")))return Finish(false,TEXT("Old reload state leaked into a fresh PIE World"));
            PC->SetControlRotation(FRotator(85,0,0));Action=Now;
            if(Scenario==TEXT("ReloadPartialFullSeed1")){ReloadKey(FPS,true);Step=106;return false;}
            Step=EmptyEntryCase()?100:1;return false;
        }
        if(Step==100)
        {
            PC->SetControlRotation(FRotator(85,0,0));
            if(bAmmoPulse){FireInput(FPS,false);bAmmoPulse=false;return false;}
            const int32 Target=EmptyEntryCase()&&E->GetState()==ERogueliteSliceEncounterState::AwaitingEntry?1:0;
            if(FPS->Ammo>Target)
            {if(Now-LastFire>=.32){FireInput(FPS,true);LastFire=Now;bAmmoPulse=true;}return false;}
            Rows+=FString::Printf(TEXT("ActualMagazineExhaustion Phase=%d Shots=%d Ammo=%d Health=%.2f NoAmmoAssignment=1\n"),int32(E->GetState()),FPS->ShotAcceptedCount,FPS->Ammo,FPS->HealthComponent->GetHealth());
            if(Target==1){if(Now-LastFire<.3)return false;Action=Now;Step=1;return false;}
            BeginReload(FPS,E,Now,15);return false;
        }
        if(Step==103)
        {
            if(Now-Action<.12)return false;
            if(!Check(FPS->IsReloading()&&FPS->ReloadStartedCount==ReloadStarts+1&&FPS->Ammo==ReloadAmmo&&FPS->GetReloadRemainingSeconds()>0,TEXT("Actual R starts exactly one managed timer without immediate ammunition refill")))return Finish(false,TEXT("Actual R did not start the expected reload"));
            ReloadLastRemaining=FPS->GetReloadRemainingSeconds();FireInput(FPS,true);Action=Now;Step=104;return false;
        }
        if(Step==104)
        {
            const double Elapsed=Now-ReloadAt;FireInput(FPS,true);
            if(Elapsed<1.1&&Now>=NextReloadKey){bReloadKeyDown=!bReloadKeyDown;ReloadKey(FPS,bReloadKeyDown);NextReloadKey=Now+.2;}
            if(ReloadEncounterState!=ERogueliteSliceEncounterState::CombatActive)
            {
                if(Elapsed<.6){MoveInput(FPS,FVector2D(.25,0));Input(FPS)->InjectInputForAction(PC->GetLookAction(),FInputActionValue(FVector2D(.2,0)),{},{});}
                else {MoveInput(FPS,FVector2D::ZeroVector);Input(FPS)->InjectInputForAction(PC->GetLookAction(),FInputActionValue(FVector2D::ZeroVector),{},{});}
                Input(FPS)->InjectInputForAction(PC->GetJumpAction(),FInputActionValue(Elapsed<.4),{},{});
                ReloadMaxZ=FMath::Max(ReloadMaxZ,float(FPS->GetActorLocation().Z));
            }
            if(FPS->IsReloading())
            {
                const float Remaining=FPS->GetReloadRemainingSeconds();
                if(Remaining>ReloadLastRemaining+.05f||FPS->ReloadStartedCount!=ReloadStarts+1||FPS->Ammo!=ReloadAmmo||FPS->ShotAcceptedCount!=ReloadShots||Elapsed>FPS->ReloadDurationSeconds+.6)
                    return Finish(false,TEXT("Repeated real R/fire reset or stacked the reload, refilled early, fired, or exceeded its bounded deadline"));
                ReloadLastRemaining=Remaining;return false;
            }
            int32 Attacks=0;float EnemyHealth=0;for(const auto& Weak:E->GetEnemies())if(Weak.IsValid()){Attacks+=Weak->AttackCount;EnemyHealth+=Weak->Health->GetHealth();}
            FRogueliteSliceShot Recorded;
            bool Good=Check(FPS->ReloadStartedCount==ReloadStarts+1&&FPS->ReloadCompletedCount==ReloadCompletes+1&&FPS->Ammo==FPS->MagazineCapacity&&Elapsed>=FPS->ReloadDurationSeconds-.1&&Elapsed<FPS->ReloadDurationSeconds+.6,TEXT("Held/repeated R completes once at the configured duration and fills exactly to capacity"));
            Good&=Check(FPS->ShotAcceptedCount==ReloadShots&&FPS->ShotRejectedCount>ReloadRejects&&FPS->LastRejectedReason.Contains(TEXT("Reloading"))&&FPS->HitConfirmedCount==ReloadHits&&FPS->TotalDamageDealt==ReloadDamage&&FPS->GetLastShot().Id==ReloadLastShot&&FPS->FindAcceptedShot(ReloadLastShot,Recorded)&&EnemyHealth==ReloadEnemyHealth,TEXT("Rejected reload-time fire creates no accepted ShotId, consumes no ammunition, damages nothing and preserves the accepted-shot ledger"));
            Good&=Check(E->GetState()==ReloadEncounterState&&E->GetActivationCount()==(ReloadEncounterState==ERogueliteSliceEncounterState::CombatActive?1:0)&&E->GetClearCount()==0&&R->GetTraversalSnapshot().bCurrent,TEXT("Reload events preserve encounter phase and current structural fingerprint"));
            if(ReloadEncounterState==ERogueliteSliceEncounterState::Preparation)for(const auto Id:E->GetIncidentConnectionIds())Good&=Check(CheckClosed(R,E,FPS,Id),TEXT("Actual Preparation passage remains physically closed through reload and rejected fire"));
            if(ReloadEncounterState==ERogueliteSliceEncounterState::CombatActive)Good&=Check(Attacks>ReloadAttacks&&FPS->HealthComponent->GetHealth()<ReloadHealth,TEXT("Actual enemy attacks and real player damage continue during reload; no reload invulnerability"));
            else Good&=Check(FVector::Dist2D(ReloadPosition,FPS->GetActorLocation())>5&&ReloadMaxZ>ReloadPosition.Z+20&&FMath::Abs(FRotator::NormalizeAxis(PC->GetControlRotation().Yaw-ReloadRotation.Yaw))>.1f,TEXT("Actual Enhanced Input movement, look and jump remain available during reload"));
            Rows+=FString::Printf(TEXT("ReloadCompleted Elapsed=%.3f AmmoBefore=%d Ammo=%d Starts=%d Completes=%d ShotsBefore=%d Shots=%d AttacksBefore=%d Attacks=%d HPBefore=%.2f HP=%.2f Movement=%.2f JumpRise=%.2f\n"),Elapsed,ReloadAmmo,FPS->Ammo,FPS->ReloadStartedCount,FPS->ReloadCompletedCount,ReloadShots,FPS->ShotAcceptedCount,ReloadAttacks,Attacks,ReloadHealth,FPS->HealthComponent->GetHealth(),FVector::Dist2D(ReloadPosition,FPS->GetActorLocation()),ReloadMaxZ-ReloadPosition.Z);
            if(!Good)return Finish(false,TEXT("Reload input, safety or exact replenishment verification failed"));
            bReloadExercised=true;ReloadKey(FPS,false);MoveInput(FPS,FVector2D::ZeroVector);Input(FPS)->InjectInputForAction(PC->GetLookAction(),FInputActionValue(FVector2D::ZeroVector),{},{});Input(FPS)->InjectInputForAction(PC->GetJumpAction(),FInputActionValue(false),{},{});Action=Now;Step=105;return false;
        }
        if(Step==105)
        {
            FireInput(FPS,true);if(Now-Action<.35)return false;
            if(!Check(FPS->ShotAcceptedCount==ReloadShots&&FPS->Ammo==FPS->MagazineCapacity,TEXT("Keeping rejected fire held past reload completion never buffers or automatically fires a shot")))return Finish(false,TEXT("Reload completion fired automatically"));
            FireInput(FPS,false);Action=Now;
            if(ReloadResume==2){Step=108;return false;}
            if(ReloadResume==15){LastFire=Now;Step=15;return false;}
            ShotsBeforeExitTrials=FPS->ShotAcceptedCount;ExitIndex=0;Step=7;return false;
        }
        if(Step==106)
        {
            ReloadKey(FPS,false);if(Now-Action<.2)return false;
            bool HasR=false;for(const auto& Mapping:PC->GetSliceMappingContext()->GetMappings())HasR|=Mapping.Key==EKeys::R&&Mapping.Action==PC->GetReloadAction();
            bool Good=Check(HasR&&PC->ReloadInputEvents>0&&!FPS->IsReloading()&&FPS->Ammo==30&&FPS->ReloadStartedCount==0,TEXT("Actual R mapping exists; a full magazine never starts a reload"));
            const float Duration=FPS->ReloadDurationSeconds;const int32 Capacity=FPS->MagazineCapacity;
            for(const float Invalid:TArray<float>{0,-1,std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity()})
            {FPS->ReloadDurationSeconds=Invalid;Good&=Check(!FPS->TryReload()&&!FPS->IsReloading()&&FPS->Ammo==30,TEXT("Nonpositive or nonfinite reload duration is explicitly rejected without a timer"));}
            FPS->ReloadDurationSeconds=Duration;FPS->MagazineCapacity=0;Good&=Check(!FPS->TryReload()&&!FPS->IsReloading(),TEXT("Invalid magazine capacity is rejected without replenishment"));FPS->MagazineCapacity=Capacity;
            if(!Good)return Finish(false,TEXT("Reload configuration or full-magazine rejection failed"));
            OriginalFireInterval=FPS->FireIntervalSeconds;FPS->FireIntervalSeconds=4.f;Action=Now;Step=1;return false;
        }
        if(Step==108)
        {
            FireInput(FPS,false);if(Now-Action<.2)return false;
            RejectedBefore=FPS->ShotRejectedCount;FireInput(FPS,true);Action=Now;Step=109;return false;
        }
        if(Step==109)
        {
            FireInput(FPS,false);if(Now-Action<.1)return false;
            if(!Check(FPS->ShotAcceptedCount==ReloadShots&&FPS->ShotRejectedCount>RejectedBefore&&FPS->LastRejectedReason.Contains(TEXT("Shot interval"))&&FPS->Ammo==FPS->MagazineCapacity,TEXT("A new post-reload fire input still respects the original shot cooldown")))return Finish(false,TEXT("Reload reset the shot cooldown"));
            FPS->FireIntervalSeconds=OriginalFireInterval;Action=Now;Step=2;return false;
        }
        if(Step==110)
        {
            if(Now-Action<.2)return false;
            if(!Check(FPS->IsReloading()&&FPS->ReloadStartedCount==ReloadStarts+1&&FPS->Ammo==ReloadAmmo,TEXT("Lifecycle interruption begins during a real R reload")))return Finish(false,TEXT("No active reload for lifecycle test"));
            if(Scenario==TEXT("ReloadLifeStopPIE"))return Finish(true,TEXT("Real EndPIE requested while reload timer is pending; post-PIE teardown must remove the old player/World"));
            if(Scenario==TEXT("ReloadLifeDeath"))FPS->HealthComponent->ApplyDamageOnce(10000,FGuid::NewGuid(),FPS);
            else if(Scenario==TEXT("ReloadLifeClear"))R->ClearRuntime();
            else if(Scenario==TEXT("ReloadLifeBatchStale"))++R->Seed;
            else return Finish(false,TEXT("Unknown reload lifecycle fault"));
            Action=Now;Step=111;return false;
        }
        if(Step==150)
        {
            if(bFiringWalk){FString Why;const int32 Done=Walker.Tick(FPS,Rows,Why);if(Done<0)return Finish(false,Why);if(!Done)return false;bFiringWalk=false;}
            Step=100;return false;
        }
        if(Step==1)
        {if(Now-Action<.15)return false;AcceptedBefore=FPS->ShotAcceptedCount;FireInput(FPS,true);Action=Now;Step=2;return false;}
        if(Step==2)
        {
            FireInput(FPS,false);if(Now-Action<.1)return false;
            if(!Check(FPS->ShotAcceptedCount==AcceptedBefore+1&&E->GetState()==ERogueliteSliceEncounterState::AwaitingEntry&&AllDormant(E),TEXT("Actual safehouse accepted shot cannot activate another room")))return Finish(false,TEXT("Safehouse activation scope failed"));
            SafehouseShot=FPS->GetLastShot();
            if(Scenario.StartsWith(TEXT("ReloadLife"))){BeginReload(FPS,E,Now,2);Step=110;return false;}
            if(Scenario==TEXT("ReloadPartialFullSeed1")&&!bReloadExercised){BeginReload(FPS,E,Now,2);return false;}
            FRogueliteRoomConnection Connection;FRogueliteDoorSocket Door;
            if(!ReadConnection(R,ExpectedEntry,Connection)||!DoorForNode(R,Connection,E->GetTargetNodeId(),Door))return Finish(false,TEXT("Partial-entry test lost its actual doorway"));
            const FVector Partial=Door.WorldPosition-Door.WorldOutwardDirection*(Door.WallThickness+10)+FVector(0,0,10);
            FString Why;if(!Walker.Begin(FPS,Partial,Rows,Why,nullptr,10))return Finish(false,Why);Step=3;return false;
        }
        if(Step==3)
        {
            if(bReleaseCorridorFire){FireInput(FPS,false);bReleaseCorridorFire=false;}
            if(!bCorridorShot&&!EmptyEntryCase())
            {
                FRogueliteRoomConnection C;if(ReadConnection(R,ExpectedEntry,C)&&C.Corridor)
                {
                    const FVector L=C.Corridor->GetActorTransform().InverseTransformPosition(FPS->GetActorLocation());
                    if(FMath::Abs(L.X)<C.Corridor->CorridorLength*.22&&FMath::Abs(L.Y)<C.Corridor->CorridorWidth*.5-35)
                    {CorridorAcceptedBefore=FPS->ShotAcceptedCount;FireInput(FPS,true);bReleaseCorridorFire=true;bCorridorShot=true;Rows+=TEXT("Actual corridor fire injection on committed connection=")+ExpectedEntry.ToString()+TEXT("\n");}
                }
            }
            FString Why;const int32 Done=Walker.Tick(FPS,Rows,Why);if(Done<0)return Finish(false,Why);if(!Done)return false;
            MoveInput(FPS,FVector2D::ZeroVector);Action=Now;Step=30;return false;
        }
        if(Step==30)
        {
            if(Now-Action<.3)return false;
            FRogueliteRoomConnection Connection;FRogueliteDoorSocket Door;
            const auto* Room=R->Batch.Rooms.FindByPredicate([&](const auto& V){return V.NodeId==E->GetTargetNodeId();});
            if(!Room||!ReadConnection(R,ExpectedEntry,Connection)||!DoorForNode(R,Connection,E->GetTargetNodeId(),Door))return Finish(false,TEXT("Partial-entry identity missing"));
            const float Signed=FVector::DotProduct(FPS->GetActorLocation()-Door.WorldPosition,Door.WorldOutwardDirection);
            Rows+=FString::Printf(TEXT("PartialEntry Connection=%s Signed=%.3f FullRoom=%d SafeDepth=%.3f Position=%s\n"),*ExpectedEntry.ToString(),Signed,FullRoom(*Room,FPS->GetActorLocation()),Door.WallThickness+34+25+2,*FPS->GetActorLocation().ToString());
            bool Good=Check(Signed>-(Door.WallThickness+34+25+2)&&!FullRoom(*Room,FPS->GetActorLocation())&&E->GetState()==ERogueliteSliceEncounterState::AwaitingEntry&&AllDormant(E),TEXT("Camera/center threshold crossing without full safe capsule entry cannot establish Preparation"));
            for(const auto Id:E->GetIncidentConnectionIds())Good&=Check(E->GetGate(Id)&&!E->GetGate(Id)->IsClosed(),TEXT("No real passage closes on a partial player entry"));
            if(!Good)return Finish(false,TEXT("Partial capsule entry closed a gate or started Preparation"));
            FString Why;if(!Walker.Begin(FPS,EntryLanding,Rows,Why))return Finish(false,Why);Step=31;return false;
        }
        if(Step==31)
        {
            FString Why;const int32 Done=Walker.Tick(FPS,Rows,Why);if(Done<0)return Finish(false,Why);if(!Done)return false;
            Action=Now;Step=4;return false;
        }
        if(Step==4)
        {
            if(Now-Action<.3)return false;
            if((Scenario==TEXT("DoorOccupied")||Scenario==TEXT("PartialRollback"))&&!bPreparationFaultRestored)
            {
                const FString Failure=E->GetLastReason();Rows+=TEXT("PreparationRefusal=")+Failure+TEXT("\n");
                bool Good=Check(E->GetState()==ERogueliteSliceEncounterState::AwaitingEntry&&E->GetActivationCount()==0&&E->GetClearCount()==0&&AllDormant(E)&&R->IsDungeonReady()&&R->GetTraversalSnapshot().bCurrent&&!R->Batch.OwnedActors.IsEmpty()&&Failure.Contains(FaultConnection.ToString()),TEXT("Refused lock names actual failed connection, keeps original Ready batch and never claims Preparation, Combat or victory"));
                for(const auto Id:E->GetIncidentConnectionIds())Good&=Check(E->GetGate(Id)&&!E->GetGate(Id)->IsClosed(),TEXT("Rejected transaction leaves no newly half-closed passage"));
                if(Scenario==TEXT("DoorOccupied"))
                {
                    auto* Occupant=DoorOccupant.Get();
                    Good&=Check(Occupant&&Occupant->GetActorTransform().Equals(OccupantBefore,.001)&&Occupant->GetCapsuleComponent()->GetCollisionEnabled()==ECollisionEnabled::QueryAndPhysics&&FRogueliteDynamicGameplayBridge::IsComponentIsolated(R,R->Batch.BatchId,Occupant->GetCapsuleComponent()),TEXT("Encounter refusal neither deletes, moves nor disables the independent doorway occupant"));
                    if(!Good)return Finish(false,TEXT("Occupied doorway was not safely refused"));
                    Occupant->GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
                    FRogueliteDynamicGameplayBridge::UnregisterComponent(R,Occupant->GetCapsuleComponent());Occupant->Destroy();DoorOccupant.Reset();
                    Rows+=TEXT("Fixture owner explicitly releases its own doorway occupant after refusal; Encounter did not remove it.\n");
                }
                else
                {
                    Good&=Check(Failure.Contains(TEXT("rollback reopened"))&&!Failure.Contains(TEXT("rollback reopened 0"))&&Failure.Contains(TEXT("restored=1")),TEXT("Actual closed-gate ray validation failure rolls back earlier real collision changes"));
                    if(!Good)return Finish(false,TEXT("Partial lock did not roll back atomically"));
                    E->GetGate(FaultConnection)->GetBlocker()->SetCollisionResponseToChannel(ECC_Visibility,FaultVisibility);
                }
                bPreparationFaultRestored=true;Action=Now;Step=32;return false;
            }
            if(!Check(E->GetState()==ERogueliteSliceEncounterState::Preparation&&E->GetCurrentRoomNodeId()==E->GetTargetNodeId()&&E->GetEntryConnectionId()==ExpectedEntry&&(EmptyEntryCase()?FPS->Ammo==0:(bCorridorShot&&FPS->ShotAcceptedCount==CorridorAcceptedBefore+1))&&E->GetActivationCount()==0&&AllDormant(E),TEXT("Real player traversed actual corridor, fired an accepted nonactivating corridor shot and entered through correct committed socket before Preparation")))return Finish(false,TEXT("Entry provenance or Preparation failed"));
            int32 ActualIncident=0;for(const auto& Edge:R->GetTraversalSnapshot().Edges)ActualIncident+=(Edge.NodeA==E->GetTargetNodeId()||Edge.NodeB==E->GetTargetNodeId());
            bool Locked=Check(ActualIncident==E->GetIncidentConnectionIds().Num()&&ActualIncident>0,TEXT("Preparation lock count equals all actual committed incident edges"));
            if(R->Seed==0)Locked&=Check(ActualIncident==3,TEXT("Seed0 fixture actually exercises three incident passages"));
            for(const auto Id:E->GetIncidentConnectionIds())Locked&=Check(CheckClosed(R,E,FPS,Id),TEXT("Every actual passage already blocks both Pawn sweeps and doorway rays before any combat shot"));
            if(!Locked)return Finish(false,TEXT("Preparation has an unverified or missing passage lock"));
            if(bPreparationFaultRestored)return Finish(true,TEXT("Bounded lock fault refused and restored without clearing structure; all passages secure only after actual fault release"));
            if(EmptyEntryCase()){BeginReload(FPS,E,Now,7);return false;}
            Action=Now;Step=5;return false;
        }
        if(Step==32)
        {
            if(E->GetState()!=ERogueliteSliceEncounterState::Preparation)
            {if(Now-Action>3)return Finish(false,TEXT("Preparation did not recover after its actual lock fault was released: ")+E->GetLastReason());return false;}
            Action=Now-.3;Step=4;return false;
        }
        if(Step==5)
        {
            if(Now-Action<1.3)return false;
            if(!Check(E->GetState()==ERogueliteSliceEncounterState::Preparation&&AllDormant(E)&&R->GetTraversalSnapshot().bCurrent,TEXT("Preparation waits across multiple Watch cycles without automatic combat")))return Finish(false,TEXT("Preparation auto-triggered or structure invalidated"));
            for(const auto Id:E->GetIncidentConnectionIds())if(!Check(CheckClosed(R,E,FPS,Id),TEXT("All Preparation passages remain physically sealed across multiple Watch cycles")))return Finish(false,TEXT("A Preparation lock drifted open"));
            FPS->OnShotAccepted.Broadcast(SafehouseShot);
            if(!Check(E->GetActivationCount()==0,TEXT("Replaying a real safehouse ShotId after entering cannot activate the room")))return Finish(false,TEXT("Replay activated combat"));
            if(ReloadCase()){ShotsBeforeExitTrials=FPS->ShotAcceptedCount;ExitIndex=0;Action=Now;Step=7;return false;}
            FPS->Ammo=0;AcceptedBefore=FPS->ShotAcceptedCount;RejectedBefore=FPS->ShotRejectedCount;FireInput(FPS,true);Action=Now;Step=6;return false;
        }
        if(Step==6)
        {
            FireInput(FPS,false);if(Now-Action<.1)return false;
            if(!Check(FPS->ShotAcceptedCount==AcceptedBefore&&FPS->ShotRejectedCount>RejectedBefore&&E->GetState()==ERogueliteSliceEncounterState::Preparation&&AllDormant(E),TEXT("Real empty-ammo input emits no ShotAccepted and cannot activate Preparation")))return Finish(false,TEXT("Rejected fire activated encounter"));
            for(const auto Id:E->GetIncidentConnectionIds())if(!Check(CheckClosed(R,E,FPS,Id),TEXT("Rejected fire leaves every actual gate locked")))return Finish(false,TEXT("Rejected fire changed a gate"));
            FPS->Ammo=30;Rows+=TEXT("Validation-only ammunition restored after explicit empty-ammo fault\n");
            if(Scenario==TEXT("PreparationDeath")||Scenario==TEXT("PreparationClear")||Scenario==TEXT("ReopenedBeforeShot"))
            {
                if(Scenario==TEXT("PreparationDeath"))FPS->HealthComponent->ApplyDamageOnce(10000,FGuid::NewGuid(),FPS);
                else if(Scenario==TEXT("PreparationClear"))R->ClearRuntime();
                else
                {
                    FaultConnection=E->GetIncidentConnectionIds().Last();FString Why;
                    if(!E->GetGate(FaultConnection)->SetClosed(false,Why))return Finish(false,TEXT("Could not arm a controlled gate-reopening bypass: ")+Why);
                    Rows+=TEXT("Fault=Controlled reopening before first shot; activation must reject, never reclose it. Connection=")+FaultConnection.ToString()+TEXT("\n");
                    BypassAcceptedShot=MakeShared<FGuid>();
                    FPS->OnShotAccepted.AddLambda([Event=BypassAcceptedShot](const FRogueliteSliceShot& Shot){*Event=Shot.Id;});
                    Action=Now;Step=33;return false;
                }
                Action=Now;Step=90;return false;
            }
            ShotsBeforeExitTrials=FPS->ShotAcceptedCount;ExitIndex=0;Action=Now;Step=7;return false;
        }
        if(Step==33)
        {
            // Let Enhanced Input observe the prior release in a real frame before a new Started event.
            FireInput(FPS,false);if(Now-Action<.2)return false;
            FireInput(FPS,true);Action=Now;Step=90;return false;
        }
        if(Step==7)
        {
            if(ExitIndex>=E->GetIncidentConnectionIds().Num()){Action=Now;Step=11;return false;}
            PushConnection=E->GetIncidentConnectionIds()[ExitIndex];FRogueliteRoomConnection C;FRogueliteDoorSocket Door;
            if(!ReadConnection(R,PushConnection,C)||!DoorForNode(R,C,E->GetTargetNodeId(),Door))return Finish(false,TEXT("Blocked movement trial lost its actual target doorway"));
            PushDoor=Door.WorldPosition;PushNormal=Door.WorldOutwardDirection;
            const auto* Room=R->Batch.Rooms.FindByPredicate([&](const auto& V){return V.NodeId==E->GetTargetNodeId();});FString Why;
            if(!Walker.Begin(FPS,PushDoor-PushNormal*180+FVector(0,0,10),Rows,Why,Room))return Finish(false,Why);
            Step=8;return false;
        }
        if(Step==8)
        {
            FString Why;const int32 Done=Walker.Tick(FPS,Rows,Why);if(Done<0)return Finish(false,Why);if(!Done)return false;
            PushStart=FPS->GetActorLocation();PC->SetControlRotation(PushNormal.Rotation());Action=Now;Step=9;return false;
        }
        if(Step==9)
        {
            PC->SetControlRotation(PushNormal.Rotation());MoveInput(FPS,FVector2D(0,1));if(Now-Action<1.2)return false;
            const FVector Position=FPS->GetActorLocation();const float Signed=FVector::DotProduct(Position-PushDoor,PushNormal);
            const float Travel=FVector::DotProduct(Position-PushStart,PushNormal);MoveInput(FPS,FVector2D::ZeroVector);
            Rows+=FString::Printf(TEXT("ActualBlockedExit Connection=%s IsEntry=%d Seconds=%.3f From=%s Actual=%s Signed=%.3f ForwardTravel=%.3f Speed=%.3f AcceptedShots=%d\n"),
                *PushConnection.ToString(),PushConnection==ExpectedEntry,Now-Action,*PushStart.ToString(),*Position.ToString(),Signed,Travel,FPS->GetVelocity().Size2D(),FPS->ShotAcceptedCount);
            if(!Check(Signed<=-55&&Travel>20&&FPS->GetVelocity().Size2D()<15&&E->GetCurrentRoomNodeId()==E->GetTargetNodeId()&&E->GetState()==ERogueliteSliceEncounterState::Preparation&&AllDormant(E)&&E->GetActivationCount()==0&&FPS->ShotAcceptedCount==ShotsBeforeExitTrials&&R->GetTraversalSnapshot().bCurrent&&CheckClosed(R,E,FPS,PushConnection),TEXT("Actual standing FPS input attempts this committed exit without shooting, reaches its physical gate and cannot leave Preparation")))return Finish(false,TEXT("Player bypassed a Preparation passage or movement never reached its blocker"));
            BlockedExits.AddUnique(PushConnection);++ExitIndex;Step=7;return false;
        }
        if(Step==11)
        {
            if(Now-Action<.3)return false;
            if(!Check(BlockedExits.Num()==E->GetIncidentConnectionIds().Num(),TEXT("Every actual incident exit, including all non-entry exits, has its own real blocked movement trial")))return Finish(false,TEXT("An actual exit lacks physical movement coverage"));
            if(FirstHitCase())
            {
                for(const auto& Weak:E->GetEnemies())if(Weak.IsValid()&&Weak->Health->IsAlive()){AimedEnemy=Weak;break;}
                FVector Position;if(!AimedEnemy.IsValid()||!FindFiringPosition(R,FPS,AimedEnemy.Get(),Position))return Finish(false,TEXT("No legal indoor line of fire for first-hit case"));
                const auto* Room=R->Batch.Rooms.FindByPredicate([&](const auto& V){return V.NodeId==E->GetTargetNodeId();});FString Why;
                if(FVector::Dist2D(Position,FPS->GetActorLocation())>45){if(!Walker.Begin(FPS,Position,Rows,Why,Room))return Finish(false,Why);bFiringWalk=true;}
                HealthBefore=AimedEnemy->Health->GetHealth();HitsBefore=FPS->HitConfirmedCount;Step=12;return false;
            }
            PC->SetControlRotation(FRotator(85,0,0));AcceptedBefore=FPS->ShotAcceptedCount;Action=Now;Step=13;return false;
        }
        if(Step==12)
        {
            if(bFiringWalk){FString Why;const int32 Done=Walker.Tick(FPS,Rows,Why);if(Done<0)return Finish(false,Why);if(!Done)return false;bFiringWalk=false;}
            Aim(FPS,AimedEnemy->GetActorLocation());AcceptedBefore=FPS->ShotAcceptedCount;Action=Now;Step=13;return false;
        }
        if(Step==13)
        {if(Now-Action<.15)return false;FireInput(FPS,true);Action=Now;Step=14;return false;}
        if(Step==14)
        {
            FireInput(FPS,false);if(Now-Action<.1)return false;
            if(!Check(FPS->ShotAcceptedCount==AcceptedBefore+1&&E->GetState()==ERogueliteSliceEncounterState::CombatActive&&E->GetActivationCount()==1,TEXT("First actual ShotAccepted inside Preparation activates only this encounter exactly once")))return Finish(false,TEXT("Combat activation failed"));
            if(FirstHitCase())
            {if(!Check(AimedEnemy.IsValid()&&AimedEnemy->Health->GetHealth()==HealthBefore-FPS->ShotDamage&&FPS->HitConfirmedCount==HitsBefore+1,TEXT("First real hit activates before actual damage; dormant rules do not swallow it")))return Finish(false,TEXT("First-hit activation/damage ordering failed"));}
            else if(!Check(!FPS->GetLastShot().bHit&&FPS->HitConfirmedCount==0,TEXT("Actual accepted miss still starts Combat")))return Finish(false,TEXT("Miss activation classification failed"));
            FPS->OnShotAccepted.Broadcast(FPS->GetLastShot());if(!Check(E->GetActivationCount()==1,TEXT("Duplicate actual accepted ShotId cannot activate twice")))return Finish(false,TEXT("Duplicate combat activation"));
            for(const auto& Id:E->GetIncidentConnectionIds())if(!Check(CheckClosed(R,E,FPS,Id),TEXT("Every actual incident connection has real closed Pawn collision in Combat")))return Finish(false,TEXT("Combat gate collision missing"));
            if(Scenario==TEXT("ReloadCombatEmpty"))
            {
                AimedEnemy.Reset();for(const auto& Weak:E->GetEnemies())if(Weak.IsValid()&&Weak->Health->IsAlive()){AimedEnemy=Weak;break;}
                FVector Position;if(!AimedEnemy.IsValid()||!FindFiringPosition(R,FPS,AimedEnemy.Get(),Position))return Finish(false,TEXT("No actual combat line of sight for reload-under-fire test"));
                const auto* Room=R->Batch.Rooms.FindByPredicate([&](const auto& V){return V.NodeId==E->GetTargetNodeId();});FString Why;
                bFiringWalk=FVector::Dist2D(Position,FPS->GetActorLocation())>45;
                if(bFiringWalk&&!Walker.Begin(FPS,Position,Rows,Why,Room))return Finish(false,Why);
                LastFire=Now;Step=150;return false;
            }
            LastFire=Now;Action=Now;Step=15;return false;
        }
        if(Step==15)
        {
            if(E->GetState()==ERogueliteSliceEncounterState::Cleared){Action=Now;Step=18;return false;}
            if(E->GetState()!=ERogueliteSliceEncounterState::CombatActive)return Finish(false,TEXT("Combat regressed or cancelled: ")+E->GetLastReason());
            if(Now-LastFire<FPS->FireIntervalSeconds+.1)return false;
            AimedEnemy.Reset();for(const auto& Weak:E->GetEnemies())if(Weak.IsValid()&&Weak->Health->IsAlive()){AimedEnemy=Weak;break;}
            if(!AimedEnemy.IsValid())return Finish(false,TEXT("Zero live actors without a legal Cleared commit"));
            FVector Position;if(!FindFiringPosition(R,FPS,AimedEnemy.Get(),Position))return Finish(false,TEXT("No reachable indoor firing point; do not teleport or ignore blockers"));
            const auto* Room=R->Batch.Rooms.FindByPredicate([&](const auto& V){return V.NodeId==E->GetTargetNodeId();});FString Why;
            bFiringWalk=FVector::Dist2D(Position,FPS->GetActorLocation())>45;
            if(bFiringWalk&&!Walker.Begin(FPS,Position,Rows,Why,Room))return Finish(false,Why);
            Step=16;return false;
        }
        if(Step==16)
        {
            if(bFiringWalk){FString Why;const int32 Done=Walker.Tick(FPS,Rows,Why);if(Done<0)return Finish(false,Why);if(!Done)return false;bFiringWalk=false;}
            if(!AimedEnemy.IsValid()||!AimedEnemy->Health->IsAlive()){Step=15;return false;}
            Aim(FPS,AimedEnemy->GetActorLocation());Action=Now;Step=17;return false;
        }
        if(Step==17)
        {
            if(ReloadCase()&&AimedEnemy.IsValid())Aim(FPS,AimedEnemy->GetActorLocation());
            if(Now-Action<(ReloadCase()?.25:.15))return false;
            DiagnosticState(TEXT("BeforeFireInput"),FPS);
            Rows+=FString::Printf(TEXT("AimBeforeFire Target=%s TargetPos=%s ActualPlayer=%s CameraOrigin=%s CameraRotation=%s ControlRotation=%s\n"),*GetNameSafe(AimedEnemy.Get()),AimedEnemy.IsValid()?*AimedEnemy->GetActorLocation().ToString():TEXT("None"),*FPS->GetActorLocation().ToString(),*FPS->FirstPersonCamera->GetComponentLocation().ToString(),*FPS->FirstPersonCamera->GetComponentRotation().ToString(),*PC->GetControlRotation().ToString());
            FireInput(FPS,true);Action=LastFire=Now;Step=19;return false;
        }
        if(Step==19)
        {
            FireInput(FPS,false);if(Now-Action<.1)return false;
            const auto& Shot=FPS->GetLastShot();
            DiagnosticCompletedShots(FPS);
            Rows+=FString::Printf(TEXT("AimActualShot Id=%s Origin=%s Direction=%s Hit=%s Impact=%s Damage=%.2f\n"),*Shot.Id.ToString(),*Shot.Origin.ToString(),*Shot.Direction.ToString(),*GetNameSafe(Shot.Hit.GetActor()),*Shot.Hit.ImpactPoint.ToString(),FPS->LastAppliedDamage);
            ConsecutiveAimMisses=FPS->LastAppliedDamage>0?0:ConsecutiveAimMisses+1;
            if(ReloadCase()&&ConsecutiveAimMisses>=3)return Finish(false,TEXT("Three real aimed shots failed to damage the target; preserve actual camera/control/trace diagnostics instead of exhausting the player"));
            Step=15;return false;
        }
        if(Step==18)
        {
            if(!Check(E->GetState()==ERogueliteSliceEncounterState::Cleared&&E->GetRemainingEnemyCount()==0&&E->GetClearCount()==1,TEXT("Only two legal member deaths commit Cleared once")))return Finish(false,TEXT("Cleared state invalid"));
            for(const auto& Id:E->GetIncidentConnectionIds())if(!Check(E->GetGate(Id)&&!E->GetGate(Id)->IsClosed(),TEXT("Cleared opens every actual gate without releasing C2 connection reservations")))return Finish(false,TEXT("Gate remained sealed after clear"));
            FVector Waiting;FString Why;if(!FindRecoveryWaitingPosition(R,FPS,Waiting,Why))return Finish(false,Why);
            const auto* Room=R->Batch.Rooms.FindByPredicate([&](const auto& Row){return Row.NodeId==E->GetTargetNodeId();});
            if(!Walker.Begin(FPS,Waiting,Rows,Why,Room))return Finish(false,Why);Step=25;return false;
        }
        if(Step==25)
        {
            FString Why;const int32 Done=Walker.Tick(FPS,Rows,Why);if(Done<0)return Finish(false,Why);if(!Done)return false;
            const FVector Feet=FPS->GetActorLocation()-FVector(0,0,88);const float Clearance=FMath::PointDistToSegment(Feet,RecoveryStart,RecoveryGoal);
            Rows+=FString::Printf(TEXT("ActualWaitingFeet=%s ActualRecoveryClearance=%.2f\n"),*Feet.ToString(),Clearance);
            if(!Check(Clearance>=160&&E->GetCurrentRoomNodeId()==E->GetTargetNodeId(),TEXT("Actual player walked within this room to a floor-supported waiting point clear of recovery path")))return Finish(false,TEXT("Actual waiting position still obstructs recovery or left the room"));
            Action=Now;Step=20;return false;
        }
        if(Step==20)
        {
            const auto Q=FRogueliteDynamicGameplayBridge::QueryConnection(R,E->GetExitConnectionId(),FPS);
            if(Q.State!=ERogueliteGameplayPassage::Traversable){if(Now-Action>45)return Finish(false,TEXT("Actual post-clear recovery did not become Traversable: ")+E->Describe());return false;}
            if(!Check(Q.bForwardCapsule&&Q.bReverseCapsule&&Q.bFullPath&&Q.bMoveVerified&&Q.bStructuralCurrent&&Q.bBaseNavigationReady,TEXT("Selected exit has current two-way sweeps, full nonPartial path and actual Walking MoveTo proof")))return Finish(false,TEXT("Incomplete gameplay passage proof"));
            Rows+=FRogueliteDynamicGameplayBridge::RecoveryEvidence(R)+TEXT("\n");
            FRogueliteRoomConnection C;FRogueliteDoorSocket Here,There;
            if(!ReadConnection(R,E->GetExitConnectionId(),C)||!DoorForNode(R,C,E->GetTargetNodeId(),Here,&OtherNode)||!DoorForNode(R,C,OtherNode,There))return Finish(false,TEXT("Recovered exit no longer authoritative"));
            TargetReturn=Here.WorldPosition-Here.WorldOutwardDirection*250+FVector(0,0,10);
            OtherLanding=There.WorldPosition-There.WorldOutwardDirection*250+FVector(0,0,10);FString Why;
            if(!Walker.Begin(FPS,OtherLanding,Rows,Why))return Finish(false,Why);Step=21;return false;
        }
        if(Step==21)
        {FString Why;const int32 Done=Walker.Tick(FPS,Rows,Why);if(Done<0)return Finish(false,Why);if(!Done)return false;Action=Now;Step=22;return false;}
        if(Step==22)
        {
            if(Now-Action<.2)return false;
            if(!Check(E->GetCurrentRoomNodeId()==OtherNode&&E->GetState()==ERogueliteSliceEncounterState::Cleared,TEXT("Real FPS player physically leaves through the verified connection")))return Finish(false,TEXT("Actual room exit failed"));
            FString Why;if(!Walker.Begin(FPS,TargetReturn,Rows,Why))return Finish(false,Why);Step=23;return false;
        }
        if(Step==23)
        {FString Why;const int32 Done=Walker.Tick(FPS,Rows,Why);if(Done<0)return Finish(false,Why);if(!Done)return false;Action=Now;Step=24;return false;}
        if(Step==24)
        {
            if(Now-Action<.3)return false;
            bool Good=Check(E->GetCurrentRoomNodeId()==E->GetTargetNodeId()&&E->GetState()==ERogueliteSliceEncounterState::Cleared&&E->GetClearCount()==1&&E->GetActivationCount()==1&&E->GetSpawnedEnemyCount()==2,TEXT("Physical cleared-room revisit does not respawn, reactivate or reclose gates"));
            for(const auto& Id:E->GetIncidentConnectionIds())Good&=Check(E->GetGate(Id)&&!E->GetGate(Id)->IsClosed(),TEXT("Revisited cleared connection stays open"));
            TSet<FGuid> ActiveIds;int32 ActiveRecords=0;for(const auto& C:R->Batch.Registry->GetConnectionRecords())if(C.bActive){++ActiveRecords;ActiveIds.Add(C.Id);}
            Good&=Check(ActiveRecords==3&&ActiveIds.Num()==3&&R->GetTraversalSnapshot().bCurrent,TEXT("Gameplay lifecycle preserves sole C2 reservations and current structural batch"));
            Rows+=FString::Printf(TEXT("FinalShots=%d Hits=%d Damage=%.1f Health=%.1f\n"),FPS->ShotAcceptedCount,FPS->HitConfirmedCount,FPS->TotalDamageDealt,FPS->HealthComponent->GetHealth());
            if(Good&&Scenario==TEXT("RecoveredSourceClear")){Rows+=TEXT("Fault=Clear after actual recovery probe walked and parked; inspect live GameWorld before PIE ends\n");R->ClearRuntime();Action=Now;Step=90;return false;}
            return Finish(Good,TEXT("Actual single-room FPS loop, recovery, physical exit and revisit verified"));
        }
        return false;
    }
};

class FAfterPIE:public IAutomationLatentCommand
{
    FAutomationTestBase* Test;TWeakObjectPtr<ARogueliteCombatSliceRig> EditorRig;bool Previous;int32 Seed;
    FString Scenario,Rows;double Started=0,NextSample=0;
    TSharedPtr<FReloadLifetimeObservation> ReloadLifetime;
    void Save() const
    {
        FString Dir;if(!FParse::Value(FCommandLine::Get(),TEXT("G1BEvidenceDir="),Dir))return;
        IFileManager::Get().MakeDirectory(*Dir,true);FString File=Dir/TEXT("encounter-stop-")+Scenario+TEXT(".txt");
        if(IFileManager::Get().FileExists(*File))File+=TEXT(".")+FDateTime::UtcNow().ToString(TEXT("%Y%m%d%H%M%S"))+TEXT(".rerun");
        if(!FFileHelper::SaveStringToFile(Rows,*File))Test->AddError(TEXT("Could not preserve post-PIE diagnostic: ")+File);
    }
public:
    FAfterPIE(FAutomationTestBase* T,ARogueliteCombatSliceRig* R,bool Mode,int32 OldSeed,FString Case,TSharedPtr<FReloadLifetimeObservation> Proof):Test(T),EditorRig(R),Previous(Mode),Seed(OldSeed),Scenario(MoveTemp(Case)),ReloadLifetime(MoveTemp(Proof)){}
    bool Update()override
    {
        const double Now=FPlatformTime::Seconds();if(!Started){Started=Now;NextSample=Now;}
        int32 PIEContexts=0;if(GEngine)for(const auto& Context:GEngine->GetWorldContexts())PIEContexts+=Context.WorldType==EWorldType::PIE;
        const bool HasPlayWorld=GEditor&&GEditor->PlayWorld;
        if(Now>=NextSample)
        {
            NextSample=Now+.25;
            Rows+=FString::Printf(TEXT("EndPIEWait Seconds=%.3f Editor=%d PlayWorld=%d PIEContexts=%d EditorRig=%d\n"),Now-Started,GEditor!=nullptr,HasPlayWorld,PIEContexts,EditorRig.IsValid());
        }
        // FEndPlayMapCommand requests EndPIE and returns immediately. Observe actual teardown,
        // rather than testing Editor state in the same frame as the asynchronous request.
        if(HasPlayWorld||PIEContexts>0)
        {
            if(Now-Started<15)return false;
            Rows+=TEXT("Passed=0\nReason=Timed out waiting for actual PIE World teardown\n");Save();
            Test->AddError(TEXT("PIE teardown timeout: PlayWorld or PIE WorldContext still present after 15 seconds"));return true;
        }
        FWorldDelegates::OnWorldCleanup.Remove(ReloadLifetime->WorldCleanupHandle);
        ReloadLifetime->WorldCleanupHandle.Reset();
        if(Scenario==TEXT("ReloadLifeStopPIE"))
        {
            const bool Safe=ReloadLifetime->bObserved&&!ReloadLifetime->bReloading&&ReloadLifetime->Remaining==0&&ReloadLifetime->Ammo==29&&ReloadLifetime->Completes==0&&ReloadLifetime->Cancels==1;
            Rows+=FString::Printf(TEXT("ActualEndPlayReload Observed=%d Ammo=%d Completed=%d Cancelled=%d Reloading=%d Remaining=%.3f Passed=%d\n"),ReloadLifetime->bObserved,ReloadLifetime->Ammo,ReloadLifetime->Completes,ReloadLifetime->Cancels,ReloadLifetime->bReloading,ReloadLifetime->Remaining,Safe);
            Test->TestTrue(TEXT("Actual World teardown cancels its pending reload without completing or replenishing it"),Safe);
        }
        auto* W=GEditor?GEditor->GetEditorWorldContext().World():nullptr;
        if(EditorRig.IsValid()){EditorRig->bEnableCombatEncounter=Previous;EditorRig->Seed=Seed;}
        int32 Gameplay=0;if(W)for(TActorIterator<AActor>I(W);I;++I)if(!I->IsActorBeingDestroyed()&&(I->IsA<ARogueliteSlicePlayer>()||I->IsA<ARogueliteSliceEnemy>()||I->IsA<ARogueliteSliceGate>()||I->IsA<ARogueliteSliceRecoveryProbe>()))++Gameplay;
        const bool RigValid=EditorRig.IsValid(),OwnedEmpty=RigValid&&EditorRig->Batch.OwnedActors.IsEmpty(),BatchInvalid=RigValid&&!EditorRig->Batch.BatchId.IsValid();
        const bool RegistryEmpty=RigValid&&EditorRig->Batch.Registry==nullptr,RoomsEmpty=RigValid&&EditorRig->Batch.Rooms.IsEmpty(),EdgesEmpty=RigValid&&EditorRig->Batch.Edges.IsEmpty();
        const bool Good=GEditor&&!HasPlayWorld&&PIEContexts==0&&W&&Gameplay==0&&RigValid&&OwnedEmpty&&BatchInvalid&&RegistryEmpty&&RoomsEmpty&&EdgesEmpty;
        Rows+=FString::Printf(TEXT("EditorAfterStop World=%s WorldType=%d GameplayActors=%d EditorRig=%s OwnedActors=%d Rooms=%d Edges=%d Registry=%s BatchId=%s OwnedEmpty=%d BatchInvalid=%d RegistryEmpty=%d RoomsEmpty=%d EdgesEmpty=%d RestoredMode=%d RestoredSeed=%d\nPassed=%d\n"),
            W?*W->GetPathName():TEXT("None"),W?int32(W->WorldType):-1,Gameplay,RigValid?*EditorRig->GetPathName():TEXT("None"),
            RigValid?EditorRig->Batch.OwnedActors.Num():-1,RigValid?EditorRig->Batch.Rooms.Num():-1,RigValid?EditorRig->Batch.Edges.Num():-1,
            RigValid?*GetNameSafe(EditorRig->Batch.Registry.Get()):TEXT("None"),RigValid?*EditorRig->Batch.BatchId.ToString():TEXT("None"),OwnedEmpty,BatchInvalid,RegistryEmpty,RoomsEmpty,EdgesEmpty,Previous,Seed,Good);
        Save();Test->TestTrue(TEXT("PIE stop writes no gameplay actors, batch or registry into Editor map"),Good);return true;
    }
};
bool Queue(FAutomationTestBase* Test,const FString& Scenario)
{
    if(!GEditor||GEditor->PlayWorld)return false;auto* W=GEditor->GetEditorWorldContext().World();if(!W||W->GetOutermost()->GetName()!=Map)return false;
    auto* R=FindRig(W);if(!R)return false;const bool Previous=R->bEnableCombatEncounter;const int32 OldSeed=R->Seed;
    R->bEnableCombatEncounter=true;R->Seed=(Scenario==TEXT("FirstHitSeed1")||Scenario==TEXT("ReloadPartialFullSeed1"))?1:0;
    auto Proof=MakeShared<FReloadLifetimeObservation>();
    auto& F=FAutomationTestFramework::Get();F.EnqueueLatentCommand(MakeShared<FStartPIECommand>(false));
    F.EnqueueLatentCommand(MakeShared<FExercise>(Test,Scenario,Proof));F.EnqueueLatentCommand(MakeShared<FEndPlayMapCommand>());
    F.EnqueueLatentCommand(MakeShared<FAfterPIE>(Test,R,Previous,OldSeed,Scenario,Proof));return true;
}
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FG1BSingleRoomLoop,"ProjectVerification.G1B.Stage2.SingleRoomLoop",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FG1BSingleRoomLoop::RunTest(const FString&){return CombatSliceEncounterVerification::Queue(this,TEXT("SingleRoomLoop"));}
IMPLEMENT_COMPLEX_AUTOMATION_TEST(FG1BFullRegression,"ProjectVerification.G1B.Stage3",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
void FG1BFullRegression::GetTests(TArray<FString>& Names,TArray<FString>& Commands)const
{Names={TEXT("FirstHitSeed1"),TEXT("PlayerDeath"),TEXT("SourceClear"),TEXT("SeedChange"),TEXT("EnemyDestroy"),TEXT("RegistrationFailure"),TEXT("IllegalStaticCollision"),TEXT("RecoveredSourceClear")};Commands=Names;}
bool FG1BFullRegression::RunTest(const FString& Scenario){return CombatSliceEncounterVerification::Queue(this,Scenario);}
IMPLEMENT_COMPLEX_AUTOMATION_TEST(FG1BLockSafety,"ProjectVerification.G1B.FIX01",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
void FG1BLockSafety::GetTests(TArray<FString>& Names,TArray<FString>& Commands)const
{Names={TEXT("DoorOccupied"),TEXT("PartialRollback"),TEXT("ReopenedBeforeShot"),TEXT("PreparationDeath"),TEXT("PreparationClear")};Commands=Names;}
bool FG1BLockSafety::RunTest(const FString& Scenario){return CombatSliceEncounterVerification::Queue(this,Scenario);}
#endif
