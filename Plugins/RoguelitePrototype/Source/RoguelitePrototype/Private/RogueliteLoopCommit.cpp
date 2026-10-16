#include "RogueliteLoopCommit.h"
#include "RogueliteSeedAssemblyInternal.h"
#include "RoguelitePrototype.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#if WITH_EDITOR
#include "ScopedTransaction.h"
#endif

namespace
{
using G=FRogueliteRoomGraphGenerator;
const FName OwnedLoopTag(TEXT("RogueliteFixedLoopOwned"));
FRogueliteDoorConfig& Door(ARogueliteParametricRoom* R,ERogueliteRoomSide S)
{return S==ERogueliteRoomSide::North?R->NorthDoor:S==ERogueliteRoomSide::South?R->SouthDoor:S==ERogueliteRoomSide::East?R->EastDoor:R->WestDoor;}
FString RecordText(const FRogueliteRoomConnection& R)
{FString T;FRogueliteRoomConnection::StaticStruct()->ExportText(T,&R,nullptr,nullptr,PPF_None,nullptr);return T;}
ARogueliteRoomTemplate* Room(const FRogueliteSeedBatch& B,int32 Node)
{const auto* R=B.Rooms.FindByPredicate([&](const auto& X){return X.NodeId==Node;});return R?R->Room.Get():nullptr;}
bool Editor(AActor* A){return IsValid(A)&&A->GetWorld()&&A->GetWorld()->WorldType==EWorldType::Editor;}
bool Owns(const FRogueliteRoomConnection& R,const FRogueliteSeedBatch& B,const FRogueliteCommittedLoopEdge& E)
{return R.Id==E.ConnectionId&&R.bOwnedFixedLoop&&R.LoopSourceBatchId==B.BatchId&&R.LoopCandidateId==E.CandidateId&&R.Corridor==E.Corridor&&R.RoomA==Room(B,E.NodeA)&&R.RoomB==Room(B,E.NodeB)&&R.SocketA==E.SocketA&&R.SocketB==E.SocketB&&R.Mode==ERogueliteConnectionMode::StraightCorridor;}
}

void ARogueliteRoomAssembly::Destroyed()
{
    // Legacy BaseTree cleanup destroys its registry last. Never leave its owned loop orphaned.
    Connections.RemoveAll([](const auto& R){return R.bOwnedFixedLoop;});
    if(GetWorld())
    {
        TArray<ARogueliteParametricCorridor*> Owned;
        for(TActorIterator<ARogueliteParametricCorridor> I(GetWorld());I;++I)if(!I->IsActorBeingDestroyed()&&I->GetOwner()==this&&I->Tags.Contains(OwnedLoopTag))Owned.Add(*I);
        for(auto* C:Owned)C->Destroy(); // No Static geometry rebuild during PIE teardown.
    }
    Super::Destroyed();
}

FString FRogueliteLoopCommitQueries::GeometryDigest(AActor* Owner,const FRogueliteSeedBatch& B,const FRogueliteCommittedLoopBatch& L)
{
    if(!IsValid(Owner)||!Owner->GetWorld())return TEXT("NoWorld");TMap<AActor*,FString> Names;
    for(const auto& R:B.Rooms)if(IsValid(R.Room))Names.Add(R.Room,FString::Printf(TEXT("Node%d"),R.NodeId));
    for(const auto& E:B.Edges)if(IsValid(E.Corridor))Names.Add(E.Corridor,FString::Printf(TEXT("TreeEdge%d"),E.EdgeId));
    for(const auto& E:L.Edges)if(IsValid(E.Corridor))Names.Add(E.Corridor,TEXT("Loop:")+E.CandidateId.ToString());
    TArray<FString> Rows;
    for(TActorIterator<AActor> I(Owner->GetWorld());I;++I)if(!I->IsActorBeingDestroyed())
    {
        const FString* Known=Names.Find(*I);const FString Label=Known?*Known:TEXT("External");
        if(const auto* R=Cast<ARogueliteParametricRoom>(*I))
        {
            Rows.Add(Label+R->GetActorTransform().ToString());
            for(auto Side:{ERogueliteRoomSide::North,ERogueliteRoomSide::South,ERogueliteRoomSide::East,ERogueliteRoomSide::West})
            {const auto& D=Door(const_cast<ARogueliteParametricRoom*>(R),Side);FString T;FRogueliteDoorConfig::StaticStruct()->ExportText(T,&D,nullptr,nullptr,PPF_None,nullptr);Rows.Add(Label+FString::FromInt(uint8(Side))+T);}
        }
        TInlineComponentArray<UPrimitiveComponent*> Components;I->GetComponents(Components);
        for(const auto* C:Components)if(C->GetCollisionEnabled()!=ECollisionEnabled::NoCollision&&C->GetCollisionResponseToChannel(ECC_Pawn)==ECR_Block)
        {const auto* M=Cast<UStaticMeshComponent>(C);Rows.Add(Label+C->GetClass()->GetPathName()+C->GetComponentTransform().ToString()+C->Bounds.Origin.ToString()+C->Bounds.BoxExtent.ToString()+(M&&M->GetStaticMesh()?M->GetStaticMesh()->GetPathName():TEXT(""))+FString::FromInt(uint8(C->GetCollisionEnabled()))+FString::FromInt(I->GetActorEnableCollision())+FString::FromInt(uint8(C->GetCollisionResponseToChannel(ECC_Visibility)))+FString::FromInt(uint8(C->GetCollisionObjectType())));}
    }
    Rows.Sort();FString T;for(const auto& R:Rows)T+=R+TEXT("\n");return G::StableTextDigest(T);
}

