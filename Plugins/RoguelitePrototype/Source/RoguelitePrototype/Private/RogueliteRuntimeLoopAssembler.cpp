#include "RogueliteRuntimeLoopAssembly.h"
#include "RogueliteSeedAssemblyInternal.h"
#include "RogueliteGenerationGeometry.h"
#include "RogueliteBranchingTestSettings.h"
#include "RoguelitePrototype.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Components/BoxComponent.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "ProfilingDebugging/CountersTrace.h"

TRACE_DECLARE_INT_COUNTER(PerfTraversalStrongCurrent,TEXT("PERF1D1.StrongCurrent"));

namespace
{
using namespace RogueliteSeedAssemblyInternal;
bool Game(AActor* A){return IsValid(A)&&A->GetWorld()&&A->GetWorld()->IsGameWorld()&&!A->GetWorld()->bIsTearingDown;}
int32 Actors(UWorld* W){int32 N=0;for(TActorIterator<AActor> I(W);I;++I)N+=!I->IsActorBeingDestroyed();return N;}
ARogueliteRoomTemplate* Room(const FRogueliteSeedBatch& B,int32 N)
{const auto* R=B.Rooms.FindByPredicate([&](const auto& X){return X.NodeId==N;});return R?R->Room.Get():nullptr;}
}

FRogueliteSeedLayoutSettings FRogueliteRuntimeLoopAssembler::SourcePreset(int32 Seed,bool Small)
{
    auto S=RogueliteBranchingTestSettings(Seed);
    if(Small)
    {
        S.MinBranches=S.MaxBranches=1;S.MinBranchDepth=S.MaxBranchDepth=1;S.MinNodes=S.MaxNodes=4;
        S.MinDecisionNodes=S.MaxDecisionNodes=1;S.MinNestedDecisionNodes=0;
        for(auto& R:S.TypeAssignment.Rules)
        {
            R.MinCount=R.MaxCount=R.Weight=0;
            if(R.Type==ERogueliteSeedRoomType::Combat){R.MinCount=2;R.MaxCount=16;R.Weight=1;}
            if(R.Type==ERogueliteSeedRoomType::Final){R.MinCount=R.MaxCount=R.Weight=1;}
        }
    }
    return S;
}

bool FRogueliteRuntimeLoopAssembler::Clear(AActor* Owner,FRogueliteSeedBatch& B,FRogueliteCommittedLoopBatch& L,FString& Reason)
{
    if(!Game(Owner)){Reason=TEXT("Runtime batch cleanup requires its current GameWorld owner.");return false;}
    if(IsValid(B.Registry)&&(B.Registry->GetOwner()!=Owner||B.Registry->GetWorld()!=Owner->GetWorld()))
    {Reason=TEXT("Foreign registry/World protected; no cleanup performed.");return false;}
    if(!L.Edges.IsEmpty()&&(!L.bRuntimePrepared||L.SourceBatchId!=B.BatchId||!IsValid(B.Registry)))
    {Reason=TEXT("Runtime loop source identity changed; refuse foreign cleanup.");return false;}
    for(const auto& E:L.Edges)
        if(IsValid(E.Corridor)&&(E.Corridor->GetOwner()!=B.Registry||E.Corridor->GetWorld()!=Owner->GetWorld()||!E.Corridor->ActorHasTag(BatchTag(B.BatchId))))
        {Reason=TEXT("Runtime loop actor ownership changed; explicit recovery required.");return false;}
    // Perform the existing ownership/attachment/POI guards before releasing any lease.
    TSet<AActor*> Owned;for(auto A:B.OwnedActors)if(IsValid(A))Owned.Add(A);
    for(auto A:Owned)
    {
        if(A->GetWorld()!=Owner->GetWorld()||A->GetOwner()!=Owner||!A->ActorHasTag(BatchTag(B.BatchId)))
        {Reason=TEXT("Runtime Tree ownership changed; cleanup left intact.");return false;}
        if(const auto* R=Cast<ARogueliteParametricRoom>(A);R&&!R->ManualPOIActors.IsEmpty())
        {Reason=TEXT("Independent ManualPOIActors protected; cleanup left intact.");return false;}
        TArray<AActor*> Children;A->GetAttachedActors(Children);
        for(auto Child:Children)if(!Owned.Contains(Child))
        {Reason=TEXT("External attachment protected; cleanup left intact.");return false;}
    }
    for(TActorIterator<ARogueliteRoomAssembly> I(Owner->GetWorld());I;++I)
        for(const auto& C:I->GetConnectionRecords())
            if(C.bActive&&(Owned.Contains(C.RoomA)||Owned.Contains(C.RoomB)||Owned.Contains(C.Corridor)))
                if(*I!=B.Registry||(!B.Edges.ContainsByPredicate([&](const auto& E){return E.ConnectionId==C.Id;})
                    &&!L.Edges.ContainsByPredicate([&](const auto& E){return E.ConnectionId==C.Id;})))
                {Reason=TEXT("External connection protected; cleanup left intact.");return false;}
    for(const auto& E:L.Edges)
    {
        if(!B.Registry->ReleaseRuntimePlacedConnection(E.ConnectionId,B.BatchId))return false;
        if(IsValid(E.Corridor))E.Corridor->Destroy();
    }
    L.Edges.Reset();++L.GraphRevision;++L.GeometryRevision;L.GeometryDigest.Reset();L.SourceBatchId.Invalidate();
    if(IsValid(B.Registry))for(const auto& E:B.Edges)
        if(!B.Registry->ReleaseRuntimePlacedConnection(E.ConnectionId,B.BatchId))return false;
    // All runtime leases already released without rebuilding registered Static shells.
    return FRogueliteSeedLayoutAssembler::Clear(Owner,B,Reason);
}

