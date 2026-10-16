#include "RogueliteLoopPlanningTestRig.h"
#include "RogueliteBranchingTestSettings.h"
#include "RogueliteSeedAssemblyInternal.h"
#include "RoguelitePrototype.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "TimerManager.h"
#include "DrawDebugHelpers.h"
namespace
{
using G=FRogueliteRoomGraphGenerator;
FString Scene(UWorld* W)
{
    TArray<FString> Rows;for(TActorIterator<AActor> I(W);I;++I)if(!I->IsActorBeingDestroyed())
    {Rows.Add(I->GetPathName()+TEXT("|")+I->GetActorTransform().ToString());TInlineComponentArray<USceneComponent*> Cs;I->GetComponents(Cs);for(const auto* C:Cs)Rows.Add(C->GetPathName()+TEXT("|")+C->GetRelativeTransform().ToString());
        if(const auto* R=Cast<ARogueliteParametricRoom>(*I))for(const auto& D:{R->NorthDoor,R->SouthDoor,R->EastDoor,R->WestDoor})Rows.Add(FString::Printf(TEXT("door=%d,%d,%.6f,%.6f,%.6f"),D.bEnabled,D.bAutoCenter,D.ManualOffset,D.DoorWidth,D.DoorHeight));
        if(const auto* R=Cast<ARogueliteRoomAssembly>(*I))for(const auto& C:R->GetConnectionRecords())Rows.Add(C.Id.ToString()+FString::Printf(TEXT("|active=%d|rev=%d|valid=%d"),C.bActive,C.ValidationRevision,C.bGeometryValid));
    }Rows.Sort();FString Text;for(const auto& R:Rows)Text+=R+TEXT("\n");return G::StableTextDigest(Text);
}
template<class T> T* Owned(UWorld* W,AActor* Owner,FRogueliteSeedBatch& B,const FTransform& Tfm,TFunction<void(T*)> Configure={})
{
    FActorSpawnParameters P;P.Owner=Owner;P.ObjectFlags|=RF_Transient;P.bDeferConstruction=true;P.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    P.CustomPreSpawnInitialization=[Configure](AActor* A){if(Configure)Configure(CastChecked<T>(A));};auto* A=W->SpawnActor<T>(T::StaticClass(),Tfm,P);
    if(A){A->Tags.Add(RogueliteSeedAssemblyInternal::BatchTag(B.BatchId));B.OwnedActors.Add(A);A->FinishSpawning(Tfm);}return A;
}
AActor* Blocker(UWorld* W,AActor* Owner,const FTransform& Pose)
{
    FActorSpawnParameters P;P.Owner=Owner;P.ObjectFlags|=RF_Transient;auto* A=W->SpawnActor<AActor>(AActor::StaticClass(),Pose,P);if(!A)return nullptr;
    auto* C=NewObject<UStaticMeshComponent>(A,TEXT("LoopFixtureIndependentBlocker"));A->AddInstanceComponent(C);A->SetRootComponent(C);C->SetMobility(EComponentMobility::Movable);
    C->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));C->SetCollisionProfileName(TEXT("BlockAll"));C->SetWorldTransform(Pose);C->RegisterComponent();return A;
}
}
ARogueliteLoopPlanningTestRig::ARogueliteLoopPlanningTestRig(){Settings=RogueliteBranchingTestSettings();}
bool ARogueliteLoopPlanningTestRig::OwnMap() const
{return GetWorld()&&!GetWorld()->IsGameWorld()&&GetWorld()->GetOutermost()->GetName()==TEXT("/Game/Roguelite/Maps/L_Roguelite_LoopPlanning_Dev");}
void ARogueliteLoopPlanningTestRig::GenerateTreeLayout()
{
    if(!OwnMap()||!Batch.OwnedActors.IsEmpty()){Summary=TEXT("Refused: only own empty test map/batch; clear your preview explicitly first.");return;}Modify();Batch=FRogueliteSeedLayoutAssembler::Generate(this,Settings,GetActorTransform());Summary=Batch.Reason;
}
void ARogueliteLoopPlanningTestRig::AnalyzeLoopCandidates()
{
    if(!OwnMap())return;const auto Before=Scene(GetWorld());LoopPlan=FRogueliteLoopPlanner::Analyze(this,Batch,Settings,LoopSettings);
    Summary=FString::Printf(TEXT("%s\nTopologyPairs=%d SocketPairs=%d StraightPairs=%d CornerDiagnostics=%d Selected=%d Checks=%d SceneUnchanged=%d\nPlanning only; ActualConnectionCommitted=0."),*LoopPlan.Reason,LoopPlan.TopologyPairs,LoopPlan.SocketCompatiblePairs,LoopPlan.StraightPassedPairs,LoopPlan.CornerRequiredPairs,LoopPlan.SelectedCandidates.Num(),LoopPlan.CandidateChecks,Before==Scene(GetWorld()));
    UE_LOG(LogRoguelitePrototype,Display,TEXT("LOOP_ANALYZE %s Digest=%s"),*Summary,*LoopPlan.StableDigest);
}
void ARogueliteLoopPlanningTestRig::DisplayCandidateSummary()
{
    if(!OwnMap())return;FString Reason;if(!FRogueliteLoopPlanner::IsCurrent(this,Batch,Settings,LoopSettings,LoopPlan,Reason)){Summary=Reason;return;}
    int32 Drawn=0;for(const auto& C:LoopPlan.Candidates)if(C.bHasSocketPair&&Drawn<32&&C.Feasibility!=ERogueliteLoopFeasibility::Rejected)
    {const bool Straight=C.Feasibility==ERogueliteLoopFeasibility::StraightPrecheckPassed;const FColor Color=Straight?FColor::Green:FColor::Orange;
        DrawDebugLine(GetWorld(),C.DoorA.WorldPosition+FVector(0,0,100),C.DoorB.WorldPosition+FVector(0,0,100),Color,false,20,0,4);
        DrawDebugString(GetWorld(),(C.DoorA.WorldPosition+C.DoorB.WorldPosition)*.5+FVector(0,0,140),FString::Printf(TEXT("PLAN %d-%d %s (no corridor)"),C.NodeA,C.NodeB,Straight?TEXT("StraightPrecheck"):TEXT("Corner UNPROVEN")),nullptr,Color,20);++Drawn;}
    Summary=LoopPlan.Reason+TEXT("\nTemporary debug lines are planning annotations, not walkable geometry.");
}
void ARogueliteLoopPlanningTestRig::ClearOwnedTestBatch()
{if(OwnMap()){Modify();FString Reason;FRogueliteSeedLayoutAssembler::Clear(this,Batch,Reason);Summary=Reason+TEXT(" Old loop plan is stale; no candidate reservation to release.");}}
FRogueliteSeedBatch ARogueliteLoopPlanningTestRig::Fixture(FRogueliteSeedLayoutSettings& S)
{
    S=FRogueliteSeedLayoutSettings();S.Seed=777;S.MinMainNodes=S.MaxMainNodes=5;S.MinBranches=S.MaxBranches=0;S.MaxNodes=5;S.Templates.Reset();
    FRogueliteSeedTemplateOption Option;Option.Id=TEXT("IndependentRectFixture");Option.TemplateClass=ARogueliteRoomTemplate::StaticClass();S.Templates.Add(Option);
    S.Corridors.Reset();FRogueliteSeedCorridorProfile Profile;Profile.Id=TEXT("FixtureStraight");Profile.LengthCandidates={1960};S.Corridors.Add(Profile);auto Alternate=Profile;Alternate.Id=TEXT("FixtureStraightAlternate");S.Corridors.Add(Alternate);
    FRogueliteSeedBatch B;B.Seed=S.Seed;B.BatchId=FGuid::NewGuid();const auto Graph=G::Generate(S.Seed,S.GraphSettings());B.GraphDigest=Graph.Graph.StableDigest;B.ConfigurationDigest=TEXT("Explicit five-room rectangular fixture; not random sample");
    const FVector Centers[]={FVector(0,0,0),FVector(4000,0,0),FVector(4000,4000,0),FVector(0,4000,0),FVector(0,8000,0)};
    const ERogueliteRoomSide From[]={ERogueliteRoomSide::North,ERogueliteRoomSide::East,ERogueliteRoomSide::South,ERogueliteRoomSide::East};
    const ERogueliteRoomSide To[]={ERogueliteRoomSide::South,ERogueliteRoomSide::West,ERogueliteRoomSide::North,ERogueliteRoomSide::West};
    B.Registry=Owned<ARogueliteRoomAssembly>(GetWorld(),this,B,FTransform::Identity);if(!B.Registry){B.Reason=TEXT("Fixture registry unavailable.");return B;}B.Registry->bCheckNavigation=false;
    for(int32 I=0;I<5;++I)
    {
        TArray<ERogueliteRoomSide> Enabled;if(I>0)Enabled.Add(To[I-1]);if(I<4)Enabled.Add(From[I]);if(I==0)Enabled.Add(ERogueliteRoomSide::East);if(I==3)Enabled.Add(ERogueliteRoomSide::West);
        auto* R=Owned<ARogueliteRoomTemplate>(GetWorld(),this,B,FTransform(Centers[I]),[Enabled](auto* A)
        {
            A->TemplateId=TEXT("IndependentRectFixture");A->RoomLength=A->RoomWidth=2000;A->RoomHeight=400;
            A->NorthDoor.bEnabled=Enabled.Contains(ERogueliteRoomSide::North);A->SouthDoor.bEnabled=Enabled.Contains(ERogueliteRoomSide::South);A->EastDoor.bEnabled=Enabled.Contains(ERogueliteRoomSide::East);A->WestDoor.bEnabled=Enabled.Contains(ERogueliteRoomSide::West);
            auto* POI=NewObject<UStaticMeshComponent>(A,TEXT("FixtureOwnedPOI"));A->AddInstanceComponent(POI);POI->SetupAttachment(A->TemplateContentRoot.Get());POI->SetMobility(EComponentMobility::Static);
            POI->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));POI->SetCollisionProfileName(TEXT("BlockAll"));POI->SetRelativeLocation(FVector(300,300,50));POI->SetRelativeScale3D(FVector(.5,.5,1));
        });
        if(R){for(auto* C:R->GetTemplateOwnedComponents())if(!C->IsRegistered())C->RegisterComponent();R->RebuildTemplate();}
        if(!R){B.Reason=TEXT("Fixture room unavailable.");return B;}auto& Record=B.Rooms.AddDefaulted_GetRef();Record.NodeId=I;Record.Room=R;Record.TemplateId=Option.Id;Record.TemplateClass=R->GetClass();Record.PlacedTransform=R->GetActorTransform();Record.Dimensions=FVector(2000,2000,400);Record.WallThickness=R->WallThickness;Record.FloorThickness=R->FloorThickness;Record.LogicalDegree=Graph.Graph.Nodes[I].Degree;Record.TemplateCapacity=Enabled.Num();Record.MemberSignature=RogueliteSeedAssemblyInternal::MembersText(R);Record.RoomType=I==4?ERogueliteSeedRoomType::Final:ERogueliteSeedRoomType::Combat;
    }
    for(int32 I=0;I<4;++I)
    {
        FRogueliteDoorSocket DA,DB;B.Rooms[I].Room->TryGetDoorSocket(From[I],DA);B.Rooms[I+1].Room->TryGetDoorSocket(To[I],DB);
        auto* C=Owned<ARogueliteParametricCorridor>(GetWorld(),this,B,FTransform(DA.WorldOutwardDirection.Rotation().Quaternion(),(DA.WorldPosition+DB.WorldPosition)*.5),[Profile](auto* A)
        {A->CorridorLength=1960;A->CorridorWidth=Profile.Width;A->CorridorHeight=Profile.Height;A->WallThickness=Profile.WallThickness;A->FloorThickness=Profile.FloorThickness;A->DoorWidth=Profile.DoorWidth;A->DoorHeight=Profile.DoorHeight;});
        if(!C){B.Reason=TEXT("Fixture corridor unavailable.");return B;}const auto Validation=B.Registry->TryAlignCorridorConnection(B.Rooms[I].Room,From[I],C,ERogueliteCorridorEndpoint::Start,B.Rooms[I+1].Room,To[I]);
        if(!Validation.bSucceeded){B.Reason=Validation.Message;return B;}auto& E=B.Edges.AddDefaulted_GetRef();E.EdgeId=I;E.ParentNode=I;E.ChildNode=I+1;E.Corridor=C;E.ConnectionId=Validation.ConnectionId;E.ParentSocket=From[I];E.ChildSocket=To[I];E.CorridorProfile=I%2?Alternate.Id:Profile.Id;E.CorridorLength=1960;E.Validation=Validation;
    }
    B.bSucceeded=B.bGeometryValid=true;B.LayoutDigest=G::StableTextDigest(FRogueliteSeedLayoutAssembler::DescribeLayout(B));B.Reason=TEXT("Independent real five-room Tree fixture generated, four actual C2 connections; future fifth edge unbuilt.");return B;
}
void ARogueliteLoopPlanningTestRig::RunLoopChecks()
{
    if(!OwnMap()||!Batch.OwnedActors.IsEmpty()){Summary=TEXT("Tests refuse to overwrite an existing user preview or another map.");return;}
    Cases.Reset();Evidence.Reset();bChecksPassed=true;const auto InitialScene=Scene(GetWorld());const auto InitialSettings=Settings;
    const auto Check=[&](const FString& Name,bool Passed){bChecksPassed&=Passed;Evidence.Add((Passed?TEXT("PASS "):TEXT("FAIL "))+Name);UE_LOG(LogRoguelitePrototype,Display,TEXT("LOOP_CHECK %s"),*Evidence.Last());};
    const auto Analyze=[&](const FString& Name,const FRogueliteSeedLayoutSettings& S,FRogueliteSeedBatch& B,const FRogueliteLoopPlanningSettings& L)
    {
        FRogueliteLoopCaseEvidence C;C.Case=Name;C.Seed=S.Seed;C.bTreeSucceeded=B.bSucceeded;C.TreeReason=B.Reason;C.LayoutDigest=B.LayoutDigest;
        const auto Before=Scene(GetWorld());if(B.bSucceeded)C.Plan=FRogueliteLoopPlanner::Analyze(this,B,S,L);C.bSceneUnchanged=Before==Scene(GetWorld());Check(Name+TEXT(" planning leaves Actors/components/doors/C2 records unchanged"),C.bSceneUnchanged);Cases.Add(C);return C.Plan;
    };
    int32 Trees=0,HasTopology=0,HasStraight=0,Corner=0;FRogueliteLoopPlanningSettings L;
    for(int32 Seed=0;Seed<20;++Seed)
    {
        const int32 FirstCase=Cases.Num();
        const auto S=RogueliteBranchingTestSettings(Seed);auto B=FRogueliteSeedLayoutAssembler::Generate(this,S,FTransform(FRotator(0,(Seed%4)*90,0)));
        const auto P=Analyze(FString::Printf(TEXT("Random Seed%d"),Seed),S,B,L);Trees+=B.bSucceeded;HasTopology+=P.TopologyPairs>0;HasStraight+=P.StraightPassedPairs>0;Corner+=P.CornerRequiredPairs>0;
        if(B.bSucceeded)
        {
            const auto Again=FRogueliteLoopPlanner::Analyze(this,B,S,L);Check(TEXT("Same live batch stable loop summary"),P.StableDigest==Again.StableDigest);
            auto Required=L;Required.MinLoops=1;const auto Need=Analyze(FString::Printf(TEXT("Random Seed%d MinLoops1"),Seed),S,B,Required);
            Check(TEXT("Mandatory loop outcome explicit"),Need.SelectedCandidates.Num()>=1?Need.bRequirementsSatisfied:Need.State==ERogueliteLoopPlanningState::LoopRequirementUnsatisfied);
            FString Reason;Check(TEXT("Live plan current before cleanup"),FRogueliteLoopPlanner::IsCurrent(this,B,S,L,P,Reason));Check(TEXT("Only owned Tree clears"),FRogueliteSeedLayoutAssembler::Clear(this,B,Reason));
            Check(TEXT("Cleared batch invalidates old loop plan"),!FRogueliteLoopPlanner::IsCurrent(this,B,S,L,P,Reason));
        }
        else Check(TEXT("Original Tree failure separate and batch cleaned"),B.bCleanupComplete&&B.OwnedActors.IsEmpty()&&!B.Reason.IsEmpty());
        for(int32 I=FirstCase;I<Cases.Num();++I)Cases[I].bCleanupComplete=B.OwnedActors.IsEmpty();
    }
    UE_LOG(LogRoguelitePrototype,Display,TEXT("LOOP_SPATIAL_STATS Seeds=20 TreeSuccess=%d TreeFailure=%d TreesWithTopologyPairs=%d TreesWithStraightPrecheck=%d TreesWithCornerDiagnosis=%d NoLoopActors=1"),Trees,20-Trees,HasTopology,HasStraight,Corner);
    FRogueliteSeedLayoutSettings FS;auto F=Fixture(FS);
    {FRogueliteLoopCaseEvidence C;C.Case=TEXT("Fixed fixture creation");C.Seed=777;C.bTreeSucceeded=F.bSucceeded;C.TreeReason=F.Reason;C.LayoutDigest=F.LayoutDigest;Cases.Add(C);}
    Check(TEXT("Independent fixed fixture actual Tree/C2/collision: ")+F.Reason,F.bSucceeded&&F.Edges.Num()==4);
    if(F.bSucceeded)
    {
        auto Required=L;Required.MinLoops=1;const auto P=Analyze(TEXT("Fixed fixture positive - not random sample"),FS,F,Required);
        Check(TEXT("Fixed fixture discovers genuine straight candidate"),P.StraightPassedPairs>0&&P.bRequirementsSatisfied&&!P.SelectedCandidates.IsEmpty());
        TSet<FString> Ports;bool Unique=true;for(const auto& C:P.Candidates)if(C.bSelected){for(const auto& Key:{FString::Printf(TEXT("%d:%d"),C.NodeA,uint8(C.SocketA)),FString::Printf(TEXT("%d:%d"),C.NodeB,uint8(C.SocketB))}){Unique&=!Ports.Contains(Key);Ports.Add(Key);}}
        Check(TEXT("Selected candidates use unique provisional ports"),Unique);Check(TEXT("No candidate committed to original Tree registry"),F.Registry->GetConnectionRecords().Num()==4&&F.Edges.Num()==4);
        Check(TEXT("Competing straight profiles cannot claim same port/pair twice"),P.Candidates.ContainsByPredicate([](const auto& C){return C.Feasibility==ERogueliteLoopFeasibility::StraightPrecheckPassed&&!C.bSelected&&C.SelectionReason.Contains(TEXT("conflict"));}));
        auto Two=Required;Two.MinLoops=Two.MaxLoops=2;const auto CannotDouble=Analyze(TEXT("Fixed fixture MinLoops2 cannot double-count same pair"),FS,F,Two);Check(TEXT("Two profile variants do not satisfy two-loop minimum"),CannotDouble.State==ERogueliteLoopPlanningState::LoopRequirementUnsatisfied&&CannotDouble.SelectedCandidates.Num()==1);
        const auto Repeat=FRogueliteLoopPlanner::Analyze(this,F,FS,Required);Check(TEXT("Fixture candidate sort/reasons deterministic"),P.StableDigest==Repeat.StableDigest);
        const auto* Good=P.Candidates.FindByPredicate([](const auto& C){return C.bSelected;});
        if(Good)
        {
            FActorSpawnParameters Args;Args.Owner=this;Args.ObjectFlags|=RF_Transient;Args.bDeferConstruction=true;Args.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
            Args.CustomPreSpawnInitialization=[](AActor* A){auto* R=CastChecked<ARogueliteParametricRoom>(A);R->RoomLength=R->RoomWidth=200;R->RoomHeight=300;};
            const FTransform RoomPose(Good->CorridorTransform.GetLocation());auto* ForeignRoom=GetWorld()->SpawnActor<ARogueliteParametricRoom>(ARogueliteParametricRoom::StaticClass(),RoomPose,Args);
            Check(TEXT("Explicit independent parameter-room fixture created"),IsValid(ForeignRoom));if(ForeignRoom)
            {
                ForeignRoom->FinishSpawning(RoomPose);const auto Before=ForeignRoom->GetActorTransform();
                const auto Conflict=Analyze(TEXT("Fixed fixture corridor intersects independent parameter room"),FS,F,Required);
                Check(TEXT("Parameter footprint rejects real other-room overlap"),Conflict.State==ERogueliteLoopPlanningState::LoopRequirementUnsatisfied&&Conflict.Candidates.ContainsByPredicate([](const auto& C){return C.Reason.Contains(TEXT("external parameter room"));}));
                Check(TEXT("Independent parameter room not moved/deleted by planner"),IsValid(ForeignRoom)&&ForeignRoom->GetActorTransform().Equals(Before,0));ForeignRoom->Destroy();
            }
            const auto OrderReplay=[&](bool Reverse)
            {
                TArray<ARogueliteParametricRoom*> ExternalRooms;
                for(int32 Step=0;Step<2;++Step)
                {
                    const int32 Index=Reverse?1-Step:Step;
                    const FTransform Pose(Good->CorridorTransform.GetLocation()+FVector(Index*50,0,0));
                    auto* Other=GetWorld()->SpawnActor<ARogueliteParametricRoom>(ARogueliteParametricRoom::StaticClass(),Pose,Args);
                    Check(TEXT("Creation-order parameter-room fixture created"),IsValid(Other));
                    if(Other){Other->FinishSpawning(Pose);ExternalRooms.Add(Other);}
                }
                const auto Plan=Analyze(Reverse?TEXT("Fixed fixture other-room reverse creation order"):TEXT("Fixed fixture other-room forward creation order"),FS,F,Required);
                Check(TEXT("Multiple occupied-room fixture rejects minimum"),ExternalRooms.Num()==2&&Plan.State==ERogueliteLoopPlanningState::LoopRequirementUnsatisfied);
                for(auto* Other:ExternalRooms)Other->Destroy();return Plan;
            };
            const auto Forward=OrderReplay(false),Reverse=OrderReplay(true);
            Check(TEXT("Actor creation order does not change source/candidate diagnostics or digest"),Forward.SourceStateDigest==Reverse.SourceStateDigest&&Forward.StableDigest==Reverse.StableDigest);
            const auto Pose=FTransform(Good->CorridorTransform.GetRotation(),Good->CorridorTransform.GetLocation()+FVector(0,0,150),FVector(1,6,3));auto* Obstacle=Blocker(GetWorld(),this,Pose);
            Check(TEXT("Explicit independent obstacle created"),IsValid(Obstacle));if(Obstacle)
            {
                const auto Before=Obstacle->GetActorTransform();FString Reason;Check(TEXT("New obstacle invalidates earlier plan"),!FRogueliteLoopPlanner::IsCurrent(this,F,FS,Required,P,Reason));
                const auto Blocked=Analyze(TEXT("Fixed fixture independent full-width obstacle"),FS,F,Required);Check(TEXT("Actual world collision refuses the candidate"),Blocked.State==ERogueliteLoopPlanningState::LoopRequirementUnsatisfied);
                Check(TEXT("Planner preserves external blocker"),IsValid(Obstacle)&&Obstacle->GetActorTransform().Equals(Before,0));Obstacle->Destroy();
            }
            auto* RA=F.Rooms[Good->NodeA].Room.Get();const auto Side=Good->SocketA;auto* Door=Side==ERogueliteRoomSide::North?&RA->NorthDoor:Side==ERogueliteRoomSide::South?&RA->SouthDoor:Side==ERogueliteRoomSide::East?&RA->EastDoor:&RA->WestDoor;
            const auto Original=*Door;Door->DoorHeight=240;RA->RebuildTemplate();const auto Bad=Analyze(TEXT("Fixed fixture free-door height mismatch"),FS,F,Required);
            Check(TEXT("Door specification mismatch explicit"),Bad.State==ERogueliteLoopPlanningState::LoopRequirementUnsatisfied&&Bad.Candidates.ContainsByPredicate([](const auto& C){return C.Reason.Contains(TEXT("SpecificationMismatch"));}));*Door=Original;RA->RebuildTemplate();
        }
        auto Exhausted=Required;Exhausted.MaxCandidateChecks=1;const auto Limited=Analyze(TEXT("Fixed fixture bounded search exhausted"),FS,F,Exhausted);
        Check(TEXT("Budget bound explicit and MinLoops unchanged"),Limited.bCandidateBudgetExhausted&&!Limited.bRequirementsSatisfied);
        FString Reason;Check(TEXT("Fixed fixture owned batch clean"),FRogueliteSeedLayoutAssembler::Clear(this,F,Reason));Check(TEXT("Fixed fixture stale plan rejected"),!FRogueliteLoopPlanner::IsCurrent(this,F,FS,Required,P,Reason));for(auto& C:Cases)if(C.Seed==777)C.bCleanupComplete=F.OwnedActors.IsEmpty();
    }
    else {FString Reason;const bool Cleaned=FRogueliteSeedLayoutAssembler::Clear(this,F,Reason);Check(TEXT("Failed fixture cleanup: ")+Reason,Cleaned);for(auto& C:Cases)if(C.Seed==777)C.bCleanupComplete=Cleaned;}
    auto Legacy=GetDefault<ARogueliteSeedAssembly>()->Settings;Legacy.Seed=2;auto B=FRogueliteSeedLayoutAssembler::Generate(this,Legacy,FTransform::Identity);Check(TEXT("V1 B2 original Seed2 layout unchanged: ")+B.Reason,B.bSucceeded&&B.LayoutDigest==TEXT("42537d09b94e589a"));FString Reason;Check(TEXT("V1 owned batch cleanup"),FRogueliteSeedLayoutAssembler::Clear(this,B,Reason));
    Check(TEXT("Initial external scene exactly preserved"),InitialScene==Scene(GetWorld()));Settings=InitialSettings;
    Summary=FString::Printf(TEXT("Tests=%s; random Trees=%d/20, random straight candidates present=%d/%d, fixed fixture separate. ActualConnectionCommitted=0."),bChecksPassed?TEXT("PASS"):TEXT("FAIL"),Trees,HasStraight,Trees);
    UE_LOG(LogRoguelitePrototype,Display,TEXT("LOOP_SUITE_FINISHED Passed=%d Cases=%d %s"),bChecksPassed,Cases.Num(),*Summary);
}
ARogueliteRuntimeLoopPlanningTestRig::ARogueliteRuntimeLoopPlanningTestRig(){Settings=RogueliteBranchingTestSettings();bEnableRuntimeNavigation=false;}
void ARogueliteRuntimeLoopPlanningTestRig::BeginPlay(){Super::BeginPlay();GetWorldTimerManager().SetTimerForNextTick(this,&ARogueliteRuntimeLoopPlanningTestRig::AnalyzeSpawnedTree);}
void ARogueliteRuntimeLoopPlanningTestRig::AnalyzeSpawnedTree()
{
    const auto Before=Scene(GetWorld());FRogueliteLoopPlanningSettings S;LoopPlan=FRogueliteLoopPlanner::Analyze(this,Batch,Settings,S);bPlanningUnchanged=Before==Scene(GetWorld())&&bGeometryValidated&&LoopPlan.bTreeValid&&Batch.Rooms.Num()==8&&Batch.Edges.Num()==7&&!bDungeonReady;
    UE_LOG(LogRoguelitePrototype,Display,TEXT("LOOP_RUNTIME_CHECK GameWorld=%d DeferredTreeGeometry=%d PlanningUnchanged=%d TopologyPairs=%d StraightPairs=%d Digest=%s ActualLoops=0 NavigationNotRun=1"),GetWorld()->IsGameWorld(),bGeometryValidated,bPlanningUnchanged,LoopPlan.TopologyPairs,LoopPlan.StraightPassedPairs,*LoopPlan.StableDigest);
}