FRogueliteFixedLoopCommitResult ARogueliteRoomAssembly::TryCommitFixedEndpointLoop(FRogueliteSeedBatch& B,
    FRogueliteCommittedLoopBatch& L,const FRogueliteFixedLoopCommitRequest& Q)
{
    FRogueliteFixedLoopCommitResult R;R.CandidateId=Q.CandidateId;
    const auto Finish=[&](const FString& Reason)
    {R.Reason=FString::Printf(TEXT("FixedLoop Batch=%s Candidate=%s: %s"),*B.BatchId.ToString(),*Q.CandidateId.ToString(),*Reason);R.Assembly.Message=R.Reason;PublishResult(R.Assembly);UE_LOG(LogRoguelitePrototype,Display,TEXT("LOOP_COMMIT Success=%d Stage=%d Rollback=%d TreeUnchanged=%d %s"),R.bSucceeded,uint8(R.Stage),R.bRollbackComplete,R.bOriginalTreeUnchanged,*R.Reason);return R;};
    if(!Editor(this)||!Editor(Q.Owner)||GetWorld()!=Q.Owner->GetWorld())return Finish(TEXT("EditorOnly: PIE/GameWorld Static geometry mutation is refused before any writes."));
    if(B.Registry!=this||GetOwner()!=Q.Owner||!Q.SourceSettings||!Q.LoopSettings||!Q.Plan)return Finish(TEXT("Invalid owned source registry/settings/plan."));
    if(L.Edges.ContainsByPredicate([&](const auto& E){return E.CandidateId==Q.CandidateId;}))return Finish(TEXT("DuplicateCommitRefused: no second corridor or reservation."));
    if(!L.Edges.IsEmpty())return Finish(TEXT("CommittedOverlayRequiresReplanning: B4-B1 remains Tree-only. Additional loop planning is a later adapter; no stale multi-loop reuse."));
    if((L.SourceBatchId.IsValid()&&L.SourceBatchId!=B.BatchId)||Q.ExpectedGeometryRevision!=L.GeometryRevision)return Finish(TEXT("Stale geometry revision or replacement batch."));
    if(Q.TestFault!=ERogueliteLoopCommitTestFault::None&&!Q.Owner->ActorHasTag(TEXT("RogueliteLoopCommitTestOnly")))return Finish(TEXT("Test injection is restricted to the explicit independent fixture."));
    FString Reason;if(!FRogueliteLoopPlanner::IsCurrent(Q.Owner,B,*Q.SourceSettings,*Q.LoopSettings,*Q.Plan,Reason))return Finish(Reason);
    // Do not trust caller-edited candidate fields. Requery the complete current source/space.
    const auto Fresh=FRogueliteLoopPlanner::Analyze(Q.Owner,B,*Q.SourceSettings,*Q.LoopSettings);
    if(Fresh.StableDigest!=Q.Plan->StableDigest)return Finish(TEXT("Plan summary changed; regenerate planning."));
    const auto* Chosen=Fresh.Candidates.FindByPredicate([&](const auto& C){return C.CandidateId==Q.CandidateId&&C.bSelected&&C.Feasibility==ERogueliteLoopFeasibility::StraightPrecheckPassed;});
    if(!Chosen)return Finish(TEXT("Candidate not selected/current StraightPrecheckPassed; no geometry writes."));
    const auto C=*Chosen;auto* A=Room(B,C.NodeA);auto* Target=Room(B,C.NodeB);
    if(!IsValid(A)||!IsValid(Target)||IsOccupied(A,C.SocketA,FGuid())||IsOccupied(Target,C.SocketB,FGuid()))return Finish(TEXT("Actual endpoint invalid or already reserved."));
    if(!ValidateSettings(R.Assembly)||FMath::Abs(TestCapsuleRadius-Q.LoopSettings->CapsuleRadius)>.001||FMath::Abs(TestCapsuleHalfHeight-Q.LoopSettings->CapsuleHalfHeight)>.001||FMath::Abs(CapsuleClearance-Q.LoopSettings->CapsuleClearance)>.001)return Finish(TEXT("C2 and planning capsule/settings must match; no weaker commit validation."));
    const auto* Profile=Q.SourceSettings->Corridors.FindByPredicate([&](const auto& P){return P.Id==C.CorridorProfile&&P.bEnabled;});
    if(!Profile)return Finish(TEXT("Selected corridor profile no longer available."));
    R.Stage=ERogueliteLoopCommitStage::Prechecked;
    const auto OldRecords=Connections;const auto OldGraph=B.GraphDigest,OldLayout=B.LayoutDigest;
    TArray<FTransform> Poses;TArray<FString> Members;for(const auto& X:B.Rooms){Poses.Add(X.Room->GetActorTransform());Members.Add(RogueliteSeedAssemblyInternal::MembersText(X.Room));}
    const auto OriginalA=Door(A,C.SocketA),OriginalB=Door(Target,C.SocketB);
    struct FComponentBefore
    {
        TWeakObjectPtr<USceneComponent> Component;FTransform Relative;bool Visible=false,Hidden=false;
        ECollisionEnabled::Type Collision=ECollisionEnabled::NoCollision;
    };
    TArray<FComponentBefore> BeforeComponents;
    for(auto* X:{A,Target})
    {TInlineComponentArray<USceneComponent*> Components;X->GetComponents(Components);for(auto* S:Components){FComponentBefore State;State.Component=S;State.Relative=S->GetRelativeTransform();State.Visible=S->IsVisible();State.Hidden=S->bHiddenInGame;if(auto* P=Cast<UPrimitiveComponent>(S))State.Collision=P->GetCollisionEnabled();BeforeComponents.Add(State);}}
    ARogueliteParametricCorridor* Connector=nullptr;FGuid AddedId;bool GeometryTouched=false;
#if WITH_EDITOR
    FScopedTransaction Transaction(NSLOCTEXT("Roguelite","FixedLoopCommit","Commit fixed-endpoint Roguelite loop"));Modify();Q.Owner->Modify();A->Modify();Target->Modify();
    for(auto* X:{A,Target})for(auto* Component:X->GetComponents())Component->Modify();
#endif
    const auto TreeUnchanged=[&]()
    {
        bool Same=B.GraphDigest==OldGraph&&B.LayoutDigest==OldLayout;
        for(int32 I=0;I<B.Rooms.Num();++I)Same&=IsValid(B.Rooms[I].Room)&&B.Rooms[I].Room->GetActorTransform().Equals(Poses[I],0)&&RogueliteSeedAssemblyInternal::MembersText(B.Rooms[I].Room)==Members[I];
        for(const auto& Old:OldRecords){const auto* Now=Connections.FindByPredicate([&](const auto& X){return X.Id==Old.Id;});Same&=Now&&RecordText(Old)==RecordText(*Now);}return Same;
    };
    const auto Fail=[&](const FString& Error)
    {
        if(AddedId.IsValid())ReleaseConnection(AddedId);
        if(IsValid(Connector))Connector->Destroy();
        if(GeometryTouched){Door(A,C.SocketA)=OriginalA;Door(Target,C.SocketB)=OriginalB;A->RebuildRoom();Target->RebuildRoom();}
        // Disabled P02 wall pieces keep their last open transforms. Restore exact prior state,
        // including hidden/no-collision pieces, without reconstructing or duplicating components.
        bool ComponentsRestored=true;
        for(const auto& State:BeforeComponents)
        {
            auto* S=State.Component.Get();if(!S){ComponentsRestored=false;continue;}
            if(!S->GetRelativeTransform().Equals(State.Relative,0))S->SetRelativeTransform(State.Relative,false,nullptr,ETeleportType::TeleportPhysics);
            if(S->IsVisible()!=State.Visible)S->SetVisibility(State.Visible,false);
            if(S->bHiddenInGame!=State.Hidden)S->SetHiddenInGame(State.Hidden,false);
            if(auto* P=Cast<UPrimitiveComponent>(S))if(P->GetCollisionEnabled()!=State.Collision)P->SetCollisionEnabled(State.Collision);
            ComponentsRestored&=S->GetRelativeTransform().Equals(State.Relative,0)&&S->IsVisible()==State.Visible&&S->bHiddenInGame==State.Hidden;
        }
        R.bOriginalTreeUnchanged=TreeUnchanged();R.bRollbackComplete=ComponentsRestored&&R.bOriginalTreeUnchanged&&Connections.Num()==OldRecords.Num()&&!IsValid(Connector)&&!IsOccupied(A,C.SocketA,FGuid())&&!IsOccupied(Target,C.SocketB,FGuid());
        R.bConnectionCommitted=R.bLoopPublished=false;R.Assembly.bSucceeded=false;R.bSucceeded=false;
#if WITH_EDITOR
        Transaction.Cancel();
#endif
        // Even restored authoring geometry dirties navigation. Never restore an old Ready flag.
        if(GeometryTouched){B.Navigation=ERogueliteNavigationCheck::NotChecked;L.GeometryDigest=FRogueliteLoopCommitQueries::GeometryDigest(Q.Owner,B,L);}
        return Finish(Error);
    };
    L.SourceBatchId=B.BatchId;L.BaseGraphDigest=B.GraphDigest;L.BaseLayoutDigest=B.LayoutDigest;
    ++L.GeometryRevision;GeometryTouched=true;B.Navigation=ERogueliteNavigationCheck::NotChecked;
    Door(A,C.SocketA).bEnabled=true;Door(Target,C.SocketB).bEnabled=true;
    if(Q.TestFault==ERogueliteLoopCommitTestFault::DoorOpen)Door(A,C.SocketA).bEnabled=false;
    A->RebuildRoom();Target->RebuildRoom();FRogueliteDoorSocket DA,DB;
    if(!A->TryGetDoorSocket(C.SocketA,DA)||!Target->TryGetDoorSocket(C.SocketB,DB))return Fail(TEXT("DoorOpenFailed: actual opening query failed; original door configuration restored."));
    R.bDoorsOpened=true;R.Stage=ERogueliteLoopCommitStage::DoorsOpened;
    if(Q.TestFault==ERogueliteLoopCommitTestFault::CancelAfterOpen)return Fail(TEXT("Cancelled after doors opened."));
    if(Q.TestFault==ERogueliteLoopCommitTestFault::Spawn)return Fail(TEXT("Injected CorridorSpawnFailed before allocation; engine-natural spawn-null outcome not claimed."));
    FActorSpawnParameters Args;Args.Owner=this;Args.ObjectFlags|=RF_Transactional;Args.bDeferConstruction=true;Args.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    const auto Settings=*Profile;Args.CustomPreSpawnInitialization=[Settings,C](AActor* Actor)
    {auto* X=CastChecked<ARogueliteParametricCorridor>(Actor);X->CorridorLength=C.CorridorLength;X->CorridorWidth=Settings.Width;X->CorridorHeight=Settings.Height;X->WallThickness=Settings.WallThickness;X->FloorThickness=Settings.FloorThickness;X->DoorWidth=Settings.DoorWidth;X->DoorHeight=Settings.DoorHeight;};
    Connector=GetWorld()->SpawnActor<ARogueliteParametricCorridor>(ARogueliteParametricCorridor::StaticClass(),C.CorridorTransform,Args);
    if(!Connector)return Fail(TEXT("CorridorSpawnFailed: engine returned null."));
    Connector->Tags.Add(OwnedLoopTag);Connector->Tags.Add(RogueliteSeedAssemblyInternal::BatchTag(B.BatchId));Connector->FinishSpawning(C.CorridorTransform);
    R.bCorridorSpawned=true;R.SpawnedCorridorPath=Connector->GetPathName();R.Stage=ERogueliteLoopCommitStage::GeometryBuilt;
    if(Q.TestFault==ERogueliteLoopCommitTestFault::CancelAfterSpawn)return Fail(TEXT("Cancelled after actual corridor spawn."));
    if(Q.TestFault==ERogueliteLoopCommitTestFault::Collision)Connector->SetActorEnableCollision(false);
    FRogueliteRoomConnection Record;Record.RoomA=A;Record.RoomB=Target;Record.SocketA=C.SocketA;Record.SocketB=C.SocketB;Record.Corridor=Connector;Record.EndpointA=ERogueliteCorridorEndpoint::Start;Record.EndpointB=ERogueliteCorridorEndpoint::End;Record.Mode=ERogueliteConnectionMode::StraightCorridor;
    if(!ValidateCorridorConnection(Record,R.Assembly,true))return Fail(TEXT("Actual C2 validation failed: ")+R.Assembly.Message);
    R.bC2Validated=true;R.Stage=ERogueliteLoopCommitStage::C2Validated;
    if(!TreeUnchanged())return Fail(TEXT("Original Tree changed during the fixed endpoint transaction; no publication."));
    if(Q.TestFault==ERogueliteLoopCommitTestFault::CancelAfterValidation)return Fail(TEXT("Cancelled after actual C2 validation before reservation."));
    Record.Id=FGuid::NewGuid();Record.bOwnedFixedLoop=true;Record.LoopSourceBatchId=B.BatchId;Record.LoopCandidateId=C.CandidateId;RecordValidation(Record,R.Assembly);
    AddedId=Record.Id;Connections.Add(Record);R.Assembly.ConnectionId=AddedId;Connector->RebuildCorridor();R.bConnectionCommitted=true;
    if(Q.TestFault==ERogueliteLoopCommitTestFault::CancelAfterReservation)return Fail(TEXT("Cancelled after C2 reservation before LoopEdge publication."));
    auto& Edge=L.Edges.AddDefaulted_GetRef();Edge.CandidateId=C.CandidateId;Edge.EdgeId=B.Edges.Num();Edge.NodeA=C.NodeA;Edge.NodeB=C.NodeB;Edge.SocketA=C.SocketA;Edge.SocketB=C.SocketB;Edge.ConnectionId=AddedId;Edge.ProfileId=C.CorridorProfile;Edge.Corridor=Connector;Edge.BeforeDoorA=OriginalA;Edge.BeforeDoorB=OriginalB;Edge.PlanDigest=Fresh.StableDigest;Edge.Validation=R.Assembly;
    ++L.GraphRevision;L.GeometryDigest=FRogueliteLoopCommitQueries::GeometryDigest(Q.Owner,B,L);
    R.bOriginalTreeUnchanged=TreeUnchanged();R.bLoopPublished=R.bSucceeded=true;R.Stage=ERogueliteLoopCommitStage::Committed;
    return Finish(TEXT("Committed: fixed room poses and original C2 records preserved; actual corridor/doors/C2 geometry/capsule passed. NavigationPending, never loop NavigationReady."));
}

