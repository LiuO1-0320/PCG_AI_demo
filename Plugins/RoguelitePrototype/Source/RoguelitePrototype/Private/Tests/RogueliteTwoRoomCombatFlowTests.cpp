#include "Tests/RogueliteCombatSliceRig.h"
#include "Gameplay/RogueliteCombatSlicePlayer.h"
#include "Gameplay/RogueliteCombatSliceActors.h"
#include "Gameplay/RogueliteCombatSliceEncounter.h"
#include "RogueliteParametricCorridor.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/BoxComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "NavigationSystem.h"
#include "NavigationPath.h"
#include "Misc/AutomationTest.h"
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Editor.h"
#include "Tests/AutomationEditorCommon.h"
#include "Engine/LocalPlayer.h"
#include "Engine/Engine.h"
#include "Engine/Level.h"
#include "EngineUtils.h"
#include "EnhancedInputSubsystems.h"
#include "InputActionValue.h"
#include "InputKeyEventArgs.h"
#include "UObject/ObjectKey.h"
#include "Misc/FileHelper.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "HAL/FileManager.h"
namespace TwoRoomFlowVerification
{
const TCHAR* Map=TEXT("/Game/Roguelite/Tests/P04G2/L_Roguelite_TwoCombatRooms_Dev");
ARogueliteCombatSliceRig* Find(UWorld* W)
{if(W)for(TActorIterator<ARogueliteCombatSliceRig>I(W);I;++I)if(!I->IsActorBeingDestroyed())return *I;return nullptr;}
void Save(const FString& Name,const FString& Rows)
{
    FString Dir;if(!FParse::Value(FCommandLine::Get(),TEXT("G2EvidenceDir="),Dir))return;
    IFileManager::Get().MakeDirectory(*Dir,true);FString File=Dir/Name+TEXT(".txt");
    if(IFileManager::Get().FileExists(*File))File+=TEXT(".")+FDateTime::UtcNow().ToString(TEXT("%Y%m%d%H%M%S"))+TEXT(".rerun");
    FFileHelper::SaveStringToFile(Rows,*File);
}
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


const FRogueliteSeedRoomRecord* Room(const ARogueliteCombatSliceRig* R,int32 Node)
{return R?R->Batch.Rooms.FindByPredicate([&](const auto& X){return X.NodeId==Node;}):nullptr;}
bool Connection(ARogueliteCombatSliceRig* R,FGuid Id,FRogueliteRoomConnection& Out)
{
    if(!R||!R->Batch.Registry)return false;
    for(const auto& C:R->Batch.Registry->GetConnectionRecords())if(C.Id==Id&&C.bActive&&C.bGeometryValid&&C.ValidationStatus==ERogueliteConnectionValidation::Passed){Out=C;return true;}return false;
}
bool Door(ARogueliteCombatSliceRig* R,int32 Node,FGuid Id,FRogueliteDoorSocket& Out)
{
    auto* Rec=Room(R,Node);FRogueliteRoomConnection C;if(!Rec||!Connection(R,Id,C))return false;
    if(C.RoomA==Rec->Room)return C.RoomA->TryGetDoorSocket(C.SocketA,Out);
    return C.RoomB==Rec->Room&&C.RoomB->TryGetDoorSocket(C.SocketB,Out);
}
bool Route(ARogueliteCombatSliceRig* R,int32 From,int32 To,TArray<int32>& Nodes,TArray<FGuid>& Edges,int32 Exclude=INDEX_NONE)
{
    Nodes.Empty();Edges.Empty();const auto S=R->GetTraversalSnapshot();TMap<int32,int32> Parent;TMap<int32,FGuid> Via;TArray<int32> Q{From};Parent.Add(From,From);
    for(int32 I=0;I<Q.Num()&&!Parent.Contains(To);++I)for(const auto& E:S.Edges)
    {int32 Next=E.NodeA==Q[I]?E.NodeB:(E.NodeB==Q[I]?E.NodeA:INDEX_NONE);if(Next!=INDEX_NONE&&Next!=Exclude&&!Parent.Contains(Next)){Parent.Add(Next,Q[I]);Via.Add(Next,E.ConnectionId);Q.Add(Next);}}
    if(!Parent.Contains(To))return false;int32 N=To;Nodes.Add(N);
    while(N!=From){Edges.Insert(Via.FindChecked(N),0);N=Parent.FindChecked(N);Nodes.Insert(N,0);}return true;
}
bool Dormant(URogueliteSliceEncounter* E)
{if(!E||E->GetEnemies().Num()!=2||E->GetRemainingEnemyCount()!=2)return false;for(const auto& W:E->GetEnemies())if(!W.IsValid()||!W->IsDormant()||W->AttackCount!=0||W->Health->GetHealth()!=60)return false;return true;}
float EnemyHealth(URogueliteSliceEncounter* E)
{float Sum=0;for(const auto& W:E->GetEnemies())if(W.IsValid())Sum+=W->Health->GetHealth();return Sum;}
struct FLifetime
{
    bool Observed=false,Reloading=true;int32 Ammo=-1,Completed=-1,Cancelled=-1;float Remaining=-1;FDelegateHandle Cleanup;
};
void Snapshot(ARogueliteSlicePlayer* P,const TSharedPtr<FLifetime>& O)
{if(!P)return;O->Observed=true;O->Ammo=P->Ammo;O->Completed=P->ReloadCompletedCount;O->Cancelled=P->ReloadCancelledCount;O->Reloading=P->IsReloading();O->Remaining=P->GetReloadRemainingSeconds();}
class FFlow:public IAutomationLatentCommand
{
    FAutomationTestBase* Test;FString Case,Rows;int32 Step=0;double Started=0,Action=0,LastFire=0,ReloadAt=0;
    TWeakObjectPtr<ARogueliteCombatSliceRig> Rig;TWeakObjectPtr<ARogueliteSlicePlayer> Player;
    TWeakObjectPtr<ARogueliteSlicePlayerController> Controller;TWeakObjectPtr<URogueliteSliceEncounter> First,Second,Fighting;
    TWeakObjectPtr<ARogueliteSliceEnemy> Aimed,DormantTarget;TWeakObjectPtr<URogueliteSliceHealth> OldDeadHealth;
    TWeakObjectPtr<AActor> Manual;TSharedPtr<FLifetime> Lifetime;FDelegateHandle Destroyed;
    FGuid Batch,EntryA,EntryB,Shared,RecoveryId,OldHit;TSet<FGuid> OriginalConnections;TArray<int32> ToB,BackA;TArray<FGuid> ToBEdges,BackEdges;
    int32 PawnId=0,PCId=0,BeforeShots=0,BeforeHits=0,BeforeRejected=0,Amp=0,RouteIndex=0,BeforeReloadStarts=0,BeforeReloadCompletes=0,BeforeReloadCancels=0,Misses=0;
    float BeforeTarget=0,AfterAHealth=0;FVector Waiting=FVector::ZeroVector,OutsideB=FVector::ZeroVector;
    bool Armed=false,CorridorShot=false,ReleaseCorridor=false,WalkingAim=false,ReloadSeenInPreparation=false,NegativePrepared=false;
    FWalk Walker;
    bool HitCase()const{return Case==TEXT("Seed1FirstHit");}
    bool LifeCase()const{return Case.StartsWith(TEXT("Life"));}
    bool Check(bool Good,const FString& Why)
    {Rows+=FString::Printf(TEXT("Check=%s Pass=%d\n"),*Why,Good);if(!Good)Test->AddError(Why);return Good;}
    bool Finish(bool Good,const FString& Why)
    {
        if(!Good)Test->AddError(Why);
        if(auto* P=Player.Get()){MoveInput(P,FVector2D::ZeroVector);FireInput(P,false);ReloadKey(P,false);}
        if(Case!=TEXT("LifeStopPIE"))if(auto* R=Rig.Get())R->GetWorld()->RemoveOnActorDestroyedHandler(Destroyed);
        if(auto* R=Rig.Get())Rows+=TEXT("FinalHUD=\n")+R->SliceStatus+TEXT("\nRecovery=\n")+FRogueliteDynamicGameplayBridge::RecoveryEvidence(R)+TEXT("\n");
        Rows+=FString::Printf(TEXT("Passed=%d\nReason=%s\n"),Good,*Why);Save(TEXT("flow-")+Case,Rows);return true;
    }
    void StateSnapshot(const TCHAR* Label,ARogueliteCombatSliceRig* R,ARogueliteSlicePlayer* P)
    {
        Rows+=FString::Printf(TEXT("State %s PawnId=%d PCId=%d Batch=%s Node=%d HP=%.2f Ammo=%d Accepted=%d Hits=%d Reloading=%d Starts=%d Completes=%d Cancels=%d Remaining=%.3f\n"),Label,P->GetUniqueID(),P->GetController()?P->GetController()->GetUniqueID():0,*P->GetBoundBatch().ToString(),R->GetCurrentPlayerRoomNode(),P->HealthComponent->GetHealth(),P->Ammo,P->ShotAcceptedCount,P->HitConfirmedCount,P->IsReloading(),P->ReloadStartedCount,P->ReloadCompletedCount,P->ReloadCancelledCount,P->GetReloadRemainingSeconds());
    }
    bool Locks(ARogueliteCombatSliceRig* R,URogueliteSliceEncounter* E,ARogueliteSlicePlayer* P,bool Closed)
    {
        bool Good=true;for(auto Id:E->GetIncidentConnectionIds())
        {
            auto* G=E->GetGate(Id);if(!G)return false;const auto Q=FRogueliteDynamicGameplayBridge::QueryConnection(R,Id,P);
            const FVector C=G->GetBlocker()->GetComponentLocation(),N=G->GetBlocker()->GetForwardVector();FCollisionQueryParams Query(SCENE_QUERY_STAT(G2_GateRay),false,P);FHitResult A,B;
            const bool F=R->GetWorld()->LineTraceSingleByChannel(A,C-N*100,C+N*100,ECC_Visibility,Query)&&A.GetComponent()==G->GetBlocker();
            const bool Rev=R->GetWorld()->LineTraceSingleByChannel(B,C+N*100,C-N*100,ECC_Visibility,Query)&&B.GetComponent()==G->GetBlocker();
            const bool Valid=G->GetRoomNodeId()==E->GetTargetNodeId()&&G->GetOwner()==R&&G->GetBoundBatch()==Batch&&G->IsClosed()==Closed&&F==Closed&&Rev==Closed;
            Good&=Valid&&(!Closed||(Q.State==ERogueliteGameplayPassage::GameplayBlocked&&!Q.bForwardCapsule&&!Q.bReverseCapsule));
            Rows+=FString::Printf(TEXT("GateCheck Node=%d Connection=%s Actor=%s Closed=%d RayF=%d RayR=%d Passage=%d CapsuleF=%d CapsuleR=%d BaseReady=%d Pass=%d\n"),E->GetTargetNodeId(),*Id.ToString(),*G->GetName(),G->IsClosed(),F,Rev,int32(Q.State),Q.bForwardCapsule,Q.bReverseCapsule,Q.bBaseNavigationReady,Valid);
        }
        return Good;
    }
    bool BeginWalk(ARogueliteSlicePlayer* P,FVector Goal,ARogueliteCombatSliceRig* R,int32 Indoor=INDEX_NONE,float Tolerance=35)
    {FString Why;if(!Walker.Begin(P,Goal,Rows,Why,Room(R,Indoor),Tolerance)){Rows+=Why;return false;}return true;}
    bool FiringPosition(ARogueliteCombatSliceRig* R,URogueliteSliceEncounter* E,ARogueliteSlicePlayer* P,ARogueliteSliceEnemy* Target,FVector& Out,bool RequireMovement=false)
    {
        const auto* Rec=Room(R,E->GetTargetNodeId());if(!Rec||!Target)return false;auto* W=R->GetWorld();FCollisionQueryParams Q(SCENE_QUERY_STAT(G2_FiringPoint),false,P);
        const FVector CameraOffset=P->FirstPersonCamera->GetComponentLocation()-P->GetActorLocation();
        auto CanHit=[&](FVector Center)
        {
            const FVector Origin=Center+CameraOffset,Goal=Target->GetActorLocation();FHitResult Short,Full;
            W->LineTraceSingleByChannel(Short,Origin,Goal,ECC_Visibility,Q);
            W->LineTraceSingleByChannel(Full,Origin,Origin+(Goal-Origin).GetSafeNormal()*P->WeaponRangeCm,ECC_Visibility,Q);
            const bool Good=Full.GetActor()==Target;
            Rows+=FString::Printf(TEXT("FiringPoint Room=%d RequireMovement=%d ActualPlayer=%s Candidate=%s Origin=%s Target=%s TargetPosition=%s ShortHit=%s ShortComponent=%s FullHit=%s FullComponent=%s FullImpact=%s Pass=%d\n"),
                E->GetTargetNodeId(),RequireMovement,*P->GetActorLocation().ToString(),*Center.ToString(),*Origin.ToString(),*Target->GetName(),*Goal.ToString(),
                *GetNameSafe(Short.GetActor()),*GetNameSafe(Short.GetComponent()),*GetNameSafe(Full.GetActor()),*GetNameSafe(Full.GetComponent()),*Full.ImpactPoint.ToString(),Good);
            return Good;
        };
        // Candidate visibility must use the real weapon segment, not a shorter ray ending inside the enemy.
        // After a real aimed miss, walk to another legal point instead of firing the same blocked ray three times.
        if(!RequireMovement&&CanHit(P->GetActorLocation())){Out=P->GetActorLocation();return true;}
        for(float X:TArray<float>{-.25f,0,.25f})for(float Y:TArray<float>{-.25f,0,.25f})
        {
            FNavLocation N;auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(W);if(!Nav||!Nav->ProjectPointToNavigation(Rec->PlacedTransform.TransformPosition(FVector(X*Rec->Dimensions.X,Y*Rec->Dimensions.Y,10)),N,FVector(80,80,100)))continue;
            FHitResult Ground;if(!W->LineTraceSingleByChannel(Ground,N.Location+FVector(0,0,150),N.Location-FVector(0,0,300),ECC_Visibility,Q)||Ground.GetActor()!=Rec->Room.Get())continue;
            const FVector Center=Ground.ImpactPoint+FVector(0,0,90);
            if(RequireMovement&&FVector::Dist2D(Center,P->GetActorLocation())<80)continue;
            if(!FullRoom(*Rec,Center)||W->OverlapBlockingTestByChannel(Center,FQuat::Identity,ECC_Pawn,FCollisionShape::MakeCapsule(34,88),Q)||!CanHit(Center))continue;
            auto* Path=UNavigationSystemV1::FindPathToLocationSynchronously(W,P->GetActorLocation(),Ground.ImpactPoint+FVector(0,0,10),P);if(!Path||!Path->IsValid()||Path->IsPartial())continue;
            bool Inside=true;for(auto V:Path->PathPoints){const FVector L=Rec->PlacedTransform.InverseTransformPosition(V);Inside&=FMath::Abs(L.X)<Rec->Dimensions.X*.5-30&&FMath::Abs(L.Y)<Rec->Dimensions.Y*.5-30;}if(Inside){Out=Ground.ImpactPoint+FVector(0,0,10);return true;}
        }return false;
    }
    bool RecoveryWaiting(ARogueliteCombatSliceRig* R,URogueliteSliceEncounter* E,ARogueliteSlicePlayer* P,FGuid Id,FVector& Out)
    {
        FRogueliteRoomConnection C;FRogueliteDoorSocket A,B;if(!Connection(R,Id,C)||!C.RoomA->TryGetDoorSocket(C.SocketA,A)||!C.RoomB->TryGetDoorSocket(C.SocketB,B))return false;
        const FVector Start=A.WorldPosition-A.WorldOutwardDirection*300,Goal=B.WorldPosition-B.WorldOutwardDirection*300;
        const auto* Rec=Room(R,E->GetTargetNodeId());if(!Rec)return false;FCollisionQueryParams Q(SCENE_QUERY_STAT(G2_Waiting),false,P);
        TArray<FVector> Candidates;for(float X:TArray<float>{-.35f,-.225f,0,.225f,.35f})for(float Y:TArray<float>{-.35f,-.225f,0,.225f,.35f})Candidates.Add(Rec->PlacedTransform.TransformPosition(FVector(X*Rec->Dimensions.X,Y*Rec->Dimensions.Y,10)));
        Candidates.Sort([&](auto APos,auto BPos){return FVector::DistSquared2D(APos,P->GetActorLocation())<FVector::DistSquared2D(BPos,P->GetActorLocation());});
        for(auto V:Candidates)
        {
            FNavLocation N;auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(R->GetWorld());if(!Nav||!Nav->ProjectPointToNavigation(V,N,FVector(80,80,100)))continue;
            FHitResult Ground;if(!R->GetWorld()->LineTraceSingleByChannel(Ground,N.Location+FVector(0,0,150),N.Location-FVector(0,0,300),ECC_Visibility,Q)||Ground.GetActor()!=Rec->Room.Get())continue;
            const FVector Center=Ground.ImpactPoint+FVector(0,0,90);if(!FullRoom(*Rec,Center)||FMath::PointDistToSegment(Ground.ImpactPoint,Start,Goal)<240||R->GetWorld()->OverlapBlockingTestByChannel(Center,FQuat::Identity,ECC_Pawn,FCollisionShape::MakeCapsule(34,88),Q))continue;
            Out=Ground.ImpactPoint+FVector(0,0,10);return true;
        }return false;
    }
    bool OutsideShot(ARogueliteCombatSliceRig* R,URogueliteSliceEncounter* E,ARogueliteSlicePlayer* P,FVector& Out)
    {
        FRogueliteDoorSocket D;if(!Door(R,E->GetTargetNodeId(),EntryB,D))return false;const FVector T=FVector::CrossProduct(FVector::UpVector,D.WorldOutwardDirection);auto* W=R->GetWorld();
        FCollisionQueryParams Q(SCENE_QUERY_STAT(G2_OutsideDormant),false,P);
        for(float Depth:TArray<float>{100,150,200})for(float Offset:TArray<float>{0,100,-100})
        {
            const FVector Guess=D.WorldPosition+D.WorldOutwardDirection*Depth+T*Offset;FNavLocation N;auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(W);
            if(!Nav||!Nav->ProjectPointToNavigation(Guess+FVector(0,0,10),N,FVector(40,40,100)))continue;
            FHitResult Ground;if(!W->LineTraceSingleByChannel(Ground,N.Location+FVector(0,0,150),N.Location-FVector(0,0,300),ECC_Visibility,Q))continue;
            FVector Center=Ground.ImpactPoint+FVector(0,0,90);if(FVector::DotProduct(Center-D.WorldPosition,D.WorldOutwardDirection)<80||W->OverlapBlockingTestByChannel(Center,FQuat::Identity,ECC_Pawn,FCollisionShape::MakeCapsule(34,88),Q))continue;
            for(const auto& Weak:E->GetEnemies())if(Weak.IsValid())
            {FHitResult Hit;if(W->LineTraceSingleByChannel(Hit,Center+FVector(0,0,64),Weak->GetActorLocation(),ECC_Visibility,Q)&&Hit.GetActor()==Weak.Get()){DormantTarget=Weak;Out=Ground.ImpactPoint+FVector(0,0,10);return true;}}
        }return false;
    }
    bool SetAim(ARogueliteSlicePlayer* P,ARogueliteSliceEnemy* Target)
    {if(!Target)return false;P->GetController()->SetControlRotation((Target->GetActorLocation()-P->FirstPersonCamera->GetComponentLocation()).Rotation());return true;}
    void ObserveLifetime(ARogueliteSlicePlayer* P)
    {
        const FObjectKey Key(P);auto Proof=Lifetime;Destroyed=P->GetWorld()->AddOnActorDestroyedHandler(FOnActorDestroyed::FDelegate::CreateLambda([Key,Proof](AActor* A){if(FObjectKey(A)==Key)Snapshot(Cast<ARogueliteSlicePlayer>(A),Proof);}));
        if(Case==TEXT("LifeStopPIE")){const FObjectKey WorldKey(P->GetWorld());Proof->Cleanup=FWorldDelegates::OnWorldCleanup.AddLambda([Key,WorldKey,Proof](UWorld* W,bool,bool){if(FObjectKey(W)!=WorldKey||!W->PersistentLevel)return;for(AActor* A:W->PersistentLevel->Actors)if(A&&FObjectKey(A)==Key){Snapshot(Cast<ARogueliteSlicePlayer>(A),Proof);break;}});}
    }
public:
    FFlow(FAutomationTestBase* T,FString C,TSharedPtr<FLifetime> O):Test(T),Case(MoveTemp(C)),Lifetime(MoveTemp(O)){}
    bool Update()override
    {
        auto* W=GEditor?GEditor->PlayWorld.Get():nullptr;if(!W)return false;const double Now=FPlatformTime::Seconds();if(!Started)Started=Now;
        if(!Rig.IsValid())Rig=Find(W);auto* R=Rig.Get();if(!R)return Finish(false,TEXT("G2 Rig missing in actual GameWorld"));
        if(!Manual.IsValid())for(TActorIterator<AActor>I(W);I;++I)if(I->GetActorLabel()==TEXT("G2_ProtectedManualMarker")){Manual=*I;break;}
        if(Case==TEXT("SpawnFailure")&&!Armed){R->SecondEncounter->SetSpawnFailureForValidation(true);Armed=true;}
        if(Now-Started>260)return Finish(false,TEXT("G2 bounded test timeout: ")+R->SliceStatus);
        if(Step==91||Case==TEXT("SpawnFailure"))
        {
            if(Case==TEXT("SpawnFailure")&&R->SliceFailure.IsEmpty())return false;
            if(Step==91&&Now-Action<2.1)return false;int32 Enemies=0,Gates=0,Probes=0,Reservations=0;
            for(TActorIterator<AActor>I(W);I;++I)if(!I->IsActorBeingDestroyed()){Enemies+=I->IsA<ARogueliteSliceEnemy>();Gates+=I->IsA<ARogueliteSliceGate>();Probes+=I->IsA<ARogueliteSliceRecoveryProbe>();}
            for(TActorIterator<ARogueliteRoomAssembly>I(W);I;++I)if(!I->IsActorBeingDestroyed())for(const auto& C:I->GetConnectionRecords())Reservations+=C.bActive;
            bool Good=Check(Enemies==0&&Gates==0&&Probes==0&&Reservations==0&&R->Batch.OwnedActors.IsEmpty()&&FRogueliteDynamicGameplayBridge::ActiveComponentCount(R)==0&&Manual.IsValid(),TEXT("Both encounters and owned recovery/layout/reservations clean in the still-active World; manual marker retained"));
            Good&=Check(R->Encounter->GetState()==ERogueliteSliceEncounterState::Cancelled&&R->SecondEncounter->GetState()==ERogueliteSliceEncounterState::Cancelled&&R->Encounter->GetClearCount()==0&&R->SecondEncounter->GetClearCount()==0,TEXT("Two-room lifecycle cancellation is never a false clear/victory"));
            if(Case!=TEXT("SpawnFailure"))Good&=Check(Lifetime->Observed&&!Lifetime->Reloading&&Lifetime->Ammo==29&&Lifetime->Completed==0&&Lifetime->Cancelled==1&&Lifetime->Remaining==0,TEXT("Old player reload cancelled once before legitimate destruction, no old refill after its deadline"));
            Rows+=FString::Printf(TEXT("Cleanup Enemies=%d Gates=%d Probes=%d Reservations=%d Leases=%d ReloadObserved=%d Ammo=%d Completed=%d Cancelled=%d Remaining=%.3f Reason=%s\n"),Enemies,Gates,Probes,Reservations,FRogueliteDynamicGameplayBridge::ActiveComponentCount(R),Lifetime->Observed,Lifetime->Ammo,Lifetime->Completed,Lifetime->Cancelled,Lifetime->Remaining,*R->SliceFailure);
            return Finish(Good,TEXT("Controlled G2 ownership/lifecycle cleanup verified"));
        }
        if(!R->SliceFailure.IsEmpty())return Finish(false,R->SliceFailure);if(!R->bPlayerBridgeReady)return false;
        auto* P=R->GetSlicePlayer();auto* PC=P?Cast<ARogueliteSlicePlayerController>(P->GetController()):nullptr;
        if(!P||!PC||!P->HasLiveBinding()||!R->GetTraversalSnapshot().bCurrent)return Finish(false,TEXT("Actual persistent local player/current structural binding lost"));
        if(Step==0)
        {
            Player=P;Controller=PC;PawnId=P->GetUniqueID();PCId=PC->GetUniqueID();Batch=P->GetBoundBatch();auto Es=R->GetRoomEncounters();
            bool Good=Check(R->bEnableTwoCombatRooms&&Es.Num()==2&&R->Batch.Rooms.Num()==4&&R->Batch.Edges.Num()==3&&R->CommittedLoops.Edges.IsEmpty()&&R->IsDungeonReady(),TEXT("Original strict-Ready four-room Floor1 and two explicit encounters"));
            for(auto* E:Es)Good&=Check(E&&E->GetState()==ERogueliteSliceEncounterState::AwaitingEntry&&Dormant(E),TEXT("Both complete independent two-enemy rooms prepared before player gameplay"));
            if(!Good)return Finish(false,TEXT("Initial two-room readiness invalid"));
            TArray<URogueliteSliceEncounter*> Reachable;
            for(auto* E:Es){auto* Other=Es[0]==E?Es[1]:Es[0];TArray<int32>N;TArray<FGuid>C;if(Route(R,R->PlayerStartNode,E->GetTargetNodeId(),N,C,Other->GetTargetNodeId()))Reachable.Add(E);}
            if(Reachable.IsEmpty())return Finish(false,TEXT("No natural first combat route without crossing another uncleared combat room"));
            Reachable.Sort([](const auto& A,const auto& B){return A.GetTargetNodeId()<B.GetTargetNodeId();});First=HitCase()?Reachable.Last():Reachable[0];Second=Es[0]==First.Get()?Es[1]:Es[0];
            TArray<int32> StartRoute;TArray<FGuid> StartEdges;
            if(!Route(R,R->PlayerStartNode,First->GetTargetNodeId(),StartRoute,StartEdges,Second->GetTargetNodeId())||!Route(R,First->GetTargetNodeId(),Second->GetTargetNodeId(),ToB,ToBEdges)||!Route(R,Second->GetTargetNodeId(),First->GetTargetNodeId(),BackA,BackEdges))return Finish(false,TEXT("Actual graph route unavailable"));
            EntryA=StartEdges.Last();EntryB=ToBEdges.Last();
            Rows+=FString::Printf(TEXT("Seed=%d Batch=%s Graph=%s Layout=%s Start=%d First=%d Second=%d FirstChoices=%d NaturalReverseAvailable=%d PawnId=%d PCId=%d\n"),R->Seed,*Batch.ToString(),*R->Batch.GraphDigest,*R->Batch.LayoutDigest,R->PlayerStartNode,First->GetTargetNodeId(),Second->GetTargetNodeId(),Reachable.Num(),Reachable.Num()>1,PawnId,PCId);
            for(int32 N:StartRoute)Rows+=FString::Printf(TEXT("StartRoute Node=%d\n"),N);for(int32 N:ToB)Rows+=FString::Printf(TEXT("ToBRoute Node=%d\n"),N);for(int32 N:BackA)Rows+=FString::Printf(TEXT("ReturnRoute Node=%d\n"),N);
            for(const auto& C:R->Batch.Registry->GetConnectionRecords())if(C.bActive)OriginalConnections.Add(C.Id);
            for(auto* E:Es)for(auto Id:E->GetIncidentConnectionIds())
            {
                auto* G=E->GetGate(Id);FRogueliteRoomConnection C;FRogueliteDoorSocket D;
                Good&=Check(G&&G->GetRoomNodeId()==E->GetTargetNodeId()&&Connection(R,Id,C)&&Door(R,E->GetTargetNodeId(),Id,D),TEXT("Actual room-side gate identity available from C2"));if(!G||!Good)return Finish(false,TEXT("Gate identity invalid"));
                Good&=Check(G->GetActorLocation().Equals(D.WorldPosition+FVector(0,0,D.DoorHeight*.5),1)&&G->GetActorForwardVector().Equals(D.WorldOutwardDirection,.001),TEXT("Gate occupies its own actual room socket, not the opposite endpoint"));
                Rows+=FString::Printf(TEXT("GateIdentity Node=%d Connection=%s Side=%s Actor=%s Pose=%s\n"),E->GetTargetNodeId(),*Id.ToString(),Room(R,E->GetTargetNodeId())->Room==C.RoomA?TEXT("RoomA"):TEXT("RoomB"),*G->GetPathName(),*G->GetActorTransform().ToHumanReadableString());
            }
            for(auto Id:First->GetIncidentConnectionIds())if(Second->GetIncidentConnectionIds().Contains(Id)){Shared=Id;auto* A=First->GetGate(Id);auto* B=Second->GetGate(Id);Good&=Check(A!=B&&FVector::Dist(A->GetActorLocation(),B->GetActorLocation())>1000,TEXT("One ConnectionId has independent, separated gates at its two room endpoints"));}
            if(!Good)return Finish(false,TEXT("Endpoint identity failed"));StateSnapshot(TEXT("InitialStart"),R,P);PC->SetControlRotation(FRotator(85,0,0));BeforeShots=P->ShotAcceptedCount;FireInput(P,true);Action=Now;Step=1;return false;
        }
        if(P->GetUniqueID()!=PawnId||PC->GetUniqueID()!=PCId||P->GetBoundBatch()!=Batch||PC->GetPawn()!=P)return Finish(false,TEXT("Room change replaced/rebound the actual Pawn or PlayerController"));
        if(Step==1)
        {
            FireInput(P,false);if(Now-Action<.15)return false;
            if(!Check(P->ShotAcceptedCount==BeforeShots+1&&Dormant(First.Get())&&Dormant(Second.Get()),TEXT("Actual safehouse shot cannot activate or damage either unentered room")))return Finish(false,TEXT("Safehouse shot routing failed"));
            if(LifeCase())
            {ObserveLifetime(P);ReloadKey(P,true);Action=Now;Step=90;return false;}
            FRogueliteDoorSocket D;if(!Door(R,First->GetTargetNodeId(),EntryA,D)||!BeginWalk(P,D.WorldPosition-D.WorldOutwardDirection*250+FVector(0,0,10),R))return Finish(false,TEXT("Cannot begin actual route to first room"));Step=2;return false;
        }
        if(Step==90)
        {
            if(Now-Action<.2)return false;if(!Check(P->IsReloading()&&P->Ammo==29,TEXT("Lifecycle interruption occurs in a real partial-magazine reload")))return Finish(false,TEXT("R did not start before interruption"));
            if(Case==TEXT("LifeStopPIE"))return Finish(true,TEXT("Stop actual PIE while original reload is pending; teardown observer verifies cancellation"));
            if(Case==TEXT("LifeDeath"))P->HealthComponent->ApplyDamageOnce(10000,FGuid::NewGuid(),P);else if(Case==TEXT("LifeClear"))R->ClearRuntime();else if(Case==TEXT("LifeBatch"))++R->Seed;
            Action=Now;Step=91;return false;
        }
        if(Step==2)
        {
            if(ReleaseCorridor){FireInput(P,false);ReleaseCorridor=false;}
            if(!CorridorShot&&R->GetCurrentPlayerRoomNode()==INDEX_NONE)
            {FRogueliteRoomConnection C;if(Connection(R,EntryA,C)){const FVector L=C.Corridor->GetActorTransform().InverseTransformPosition(P->GetActorLocation());if(FMath::Abs(L.X)<C.Corridor->CorridorLength*.22){FireInput(P,true);CorridorShot=ReleaseCorridor=true;Rows+=TEXT("ActualCorridorShot Connection=")+EntryA.ToString()+TEXT("\n");}}}
            FString Why;int32 Done=Walker.Tick(P,Rows,Why);if(Done<0)return Finish(false,Why);if(!Done)return false;Action=Now;Step=3;return false;
        }
        if(Step==3)
        {
            if(Now-Action<.2)return false;
            if(!Check(CorridorShot&&First->GetState()==ERogueliteSliceEncounterState::Preparation&&First->GetEntryConnectionId()==EntryA&&Dormant(First.Get())&&Dormant(Second.Get())&&R->GetActiveEncounter()==First.Get()&&Locks(R,First.Get(),P,true)&&Locks(R,Second.Get(),P,false),TEXT("Actual full-body first entry locks only its own complete endpoint set, other room stays Dormant/open")))return Finish(false,TEXT("First Preparation isolation failed"));
            Fighting=First;Aimed.Reset();for(auto E:First->GetEnemies())if(E.IsValid()){Aimed=E;break;}
            BeforeShots=P->ShotAcceptedCount;BeforeHits=P->HitConfirmedCount;BeforeTarget=Aimed->Health->GetHealth();Action=Now;
            if(HitCase()){FVector Pos;if(!FiringPosition(R,First.Get(),P,Aimed.Get(),Pos))return Finish(false,TEXT("No actual first-room firing point"));WalkingAim=FVector::Dist2D(Pos,P->GetActorLocation())>40;if(WalkingAim&&!BeginWalk(P,Pos,R,First->GetTargetNodeId()))return Finish(false,TEXT("No in-room firing route"));Step=4;}else{PC->SetControlRotation(FRotator(85,0,0));Step=5;}return false;
        }
        if(Step==4)
        {if(WalkingAim){FString Why;int32 Done=Walker.Tick(P,Rows,Why);if(Done<0)return Finish(false,Why);if(!Done)return false;WalkingAim=false;}SetAim(P,Aimed.Get());Action=Now;Step=5;return false;}
        if(Step==5)
        {if(HitCase())SetAim(P,Aimed.Get());if(Now-Action<.25)return false;FireInput(P,true);Action=LastFire=Now;Step=6;return false;}
        if(Step==6)
        {
            FireInput(P,false);if(Now-Action<.15)return false;
            const auto& Shot=P->GetLastShot();Rows+=FString::Printf(TEXT("FirstShot Room=%d Origin=%s Direction=%s Target=%s TargetPosition=%s Hit=%s Component=%s Impact=%s Damage=%.2f\n"),
                Fighting->GetTargetNodeId(),*Shot.Origin.ToString(),*Shot.Direction.ToString(),*GetNameSafe(Aimed.Get()),*Aimed->GetActorLocation().ToString(),*GetNameSafe(Shot.Hit.GetActor()),*GetNameSafe(Shot.Hit.GetComponent()),*Shot.Hit.ImpactPoint.ToString(),P->LastAppliedDamage);
            if(!Check(P->ShotAcceptedCount==BeforeShots+1&&Fighting->GetState()==ERogueliteSliceEncounterState::CombatActive&&Fighting->GetActivationCount()==1,TEXT("One actual accepted shot activates exactly the current room")))return Finish(false,TEXT("First shot routing failed"));
            if(HitCase()){if(!Check(Aimed->Health->GetHealth()==BeforeTarget-50&&P->HitConfirmedCount==BeforeHits+1,TEXT("First real hit activates its own roster before exactly one damage settlement")))return Finish(false,TEXT("First-hit order swallowed or duplicated damage"));if(Fighting==First)OldHit=P->GetLastShot().Id;}
            else if(!Check(P->LastAppliedDamage==0,TEXT("Actual miss still activates the current Slice encounter")))return Finish(false,TEXT("Miss activation invalid"));
            if(Fighting==First&&!Check(Dormant(Second.Get())&&Second->GetActivationCount()==0&&Second->GetClearCount()==0,TEXT("First-room activation does not change the second room's roster/state")))return Finish(false,TEXT("Cross-room activation leaked"));
            Step=7;return false;
        }
        if(Step==7)
        {
            if(Fighting->GetState()==ERogueliteSliceEncounterState::Cleared){Action=Now;Step=10;return false;}
            if(Now-LastFire<P->FireIntervalSeconds+.1)return false;Aimed.Reset();for(auto E:Fighting->GetEnemies())if(E.IsValid()&&E->Health->IsAlive()){Aimed=E;break;}
            FVector Pos;if(!Aimed.IsValid()||!FiringPosition(R,Fighting.Get(),P,Aimed.Get(),Pos,Misses>0))return Finish(false,TEXT("No legal line of fire to actual remaining member"));
            WalkingAim=FVector::Dist2D(Pos,P->GetActorLocation())>40;if(WalkingAim&&!BeginWalk(P,Pos,R,Fighting->GetTargetNodeId()))return Finish(false,TEXT("Actual firing movement unavailable"));Step=8;return false;
        }
        if(Step==8)
        {if(WalkingAim){FString Why;int32 Done=Walker.Tick(P,Rows,Why);if(Done<0)return Finish(false,Why);if(!Done)return false;WalkingAim=false;}SetAim(P,Aimed.Get());Action=Now;Step=9;return false;}
        if(Step==9)
        {SetAim(P,Aimed.Get());if(Now-Action<.25)return false;FireInput(P,true);Action=LastFire=Now;Step=11;return false;}
        if(Step==11)
        {
            FireInput(P,false);if(Now-Action<.15)return false;const auto& Shot=P->GetLastShot();
            Rows+=FString::Printf(TEXT("ActualShot Room=%d Id=%s Origin=%s Direction=%s Target=%s TargetPosition=%s Hit=%s Component=%s Impact=%s Damage=%.2f Ammo=%d\n"),Fighting->GetTargetNodeId(),*Shot.Id.ToString(),*Shot.Origin.ToString(),*Shot.Direction.ToString(),*GetNameSafe(Aimed.Get()),*Aimed->GetActorLocation().ToString(),*GetNameSafe(Shot.Hit.GetActor()),*GetNameSafe(Shot.Hit.GetComponent()),*Shot.Hit.ImpactPoint.ToString(),P->LastAppliedDamage,P->Ammo);
            Misses=P->LastAppliedDamage>0?0:Misses+1;if(Misses>=3)return Finish(false,TEXT("Three actual aimed misses; preserve diagnostics without lowering enemy damage"));
            if(Fighting==First&&P->LastAppliedDamage>0&&!OldHit.IsValid())OldHit=Shot.Id;Step=7;return false;
        }
        if(Step==10)
        {
            if(Fighting==First&&Now-LastFire<P->FireIntervalSeconds+.1)return false;
            if(!Check(Fighting->GetState()==ERogueliteSliceEncounterState::Cleared&&Fighting->GetClearCount()==1&&Fighting->GetRemainingEnemyCount()==0&&Locks(R,Fighting.Get(),P,false),TEXT("Only this room's two legal deaths clear once and open its own endpoints")))return Finish(false,TEXT("Room clear state invalid"));
            StateSnapshot(Fighting==First?TEXT("FirstCleared"):TEXT("SecondCleared"),R,P);
            if(Fighting==First)
            {
                if(!Check(Dormant(Second.Get())&&Second->GetClearCount()==0&&OldHit.IsValid(),TEXT("Second room remains complete Dormant after first room clear")))return Finish(false,TEXT("First room consumed second roster"));
                OldDeadHealth=First->GetEnemies()[0]->Health;AfterAHealth=P->HealthComponent->GetHealth();BeforeShots=P->ShotAcceptedCount;PC->SetControlRotation(FRotator(85,0,0));FireInput(P,true);Action=Now;Step=12;return false;
            }
            RecoveryId=BackEdges[0];FString Why;if(!R->RequestRoomRecovery(Second->GetTargetNodeId(),RecoveryId,Why))return Finish(false,TEXT("Second exit recovery selection: ")+Why);
            if(!RecoveryWaiting(R,Second.Get(),P,RecoveryId,Waiting)||!BeginWalk(P,Waiting,R,Second->GetTargetNodeId()))return Finish(false,TEXT("No honest waiting position for second-room recovery"));Action=Now;Step=30;return false;
        }
        if(Step==12)
        {
            FireInput(P,false);if(Now-Action<.15)return false;
            if(!Check(P->ShotAcceptedCount==BeforeShots+1&&Dormant(Second.Get())&&Second->GetActivationCount()==0,TEXT("Actual shot from Cleared first room cannot wake or damage second room")))return Finish(false,TEXT("Cleared-room shot leaked"));
            RecoveryId=ToBEdges[0];FString Why;if(!R->RequestRoomRecovery(First->GetTargetNodeId(),RecoveryId,Why))return Finish(false,TEXT("First exit recovery selection: ")+Why);
            if(!RecoveryWaiting(R,First.Get(),P,RecoveryId,Waiting)||!BeginWalk(P,Waiting,R,First->GetTargetNodeId()))return Finish(false,TEXT("No physical waiting route clear of actual first recovery segment"));Action=Now;Step=13;return false;
        }
        if(Step==13||Step==30)
        {FString Why;int32 Done=Walker.Tick(P,Rows,Why);if(Done<0)return Finish(false,Why);if(!Done)return false;Action=Now;Step=Step==13?14:31;return false;}
        if(Step==14||Step==31)
        {
            auto* E=Step==14?First.Get():Second.Get();const auto Query=FRogueliteDynamicGameplayBridge::QueryConnection(R,RecoveryId,P);
            if(Step==31&&E->IsRecoveryBusy())
            {FString Why;const bool Accepted=R->RequestRoomRecovery(First->GetTargetNodeId(),First->GetExitConnectionId(),Why);if(!Check(!Accepted,TEXT("Valid other Cleared room cannot replace the Source's running recovery request")))return Finish(false,TEXT("Concurrent recovery accepted"));}
            if(Query.State!=ERogueliteGameplayPassage::Traversable||!E->IsRecoveryParked()){if(Now-Action>50)return Finish(false,TEXT("Real recovery/parking unavailable: ")+E->GetRecoveryDiagnostic());return false;}
            if(!Check(Query.bForwardCapsule&&Query.bReverseCapsule&&Query.bFullPath&&Query.bMoveVerified&&Query.bStructuralCurrent&&Query.bBaseNavigationReady,TEXT("Selected exit alone has two-way capsules, full nonPartial path, actual Walking proof and safely parked verifier")))return Finish(false,TEXT("Missing actual passage proof"));
            Rows+=TEXT("ActualRecovery=\n")+FRogueliteDynamicGameplayBridge::RecoveryEvidence(R)+TEXT("\n");
            if(Step==14)
            {
                RouteIndex=1;Amp=P->Ammo;
                if(ToB.Num()>2){FRogueliteDoorSocket D;if(!Door(R,ToB[RouteIndex],ToBEdges[RouteIndex-1],D)||!BeginWalk(P,D.WorldPosition-D.WorldOutwardDirection*200+FVector(0,0,10),R))return Finish(false,TEXT("Natural intermediate route unavailable"));Step=15;}
                else {if(!OutsideShot(R,Second.Get(),P,OutsideB)||!BeginWalk(P,OutsideB,R,INDEX_NONE,15))return Finish(false,TEXT("No real corridor-side view to dormant second room"));Step=17;}return false;
            }
            FRogueliteDoorSocket D;if(!Door(R,First->GetTargetNodeId(),BackEdges.Last(),D)||!BeginWalk(P,D.WorldPosition-D.WorldOutwardDirection*200+FVector(0,0,10),R))return Finish(false,TEXT("Actual return route to first cleared room unavailable"));Step=32;return false;
        }
        if(Step==15)
        {
            FString Why;int32 Done=Walker.Tick(P,Rows,Why);if(Done<0)return Finish(false,Why);if(!Done)return false;
            if(!Check(P->Ammo==Amp&&P->HealthComponent->GetHealth()==AfterAHealth&&Dormant(Second.Get()),TEXT("Natural route through safe/cleared intermediate room does not refill/reset player or wake second roster")))return Finish(false,TEXT("Intermediate room changed persistent state"));
            if(ToB[RouteIndex]==R->PlayerStartNode){PC->SetControlRotation(FRotator(85,0,0));BeforeShots=P->ShotAcceptedCount;FireInput(P,true);Action=Now;Step=16;return false;}
            ++RouteIndex;Step=18;return false;
        }
        if(Step==16)
        {FireInput(P,false);if(Now-Action<.15)return false;if(!Check(P->ShotAcceptedCount==BeforeShots+1&&Dormant(Second.Get()),TEXT("Revisited actual safehouse shot still cannot wake second room")))return Finish(false,TEXT("Safehouse replay scope failed"));Amp=P->Ammo;++RouteIndex;Step=18;return false;}
        if(Step==18)
        {
            if(RouteIndex<ToB.Num()-1){FRogueliteDoorSocket D;if(!Door(R,ToB[RouteIndex],ToBEdges[RouteIndex-1],D)||!BeginWalk(P,D.WorldPosition-D.WorldOutwardDirection*200+FVector(0,0,10),R))return Finish(false,TEXT("Intermediate path missing"));Step=15;return false;}
            if(!OutsideShot(R,Second.Get(),P,OutsideB)||!BeginWalk(P,OutsideB,R,INDEX_NONE,15))return Finish(false,TEXT("Outside dormant-shot candidate not physically reachable"));Step=17;return false;
        }
        if(Step==17)
        {
            FString Why;int32 Done=Walker.Tick(P,Rows,Why);if(Done<0)return Finish(false,Why);if(!Done)return false;
            if(!Check(R->GetCurrentPlayerRoomNode()==INDEX_NONE&&P->Ammo==Amp&&P->HealthComponent->GetHealth()==AfterAHealth&&Dormant(Second.Get()),TEXT("Same persistent player reached actual corridor outside second room with unchanged state")))return Finish(false,TEXT("Cross-room state continuity failed"));
            StateSnapshot(TEXT("OutsideSecond"),R,P);SetAim(P,DormantTarget.Get());BeforeShots=P->ShotAcceptedCount;Action=Now;Step=19;return false;
        }
        if(Step==19)
        {SetAim(P,DormantTarget.Get());if(Now-Action<.25)return false;FireInput(P,true);Action=Now;Step=20;return false;}
        if(Step==20)
        {
            FireInput(P,false);if(Now-Action<.15)return false;
            if(!Check(P->ShotAcceptedCount==BeforeShots+1&&P->GetLastShot().Hit.GetActor()==DormantTarget.Get()&&P->LastAppliedDamage==0&&Dormant(Second.Get())&&Second->GetState()==ERogueliteSliceEncounterState::AwaitingEntry,TEXT("Actual corridor bullet hits a Dormant second-room enemy but cannot damage/activate it before entry")))return Finish(false,TEXT("Out-of-room damage/admission bypass"));
            BeforeReloadStarts=P->ReloadStartedCount;BeforeReloadCompletes=P->ReloadCompletedCount;BeforeReloadCancels=P->ReloadCancelledCount;Amp=P->Ammo;BeforeShots=P->ShotAcceptedCount;BeforeRejected=P->ShotRejectedCount;
            ReloadKey(P,true);ReloadAt=Action=Now;Step=21;return false;
        }
        if(Step==21)
        {
            if(Now-Action<.12)return false;ReloadKey(P,false);if(!Check(P->IsReloading()&&P->ReloadStartedCount==BeforeReloadStarts+1&&P->Ammo==Amp,TEXT("Actual R begins in corridor under the same World/Source/Batch")))return Finish(false,TEXT("Cross-room reload start failed"));
            FRogueliteDoorSocket D;if(!Door(R,Second->GetTargetNodeId(),EntryB,D))return Finish(false,TEXT("Second actual entry missing"));
            const float Depth=D.WallThickness+34+25+2+60;
            if(!BeginWalk(P,D.WorldPosition-D.WorldOutwardDirection*Depth+FVector(0,0,10),R,INDEX_NONE,20))return Finish(false,TEXT("No actual second entry walk"));Step=22;return false;
        }
        if(Step==22)
        {
            FString Why;int32 Done=Walker.Tick(P,Rows,Why);if(Done<0)return Finish(false,Why);
            if(Second->GetState()==ERogueliteSliceEncounterState::Preparation&&P->IsReloading()&&!ReloadSeenInPreparation)
            {ReloadSeenInPreparation=true;StateSnapshot(TEXT("EnteredSecondWhileReloading"),R,P);FireInput(P,true);}
            if(!Done)return false;MoveInput(P,FVector2D::ZeroVector);Action=Now;Step=23;return false;
        }
        if(Step==23)
        {
            FireInput(P,false);
            if(Second->GetState()!=ERogueliteSliceEncounterState::Preparation){if(Now-Action>3)return Finish(false,TEXT("Second physical entry failed: ")+Second->GetLastReason());return false;}
            if(!NegativePrepared)
            {
                bool Good=Check(ReloadSeenInPreparation&&First->GetState()==ERogueliteSliceEncounterState::Cleared&&First->GetClearCount()==1&&Second->GetEntryConnectionId()==EntryB&&Locks(R,Second.Get(),P,true)&&Locks(R,First.Get(),P,false),TEXT("Real second entry owns complete Prep lock while first remains Cleared/open and original reload continues"));
                FRogueliteSliceShot Old;if(!P->FindAcceptedShot(OldHit,Old))Good=false;else
                {
                    P->OnShotAccepted.Broadcast(Old);if(OldDeadHealth.IsValid())OldDeadHealth->OnKilled.Broadcast(OldDeadHealth.Get());
                    for(auto E:Second->GetEnemies())if(E.IsValid())Good&=E->ReceiveShot(P,OldHit)==0;
                }
                Good&=Check(Dormant(Second.Get())&&Second->GetActivationCount()==0&&First->GetState()==ERogueliteSliceEncounterState::Cleared&&First->GetClearCount()==1&&Locks(R,Second.Get(),P,true),TEXT("Old first-room ShotId/hit/death replays never activate/decrement second room or reopen its endpoint gates"));
                const auto OldProof=FRogueliteDynamicGameplayBridge::QueryConnection(R,First->GetExitConnectionId(),P);
                Good&=Check(OldProof.State!=ERogueliteGameplayPassage::Traversable&&R->IsDungeonReady()&&R->GetTraversalSnapshot().bCurrent,TEXT("Second gate changes invalidate old source proof truthfully without restarting first combat or clearing geometry"));
                Rows+=FString::Printf(TEXT("OldProofAfterSecondLock Connection=%s State=%d Forward=%d Reverse=%d FullPath=%d MoveVerified=%d\n"),*First->GetExitConnectionId().ToString(),int32(OldProof.State),OldProof.bForwardCapsule,OldProof.bReverseCapsule,OldProof.bFullPath,OldProof.bMoveVerified);
                if(!Good)return Finish(false,TEXT("Second-room state/event/gate isolation failed"));NegativePrepared=true;
            }
            if(P->IsReloading()){if(Now-ReloadAt>2.2)return Finish(false,TEXT("Room entry reset/extended reload timer"));return false;}
            if(!Check(P->Ammo==30&&P->ReloadStartedCount==BeforeReloadStarts+1&&P->ReloadCompletedCount==BeforeReloadCompletes+1&&P->ReloadCancelledCount==BeforeReloadCancels&&P->ShotAcceptedCount==BeforeShots&&P->ShotRejectedCount>BeforeRejected&&P->HealthComponent->GetHealth()==AfterAHealth&&Dormant(Second.Get()),TEXT("Original-deadline reload completes once across rooms, no rejected-fire buffer, HP unchanged and second room still Dormant")))return Finish(false,TEXT("Cross-room reload or persistence failed"));
            StateSnapshot(TEXT("SecondPreparationReloadComplete"),R,P);Rows+=FString::Printf(TEXT("CrossRoomReloadElapsed=%.3f\n"),Now-ReloadAt);
            Fighting=Second;Aimed.Reset();for(auto E:Second->GetEnemies())if(E.IsValid()){Aimed=E;break;}BeforeShots=P->ShotAcceptedCount;BeforeHits=P->HitConfirmedCount;BeforeTarget=Aimed->Health->GetHealth();Action=Now;
            if(HitCase()){FVector Pos;if(!FiringPosition(R,Second.Get(),P,Aimed.Get(),Pos))return Finish(false,TEXT("No second-room first-hit line of fire"));WalkingAim=FVector::Dist2D(Pos,P->GetActorLocation())>40;if(WalkingAim&&!BeginWalk(P,Pos,R,Second->GetTargetNodeId()))return Finish(false,TEXT("Second firing walk unavailable"));Step=4;}else{PC->SetControlRotation(FRotator(85,0,0));Step=5;}return false;
        }
        if(Step==32)
        {
            FString Why;int32 Done=Walker.Tick(P,Rows,Why);if(Done<0)return Finish(false,Why);if(!Done)return false;Action=Now;Step=33;return false;
        }
        if(Step==33)
        {
            if(Now-Action<.3)return false;int32 LivePlayers=0;for(TActorIterator<ARogueliteSlicePlayer>I(W);I;++I)LivePlayers+=!I->IsActorBeingDestroyed();
            TSet<FGuid> CurrentIds;for(const auto& C:R->Batch.Registry->GetConnectionRecords())if(C.bActive)CurrentIds.Add(C.Id);
            bool Good=Check(LivePlayers==1&&R->GetCurrentPlayerRoomNode()==First->GetTargetNodeId()&&First->GetState()==ERogueliteSliceEncounterState::Cleared&&Second->GetState()==ERogueliteSliceEncounterState::Cleared&&First->GetClearCount()==1&&Second->GetClearCount()==1&&First->GetRemainingEnemyCount()==0&&Second->GetRemainingEnemyCount()==0&&Locks(R,First.Get(),P,false)&&Locks(R,Second.Get(),P,false),TEXT("Same actual player physically returned to first room; both cleared once, no respawn/relock or reset"));
            Good&=Check(CurrentIds.Num()==3&&CurrentIds.Difference(OriginalConnections).IsEmpty()&&OriginalConnections.Difference(CurrentIds).IsEmpty()&&R->GetTraversalSnapshot().bCurrent&&R->IsDungeonReady(),TEXT("Only original C2 owns the same three reservations; structure and strict base navigation remain current"));
            FRogueliteSliceShot Old;Good&=Check(P->FindAcceptedShot(OldHit,Old),TEXT("First-room accepted shot ledger survives complete A-to-B-to-A flow"));StateSnapshot(TEXT("ActualReturnToFirst"),R,P);
            return Finish(Good,TEXT("Real input A-to-B-to-A, persistent player, isolated combat/endpoints and original recovery verified"));
        }
        return false;
    }
};
class FAfter:public IAutomationLatentCommand
{
    FAutomationTestBase* Test;TWeakObjectPtr<ARogueliteCombatSliceRig> Rig;int32 Seed;FString Case;TSharedPtr<FLifetime> Lifetime;double Started=0;
public:
    FAfter(FAutomationTestBase* T,ARogueliteCombatSliceRig* R,int32 S,FString C,TSharedPtr<FLifetime> O):Test(T),Rig(R),Seed(S),Case(MoveTemp(C)),Lifetime(MoveTemp(O)){}
    ~FAfter(){FWorldDelegates::OnWorldCleanup.Remove(Lifetime->Cleanup);}
    bool Update()override
    {
        if(!Started)Started=FPlatformTime::Seconds();int32 Worlds=0;if(GEngine)for(const auto& C:GEngine->GetWorldContexts())Worlds+=C.WorldType==EWorldType::PIE;
        if((GEditor&&GEditor->PlayWorld)||Worlds){if(FPlatformTime::Seconds()-Started<15)return false;Test->AddError(TEXT("G2 actual EndPIE timeout"));return true;}
        if(Rig.IsValid())Rig->Seed=Seed;auto* W=GEditor?GEditor->GetEditorWorldContext().World():nullptr;int32 Actors=0;
        if(W)for(TActorIterator<AActor>I(W);I;++I)if(!I->IsActorBeingDestroyed())Actors+=I->IsA<ARogueliteSlicePlayer>()||I->IsA<ARogueliteSliceEnemy>()||I->IsA<ARogueliteSliceGate>()||I->IsA<ARogueliteSliceRecoveryProbe>();
        const bool Good=Rig.IsValid()&&Actors==0&&Rig->Batch.Rooms.IsEmpty()&&Rig->Batch.Edges.IsEmpty()&&Rig->Batch.OwnedActors.IsEmpty()&&!Rig->Batch.BatchId.IsValid()&&!Rig->Batch.Registry;
        bool ReloadSafe=true;if(Case==TEXT("LifeStopPIE"))ReloadSafe=Lifetime->Observed&&!Lifetime->Reloading&&Lifetime->Ammo==29&&Lifetime->Completed==0&&Lifetime->Cancelled==1&&Lifetime->Remaining==0;
        Test->TestTrue(TEXT("Actual EndPIE removes both rooms' gameplay and old pending reload without writing Editor layout"),Good&&ReloadSafe);
        Save(TEXT("stop-flow-")+Case,FString::Printf(TEXT("PIEWorlds=%d GameplayActors=%d EmptyEditorBatch=%d RestoredSeed=%d ReloadObserved=%d Ammo=%d Completed=%d Cancelled=%d Remaining=%.3f\nPassed=%d\n"),Worlds,Actors,Good,Seed,Lifetime->Observed,Lifetime->Ammo,Lifetime->Completed,Lifetime->Cancelled,Lifetime->Remaining,Good&&ReloadSafe));return true;
    }
};
}
IMPLEMENT_COMPLEX_AUTOMATION_TEST(FG2Flows,"ProjectVerification.G2.Flow",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
void FG2Flows::GetTests(TArray<FString>& Names,TArray<FString>& Commands)const
{Names={TEXT("Seed0Miss"),TEXT("Seed1FirstHit"),TEXT("LifeDeath"),TEXT("LifeClear"),TEXT("LifeBatch"),TEXT("LifeStopPIE"),TEXT("SpawnFailure")};Commands=Names;}
bool FG2Flows::RunTest(const FString& Case)
{
    using namespace TwoRoomFlowVerification;if(!GEditor||GEditor->PlayWorld)return false;auto* W=GEditor->GetEditorWorldContext().World();
    if(!W||W->GetOutermost()->GetName()!=Map)return false;auto* R=Find(W);if(!R||!R->bEnableTwoCombatRooms)return false;
    const int32 OldSeed=R->Seed;R->Seed=Case==TEXT("Seed1FirstHit")?1:0;auto Proof=MakeShared<FLifetime>();auto& F=FAutomationTestFramework::Get();
    F.EnqueueLatentCommand(MakeShared<FStartPIECommand>(false));F.EnqueueLatentCommand(MakeShared<FFlow>(this,Case,Proof));F.EnqueueLatentCommand(MakeShared<FEndPlayMapCommand>());F.EnqueueLatentCommand(MakeShared<FAfter>(this,R,OldSeed,Case,Proof));return true;
}
#endif
