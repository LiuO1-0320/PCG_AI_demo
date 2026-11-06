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
#include "Misc/AutomationTest.h"
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Editor.h"
#include "Tests/AutomationEditorCommon.h"
#include "Engine/LocalPlayer.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "EnhancedInputSubsystems.h"
#include "InputActionValue.h"
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
/** Drives the actual possessed player through Enhanced Input; never writes actor transforms. */
struct FWalk
{
    TArray<FVector> Points;FVector Goal=FVector::ZeroVector,LastProgress=FVector::ZeroVector;
    int32 Index=0;double Started=0,ProgressAt=0,NextSample=0;bool bActive=false;
    bool Begin(ARogueliteSlicePlayer* FPS,const FVector& Requested,FString& Rows,FString& Why,const FRogueliteSeedRoomRecord* Constrain=nullptr)
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
        Points=Path->PathPoints;Index=1;Goal=Requested;Started=ProgressAt=FPlatformTime::Seconds();NextSample=Started;
        LastProgress=FPS->GetActorLocation();bActive=true;
        Rows+=FString::Printf(TEXT("WalkBegin ActualPlayer=%s From=%s Goal=%s FullPathPoints=%d Indoor=%d\n"),*FPS->GetPathName(),*LastProgress.ToString(),*Goal.ToString(),Points.Num(),Constrain!=nullptr);return true;
    }
    // -1 failure, 0 in progress, 1 physically arrived.
    int32 Tick(ARogueliteSlicePlayer* FPS,FString& Rows,FString& Why)
    {
        if(!bActive||!FPS||!FPS->HasLiveBinding()){Why=TEXT("Actual player walk identity expired");return -1;}
        const double Now=FPlatformTime::Seconds();const FVector Position=FPS->GetActorLocation();
        if(FVector::Dist2D(Position,Goal)<=42)
        {MoveInput(FPS,FVector2D::ZeroVector);bActive=false;Rows+=TEXT("WalkArrived=")+Position.ToString()+TEXT("\n");return 1;}
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
    bool Check(bool Good,const FString& What)
    {Rows+=FString::Printf(TEXT("Check=%s\tPass=%d\n"),*What,Good);if(!Good)Test->AddError(What);return Good;}
    bool Finish(bool Good,const FString& Why)
    {
        if(!Good)Test->AddError(Why);
        if(auto* R=Rig.Get())
        {
            if(auto* FPS=R->GetSlicePlayer())
            {
                MoveInput(FPS,FVector2D::ZeroVector);FireInput(FPS,false);
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
        Rows+=FString::Printf(TEXT("Closed Connection=%s State=%d BaseReady=%d Fwd=%d Rev=%d StaticFullPath=%d ForwardRay=%d ReverseRay=%d\n"),*Id.ToString(),int32(Q.State),Q.bBaseNavigationReady,Q.bForwardCapsule,Q.bReverseCapsule,Q.bFullPath,ForwardRay,ReverseRay);
        return Gate&&Gate->IsClosed()&&Gate->GetBlocker()->GetCollisionEnabled()!=ECollisionEnabled::NoCollision&&ForwardRay&&ReverseRay
            &&Q.State==ERogueliteGameplayPassage::GameplayBlocked&&!Q.bForwardCapsule&&!Q.bReverseCapsule&&Q.bStructuralCurrent&&Q.bBaseNavigationReady;
    }
    bool FindFiringPosition(ARogueliteCombatSliceRig* R,ARogueliteSlicePlayer* FPS,ARogueliteSliceEnemy* Enemy,FVector& Out)
    {
        const auto* Room=R->Batch.Rooms.FindByPredicate([&](const auto& X){return X.NodeId==R->Encounter->GetTargetNodeId();});if(!Room)return false;
        auto* W=R->GetWorld();FCollisionQueryParams Q(SCENE_QUERY_STAT(G1B_FiringPosition),false,FPS);
        const auto CanHit=[&](const FVector& Center){FHitResult Hit;return W->LineTraceSingleByChannel(Hit,Center+FVector(0,0,64),Enemy->GetActorLocation(),ECC_Visibility,Q)&&Hit.GetActor()==Enemy;};
        if(CanHit(FPS->GetActorLocation())){Out=FPS->GetActorLocation();return true;}
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
            if(Inside){Out=Ground.ImpactPoint+FVector(0,0,10);return true;}
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
public:
    FExercise(FAutomationTestBase* T,FString S):Test(T),Scenario(MoveTemp(S)){}
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
            return Finish(Good,TEXT("Controlled cancellation path verified"));
        }
        if(Step==90){if(Now-Action>5)return Finish(false,TEXT("Expected cancellation/owned cleanup did not occur"));return false;}
        if(Scenario==TEXT("RegistrationFailure"))return false;
        if(!R->SliceFailure.IsEmpty())return Finish(false,TEXT("Rig refused gameplay: ")+R->SliceFailure);
        if(!R->bPlayerBridgeReady)return false;auto* FPS=R->GetSlicePlayer();if(!FPS||!FPS->HasLiveBinding())return Finish(false,TEXT("Actual local player binding lost"));
        auto* PC=Cast<ARogueliteSlicePlayerController>(FPS->GetController());if(!PC||!Input(FPS))return Finish(false,TEXT("Actual local EnhancedInput player unavailable"));
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
            PC->SetControlRotation(FRotator(85,0,0));Action=Now;Step=1;return false;
        }
        if(Step==1)
        {if(Now-Action<.15)return false;AcceptedBefore=FPS->ShotAcceptedCount;FireInput(FPS,true);Action=Now;Step=2;return false;}
        if(Step==2)
        {
            FireInput(FPS,false);if(Now-Action<.1)return false;
            if(!Check(FPS->ShotAcceptedCount==AcceptedBefore+1&&E->GetState()==ERogueliteSliceEncounterState::AwaitingEntry&&AllDormant(E),TEXT("Actual safehouse accepted shot cannot activate another room")))return Finish(false,TEXT("Safehouse activation scope failed"));
            SafehouseShot=FPS->GetLastShot();FString Why;if(!Walker.Begin(FPS,EntryLanding,Rows,Why))return Finish(false,Why);Step=3;return false;
        }
        if(Step==3)
        {
            if(bReleaseCorridorFire){FireInput(FPS,false);bReleaseCorridorFire=false;}
            if(!bCorridorShot)
            {
                FRogueliteRoomConnection C;if(ReadConnection(R,ExpectedEntry,C)&&C.Corridor)
                {
                    const FVector L=C.Corridor->GetActorTransform().InverseTransformPosition(FPS->GetActorLocation());
                    if(FMath::Abs(L.X)<C.Corridor->CorridorLength*.22&&FMath::Abs(L.Y)<C.Corridor->CorridorWidth*.5-35)
                    {CorridorAcceptedBefore=FPS->ShotAcceptedCount;FireInput(FPS,true);bReleaseCorridorFire=true;bCorridorShot=true;Rows+=TEXT("Actual corridor fire injection on committed connection=")+ExpectedEntry.ToString()+TEXT("\n");}
                }
            }
            FString Why;const int32 Done=Walker.Tick(FPS,Rows,Why);if(Done<0)return Finish(false,Why);if(!Done)return false;
            MoveInput(FPS,FVector2D::ZeroVector);Action=Now;Step=4;return false;
        }
        if(Step==4)
        {
            if(Now-Action<.3)return false;
            if(!Check(E->GetState()==ERogueliteSliceEncounterState::Preparation&&E->GetCurrentRoomNodeId()==E->GetTargetNodeId()&&E->GetEntryConnectionId()==ExpectedEntry&&bCorridorShot&&FPS->ShotAcceptedCount==CorridorAcceptedBefore+1&&E->GetActivationCount()==0&&AllDormant(E),TEXT("Real player traversed actual corridor, fired an accepted nonactivating corridor shot and entered through correct committed socket before Preparation")))return Finish(false,TEXT("Entry provenance or Preparation failed"));
            if(!Check(CheckClosed(R,E,FPS,ExpectedEntry),TEXT("Entry gate really blocks both Pawn sweeps while base structural/navigation Ready remain true")))return Finish(false,TEXT("Entry collision failed"));
            for(const auto& Id:E->GetIncidentConnectionIds())if(Id!=ExpectedEntry&&!Check(E->GetGate(Id)&&!E->GetGate(Id)->IsClosed(),TEXT("Other actual incident gates remain open during Preparation")))return Finish(false,TEXT("Premature additional gate closure"));
            Action=Now;Step=5;return false;
        }
        if(Step==5)
        {
            if(Now-Action<1.3)return false;
            if(!Check(E->GetState()==ERogueliteSliceEncounterState::Preparation&&AllDormant(E)&&R->GetTraversalSnapshot().bCurrent,TEXT("Preparation waits across multiple Watch cycles without automatic combat")))return Finish(false,TEXT("Preparation auto-triggered or structure invalidated"));
            FPS->OnShotAccepted.Broadcast(SafehouseShot);
            if(!Check(E->GetActivationCount()==0,TEXT("Replaying a real safehouse ShotId after entering cannot activate the room")))return Finish(false,TEXT("Replay activated combat"));
            FPS->Ammo=0;AcceptedBefore=FPS->ShotAcceptedCount;RejectedBefore=FPS->ShotRejectedCount;FireInput(FPS,true);Action=Now;Step=6;return false;
        }
        if(Step==6)
        {
            FireInput(FPS,false);if(Now-Action<.1)return false;
            if(!Check(FPS->ShotAcceptedCount==AcceptedBefore&&FPS->ShotRejectedCount>RejectedBefore&&E->GetState()==ERogueliteSliceEncounterState::Preparation&&AllDormant(E),TEXT("Real empty-ammo input emits no ShotAccepted and cannot activate Preparation")))return Finish(false,TEXT("Rejected fire activated encounter"));
            FPS->Ammo=30;Rows+=TEXT("Validation-only ammunition restored after explicit empty-ammo fault\n");
            // Exercise another actual room during Preparation, then return through its still-open gate.
            FRogueliteRoomConnection C;const FGuid Exit=E->GetExitConnectionId();FRogueliteDoorSocket TargetDoor,NeighborDoor;
            if(Exit.IsValid()&&ReadConnection(R,Exit,C)&&DoorForNode(R,C,E->GetTargetNodeId(),TargetDoor,&OtherNode)&&OtherNode!=INDEX_NONE&&DoorForNode(R,C,OtherNode,NeighborDoor))
            {
                OtherLanding=NeighborDoor.WorldPosition-NeighborDoor.WorldOutwardDirection*250+FVector(0,0,10);
                TargetReturn=TargetDoor.WorldPosition-TargetDoor.WorldOutwardDirection*250+FVector(0,0,10);FString Why;
                if(!Walker.Begin(FPS,OtherLanding,Rows,Why))return Finish(false,Why);Step=7;return false;
            }
            return Finish(false,TEXT("Target has no actual non-entry connection for preparation room-scope validation"));
        }
        if(Step==7)
        {FString Why;const int32 Done=Walker.Tick(FPS,Rows,Why);if(Done<0)return Finish(false,Why);if(!Done)return false;PC->SetControlRotation(FRotator(85,0,0));Action=Now;Step=8;return false;}
        if(Step==8)
        {if(Now-Action<.15)return false;AcceptedBefore=FPS->ShotAcceptedCount;FireInput(FPS,true);Action=Now;Step=9;return false;}
        if(Step==9)
        {
            FireInput(FPS,false);if(Now-Action<.1)return false;
            if(!Check(FPS->ShotAcceptedCount==AcceptedBefore+1&&E->GetState()==ERogueliteSliceEncounterState::Preparation&&E->GetActivationCount()==0&&AllDormant(E),TEXT("Accepted fire from a different actual room cannot activate the target Preparation")))return Finish(false,TEXT("Other-room activation scope failed"));
            FString Why;if(!Walker.Begin(FPS,TargetReturn,Rows,Why))return Finish(false,Why);Step=10;return false;
        }
        if(Step==10)
        {FString Why;const int32 Done=Walker.Tick(FPS,Rows,Why);if(Done<0)return Finish(false,Why);if(!Done)return false;Action=Now;Step=11;return false;}
        if(Step==11)
        {
            if(Now-Action<.3)return false;
            if(Scenario==TEXT("FirstHitSeed1"))
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
            if(Scenario==TEXT("FirstHitSeed1"))
            {if(!Check(AimedEnemy.IsValid()&&AimedEnemy->Health->GetHealth()==HealthBefore-FPS->ShotDamage&&FPS->HitConfirmedCount==HitsBefore+1,TEXT("First real hit activates before actual damage; dormant rules do not swallow it")))return Finish(false,TEXT("First-hit activation/damage ordering failed"));}
            else if(!Check(!FPS->GetLastShot().bHit&&FPS->HitConfirmedCount==0,TEXT("Actual accepted miss still starts Combat")))return Finish(false,TEXT("Miss activation classification failed"));
            FPS->OnShotAccepted.Broadcast(FPS->GetLastShot());if(!Check(E->GetActivationCount()==1,TEXT("Duplicate actual accepted ShotId cannot activate twice")))return Finish(false,TEXT("Duplicate combat activation"));
            for(const auto& Id:E->GetIncidentConnectionIds())if(!Check(CheckClosed(R,E,FPS,Id),TEXT("Every actual incident connection has real closed Pawn collision in Combat")))return Finish(false,TEXT("Combat gate collision missing"));
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
        {if(Now-Action<.15)return false;FireInput(FPS,true);Action=LastFire=Now;Step=19;return false;}
        if(Step==19)
        {FireInput(FPS,false);if(Now-Action<.1)return false;Step=15;return false;}
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
    void Save() const
    {
        FString Dir;if(!FParse::Value(FCommandLine::Get(),TEXT("G1BEvidenceDir="),Dir))return;
        IFileManager::Get().MakeDirectory(*Dir,true);FString File=Dir/TEXT("encounter-stop-")+Scenario+TEXT(".txt");
        if(IFileManager::Get().FileExists(*File))File+=TEXT(".")+FDateTime::UtcNow().ToString(TEXT("%Y%m%d%H%M%S"))+TEXT(".rerun");
        if(!FFileHelper::SaveStringToFile(Rows,*File))Test->AddError(TEXT("Could not preserve post-PIE diagnostic: ")+File);
    }
public:
    FAfterPIE(FAutomationTestBase* T,ARogueliteCombatSliceRig* R,bool Mode,int32 OldSeed,FString Case):Test(T),EditorRig(R),Previous(Mode),Seed(OldSeed),Scenario(MoveTemp(Case)){}
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
    R->bEnableCombatEncounter=true;R->Seed=Scenario==TEXT("FirstHitSeed1")?1:0;
    auto& F=FAutomationTestFramework::Get();F.EnqueueLatentCommand(MakeShared<FStartPIECommand>(false));
    F.EnqueueLatentCommand(MakeShared<FExercise>(Test,Scenario));F.EnqueueLatentCommand(MakeShared<FEndPlayMapCommand>());
    F.EnqueueLatentCommand(MakeShared<FAfterPIE>(Test,R,Previous,OldSeed,Scenario));return true;
}
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FG1BSingleRoomLoop,"ProjectVerification.G1B.Stage2.SingleRoomLoop",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FG1BSingleRoomLoop::RunTest(const FString&){return CombatSliceEncounterVerification::Queue(this,TEXT("SingleRoomLoop"));}
IMPLEMENT_COMPLEX_AUTOMATION_TEST(FG1BFullRegression,"ProjectVerification.G1B.Stage3",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
void FG1BFullRegression::GetTests(TArray<FString>& Names,TArray<FString>& Commands)const
{Names={TEXT("FirstHitSeed1"),TEXT("PlayerDeath"),TEXT("SourceClear"),TEXT("SeedChange"),TEXT("EnemyDestroy"),TEXT("RegistrationFailure"),TEXT("IllegalStaticCollision"),TEXT("RecoveredSourceClear")};Commands=Names;}
bool FG1BFullRegression::RunTest(const FString& Scenario){return CombatSliceEncounterVerification::Queue(this,Scenario);}
#endif
