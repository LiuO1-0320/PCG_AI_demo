#include "RogueliteDynamicGameplayBridge.h"
#include "RogueliteRuntimeLoopAssembly.h"
#include "Gameplay/RogueliteGameplayObservation.h"
#include "RogueliteNavigationTestCharacter.h"
#include "Components/PrimitiveComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/BoxComponent.h"
#include "AIController.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Navigation/PathFollowingComponent.h"
#include "NavigationPath.h"
#include "NavigationSystem.h"
#include "Engine/World.h"
#include "TimerManager.h"

namespace DynamicBridge
{
struct FLease
{
    TWeakObjectPtr<AActor> Actor,ActualOwner;
    TWeakObjectPtr<UPrimitiveComponent> Component;
    ERogueliteDynamicParticipant Kind=ERogueliteDynamicParticipant::Pawn;
    FVector Scale=FVector::OneVector,Shape=FVector::ZeroVector;
    FTransform GatePose;
    ECollisionChannel ObjectType=ECC_WorldDynamic;
};
struct FContext
{
    TWeakObjectPtr<ARogueliteRuntimeLoopAssembly> Source;
    TWeakObjectPtr<UWorld> World;
    FGuid Batch,ProofConnection;
    TArray<FLease> Leases;
    uint64 GateRevision=0,ProofGateRevision=0;
    FString ProofEpoch,Evidence;
    bool bMovePassed=false,bMoving=false,bCrossedInterior=false;
    TWeakObjectPtr<APawn> MovingPawn;
    TWeakObjectPtr<AAIController> Controller;
    TWeakObjectPtr<UPathFollowingComponent> Following;
    FDelegateHandle Completion;
    FTimerHandle Poll;
    FAIRequestID Request;
    FVector Goal=FVector::ZeroVector;
    FTransform CorridorPose;
    FVector CorridorHalf=FVector::ZeroVector;
    double MoveStarted=0;
};
TMap<TWeakObjectPtr<ARogueliteRuntimeLoopAssembly>,TSharedPtr<FContext>> Contexts;
bool Identity(const FContext& C)
{
    const auto* S=C.Source.Get();const auto* W=C.World.Get();
    return IsInGameThread()&&IsValid(S)&&!S->IsActorBeingDestroyed()&&W&&W->IsGameWorld()&&!W->bIsTearingDown&&S->GetWorld()==W
        &&C.Batch.IsValid()&&S->Batch.BatchId==C.Batch&&S->Batch.bSucceeded&&S->Batch.bGeometryValid&&S->bGeometryValidated
        &&S->CommittedLoops.bRuntimePrepared&&S->CommittedLoops.SourceBatchId==C.Batch
        &&IsValid(S->Batch.Registry)&&S->Batch.Registry->GetOwner()==S&&S->Batch.Registry->GetWorld()==W;
}
TSharedPtr<FContext> Context(const ARogueliteRuntimeLoopAssembly* S)
{const auto* C=Contexts.Find(TWeakObjectPtr<ARogueliteRuntimeLoopAssembly>(const_cast<ARogueliteRuntimeLoopAssembly*>(S)));return C?*C:nullptr;}
bool Ownership(AActor* Actor,ARogueliteRuntimeLoopAssembly* Source,ERogueliteDynamicParticipant Kind)
{
    if(Actor->GetOwner()==Source)return true;
    if(Kind==ERogueliteDynamicParticipant::Pawn)if(auto* P=Cast<APawn>(Actor))if(auto* Controller=P->GetController())
        if(P->GetOwner()==Controller&&Controller->GetPawn()==P)
            return Controller->GetOwner()==Source||(Cast<APlayerController>(Controller)&&Controller->IsLocalController()&&Controller->GetWorld()==Source->GetWorld());
    return false;
}
FVector Shape(const UPrimitiveComponent* P)
{
    if(const auto* C=Cast<UCapsuleComponent>(P))return FVector(C->GetUnscaledCapsuleRadius(),C->GetUnscaledCapsuleHalfHeight(),0);
    if(const auto* B=Cast<UBoxComponent>(P))return B->GetUnscaledBoxExtent();return FVector(-1);
}
bool Usable(const FContext& C,const FLease& L)
{
    auto* A=L.Actor.Get();auto* P=L.Component.Get();auto* S=C.Source.Get();
    if(!Identity(C)||!IsValid(A)||A->IsActorBeingDestroyed()||!IsValid(P)||!P->IsRegistered()||P->GetOwner()!=A
        ||A->GetWorld()!=C.World.Get()||P->GetWorld()!=C.World.Get()||A->GetOwner()!=L.ActualOwner.Get()
        ||!Ownership(A,S,L.Kind)||P->Mobility!=EComponentMobility::Movable||P->CanEverAffectNavigation()
        ||!A->GetActorScale3D().Equals(FVector::OneVector,.0001)||!P->GetComponentScale().Equals(L.Scale,.0001)
        ||!Shape(P).Equals(L.Shape,.0001)||P->GetCollisionObjectType()!=L.ObjectType)return false;
    if(L.Kind==ERogueliteDynamicParticipant::Gate)return P->GetComponentTransform().Equals(L.GatePose,.0001);
    return Cast<APawn>(A)&&P==A->GetRootComponent()&&Cast<UCapsuleComponent>(P);
}
void Cancel(const TSharedPtr<FContext>& C)
{
    if(!C)return;C->bMoving=false;
    if(auto* P=C->Following.Get())P->OnRequestFinished.Remove(C->Completion);
    if(auto* W=C->World.Get())W->GetTimerManager().ClearTimer(C->Poll);
    if(auto* A=C->Controller.Get())if(A->GetOwner()==C->Source.Get()&&A->GetPawn()==C->MovingPawn.Get())A->StopMovement();
    C->Following.Reset();C->Controller.Reset();C->MovingPawn.Reset();
}
struct FPassageFrame
{
    FVector Start,End;
    TWeakObjectPtr<ARogueliteParametricCorridor> Corridor;
};
bool Frame(const TSharedPtr<FContext>& C,FGuid Id,FPassageFrame& Out)
{
    if(!C||!Identity(*C))return false;auto* S=C->Source.Get();
    const bool Owned=S->Batch.Edges.ContainsByPredicate([&](const auto& E){return E.ConnectionId==Id;})
        ||S->CommittedLoops.Edges.ContainsByPredicate([&](const auto& E){return E.ConnectionId==Id;});
    if(!Owned)return false;const auto Records=S->Batch.Registry->GetConnectionRecords();
    const auto* R=Records.FindByPredicate([&](const auto& E){return E.Id==Id&&E.bActive&&E.bGeometryValid&&E.ValidationStatus==ERogueliteConnectionValidation::Passed;});
    if(!R||R->Mode!=ERogueliteConnectionMode::StraightCorridor||!IsValid(R->RoomA)||!IsValid(R->RoomB)||!IsValid(R->Corridor))return false;
    FRogueliteDoorSocket A,B;if(!R->RoomA->TryGetDoorSocket(R->SocketA,A)||!R->RoomB->TryGetDoorSocket(R->SocketB,B))return false;
    Out.Start=A.WorldPosition-A.WorldOutwardDirection*300;Out.End=B.WorldPosition-B.WorldOutwardDirection*300;Out.Corridor=R->Corridor;return true;
}
bool Sweeps(const TSharedPtr<FContext>& C,const FPassageFrame& F,APawn* Pawn,bool& Forward,bool& Reverse)
{
    auto* W=C->World.Get();if(!W)return false;FCollisionQueryParams Q(SCENE_QUERY_STAT(G1A_Passage),false);
    if(Pawn&&FRogueliteDynamicGameplayBridge::IsComponentIsolated(C->Source.Get(),C->Batch,Cast<UPrimitiveComponent>(Pawn->GetRootComponent())))Q.AddIgnoredActor(Pawn);
    const FVector Lift(0,0,90);const auto Capsule=FCollisionShape::MakeCapsule(34,88);FHitResult Hit;
    Forward=!W->SweepSingleByChannel(Hit,F.Start+Lift,F.End+Lift,FQuat::Identity,ECC_Pawn,Capsule,Q);
    Reverse=!W->SweepSingleByChannel(Hit,F.End+Lift,F.Start+Lift,FQuat::Identity,ECC_Pawn,Capsule,Q);return Forward&&Reverse;
}
bool Path(const TSharedPtr<FContext>& C,const FPassageFrame& F,APawn* Pawn)
{
    auto* W=C->World.Get();if(!W||UNavigationSystemV1::IsNavigationBeingBuiltOrLocked(W))return false;
    auto* P=UNavigationSystemV1::FindPathToLocationSynchronously(W,F.Start+FVector(0,0,10),F.End+FVector(0,0,10),Pawn?static_cast<AActor*>(Pawn):C->Source.Get());
    return IsValid(P)&&P->IsValid()&&!P->IsPartial()&&P->PathPoints.Num()>=2;
}
void PollRecovery(TWeakPtr<FContext> Weak)
{
    const auto C=Weak.Pin();if(!C||!C->bMoving)return;
    if(!Identity(*C)||!C->MovingPawn.IsValid()||C->GateRevision!=C->ProofGateRevision||FPlatformTime::Seconds()-C->MoveStarted>30)
    {C->bMovePassed=false;C->Evidence+=TEXT("Recovery cancelled: stale identity, gate changed or timeout.\n");Cancel(C);return;}
    const FVector P=C->MovingPawn->GetActorLocation();const FVector Local=C->CorridorPose.InverseTransformPosition(P);
    if(FMath::Abs(Local.X)<C->CorridorHalf.X-60&&FMath::Abs(Local.Y)<C->CorridorHalf.Y-34)
    {C->bCrossedInterior=true;C->Evidence+=TEXT("InteriorSample=")+P.ToString()+TEXT("\n");}
}
}