FRogueliteLoopTraversalSnapshot FRogueliteLoopCommitQueries::Capture(AActor* Owner,const FRogueliteSeedBatch& B,
    const FRogueliteSeedLayoutSettings& Source,const FRogueliteCommittedLoopBatch& L)
{
    FRogueliteLoopTraversalSnapshot S;S.BatchId=B.BatchId;S.Seed=B.Seed;S.GraphRevision=L.GraphRevision;S.GeometryRevision=L.GeometryRevision;
    if(!IsValid(Owner)||!Owner->GetWorld()||!B.bSucceeded||!B.bGeometryValid||!IsValid(B.Registry)||B.Registry->GetWorld()!=Owner->GetWorld()||L.SourceBatchId!=B.BatchId||L.BaseGraphDigest!=B.GraphDigest||L.BaseLayoutDigest!=B.LayoutDigest){S.Reason=TEXT("Stale/cleared/replaced source batch or World.");return S;}
    const auto Graph=G::Generate(B.Seed,Source.GraphSettings());if(!Graph.bSucceeded||Graph.Graph.StableDigest!=B.GraphDigest){S.Reason=TEXT("Base graph settings changed.");return S;}
    const auto Records=B.Registry->GetConnectionRecords();int32 Active=0;for(const auto& R:Records)Active+=R.bActive;
    if(Active!=B.Edges.Num()+L.Edges.Num()){S.Reason=TEXT("Unmatched authority records; revalidate/clean the overlay.");return S;}
    for(const auto& E:B.Edges)
    {const auto* C=Records.FindByPredicate([&](const auto& R){return R.Id==E.ConnectionId&&R.bActive;});if(!C||!C->bGeometryValid||C->ValidationStatus!=ERogueliteConnectionValidation::Passed){S.Reason=TEXT("Base Tree C2 validation is not current.");return S;}auto& X=S.Edges.AddDefaulted_GetRef();X.EdgeId=E.EdgeId;X.NodeA=E.ParentNode;X.NodeB=E.ChildNode;X.ConnectionId=E.ConnectionId;}
    for(const auto& E:L.Edges)
    {const auto* C=Records.FindByPredicate([&](const auto& R){return Owns(R,B,E)&&R.bActive;});if(!C||!IsValid(E.Corridor)||E.Corridor->GetWorld()!=Owner->GetWorld()||!C->bGeometryValid||C->ValidationStatus!=ERogueliteConnectionValidation::Passed){S.Edges.Reset();S.Reason=TEXT("Committed Loop is missing, released or requires actual C2 revalidation.");return S;}auto& X=S.Edges.AddDefaulted_GetRef();X.EdgeId=E.EdgeId;X.NodeA=E.NodeA;X.NodeB=E.NodeB;X.bCommittedLoop=true;X.CandidateId=E.CandidateId;X.ConnectionId=E.ConnectionId;}
    if(L.GeometryDigest!=GeometryDigest(Owner,B,L)){S.Edges.Reset();S.Reason=TEXT("Geometry/obstacles changed since validation; no fresh traversal or Ready claim.");return S;}
    for(const auto& N:Graph.Graph.Nodes)
    {
        auto& Row=S.Nodes.AddDefaulted_GetRef();Row.NodeId=N.NodeId;Row.Degree=N.Degree;Row.Role=N.Role==ERogueliteGraphNodeRole::Start?TEXT("Start"):N.Role==ERogueliteGraphNodeRole::Final?TEXT("Final"):N.Role==ERogueliteGraphNodeRole::Branch?TEXT("Branch"):TEXT("MainPath");
        const auto* R=B.Rooms.FindByPredicate([&](const auto& X){return X.NodeId==N.NodeId;});if(R){Row.RoomType=R->RoomType;Row.TemplateId=R->TemplateId;}for(const auto& E:L.Edges)Row.Degree+=(E.NodeA==N.NodeId||E.NodeB==N.NodeId);
    }
    S.Edges.Sort([](const auto& A,const auto& C){return A.EdgeId<C.EdgeId;});S.IndependentCycles=S.Edges.Num()-S.Nodes.Num()+1;
    FString Text=TEXT("TraversalV1|base=")+B.GraphDigest+TEXT("|layout=")+B.LayoutDigest;
    for(const auto& E:S.Edges)Text+=FString::Printf(TEXT("|%d:%d,%d:%d:%s"),E.EdgeId,E.NodeA,E.NodeB,E.bCommittedLoop,*E.CandidateId.ToString());
    S.StableDigest=G::StableTextDigest(Text);S.bCurrent=S.bGeometryValidated=true;S.Reason=TEXT("BaseTree + actual authoritative CommittedLoopEdges only. NavigationPending; B4-B3 readiness not implemented.");return S;
}