bool FRogueliteRuntimeLoopAssembler::Current(AActor* Owner,const FRogueliteSeedBatch& B,const FRogueliteSeedLayoutSettings& S,const FRogueliteCommittedLoopBatch& L)
{
    return CurrentWithTraversal(Owner,B,S,L,nullptr);
}

bool FRogueliteRuntimeLoopAssembler::CurrentWithTraversal(AActor* Owner,const FRogueliteSeedBatch& B,
    const FRogueliteSeedLayoutSettings& S,const FRogueliteCommittedLoopBatch& L,FRogueliteLoopTraversalSnapshot* OutTraversal)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(PERF1D1_StrongCurrent);
    TRACE_COUNTER_INCREMENT(PerfTraversalStrongCurrent);
    if(!Game(Owner)||!L.bRuntimePrepared||!B.bSucceeded||!B.bGeometryValid)return false;
    for(const auto& R:B.Rooms)
    {
        if(!IsValid(R.Room)||R.Room->GetOwner()!=Owner||R.Room->GetWorld()!=Owner->GetWorld()
            ||!R.Room->GetActorTransform().Equals(R.PlacedTransform,.0001)||!R.Room->GetActorScale3D().Equals(FVector::OneVector,.0001)
            ||!FVector(R.Room->RoomLength,R.Room->RoomWidth,R.Room->RoomHeight).Equals(R.Dimensions,.0001)
            ||R.Room->WallThickness!=R.WallThickness||R.Room->FloorThickness!=R.FloorThickness
            ||!R.Room->RuntimeRegistrationAudit.Passed(R.Room)||MembersText(R.Room)!=R.MemberSignature)return false;
        int32 Extra=0;for(const auto& E:L.Edges)Extra+=(E.NodeA==R.NodeId||E.NodeB==R.NodeId);
        if(R.Room->GetDoorSockets().Num()!=R.LogicalDegree+Extra)return false;
    }
    for(const auto& E:B.Edges)if(!IsValid(E.Corridor)||!E.Corridor->RuntimeRegistrationAudit.Passed(E.Corridor))return false;
    for(const auto& E:L.Edges)if(!IsValid(E.Corridor)||!E.Corridor->RuntimeRegistrationAudit.Passed(E.Corridor))return false;
    auto Traversal=FRogueliteLoopCommitQueries::Capture(Owner,B,S,L);
    if(!Traversal.bCurrent)return false;
    if(OutTraversal){TRACE_CPUPROFILER_EVENT_SCOPE(PERF1D1_CaptureMoveToProof);*OutTraversal=MoveTemp(Traversal);}
    return true;
}

