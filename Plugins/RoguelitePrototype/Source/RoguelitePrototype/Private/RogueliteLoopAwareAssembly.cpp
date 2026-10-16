#include "RogueliteLoopAwarePlanning.h"
#include "RogueliteSeedAssemblyInternal.h"
#include "RoguelitePrototype.h"
#include "Engine/World.h"

namespace
{
using G=FRogueliteRoomGraphGenerator;
using namespace RogueliteSeedAssemblyInternal;
bool Editor(AActor* Owner){return IsValid(Owner)&&Owner->GetWorld()&&Owner->GetWorld()->WorldType==EWorldType::Editor;}
template<class T> T* Deferred(AActor* Owner,UClass* Class,const FTransform& Transform,const FGuid& Id,TFunction<void(AActor*)> Configure={})
{
    FActorSpawnParameters P;P.Owner=Owner;P.bDeferConstruction=true;P.ObjectFlags|=RF_Transactional;P.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;P.TransformScaleMethod=ESpawnActorScaleMethod::OverrideRootScale;
    P.CustomPreSpawnInitialization=MoveTemp(Configure);auto* A=Owner->GetWorld()->SpawnActor<T>(Class,Transform,P);if(A)A->Tags.Add(BatchTag(Id));return A;
}
bool SameBox(const FBox& A,const FBox& B){return A.IsValid&&B.IsValid&&A.Min.Equals(B.Min,.01)&&A.Max.Equals(B.Max,.01);}
}