bool FRogueliteDynamicGameplayBridge::Enable(ARogueliteRuntimeLoopAssembly* S,FGuid Batch,FString& Why)
{
    using namespace DynamicBridge;if(!IsInGameThread()){Why=TEXT("Game thread required.");return false;}
    FRogueliteGameplayObservationWrite ObservationWrite(IsValid(S)?S->AActor::GetWorld():nullptr,TEXT("G1A context enable"));
    auto C=MakeShared<FContext>();C->Source=S;C->World=IsValid(S)?S->GetWorld():nullptr;C->Batch=Batch;
    if(!Identity(*C)||!FRogueliteRuntimeLoopAssembler::Current(S,S->Batch,S->Settings,S->CommittedLoops)){Why=TEXT("Enable requires current successful runtime Source/World/Batch geometry.");return false;}
    if(const auto Existing=Context(S);Existing&&Identity(*Existing)&&Existing->Batch==Batch){Why=TEXT("Already enabled; existing component leases preserved.");return true;}
    Disable(S);for(auto It=Contexts.CreateIterator();It;++It)if(!Identity(*It.Value())){FRogueliteGameplayObservationWrite StaleContextWrite(It.Value()->World.Get(),TEXT("G1A stale context cleanup"));Cancel(It.Value());It.RemoveCurrent();}
    Contexts.Add(S,C);Why=TEXT("Explicit component registration enabled for this Source/World/Batch only.");return true;
}
void FRogueliteDynamicGameplayBridge::Disable(ARogueliteRuntimeLoopAssembly* S)
{
    using namespace DynamicBridge;if(!IsInGameThread())return;
    if(auto C=Context(S))
    {
        FRogueliteGameplayObservationWrite ObservationWrite(C->World.Get(),TEXT("G1A context disable"));
        Cancel(C);Contexts.Remove(S);
    }
}
bool FRogueliteDynamicGameplayBridge::RegisterComponent(ARogueliteRuntimeLoopAssembly* S,FGuid Batch,AActor* A,UPrimitiveComponent* P,ERogueliteDynamicParticipant Kind,FString& Why)
{
    using namespace DynamicBridge;if(!IsInGameThread()){Why=TEXT("Game thread required.");return false;}const auto C=Context(S);
    FRogueliteGameplayObservationWrite ObservationWrite(C?C->World.Get():(IsValid(S)?S->AActor::GetWorld():nullptr),TEXT("G1A component registration"));
    if(!C||!Identity(*C)||C->Batch!=Batch||!IsValid(A)||!IsValid(P)||P->GetOwner()!=A||A==S
        ||A->IsA<ARogueliteParametricRoom>()||A->IsA<ARogueliteParametricCorridor>()||A->IsA<ARogueliteRoomAssembly>()||A->IsA<ARogueliteNavigationTestCharacter>())
    {Why=TEXT("Component lease rejected: invalid Source/Batch/component owner or protected structural/verifier actor.");return false;}
    if(Kind!=ERogueliteDynamicParticipant::Pawn&&Kind!=ERogueliteDynamicParticipant::Gate)
    {Why=TEXT("Unknown participant kind.");return false;}
    if(Kind==ERogueliteDynamicParticipant::Pawn&&(!Cast<APawn>(A)||P!=A->GetRootComponent()||!Cast<UCapsuleComponent>(P)))
    {Why=TEXT("Pawn lease requires its actual root capsule.");return false;}
    if(Kind==ERogueliteDynamicParticipant::Gate&&(!Cast<UBoxComponent>(P)||Cast<APawn>(A)))
    {Why=TEXT("Test gate lease requires a distinct non-Pawn box component.");return false;}
    for(const auto& Pair:Contexts)if(Pair.Value!=C&&Identity(*Pair.Value))for(const auto& L:Pair.Value->Leases)
        if(L.Component.Get()==P&&Usable(*Pair.Value,L)){Why=TEXT("Component already belongs to another current Source/Batch.");return false;}
    for(const auto& L:C->Leases)if(L.Component.Get()==P)
    {const bool Same=L.Kind==Kind&&Usable(*C,L);Why=Same?TEXT("Already registered; no duplicate lease."):TEXT("Conflicting or invalid existing component lease.");return Same;}
    if(P->GetCollisionEnabled()!=ECollisionEnabled::NoCollision)
    {Why=TEXT("New component leases must be established before enabling collision; existing blockers are not retroactively hidden.");return false;}
    FLease L;L.Actor=A;L.ActualOwner=A->GetOwner();L.Component=P;L.Kind=Kind;L.Scale=P->GetComponentScale();L.Shape=Shape(P);L.GatePose=P->GetComponentTransform();L.ObjectType=P->GetCollisionObjectType();
    if(!Usable(*C,L)){Why=TEXT("Component must be registered, Movable, same owned GameWorld, unit Actor scale and non-navigation-affecting; owner/shape/pose must remain valid.");return false;}
    C->Leases.Add(L);if(Kind==ERogueliteDynamicParticipant::Gate){++C->GateRevision;C->bMovePassed=false;}
    Why=TEXT("Only this exact component leased; other components remain in GeometryDigest.");return true;
}
bool FRogueliteDynamicGameplayBridge::UnregisterComponent(ARogueliteRuntimeLoopAssembly* S,UPrimitiveComponent* P)
{
    using namespace DynamicBridge;if(!IsInGameThread())return false;
    if(const auto C=Context(S))
    {
        FRogueliteGameplayObservationWrite ObservationWrite(C->World.Get(),TEXT("G1A component unregister"));
        C->Leases.RemoveAll([&](const auto& L){return L.Component.Get()==P;});++C->GateRevision;C->bMovePassed=false;
    }
    return true;
}
int32 FRogueliteDynamicGameplayBridge::ActiveComponentCount(const ARogueliteRuntimeLoopAssembly* S)
{using namespace DynamicBridge;if(!IsInGameThread())return 0;int32 N=0;if(const auto C=Context(S);C&&Identity(*C))for(const auto& L:C->Leases)N+=Usable(*C,L);return N;}
bool FRogueliteDynamicGameplayBridge::IsComponentIsolated(const AActor* Owner,FGuid Batch,const UPrimitiveComponent* P)
{
    using namespace DynamicBridge;if(!IsInGameThread())return false;const auto* S=Cast<ARogueliteRuntimeLoopAssembly>(Owner);const auto C=Context(S);
    if(!C||C->Batch!=Batch||!Identity(*C))return false;
    for(const auto& L:C->Leases)if(L.Component.Get()==P)return Usable(*C,L);return false;
}
void FRogueliteDynamicGameplayBridge::NotifyGateCollisionChanged(UPrimitiveComponent* P)
{
    using namespace DynamicBridge;if(!IsInGameThread())return;for(const auto& Pair:Contexts)if(Identity(*Pair.Value))for(const auto& L:Pair.Value->Leases)
        if(L.Kind==ERogueliteDynamicParticipant::Gate&&L.Component.Get()==P)
        {
            FRogueliteGameplayObservationWrite ObservationWrite(Pair.Value->World.Get(),TEXT("G1A gate collision change"));
            ++Pair.Value->GateRevision;Pair.Value->bMovePassed=false;Cancel(Pair.Value);
        }
}
FRogueliteGameplayPassageQuery FRogueliteDynamicGameplayBridge::QueryConnection(ARogueliteRuntimeLoopAssembly* S,FGuid Id,APawn* Pawn)
{
    using namespace DynamicBridge;FRogueliteGameplayPassageQuery Q;Q.ConnectionId=Id;if(!IsInGameThread())return Q;const auto C=Context(S);FPassageFrame F;
    if(!Frame(C,Id,F)){Q.Reason=TEXT("Unavailable: stale Source/World/Batch or non-authoritative connection.");return Q;}
    Q.BatchId=C->Batch;Q.bStructuralCurrent=S->GetTraversalSnapshot().bCurrent&&S->bGeometryValidated;
    Q.bBaseNavigationReady=S->IsDungeonReady();
    if(!Q.bStructuralCurrent||!Q.bBaseNavigationReady){Q.Reason=TEXT("Base structure/navigation not current Ready; no gameplay passage claim.");return Q;}
    Sweeps(C,F,Pawn,Q.bForwardCapsule,Q.bReverseCapsule);Q.bFullPath=Path(C,F,Pawn);
    if(!Q.bForwardCapsule||!Q.bReverseCapsule){Q.State=ERogueliteGameplayPassage::GameplayBlocked;Q.Reason=TEXT("Actual physical capsule sweep blocked; a static NavPath is not permission to pass.");return Q;}
    Q.bMoveVerified=C->bMovePassed&&C->ProofConnection==Id&&C->ProofGateRevision==C->GateRevision&&C->ProofEpoch==S->GetNavigationEpoch();
    if(Q.bFullPath&&Q.bMoveVerified){Q.State=ERogueliteGameplayPassage::Traversable;Q.Reason=TEXT("Current two-way physical sweeps, full non-partial path and post-change actual corridor MoveTo verified.");}
    else {Q.State=ERogueliteGameplayPassage::Recovering;Q.Reason=TEXT("Open geometry requires fresh full path and actual MoveTo recovery proof.");}
    return Q;
}
bool FRogueliteDynamicGameplayBridge::BeginRecovery(ARogueliteRuntimeLoopAssembly* S,FGuid Id,APawn* Pawn,AAIController* Controller,FString& Why)
{
    using namespace DynamicBridge;if(!IsInGameThread()){Why=TEXT("Game thread required.");return false;}const auto C=Context(S);FPassageFrame F;
    FRogueliteGameplayObservationWrite ObservationWrite(C?C->World.Get():(IsValid(S)?S->AActor::GetWorld():nullptr),TEXT("G1A recovery request"));
    if(!Frame(C,Id,F)||!IsValid(Pawn)||!IsValid(Controller)||Controller->GetPawn()!=Pawn||Controller->GetOwner()!=S
        ||!IsComponentIsolated(S,C->Batch,Cast<UPrimitiveComponent>(Pawn->GetRootComponent())))
    {Why=TEXT("Recovery requires a registered pawn and its Source-owned controller.");return false;}
    const auto* Character=Cast<ACharacter>(Pawn);
    if(!Character||!Character->GetCharacterMovement()->IsMovingOnGround()
        ||!FMath::IsNearlyEqual(Character->GetCapsuleComponent()->GetScaledCapsuleRadius(),34.f,.01f)
        ||!FMath::IsNearlyEqual(Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight(),88.f,.01f))
    {Why=TEXT("Recovery currently requires the verified standing 34cm-radius/88cm-half-height walking character; flying and other agents are unsupported.");return false;}
    const auto Q=QueryConnection(S,Id,Pawn);
    if(!Q.bStructuralCurrent||!Q.bBaseNavigationReady||!Q.bForwardCapsule||!Q.bReverseCapsule||!Q.bFullPath)
    {Why=Q.Reason;return false;}
    const FVector Feet=Pawn->GetActorLocation()-FVector(0,0,88);
    if(FVector::Dist2D(Feet,F.Start)>60||FMath::Abs(Feet.Z-F.Start.Z)>50){Why=TEXT("Recovery pawn must already be at this connection's actual ground start; no teleport is performed.");return false;}
    Cancel(C);C->bMovePassed=false;C->ProofConnection=Id;C->ProofEpoch=S->GetNavigationEpoch();C->ProofGateRevision=C->GateRevision;
    C->Goal=F.End;C->MovingPawn=Pawn;C->Controller=Controller;C->Following=Controller->GetPathFollowingComponent();
    if(!C->Following.IsValid()){Why=TEXT("Path following unavailable.");return false;}
    C->CorridorPose=F.Corridor->GetActorTransform();C->CorridorHalf=FVector(F.Corridor->CorridorLength*.5,F.Corridor->CorridorWidth*.5,F.Corridor->CorridorHeight*.5);
    C->Evidence=FString::Printf(TEXT("Batch=%s\nConnection=%s\nStart=%s\nGoal=%s\n"),*C->Batch.ToString(),*Id.ToString(),*Feet.ToString(),*F.End.ToString());
    TWeakPtr<FContext> Weak=C;
    C->Completion=C->Following->OnRequestFinished.AddLambda([Weak](FAIRequestID Request,const FPathFollowingResult& Result)
    {
        const auto X=Weak.Pin();if(!X||!X->bMoving||Request!=X->Request)return;
        PollRecovery(Weak);const FVector Actual=X->MovingPawn.IsValid()?X->MovingPawn->GetActorLocation()-FVector(0,0,88):FVector(MAX_flt);
        X->bMovePassed=Identity(*X)&&Result.IsSuccess()&&X->bCrossedInterior&&FVector::Dist2D(Actual,X->Goal)<=40
            &&FMath::Abs(Actual.Z-X->Goal.Z)<50&&X->ProofGateRevision==X->GateRevision;
        X->Evidence+=FString::Printf(TEXT("Result=%s\nActualFeet=%s\nSeconds=%.3f\nCrossedInterior=%d\nMovePassed=%d\n"),*Result.ToString(),*Actual.ToString(),FPlatformTime::Seconds()-X->MoveStarted,X->bCrossedInterior,X->bMovePassed);
        X->bMoving=false;if(auto* W=X->World.Get())W->GetTimerManager().ClearTimer(X->Poll);
    });
    FAIMoveRequest Move(F.End);Move.SetAcceptanceRadius(30);Move.SetReachTestIncludesAgentRadius(false);Move.SetReachTestIncludesGoalRadius(false);Move.SetAllowPartialPath(false);Move.SetUsePathfinding(true);Move.SetProjectGoalLocation(false);
    C->bMoving=true;C->bCrossedInterior=false;C->MoveStarted=FPlatformTime::Seconds();const auto R=Controller->MoveTo(Move);
    if(R.Code!=EPathFollowingRequestResult::RequestSuccessful){Cancel(C);Why=TEXT("Fresh actual movement request not accepted.");return false;}
    C->Request=R.MoveId;C->World->GetTimerManager().SetTimer(C->Poll,FTimerDelegate::CreateWeakLambda(S,[Weak](){PollRecovery(Weak);}),.1,true);
    Why=TEXT("Actual post-open MoveTo started; connection remains Recovering until completion.");return true;
}
FString FRogueliteDynamicGameplayBridge::RecoveryEvidence(const ARogueliteRuntimeLoopAssembly* S)
{if(!IsInGameThread())return TEXT("Game thread required.");const auto C=DynamicBridge::Context(S);return C&&DynamicBridge::Identity(*C)?C->Evidence:TEXT("No current recovery evidence.");}