bool FRogueliteLoopCommitQueries::Clear(AActor* Owner,FRogueliteSeedBatch& B,FRogueliteCommittedLoopBatch& L,FString& Reason)
{
    if(!Editor(Owner)){Reason=TEXT("EditorOnly: never restore registered Static doors in PIE.");return false;}
    if(L.Edges.IsEmpty()){Reason=TEXT("No committed loops to clear.");return true;}
    if(!IsValid(B.Registry)||L.SourceBatchId!=B.BatchId){Reason=TEXT("Registry/base cleared. Owned registry Destroyed hook cleans its loop corridors; stale overlay discarded.");for(const auto& E:L.Edges)if(IsValid(E.Corridor)){Reason=TEXT("Stale source still has geometry; refuse unowned cleanup.");return false;}L.Edges.Reset();return true;}
    const auto Records=B.Registry->GetConnectionRecords();
    for(const auto& E:L.Edges)
    {
        const auto* C=Records.FindByPredicate([&](const auto& R){return Owns(R,B,E);});auto* A=Room(B,E.NodeA);auto* Target=Room(B,E.NodeB);
        if(!C||!IsValid(A)||!IsValid(Target)||!IsValid(E.Corridor)||E.Corridor->GetOwner()!=B.Registry||!E.Corridor->Tags.Contains(OwnedLoopTag)||ARogueliteRoomAssembly::GetRoomSocketConnectionId(A,E.SocketA)!=E.ConnectionId||ARogueliteRoomAssembly::GetRoomSocketConnectionId(Target,E.SocketB)!=E.ConnectionId){Reason=TEXT("Owned loop identity/reservation changed; explicit conflict recovery required.");return false;}
    }
#if WITH_EDITOR
    FScopedTransaction T(NSLOCTEXT("Roguelite","ClearFixedLoops","Clear owned fixed loops"));Owner->Modify();B.Registry->Modify();
#endif
    for(const auto& E:L.Edges)
    {auto* A=Room(B,E.NodeA);auto* Target=Room(B,E.NodeB);A->Modify();Target->Modify();B.Registry->ReleaseConnection(E.ConnectionId);E.Corridor->Destroy();Door(A,E.SocketA)=E.BeforeDoorA;Door(Target,E.SocketB)=E.BeforeDoorB;A->RebuildRoom();Target->RebuildRoom();}
    L.Edges.Reset();++L.GraphRevision;++L.GeometryRevision;B.Navigation=ERogueliteNavigationCheck::NotChecked;L.GeometryDigest=GeometryDigest(Owner,B,L);Reason=TEXT("Only owned loop corridor/lease removed and its door settings restored. Tree retained; old epoch/Ready revoked.");return true;
}

