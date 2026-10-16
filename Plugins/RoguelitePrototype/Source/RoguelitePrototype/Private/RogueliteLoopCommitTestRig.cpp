#include "RogueliteLoopCommitTestRig.h"
#include "RogueliteSeedAssemblyInternal.h"
#include "RoguelitePrototype.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
namespace
{
using G=FRogueliteRoomGraphGenerator;
FRogueliteDoorConfig& D(ARogueliteParametricRoom* R,ERogueliteRoomSide S)
{return S==ERogueliteRoomSide::North?R->NorthDoor:S==ERogueliteRoomSide::South?R->SouthDoor:S==ERogueliteRoomSide::East?R->EastDoor:R->WestDoor;}
FString Scene(UWorld* W)
{
    TArray<FString> Rows;for(TActorIterator<AActor> I(W);I;++I)if(!I->IsActorBeingDestroyed())
    {
        Rows.Add(I->GetPathName()+I->GetActorTransform().ToString());TInlineComponentArray<USceneComponent*> Cs;I->GetComponents(Cs);
        for(const auto* C:Cs){FString T=C->GetPathName()+C->GetRelativeTransform().ToString();if(const auto* M=Cast<UStaticMeshComponent>(C))T+=GetPathNameSafe(M->GetStaticMesh())+FString::FromInt(uint8(M->GetCollisionEnabled()));Rows.Add(T);}
        if(const auto* R=Cast<ARogueliteParametricRoom>(*I))for(auto Side:{ERogueliteRoomSide::North,ERogueliteRoomSide::South,ERogueliteRoomSide::East,ERogueliteRoomSide::West}){FString T;const auto& Door=D(const_cast<ARogueliteParametricRoom*>(R),Side);FRogueliteDoorConfig::StaticStruct()->ExportText(T,&Door,nullptr,nullptr,PPF_None,nullptr);Rows.Add(R->GetPathName()+T);}
        if(const auto* R=Cast<ARogueliteRoomAssembly>(*I))for(const auto& C:R->GetConnectionRecords()){FString T;FRogueliteRoomConnection::StaticStruct()->ExportText(T,&C,nullptr,nullptr,PPF_None,nullptr);Rows.Add(T);}
    }Rows.Sort();FString T;for(const auto& R:Rows)T+=R+TEXT("\n");return G::StableTextDigest(T);
}
template<class T> T* Spawn(UWorld* W,AActor* Owner,FRogueliteSeedBatch& B,const FTransform& Pose,TFunction<void(T*)> Set={})
{
    FActorSpawnParameters Args;Args.Owner=Owner;Args.ObjectFlags|=RF_Transactional;Args.bDeferConstruction=true;Args.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    Args.CustomPreSpawnInitialization=[Set](AActor* A){if(Set)Set(CastChecked<T>(A));};auto* A=W->SpawnActor<T>(T::StaticClass(),Pose,Args);if(A){A->Tags.Add(RogueliteSeedAssemblyInternal::BatchTag(B.BatchId));B.OwnedActors.Add(A);A->FinishSpawning(Pose);}return A;
}
}
ARogueliteFixedLoopFixtureRoom::ARogueliteFixedLoopFixtureRoom()
{
    TemplateId=TEXT("FixedLoopFixtureNative");RoomLength=RoomWidth=2000;RoomHeight=400;
    // Four real authored ports in this independent native fixture, not in old BP assets.
    NorthDoor.bEnabled=SouthDoor.bEnabled=EastDoor.bEnabled=WestDoor.bEnabled=true;
    FixturePOI=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("FixedLoopOwnedPOI"));FixturePOI->SetupAttachment(TemplateContentRoot.Get());FixturePOI->SetMobility(EComponentMobility::Static);
    FixturePOI->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));FixturePOI->SetCollisionProfileName(TEXT("BlockAll"));FixturePOI->SetRelativeLocation(FVector(300,300,50));FixturePOI->SetRelativeScale3D(FVector(.5,.5,1));
}
ARogueliteLoopCommitTestRig::ARogueliteLoopCommitTestRig(){PrimaryActorTick.bCanEverTick=false;SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("FixedLoopFixtureRoot")));Tags.Add(TEXT("RogueliteLoopCommitTestOnly"));LoopSettings.MinLoops=1;LoopSettings.MaxLoops=1;}
bool ARogueliteLoopCommitTestRig::OwnMap() const
{return GetWorld()&&!GetWorld()->IsGameWorld()&&(GetWorld()->GetOutermost()->GetName()==TEXT("/Game/Roguelite/Maps/L_Roguelite_LoopCommit_Dev")||GetWorld()->GetOutermost()->GetName()==TEXT("/Game/Roguelite/Maps/L_Roguelite_LoopAwarePlanning_Dev"));}
void ARogueliteLoopCommitTestRig::BuildFixture()
{
    if(!OwnMap()||!Batch.OwnedActors.IsEmpty()||!CommittedLoops.Edges.IsEmpty()||(FixtureRoomCount!=5&&FixtureRoomCount!=6)){Summary=TEXT("Refused: only empty owned independent fixture (5/6 test rooms), outside PIE.");return;}
    Modify();CommittedLoops=FRogueliteCommittedLoopBatch();Settings=FRogueliteSeedLayoutSettings();Settings.Seed=FixtureSeed;Settings.MinMainNodes=Settings.MaxMainNodes=FixtureRoomCount;Settings.MinBranches=Settings.MaxBranches=0;Settings.MaxNodes=FixtureRoomCount;Settings.MaxDegree=3;Settings.Templates.Reset();
    FRogueliteSeedTemplateOption Option;Option.Id=TEXT("FixedLoopFixtureNative");Option.TemplateClass=ARogueliteFixedLoopFixtureRoom::StaticClass();Settings.Templates.Add(Option);
    FRogueliteSeedCorridorProfile Profile;Profile.Id=TEXT("FixedLoopStraight");Profile.LengthCandidates={1000,1960,3000};Settings.Corridors={Profile};
    Batch=FRogueliteSeedBatch();Batch.Seed=FixtureSeed;Batch.BatchId=FGuid::NewGuid();const auto Graph=G::Generate(Settings.Seed,Settings.GraphSettings());
    if(!Graph.bSucceeded){Summary=Graph.Reason;return;}Batch.GraphDigest=Graph.Graph.StableDigest;Batch.ConfigurationDigest=TEXT("Independent fixed fixture, not a random floor or FloorProfile implementation");
    // Fixture arrangement only. Commit code discovers the actual nodes/ports/length and never uses these coordinates.
    const FVector Centers[]={FVector(0,0,0),FVector(4000,0,0),FVector(4000,4000,0),FVector(0,4000,0),FVector(0,8000,0),FVector(4000,8000,0)};
    const ERogueliteRoomSide From[]={ERogueliteRoomSide::North,ERogueliteRoomSide::East,ERogueliteRoomSide::South,ERogueliteRoomSide::East,ERogueliteRoomSide::North};
    const ERogueliteRoomSide To[]={ERogueliteRoomSide::South,ERogueliteRoomSide::West,ERogueliteRoomSide::North,ERogueliteRoomSide::West,ERogueliteRoomSide::South};
    Batch.Registry=Spawn<ARogueliteRoomAssembly>(GetWorld(),this,Batch,FTransform::Identity);if(!Batch.Registry){Summary=TEXT("Fixture registry spawn failed.");return;}Batch.Registry->bCheckNavigation=false;
    for(int32 I=0;I<FixtureRoomCount;++I)
    {
        TArray<ERogueliteRoomSide> Used;if(I>0)Used.Add(To[I-1]);if(I<FixtureRoomCount-1)Used.Add(From[I]);
        const FTransform Pose(GetActorQuat(),GetActorTransform().TransformPosition(Centers[I]));
        auto* R=Spawn<ARogueliteFixedLoopFixtureRoom>(GetWorld(),this,Batch,Pose,[Used](auto* A){for(auto Side:{ERogueliteRoomSide::North,ERogueliteRoomSide::South,ERogueliteRoomSide::East,ERogueliteRoomSide::West})D(A,Side).bEnabled=Used.Contains(Side);});
        if(!R){Summary=TEXT("Fixture room spawn failed.");return;}R->RebuildTemplate();auto& Row=Batch.Rooms.AddDefaulted_GetRef();Row.NodeId=I;Row.Room=R;Row.TemplateId=Option.Id;Row.TemplateClass=R->GetClass();Row.PlacedTransform=R->GetActorTransform();Row.Dimensions=FVector(R->RoomLength,R->RoomWidth,R->RoomHeight);Row.WallThickness=R->WallThickness;Row.FloorThickness=R->FloorThickness;Row.LogicalDegree=Graph.Graph.Nodes[I].Degree;Row.TemplateCapacity=4;Row.MemberSignature=RogueliteSeedAssemblyInternal::MembersText(R);Row.RoomType=I==FixtureRoomCount-1?ERogueliteSeedRoomType::Final:ERogueliteSeedRoomType::Combat;
    }
    for(int32 I=0;I<FixtureRoomCount-1;++I)
    {
        FRogueliteDoorSocket A,B;Batch.Rooms[I].Room->TryGetDoorSocket(From[I],A);Batch.Rooms[I+1].Room->TryGetDoorSocket(To[I],B);const float Length=FVector::DotProduct(B.WorldPosition-A.WorldPosition,A.WorldOutwardDirection);
        const FTransform Pose(A.WorldOutwardDirection.Rotation().Quaternion(),(A.WorldPosition+B.WorldPosition)*.5);
        auto* C=Spawn<ARogueliteParametricCorridor>(GetWorld(),this,Batch,Pose,[Profile,Length](auto* X){X->CorridorLength=Length;X->CorridorWidth=Profile.Width;X->CorridorHeight=Profile.Height;X->WallThickness=Profile.WallThickness;X->FloorThickness=Profile.FloorThickness;X->DoorWidth=Profile.DoorWidth;X->DoorHeight=Profile.DoorHeight;});
        if(!C){Summary=TEXT("Fixture tree corridor spawn failed.");return;}const auto V=Batch.Registry->TryAlignCorridorConnection(Batch.Rooms[I].Room,From[I],C,ERogueliteCorridorEndpoint::Start,Batch.Rooms[I+1].Room,To[I]);
        if(!V.bSucceeded){Summary=V.Message;return;}auto& E=Batch.Edges.AddDefaulted_GetRef();E.EdgeId=I;E.ParentNode=I;E.ChildNode=I+1;E.ParentSocket=From[I];E.ChildSocket=To[I];E.Corridor=C;E.CorridorProfile=Profile.Id;E.CorridorLength=Length;E.ConnectionId=V.ConnectionId;E.Validation=V;
    }
    Batch.bSucceeded=Batch.bGeometryValid=true;Batch.LayoutDigest=G::StableTextDigest(FRogueliteSeedLayoutAssembler::DescribeLayout(Batch));Batch.Reason=TEXT("Real independent Tree generated; unused authored ports are sealed, all C2 tree edges physically verified.");Summary=Batch.Reason;
}
FRogueliteFixedLoopCommitRequest ARogueliteLoopCommitTestRig::Request(ERogueliteLoopCommitTestFault Fault) const
{
    FRogueliteFixedLoopCommitRequest Q;Q.Owner=const_cast<ARogueliteLoopCommitTestRig*>(this);Q.SourceSettings=&Settings;Q.LoopSettings=&LoopSettings;Q.Plan=&Plan;Q.ExpectedGeometryRevision=PlanGeometryRevision;Q.TestFault=Fault;
    if(!Plan.SelectedCandidates.IsEmpty())Q.CandidateId=Plan.SelectedCandidates[0];return Q;
}
void ARogueliteLoopCommitTestRig::AnalyzeLoop()
{if(OwnMap()){Plan=FRogueliteLoopPlanner::Analyze(this,Batch,Settings,LoopSettings);PlanGeometryRevision=CommittedLoops.GeometryRevision;Summary=Plan.Reason;}}
void ARogueliteLoopCommitTestRig::CommitLoop()
{if(OwnMap()&&IsValid(Batch.Registry)){Modify();LastCommit=Batch.Registry->TryCommitFixedEndpointLoop(Batch,CommittedLoops,Request());Traversal=FRogueliteLoopCommitQueries::Capture(this,Batch,Settings,CommittedLoops);Summary=LastCommit.Reason;}}
void ARogueliteLoopCommitTestRig::RevalidateLoop()
{if(OwnMap()){Modify();FRogueliteLoopCommitQueries::Revalidate(this,Batch,CommittedLoops,Summary);Traversal=FRogueliteLoopCommitQueries::Capture(this,Batch,Settings,CommittedLoops);}}
void ARogueliteLoopCommitTestRig::DisconnectLoop()
{if(OwnMap()){Modify();FRogueliteLoopCommitQueries::Clear(this,Batch,CommittedLoops,Summary);Traversal=FRogueliteLoopCommitQueries::Capture(this,Batch,Settings,CommittedLoops);}}
void ARogueliteLoopCommitTestRig::ClearFixture()
{if(OwnMap()){Modify();if(FRogueliteLoopCommitQueries::Clear(this,Batch,CommittedLoops,Summary)){FRogueliteSeedLayoutAssembler::Clear(this,Batch,Summary);Traversal=FRogueliteLoopCommitQueries::Capture(this,Batch,Settings,CommittedLoops);}}}
void ARogueliteLoopCommitTestRig::BeginPlay()
{
    Super::BeginPlay();if(GetWorld()->IsGameWorld()&&IsValid(Batch.Registry))
    {const auto Before=Scene(GetWorld());const auto R=Batch.Registry->TryCommitFixedEndpointLoop(Batch,CommittedLoops,Request());bRuntimeMutationRefused=!R.bSucceeded&&R.Reason.Contains(TEXT("EditorOnly"))&&Scene(GetWorld())==Before;
        UE_LOG(LogRoguelitePrototype,Display,TEXT("LOOP_COMMIT_RUNTIME_GUARD Actor=%s GameWorld=1 Refused=%d"),*GetPathName(),bRuntimeMutationRefused);}
}
void ARogueliteLoopCommitTestRig::RunCommitChecks()
{
    if(!OwnMap()||!Batch.OwnedActors.IsEmpty()){Summary=TEXT("Refused: do not overwrite an existing preview/user layout.");return;}
    Cases.Reset();Evidence.Reset();bChecksPassed=true;const auto Initial=Scene(GetWorld());const auto OldPose=GetActorTransform();const auto OldSeed=FixtureSeed;const auto OldCount=FixtureRoomCount;
    const auto Check=[&](const FString& Name,bool Pass){bChecksPassed&=Pass;Evidence.Add((Pass?TEXT("PASS "):TEXT("FAIL "))+Name);UE_LOG(LogRoguelitePrototype,Display,TEXT("FIXED_LOOP_CHECK %s"),*Evidence.Last());};
    const auto Prepare=[&](){BuildFixture();Check(TEXT("Actual Tree fixture/C2 geometry created"),Batch.bSucceeded&&Batch.bGeometryValid);AnalyzeLoop();Check(TEXT("Current authored sealed-port straight precheck"),Plan.bRequirementsSatisfied&&!Plan.SelectedCandidates.IsEmpty());};
    const auto Run=[&](const FString& Name,ERogueliteLoopCommitTestFault Fault,bool Success)
    {
        FRogueliteLoopCommitCase C;C.Name=Name;C.bExpectedSuccess=Success;const auto Before=Scene(GetWorld());C.Result=Batch.Registry->TryCommitFixedEndpointLoop(Batch,CommittedLoops,Request(Fault));C.Traversal=FRogueliteLoopCommitQueries::Capture(this,Batch,Settings,CommittedLoops);C.bSceneRestored=Before==Scene(GetWorld());
        C.bPassed=C.Result.bSucceeded==Success&&C.Result.bOriginalTreeUnchanged&&(Success?(C.Result.bDoorsOpened&&C.Result.bC2Validated&&C.Result.bConnectionCommitted&&C.Result.bLoopPublished&&C.Traversal.bCurrent&&C.Traversal.IndependentCycles==1&&!C.Traversal.bNavigationReady):(C.Result.bRollbackComplete&&C.bSceneRestored&&CommittedLoops.Edges.IsEmpty()));Cases.Add(C);Check(Name,C.bPassed);return C.Result;
    };
    Prepare();if(Batch.bSucceeded&&!Plan.SelectedCandidates.IsEmpty())
    {
        const auto SavedRequest=Request();const auto P=Run(TEXT("Five-room actual fixed endpoint commit with sealed doors"),ERogueliteLoopCommitTestFault::None,true);
        Check(TEXT("Actual complete original C2 probe suite"),P.Assembly.Probes.Num()==59&&P.Assembly.Probes.ContainsByPredicate([](const auto& X){return X.Name.Contains(TEXT("PawnCapsule entire"));})&&P.Assembly.bGeometryPassed&&P.Assembly.bCapsulePassed);
        const auto Before=Scene(GetWorld());const auto Duplicate=Batch.Registry->TryCommitFixedEndpointLoop(Batch,CommittedLoops,SavedRequest);Check(TEXT("Duplicate candidate safely rejected, no extra Actor/reservation"),!Duplicate.bSucceeded&&Duplicate.Reason.Contains(TEXT("DuplicateCommitRefused"))&&Before==Scene(GetWorld()));
        const auto V=Traversal=FRogueliteLoopCommitQueries::Capture(this,Batch,Settings,CommittedLoops);Check(TEXT("Traversal has only base edges + actual committed loop"),V.bCurrent&&V.Nodes.Num()==Batch.Rooms.Num()&&V.Edges.Num()==Batch.Edges.Num()+1&&V.IndependentCycles==1);
        DisconnectLoop();const auto Stale=Batch.Registry->TryCommitFixedEndpointLoop(Batch,CommittedLoops,SavedRequest);Check(TEXT("Disconnect revokes old plan geometry epoch"),!Stale.bSucceeded&&Stale.Reason.Contains(TEXT("revision")));ClearFixture();const auto Cleared=FRogueliteLoopCommitQueries::Capture(this,Batch,Settings,CommittedLoops);Check(TEXT("Clear revokes traversal, no stale loop data"),!Cleared.bCurrent&&Cleared.Edges.IsEmpty());
    }
    ClearFixture();FixtureSeed=99;FixtureRoomCount=6;const FTransform AlternatePose(FRotator(0,270,0),FVector(10000,-12000,0));Check(TEXT("Alternate fixture root truly moved/rotated"),SetActorTransform(AlternatePose)&&GetActorTransform().Equals(AlternatePose,.0001));Prepare();
    if(Batch.bSucceeded&&!Plan.SelectedCandidates.IsEmpty())Run(TEXT("Six-room different seed/quarter rotation actual commit"),ERogueliteLoopCommitTestFault::None,true);ClearFixture();SetActorTransform(OldPose);FixtureSeed=OldSeed;FixtureRoomCount=OldCount;
    const ERogueliteLoopCommitTestFault Faults[]={ERogueliteLoopCommitTestFault::DoorOpen,ERogueliteLoopCommitTestFault::Spawn,ERogueliteLoopCommitTestFault::Collision,ERogueliteLoopCommitTestFault::CancelAfterOpen,ERogueliteLoopCommitTestFault::CancelAfterSpawn,ERogueliteLoopCommitTestFault::CancelAfterValidation,ERogueliteLoopCommitTestFault::CancelAfterReservation};
    const TCHAR* Labels[]={TEXT("Injected actual opening query failure"),TEXT("Injected spawn allocation failure branch"),TEXT("Actual C2 collision failure after disabling owned corridor collision"),TEXT("Cancel after doors"),TEXT("Cancel after actual spawn"),TEXT("Cancel after actual C2 probes"),TEXT("Cancel after unique C2 lease before edge publication")};
    for(int32 I=0;I<UE_ARRAY_COUNT(Faults);++I){Prepare();if(Batch.bSucceeded&&!Plan.SelectedCandidates.IsEmpty())Run(Labels[I],Faults[I],false);ClearFixture();}
    Prepare();if(Batch.bSucceeded&&!Plan.SelectedCandidates.IsEmpty())
    {
        const auto* C=Plan.Candidates.FindByPredicate([&](const auto& X){return X.CandidateId==Plan.SelectedCandidates[0];});
        auto& Door=D(Batch.Rooms[C->NodeA].Room,C->SocketA);const auto Original=Door;Door.DoorHeight=240;Batch.Rooms[C->NodeA].Room->RebuildRoom();Run(TEXT("Changed door specification invalidates old source plan"),ERogueliteLoopCommitTestFault::None,false);Door=Original;Batch.Rooms[C->NodeA].Room->RebuildRoom();
        auto OriginalSettings=LoopSettings;LoopSettings.MaxCorridorLength=100;Run(TEXT("Illegal required corridor length/settings rejected without writes"),ERogueliteLoopCommitTestFault::None,false);LoopSettings=OriginalSettings;
        const auto B=Batch;ClearFixture();const auto Fail=FRogueliteLoopCommitQueries::Capture(this,B,Settings,CommittedLoops);Check(TEXT("Destroyed/replaced batch returns no old traversal"),!Fail.bCurrent);
    }
    ClearFixture();Prepare();if(Batch.bSucceeded&&!Plan.SelectedCandidates.IsEmpty())
    {
        const auto* C=Plan.Candidates.FindByPredicate([&](const auto& X){return X.CandidateId==Plan.SelectedCandidates[0];});FActorSpawnParameters Args;Args.Owner=this;Args.ObjectFlags|=RF_Transient;
        auto* Block=GetWorld()->SpawnActor<AActor>(AActor::StaticClass(),C->CorridorTransform,Args);auto* Mesh=NewObject<UStaticMeshComponent>(Block);Block->AddInstanceComponent(Mesh);Block->SetRootComponent(Mesh);Mesh->SetMobility(EComponentMobility::Movable);Mesh->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));Mesh->SetCollisionProfileName(TEXT("BlockAll"));Mesh->SetWorldTransform(FTransform(C->CorridorTransform.GetRotation(),C->CorridorTransform.GetLocation()+FVector(0,0,150),FVector(1,6,3)));Mesh->RegisterComponent();const auto Pose=Block->GetActorTransform();
        Run(TEXT("Real independent blocking object makes plan stale/rejected"),ERogueliteLoopCommitTestFault::None,false);Check(TEXT("Planner/commit preserved independent blocker"),IsValid(Block)&&Block->GetActorTransform().Equals(Pose,0));Block->Destroy();
    }
    ClearFixture();Prepare();if(Batch.bSucceeded&&!Plan.SelectedCandidates.IsEmpty())
    {
        const auto Saved=Plan;const auto Q=Request();auto* R=Batch.Registry.Get();const auto* C=Plan.Candidates.FindByPredicate([&](const auto& X){return X.CandidateId==Q.CandidateId;});FRogueliteRoomConnection Foreign;Foreign.Id=FGuid::NewGuid();Foreign.RoomA=Batch.Rooms[C->NodeA].Room;Foreign.SocketA=C->SocketA;Foreign.RoomB=Batch.Rooms[C->NodeB].Room;Foreign.SocketB=C->SocketB;R->Connections.Add(Foreign);
        Run(TEXT("Actual C2 foreign lease prevents candidate commit"),ERogueliteLoopCommitTestFault::None,false);R->Connections.RemoveAll([&](const auto& X){return X.Id==Foreign.Id;});ClearFixture();
    }
    // Original Legacy Align guard must still refuse moving a target with its Tree subgraph.
    Prepare();if(Batch.bSucceeded&&!Plan.SelectedCandidates.IsEmpty())
    {
        const auto* C=Plan.Candidates.FindByPredicate([&](const auto& X){return X.CandidateId==Plan.SelectedCandidates[0];});auto* A=Batch.Rooms[C->NodeA].Room.Get();auto* B=Batch.Rooms[C->NodeB].Room.Get();const auto AD=D(A,C->SocketA),BD=D(B,C->SocketB);D(A,C->SocketA).bEnabled=D(B,C->SocketB).bEnabled=true;A->RebuildRoom();B->RebuildRoom();
        auto* Connector=Spawn<ARogueliteParametricCorridor>(GetWorld(),this,Batch,C->CorridorTransform,[C](auto* X){X->CorridorLength=C->CorridorLength;X->CorridorWidth=C->CorridorDimensions.Y;X->CorridorHeight=C->CorridorDimensions.Z;});const auto Before=Scene(GetWorld());const auto Old=Batch.Registry->TryAlignCorridorConnection(A,C->SocketA,Connector,ERogueliteCorridorEndpoint::Start,B,C->SocketB);Check(TEXT("Original C2 automatic subgraph movement guard preserved"),!Old.bSucceeded&&Old.Message.Contains(TEXT("subgraph"))&&Scene(GetWorld())==Before);D(A,C->SocketA)=AD;D(B,C->SocketB)=BD;A->RebuildRoom();B->RebuildRoom();
    }
    ClearFixture();Prepare();if(Batch.bSucceeded&&!Plan.SelectedCandidates.IsEmpty())
    {Run(TEXT("Owned cleanup safety fixture success"),ERogueliteLoopCommitTestFault::None,true);if(!CommittedLoops.Edges.IsEmpty()){const auto Corridor=CommittedLoops.Edges[0].Corridor;FString Reason;const auto Before=Scene(GetWorld());const bool OldClear=FRogueliteSeedLayoutAssembler::Clear(this,Batch,Reason);Check(TEXT("Legacy BaseTree cleanup guard refuses extra loop without deleting anything"),!OldClear&&IsValid(Corridor)&&Scene(GetWorld())==Before);ClearFixture();Check(TEXT("Loops-first owned cleanup leaves no orphan or lease"),!IsValid(Corridor)&&Batch.bCleanupComplete);}}
    SetActorTransform(OldPose);FixtureSeed=OldSeed;FixtureRoomCount=OldCount;ClearFixture();Check(TEXT("Original external scene exactly preserved"),Scene(GetWorld())==Initial);
    Summary=FString::Printf(TEXT("Fixed loop tests=%s Cases=%d; actual Editor geometry/C2 only. Runtime commit refused, loop NavigationReady not implemented."),bChecksPassed?TEXT("PASS"):TEXT("FAIL"),Cases.Num());UE_LOG(LogRoguelitePrototype,Display,TEXT("FIXED_LOOP_SUITE_FINISHED Passed=%d Cases=%d %s"),bChecksPassed,Cases.Num(),*Summary);
}