bool FRogueliteRuntimeLoopAssembler::Revalidate(AActor* Owner,FRogueliteSeedBatch& B,const FRogueliteSeedLayoutSettings& S,
    FRogueliteCommittedLoopBatch& L,FString& Reason)
{
    if(!Game(Owner)||!L.bRuntimePrepared||L.SourceBatchId!=B.BatchId||!IsValid(B.Registry))return false;
    bool Good=true;
    for(auto& E:B.Edges){E.Validation=B.Registry->RevalidateConnection(E.ConnectionId);Good&=E.Validation.bSucceeded&&E.Validation.bGeometryPassed&&E.Validation.bCapsulePassed&&E.Validation.Probes.Num()==59;}
    for(auto& E:L.Edges){E.Validation=B.Registry->RevalidateConnection(E.ConnectionId);Good&=E.Validation.bSucceeded&&E.Validation.bGeometryPassed&&E.Validation.bCapsulePassed&&E.Validation.Probes.Num()==59;}
    B.bGeometryValid=Good;B.Navigation=ERogueliteNavigationCheck::NotChecked;++L.GeometryRevision;
    L.GeometryDigest=Good?FRogueliteLoopCommitQueries::GeometryDigest(Owner,B,L):FString();
    Good=Good&&Current(Owner,B,S,L);Reason=Good?TEXT("Actual Runtime Tree+Loop C2 revalidated; strict navigation must run again."):TEXT("Runtime actual geometry/C2/ports no longer valid.");return Good;
}

bool FRogueliteRuntimeLoopAssembler::RevalidateConfigured(AActor* Owner,FRogueliteSeedBatch& B,const FRogueliteSeedLayoutSettings& S,
    FRogueliteCommittedLoopBatch& L,FString& Reason)
{
    if(!Game(Owner)||!L.bRuntimePrepared||L.SourceBatchId!=B.BatchId||!IsValid(B.Registry))
    {Reason=TEXT("Configured runtime World/batch/registry unavailable; no geometry changed.");return false;}
    bool Good=true;
    for(auto& E:B.Edges){E.Validation=B.Registry->RevalidateConfiguredConnection(E.ConnectionId);Good&=E.Validation.bSucceeded&&E.Validation.bGeometryPassed&&E.Validation.bCapsulePassed;}
    for(auto& E:L.Edges){E.Validation=B.Registry->RevalidateConfiguredConnection(E.ConnectionId);Good&=E.Validation.bSucceeded&&E.Validation.bGeometryPassed&&E.Validation.bCapsulePassed;}
    B.bGeometryValid=Good;B.Navigation=ERogueliteNavigationCheck::NotChecked;++L.GeometryRevision;
    L.GeometryDigest=Good?FRogueliteLoopCommitQueries::GeometryDigest(Owner,B,L):FString();
    Good=Good&&Current(Owner,B,S,L);
    Reason=Good?TEXT("Configured Runtime Tree+Loop actual C2 probes revalidated without rebuilding registered Static geometry; strict navigation must run again."):TEXT("Configured runtime C2/geometry/registration audit failed; no Static geometry rebuilt.");
    B.Reason=Reason;return Good;
}