bool FRogueliteLoopAwarePlanner::Clear(AActor* Owner,FRogueliteSeedBatch& B,FRogueliteCommittedLoopBatch& L,FString& Reason)
{
    if(!Editor(Owner)){Reason=TEXT("EditorOnly: runtime loop teardown belongs to B4-B3.");return false;}
    if(!FRogueliteLoopCommitQueries::Clear(Owner,B,L,Reason))return false;
    return FRogueliteSeedLayoutAssembler::Clear(Owner,B,Reason);
}
bool FRogueliteLoopAwarePlanner::MaterializeEditor(AActor* Owner,const FRogueliteSeedLayoutSettings& Source,const FRogueliteLoopAwareSettings& S,
    const FRogueliteLoopAwareSpatialPlan& P,FRogueliteSeedBatch& B,FRogueliteCommittedLoopBatch& L,FRogueliteLoopAwareEvidence& E,ERogueliteLoopAwareTestFault Fault)
{
    E=P.Evidence;E.bSucceeded=false;E.bCleanupComplete=false;
    const auto Log=[&](){UE_LOG(LogRoguelitePrototype,Display,TEXT("LOOP_AWARE_RESULT Seed=%d SpatialVersion=%d Stage=%s Success=%d Rooms=%d TreeEdges=%d Loops=%d Cycles=%d Clean=%d Plan=%s Layout=%s Reason=%s"),E.Seed,E.SpatialPlanningVersion,*E.Stage,E.bSucceeded,E.RoomCount,E.TreeEdgeCount,E.CommittedLoops,E.IndependentCycles,E.bCleanupComplete,*E.StablePlanDigest,*E.LayoutDigest,*E.Reason);};
    if(!Editor(Owner)||!B.OwnedActors.IsEmpty()||!L.Edges.IsEmpty())
    {E.Stage=TEXT("ExistingBatchOrWorldProtected");E.Reason=TEXT("Editor-only empty batch required; existing layout was not moved or cleaned.");Log();return false;}
    FString Reason;if(!IsCurrent(Owner,Source,S,P,Reason)){E.Stage=TEXT("StalePlan");E.Reason=Reason;E.bCleanupComplete=true;Log();return false;}
    if(Fault!=ERogueliteLoopAwareTestFault::None&&!Owner->ActorHasTag(TEXT("RogueliteLoopAwareTestOnly")))
    {E.Stage=TEXT("InvalidTestInjection");E.Reason=TEXT("Fault injection requires the independent test rig tag.");Log();return false;}
    *P.Unconsumed=false;B=P.Tree.Metadata;B.ConfigurationDigest=G::StableTextDigest(P.SourceCatalogDigest+TEXT("|Spatial2|")+E.StablePlanDigest);L=FRogueliteCommittedLoopBatch();
    const auto Fail=[&](const FString& Stage,const FString& Why)
    {
        E.Stage=Stage;E.Reason=Why;E.bSucceeded=false;FString Cleanup;E.bCleanupComplete=Clear(Owner,B,L,Cleanup);E.Reason+=TEXT(" | ")+Cleanup;
        if(E.bCleanupComplete){E.Attempts.Add(FString::Printf(TEXT("RollbackComplete PublishedLoopsRemoved=%d"),E.CommittedLoops));E.CommittedLoops=E.IndependentCycles=0;E.bC2Validated=false;E.TraversalDigest.Reset();}Log();return false;
    };
    auto* Registry=Deferred<ARogueliteRoomAssembly>(Owner,ARogueliteRoomAssembly::StaticClass(),FTransform::Identity,B.BatchId);
    if(!Registry)return Fail(TEXT("TreeEntitySpawnFailed"),TEXT("Registry allocation failed."));
    B.Registry=Registry;B.OwnedActors.Add(Registry);Registry->bCheckNavigation=false;Registry->FinishSpawning(FTransform::Identity);
    // Editor adapter of the existing B3A spatial-plan contract. No new graph/random algorithm.
    // Every formal Actor receives final dimensions, ports and Transform before FinishSpawning.
    for(const auto& R:P.Tree.Rooms)
    {
        auto* A=Deferred<ARogueliteRoomTemplate>(Owner,R.Record.TemplateClass.Get(),R.Record.PlacedTransform,B.BatchId,[R](AActor* Actor)
        {
            auto* Room=CastChecked<ARogueliteRoomTemplate>(Actor);Room->RoomLength=R.Record.Dimensions.X;Room->RoomWidth=R.Record.Dimensions.Y;Room->RoomHeight=R.Record.Dimensions.Z;Room->WallThickness=R.Record.WallThickness;Room->FloorThickness=R.Record.FloorThickness;
            Room->NorthDoor.bEnabled=R.UsedSockets.Contains(ERogueliteRoomSide::North);Room->SouthDoor.bEnabled=R.UsedSockets.Contains(ERogueliteRoomSide::South);Room->EastDoor.bEnabled=R.UsedSockets.Contains(ERogueliteRoomSide::East);Room->WestDoor.bEnabled=R.UsedSockets.Contains(ERogueliteRoomSide::West);
        });
        if(!A)return Fail(TEXT("TreeEntitySpawnFailed"),FString::Printf(TEXT("Node=%d deferred room allocation failed."),R.Record.NodeId));
        B.OwnedActors.Add(A);const auto Initial=A->GetActorTransform();A->FinishSpawning(R.Record.PlacedTransform);const auto Valid=A->ValidateTemplate();
        if(!Initial.Equals(R.Record.PlacedTransform,.0001)||!A->GetActorTransform().Equals(R.Record.PlacedTransform,.0001)||!Valid.bValid||Valid.bDeferredContent||MembersText(A)!=R.Record.MemberSignature||!FVector(A->RoomLength,A->RoomWidth,A->RoomHeight).Equals(R.Record.Dimensions,.0001)||A->WallThickness!=R.Record.WallThickness||A->FloorThickness!=R.Record.FloorThickness||A->GetDoorSockets().Num()!=R.UsedSockets.Num())
            return Fail(TEXT("AuthoredTemplateMismatch"),FString::Printf(TEXT("Node=%d actual template/content/ports/pose differ from plan; no relocation attempted."),R.Record.NodeId));
        auto Row=R.Record;Row.Room=A;B.Rooms.Add(Row);
    }
    for(const auto& R:P.Tree.Edges)
    {
        auto* C=Deferred<ARogueliteParametricCorridor>(Owner,ARogueliteParametricCorridor::StaticClass(),R.Transform,B.BatchId,[R](AActor* Actor)
        {auto* C=CastChecked<ARogueliteParametricCorridor>(Actor);C->CorridorLength=R.Record.CorridorLength;C->CorridorWidth=R.Profile.Width;C->CorridorHeight=R.Profile.Height;C->WallThickness=R.Profile.WallThickness;C->FloorThickness=R.Profile.FloorThickness;C->DoorWidth=R.Profile.DoorWidth;C->DoorHeight=R.Profile.DoorHeight;});
        if(!C)return Fail(TEXT("TreeEntitySpawnFailed"),TEXT("Corridor deferred allocation failed."));B.OwnedActors.Add(C);const auto Initial=C->GetActorTransform();C->FinishSpawning(R.Transform);
        if(!Initial.Equals(R.Transform,.0001)||!C->GetActorTransform().Equals(R.Transform,.0001)||!SameBox(C->GetWorldOccupancyBounds(),R.Occupancy))return Fail(TEXT("CorridorPlanMismatch"),TEXT("Actual C1 corridor bounds/pose differ from planned descriptor."));
        auto Row=R.Record;Row.Corridor=C;B.Edges.Add(Row);
    }
    E.Stage=TEXT("GeometryBuilt");
    for(auto& Edge:B.Edges)
    {
        auto* A=B.Rooms.FindByPredicate([&](const auto& R){return R.NodeId==Edge.ParentNode;});auto* Target=B.Rooms.FindByPredicate([&](const auto& R){return R.NodeId==Edge.ChildNode;});
        if(!A||!Target)return Fail(TEXT("TreeC2Failed"),TEXT("Missing real node mapping."));
        const auto BeforeA=A->Room->GetActorTransform(),BeforeB=Target->Room->GetActorTransform(),BeforeC=Edge.Corridor->GetActorTransform();
        Edge.Validation=Registry->TryAlignCorridorConnection(A->Room,Edge.ParentSocket,Edge.Corridor,ERogueliteCorridorEndpoint::Start,Target->Room,Edge.ChildSocket);Edge.ConnectionId=Edge.Validation.ConnectionId;
        if(!Edge.Validation.bSucceeded||!Edge.Validation.bGeometryPassed||!Edge.Validation.bCapsulePassed||Edge.Validation.Probes.Num()!=59||!A->Room->GetActorTransform().Equals(BeforeA,.0001)||!Target->Room->GetActorTransform().Equals(BeforeB,.0001)||!Edge.Corridor->GetActorTransform().Equals(BeforeC,.0001))
        {B.FailedEdge=Edge.EdgeId;B.FailureValidation=Edge.Validation;return Fail(TEXT("TreeC2Failed"),Edge.Validation.Message);}
    }
    if(!FRogueliteSeedLayoutAssembler::Revalidate(Owner,B))return Fail(TEXT("TreeC2Failed"),B.Reason);
    B.LayoutDigest=G::StableTextDigest(FRogueliteSeedLayoutAssembler::DescribeLayout(B));E.LayoutDigest=B.LayoutDigest;E.bTreeBuilt=true;
    L.SourceBatchId=B.BatchId;L.BaseGraphDigest=B.GraphDigest;L.BaseLayoutDigest=B.LayoutDigest;L.GeometryDigest=FRogueliteLoopCommitQueries::GeometryDigest(Owner,B,L);
    if(Fault==ERogueliteLoopAwareTestFault::CancelAfterTree)return Fail(TEXT("CancelledAfterTree"),TEXT("Controlled cancellation after actual Tree entities/C2, before any Loop commit."));
    if(E.FeasibleIntentCount)
    {
        auto Settings=LoopSettings(S);Settings.TreeDistanceWeight=Settings.DifferentTypeWeight=Settings.LengthPerMeterPenalty=Settings.AlreadyOpenWeight=0;Settings.bOverrideLoopSeed=true;
        FRogueliteLoopPlan Actual;const auto& Intent=E.Intents[0];const FRogueliteLoopCandidate* Chosen=nullptr;
        // Explicit spatial priority through B1's existing independent ordering controls.
        // No caller-edited bSelected flags; B2A will recapture and recheck this exact plan.
        for(int32 Try=0;Try<S.MaxLoopIntentCandidates;++Try)
        {
            Settings.LoopSelectionSeed=SpatialSeed(Source.Seed,S.ConfigVersion,100000+Try);Actual=FRogueliteLoopPlanner::Analyze(Owner,B,Source,Settings);
            Chosen=Actual.Candidates.FindByPredicate([&](const auto& C){return C.bSelected&&C.NodeA==Intent.NodeA&&C.NodeB==Intent.NodeB&&C.SocketA==Intent.SocketA&&C.SocketB==Intent.SocketB&&C.Feasibility==ERogueliteLoopFeasibility::StraightPrecheckPassed;});
            E.Attempts.Add(FString::Printf(TEXT("ActualB1PriorityAttempt=%d Straight=%d PlannedPairSelected=%d"),Try,Actual.StraightPassedPairs,Chosen!=nullptr));if(Chosen)break;
            if(Actual.StraightPassedPairs==0)break;
        }
        if(!Chosen)return Fail(TEXT("ActualLoopPrecheckFailed"),Actual.Reason+TEXT(" Planned intent was not physically prechecked/selected within bounded B1 ordering budget."));
        E.PrecheckedCount=1;E.Stage=TEXT("SpatiallyFeasibleLoop");FRogueliteFixedLoopCommitRequest Q;Q.Owner=Owner;Q.SourceSettings=&Source;Q.LoopSettings=&Settings;Q.Plan=&Actual;Q.CandidateId=Chosen->CandidateId;Q.ExpectedGeometryRevision=L.GeometryRevision;
        const bool HadFaultTag=Owner->ActorHasTag(TEXT("RogueliteLoopCommitTestOnly"));
        if(Fault==ERogueliteLoopAwareTestFault::FailLoopCommit){Owner->Tags.AddUnique(TEXT("RogueliteLoopCommitTestOnly"));Q.TestFault=ERogueliteLoopCommitTestFault::Collision;}
        E.CommitResult=Registry->TryCommitFixedEndpointLoop(B,L,Q);
        if(Fault==ERogueliteLoopAwareTestFault::FailLoopCommit&&!HadFaultTag)Owner->Tags.Remove(TEXT("RogueliteLoopCommitTestOnly"));
        if(!E.CommitResult.bSucceeded)return Fail(TEXT("FixedLoopC2CommitFailed"),E.CommitResult.Reason);
        E.CommittedLoops=L.Edges.Num();E.bC2Validated=E.CommitResult.bC2Validated;
        if(Fault==ERogueliteLoopAwareTestFault::CancelAfterLoop)return Fail(TEXT("CancelledAfterLoop"),TEXT("Controlled cancellation after actual C2 Loop publication; loops-first owned rollback required."));
        const auto Snapshot=FRogueliteLoopCommitQueries::Capture(Owner,B,Source,L);E.IndependentCycles=Snapshot.IndependentCycles;E.TraversalDigest=Snapshot.StableDigest;
        if(!Snapshot.bCurrent||!Snapshot.bGeometryValidated||Snapshot.bNavigationReady||E.IndependentCycles!=1||Snapshot.Edges.Num()!=B.Rooms.Num())return Fail(TEXT("TraversalMismatch"),Snapshot.Reason);
    }
    else {E.bC2Validated=true;E.CommittedLoops=E.IndependentCycles=0;}
    if(E.CommittedLoops<S.MinLoops)return Fail(TEXT("LoopMinimumUnsatisfied"),TEXT("MinLoops was not reduced; whole batch rejected."));
    E.bSucceeded=true;E.Stage=E.CommittedLoops?TEXT("EditorCommittedLoop"):TEXT("EditorTreeGeometryValidated");E.Reason=TEXT("Actual Tree/C2 geometry validated; planned loop committed if requested. NavigationPending, never Runtime Navigation Ready.");Log();return true;
}