bool FRogueliteLoopCommitQueries::Revalidate(AActor* Owner,FRogueliteSeedBatch& B,FRogueliteCommittedLoopBatch& L,FString& Reason)
{
    if(!Editor(Owner)||!IsValid(B.Registry)||L.SourceBatchId!=B.BatchId){Reason=TEXT("Invalid/current Editor source required.");return false;}
    // The old B2 revalidator intentionally requires ports/records == Tree degree/edges.
    // Keep that contract intact; validate the combined real authority here through C2.
    bool TreeGood=true;
    for(const auto& R:B.Rooms)
    {
        int32 Extra=0;for(const auto& E:L.Edges)Extra+=(E.NodeA==R.NodeId||E.NodeB==R.NodeId);
        int32 Ports=0;if(IsValid(R.Room))for(int32 I=0;I<4;++I){FRogueliteDoorSocket D;Ports+=R.Room->TryGetDoorSocket(ERogueliteRoomSide(I),D);}
        TreeGood&=IsValid(R.Room)&&R.Room->GetWorld()==Owner->GetWorld()&&R.Room->GetActorTransform().Equals(R.PlacedTransform,.0001)&&R.Room->EvaluateTemplate().bValid&&FVector(R.Room->RoomLength,R.Room->RoomWidth,R.Room->RoomHeight).Equals(R.Dimensions,.0001)&&R.Room->WallThickness==R.WallThickness&&R.Room->FloorThickness==R.FloorThickness&&RogueliteSeedAssemblyInternal::MembersText(R.Room)==R.MemberSignature&&Ports==R.LogicalDegree+Extra;
    }
    for(auto& E:B.Edges){E.Validation=B.Registry->RevalidateConnection(E.ConnectionId);TreeGood&=E.Validation.bSucceeded&&E.Validation.bGeometryPassed&&E.Validation.bCapsulePassed;}
    B.bGeometryValid=TreeGood;bool Good=TreeGood;
    for(auto& E:L.Edges){E.Validation=B.Registry->RevalidateConnection(E.ConnectionId);Good&=E.Validation.bSucceeded&&E.Validation.bGeometryPassed&&E.Validation.bCapsulePassed;}
    B.Navigation=ERogueliteNavigationCheck::NotChecked;++L.GeometryRevision;L.GeometryDigest=Good?GeometryDigest(Owner,B,L):TEXT("");Reason=Good?TEXT("Actual C2 revalidation complete; still NavigationPending for the whole loop layout."):TEXT("C2 revalidation failed; inspect probes. No fresh traversal/Ready.");return Good;
}