bool FRogueliteRuntimeLoopAssembler::Generate(AActor* Owner,const FRogueliteSeedLayoutSettings& S,const FRogueliteLoopAwareSettings& Planning,
    const FRoguelitePreparedTemplateCatalog& Catalog,FRogueliteSeedBatch& B,FRogueliteCommittedLoopBatch& L,
    FRogueliteRuntimeLoopResult& R,ERogueliteRuntimeLoopFault Fault)
{
    R={};R.Seed=S.Seed;const double Start=FPlatformTime::Seconds();ARogueliteParametricCorridor* PendingLoop=nullptr;
    const auto Log=[&](){R.GeometrySeconds=FPlatformTime::Seconds()-Start;UE_LOG(LogRoguelitePrototype,Display,TEXT("B4B3_GEOMETRY Seed=%d Stage=%s Success=%d ProbeFree=%d Registration=%d Tree=%d Loop=%d Cleanup=%d Reason=%s"),S.Seed,*R.Stage,R.bSucceeded,R.bProbeFreePlanning,R.bRegistrationPassed,B.Edges.Num(),L.Edges.Num(),R.bCleanupComplete,*R.Reason);};
    if(!Game(Owner)||!B.OwnedActors.IsEmpty()||!L.Edges.IsEmpty())
    {R.Stage=TEXT("InvalidOrExistingBatch");R.Reason=TEXT("Live GameWorld and empty owned batch required; existing actors/leases unchanged.");Log();return false;}
    if(Fault!=ERogueliteRuntimeLoopFault::None&&!Owner->ActorHasTag(TEXT("RogueliteRuntimeLoopTestOnly")))
    {R.Stage=TEXT("InvalidTestFault");R.Reason=TEXT("Controlled fault requires the explicit test owner.");Log();return false;}
    const auto Fail=[&](const FString& Stage,const FString& Why)
    {
        R.Stage=Stage;R.Reason=Why;R.bSucceeded=false;
        if(IsValid(PendingLoop))PendingLoop->Destroy();FString Cleanup;R.bCleanupComplete=Clear(Owner,B,L,Cleanup);R.Reason+=TEXT(" | ")+Cleanup;Log();return false;
    };
    const int32 Before=Actors(Owner->GetWorld());int32 PlanningSpawns=0;
    auto SpawnObserver=FOnActorSpawned::FDelegate::CreateLambda([&](AActor*){++PlanningSpawns;});
    const auto Handle=Owner->GetWorld()->AddOnActorSpawnedHandler(SpawnObserver);
    auto P=FRogueliteLoopAwarePlanner::PlanRuntime(Owner,S,Planning,Owner->GetActorTransform(),Catalog);
    Owner->GetWorld()->RemoveOnActorSpawnedHandler(Handle);
    R.bProbeFreePlanning=PlanningSpawns==0&&Actors(Owner->GetWorld())==Before;R.Plan=P.Evidence;
    if(!P.Tree.bValid||!R.bProbeFreePlanning)return Fail(TEXT("PlanningFailed"),P.Evidence.Reason);
    FString Why;if(!FRogueliteLoopAwarePlanner::IsRuntimeCurrent(Owner,S,Planning,P,Catalog,Why))return Fail(TEXT("StalePlan"),Why);
    *P.Unconsumed=false;
    auto Final=P.Tree;
    for(const auto& I:P.Evidence.Intents)if(I.bSpatiallyFeasible)
    {
        auto* A=Final.Rooms.FindByPredicate([&](const auto& X){return X.Record.NodeId==I.NodeA;});
        auto* Target=Final.Rooms.FindByPredicate([&](const auto& X){return X.Record.NodeId==I.NodeB;});
        if(!A||!Target)return Fail(TEXT("PlanningFailed"),TEXT("Final loop endpoint mapping absent."));
        A->UsedSockets.AddUnique(I.SocketA);Target->UsedSockets.AddUnique(I.SocketB);
    }
    Final.Metadata.ConfigurationDigest=FRogueliteRoomGraphGenerator::StableTextDigest(P.SourceCatalogDigest+TEXT("|Spatial2|")+P.Evidence.StablePlanDigest);
    B=FRogueliteRuntimeSeedAssembler::SpawnPlannedActors(Owner,Final);
    if(B.bCleanupComplete||!IsValid(B.Registry))return Fail(TEXT("ActorsSpawnFailed"),B.Reason);
    L={};L.bRuntimePrepared=true;L.SourceBatchId=B.BatchId;L.BaseGraphDigest=B.GraphDigest;
    R.bRegistrationPassed=true;
    for(const auto& X:B.Rooms){R.Registration.Add(X.Room->RuntimeRegistrationAudit.Describe(X.Room));R.Registration.Append(X.Room->RuntimeOwnedRegistration);R.bRegistrationPassed&=X.Room->RuntimeRegistrationAudit.Passed(X.Room)&&X.Room->bRuntimeOwnedRegisteredBeforeBeginPlay;}
    for(const auto& X:B.Edges){R.Registration.Add(X.Corridor->RuntimeRegistrationAudit.Describe(X.Corridor));R.bRegistrationPassed&=X.Corridor->RuntimeRegistrationAudit.Passed(X.Corridor);}
    if(!R.bRegistrationPassed)return Fail(TEXT("StaticRegistrationFailed"),TEXT("Final native geometry was not prepared before first registration or changed after construction."));
    R.Stage=TEXT("GeometryPending");
    for(auto& E:B.Edges)
    {
        E.Validation=RogueliteGenerationGeometry::IsLegacyDefaults(S)?B.Registry->TryRegisterRuntimePlacedConnection(Room(B,E.ParentNode),E.ParentSocket,E.Corridor,Room(B,E.ChildNode),E.ChildSocket,B.BatchId):B.Registry->TryRegisterConfiguredPlacedConnection(Room(B,E.ParentNode),E.ParentSocket,E.Corridor,Room(B,E.ChildNode),E.ChildSocket,B.BatchId);
        E.ConnectionId=E.Validation.ConnectionId;R.Connections.Add(E.Validation);
        if(!E.Validation.bSucceeded)return Fail(TEXT("TreeC2Rejected"),E.Validation.Message);
    }
    for(const auto& I:P.Evidence.Intents)if(I.bSpatiallyFeasible)
    {
        const auto* Profile=S.Corridors.FindByPredicate([&](const auto& X){return X.Id==I.CorridorProfile;});
        if(!Profile)return Fail(TEXT("LoopSpawnFailed"),TEXT("Validated loop profile disappeared."));
        FActorSpawnParameters Args;Args.Owner=B.Registry;Args.bDeferConstruction=true;Args.ObjectFlags|=RF_Transient;
        Args.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;Args.TransformScaleMethod=ESpawnActorScaleMethod::OverrideRootScale;
        const auto Values=*Profile;const float Length=I.CorridorLength;
        Args.CustomPreSpawnInitialization=[Values,Length](AActor* A){auto* C=CastChecked<ARogueliteParametricCorridor>(A);C->CorridorLength=Length;C->CorridorWidth=Values.Width;C->CorridorHeight=Values.Height;C->WallThickness=Values.WallThickness;C->FloorThickness=Values.FloorThickness;C->DoorWidth=Values.DoorWidth;C->DoorHeight=Values.DoorHeight;C->bGenerateCeiling=Values.bGenerateCeiling;};
        PendingLoop=Owner->GetWorld()->SpawnActor<ARogueliteParametricCorridor>(ARogueliteParametricCorridor::StaticClass(),I.CorridorTransform,Args);
        if(!PendingLoop)return Fail(TEXT("LoopSpawnFailed"),TEXT("Actual runtime loop allocation failed."));
        PendingLoop->Tags.Add(TEXT("RogueliteFixedLoopOwned"));PendingLoop->Tags.Add(BatchTag(B.BatchId));PendingLoop->FinishSpawning(I.CorridorTransform);
        R.Registration.Add(PendingLoop->RuntimeRegistrationAudit.Describe(PendingLoop));
        R.bRegistrationPassed&=PendingLoop->RuntimeRegistrationAudit.Passed(PendingLoop)&&PendingLoop->GetActorTransform().Equals(I.CorridorTransform,.0001);
        if(!R.bRegistrationPassed)return Fail(TEXT("StaticRegistrationFailed"),TEXT("Runtime loop first registration differs from complete plan."));
        if(Fault==ERogueliteRuntimeLoopFault::CancelLoopSpawn)return Fail(TEXT("CancelledLoopSpawn"),TEXT("Controlled cancellation after actual deferred loop spawn; no loop lease published."));
        if(Fault==ERogueliteRuntimeLoopFault::RejectLoopC2)
        {
            // A real independent physical blocker, deliberately outside OwnedActors/BatchTag.
            auto* Obstacle=Owner->GetWorld()->SpawnActor<AActor>();auto* Box=NewObject<UBoxComponent>(Obstacle);
            Obstacle->SetRootComponent(Box);Box->SetBoxExtent(FVector(80,600,180));Box->SetCollisionProfileName(TEXT("BlockAll"));Box->SetMobility(EComponentMobility::Static);
            Obstacle->SetActorLocation(I.CorridorTransform.GetLocation()+FVector(0,0,150));Box->RegisterComponent();
            R.TestObstacle=Obstacle;
        }
        const auto Validation=RogueliteGenerationGeometry::IsLegacyDefaults(S)?B.Registry->TryRegisterRuntimePlacedConnection(Room(B,I.NodeA),I.SocketA,PendingLoop,Room(B,I.NodeB),I.SocketB,B.BatchId,I.StableId):B.Registry->TryRegisterConfiguredPlacedConnection(Room(B,I.NodeA),I.SocketA,PendingLoop,Room(B,I.NodeB),I.SocketB,B.BatchId,I.StableId);
        R.Connections.Add(Validation);if(!Validation.bSucceeded)return Fail(TEXT("LoopC2Rejected"),Validation.Message);
        auto& Edge=L.Edges.AddDefaulted_GetRef();Edge.CandidateId=I.StableId;Edge.EdgeId=B.Edges.Num();Edge.NodeA=I.NodeA;Edge.NodeB=I.NodeB;Edge.SocketA=I.SocketA;Edge.SocketB=I.SocketB;
        Edge.ConnectionId=Validation.ConnectionId;Edge.ProfileId=I.CorridorProfile;Edge.Corridor=PendingLoop;Edge.PlanDigest=P.Evidence.StablePlanDigest;Edge.Validation=Validation;
        PendingLoop=nullptr;++L.GraphRevision;++L.GeometryRevision;
        if(Fault==ERogueliteRuntimeLoopFault::CancelAfterCommit)return Fail(TEXT("CancelledAfterLoopCommit"),TEXT("Controlled cancellation after actual C2/overlay publication; whole batch revoked."));
    }
    if(L.Edges.Num()<Planning.MinLoops)return Fail(TEXT("LoopMinimumUnsatisfied"),TEXT("Hard loop minimum retained; no downgrade."));
    B.bSucceeded=B.bGeometryValid=true;B.LayoutDigest=FRogueliteRoomGraphGenerator::StableTextDigest(FRogueliteSeedLayoutAssembler::DescribeLayout(B));
    L.BaseLayoutDigest=B.LayoutDigest;L.GeometryDigest=FRogueliteLoopCommitQueries::GeometryDigest(Owner,B,L);
    R.Traversal=FRogueliteLoopCommitQueries::Capture(Owner,B,S,L);
    if(!Current(Owner,B,S,L)||R.Traversal.IndependentCycles!=L.Edges.Num()||B.Registry->GetConnectionRecords().Num()!=B.Edges.Num()+L.Edges.Num())
        return Fail(TEXT("TraversalRejected"),R.Traversal.Reason);
    for(const auto& X:B.Rooms)R.ActualTransforms.Add(FString::Printf(TEXT("Node=%d Template=%s Actual=%s Dimensions=%s"),X.NodeId,*X.TemplateId.ToString(),*X.Room->GetActorTransform().ToString(),*X.Dimensions.ToString()));
    for(const auto& X:B.Edges)R.ActualTransforms.Add(FString::Printf(TEXT("TreeEdge=%d Actual=%s LWH=%.3f,%.3f,%.3f"),X.EdgeId,*X.Corridor->GetActorTransform().ToString(),X.Corridor->CorridorLength,X.Corridor->CorridorWidth,X.Corridor->CorridorHeight));
    for(const auto& X:L.Edges)R.ActualTransforms.Add(FString::Printf(TEXT("LoopEdge=%d Actual=%s LWH=%.3f,%.3f,%.3f"),X.EdgeId,*X.Corridor->GetActorTransform().ToString(),X.Corridor->CorridorLength,X.Corridor->CorridorWidth,X.Corridor->CorridorHeight));
    for(const auto& X:B.Registry->GetConnectionRecords())R.ConnectionIds.Add(X.Id);
    R.Plan.LayoutDigest=B.LayoutDigest;R.Plan.TraversalDigest=R.Traversal.StableDigest;R.Plan.bTreeBuilt=R.Plan.bC2Validated=true;
    R.Plan.CommittedLoops=L.Edges.Num();R.Plan.IndependentCycles=R.Traversal.IndependentCycles;
    R.bSucceeded=true;R.Stage=L.Edges.IsEmpty()?TEXT("RuntimeGeometryValidated"):TEXT("RuntimeLoopCommitted");
    R.Reason=TEXT("Actual GameWorld final geometry/C2/overlay passed. NavigationPending; no DungeonReady claim.");Log();return true;
}
