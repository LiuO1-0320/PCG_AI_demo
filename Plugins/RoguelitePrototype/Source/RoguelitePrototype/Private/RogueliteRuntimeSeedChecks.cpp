#include "RogueliteRuntimeSeedAssembly.h"
#include "RoguelitePrototype.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"

namespace
{
    int32 FormalCount(UWorld* W)
    {
        int32 Count=0;for(TActorIterator<AActor> I(W);I;++I)
            if(!I->IsActorBeingDestroyed() && (I->IsA<ARogueliteParametricRoom>() || I->IsA<ARogueliteParametricCorridor>() || I->IsA<ARogueliteRoomAssembly>()))++Count;
        return Count;
    }
}

void ARogueliteRuntimeSeedAssembly::RunRuntimeChecks()
{
    if(!GetWorld()->IsGameWorld() || (!GetWorld()->GetOutermost()->GetName().Contains(TEXT("L_Roguelite_RuntimeSeedAssembly_Dev"))
        && !GetWorld()->GetOutermost()->GetName().Contains(TEXT("L_Roguelite_RuntimeNavigation_Dev"))) || !Batch.OwnedActors.IsEmpty())return;
    Evidence.Reset();bool Passed=true;auto* W=GetWorld();const auto OriginalSettings=Settings;
    const auto Check=[&](const FString& Name,bool OK){Passed&=OK;Evidence.Add((OK?TEXT("PASS "):TEXT("FAIL "))+Name);};
    TMap<AActor*,FTransform> Original;
    for(TActorIterator<AActor> I(W);I;++I)if(!I->IsActorBeingDestroyed())Original.Add(*I,I->GetActorTransform());
    Check(TEXT("No saved dungeon actors in GameWorld before generation"),FormalCount(W)==0);
    FRogueliteSeedSpatialPlan InvalidPlan;FRogueliteSeedBatch InvalidBatch;
    Check(TEXT("No-world owner rejected without reservation/actor changes"),!FRogueliteRuntimeSeedAssembler::ValidateAndCommit(
        GetMutableDefault<ARogueliteRuntimeSeedAssembly>(),InvalidPlan,InvalidBatch) && FormalCount(W)==0);
    Settings.Seed=2;Check(TEXT("GameWorld four-room three-corridor generation"),GenerateRuntime() && Batch.Rooms.Num()==4 && Batch.Edges.Num()==3);
    if(!Batch.bSucceeded){Evidence.Add(Batch.Reason);bRuntimeChecksPassed=false;return;}
    Check(TEXT("Junction3 degree3 and Warehouse actual owned POIs"),Batch.Rooms[1].TemplateId==TEXT("Junction3") && Batch.Rooms[1].LogicalDegree==3
        && Batch.Rooms[0].TemplateId==TEXT("Warehouse") && Batch.Rooms[0].Room->GetTemplateMemberStates().Num()>=6);
    const FString Digest=Batch.LayoutDigest;int32 Probes=0;TSet<FGuid> Ids;
    for(const auto& E:Batch.Edges)
    {Ids.Add(E.ConnectionId);Probes+=E.Validation.Probes.Num();Check(TEXT("Runtime edge physical probes"),E.Validation.bCapsulePassed && E.Validation.bGeometryPassed);}
    Check(TEXT("Only C2 unique reservations"),Ids.Num()==3 && Batch.Registry->GetConnectionRecords().Num()==3);
    Check(TEXT("Explicit stages stop at NavigationPending without DungeonReady"),Stage==ERogueliteDungeonBuildStage::NavigationPending && !bDungeonReady && bGeometryValidated
        && StageHistory.Contains(ERogueliteDungeonBuildStage::GraphGenerated) && StageHistory.Contains(ERogueliteDungeonBuildStage::SpatialPlanValid)
        && StageHistory.Contains(ERogueliteDungeonBuildStage::ActorsSpawned) && StageHistory.Contains(ERogueliteDungeonBuildStage::GeometryValidated));
    Check(TEXT("B2 known fixture spatial digest retained"),Digest==TEXT("42537d09b94e589a"));
    Evidence.Add(FString::Printf(TEXT("B3A_SMALL Seed=2 Rooms=4 Corridors=3 Probes=%d Digest=%s"),Probes,*Digest));
    auto& Leaf=Batch.Edges.Last();const auto* LeafRoom=Batch.Rooms.FindByPredicate([&](const auto& R){return R.NodeId==Leaf.ChildNode;});
    const auto* LeafParent=Batch.Rooms.FindByPredicate([&](const auto& R){return R.NodeId==Leaf.ParentNode;});
    const auto BeforeRoom=LeafRoom->Room->GetActorTransform(),BeforeCorridor=Leaf.Corridor->GetActorTransform();
    // Change only the proposed scalar input, never rebuild/move registered Static geometry for this negative test.
    const float BeforeLength=Leaf.Corridor->CorridorLength;Leaf.Corridor->CorridorLength+=100;
    const auto Unsafe=Batch.Registry->TryAlignCorridorConnection(LeafParent->Room,Leaf.ParentSocket,Leaf.Corridor,ERogueliteCorridorEndpoint::Start,LeafRoom->Room,Leaf.ChildSocket);
    Check(TEXT("Registered Static re-alignment rejected without movement"),!Unsafe.bSucceeded && Unsafe.Message.Contains(TEXT("Registered Static runtime"))
        && BeforeRoom.Equals(LeafRoom->Room->GetActorTransform(),.0001) && BeforeCorridor.Equals(Leaf.Corridor->GetActorTransform(),.0001));
    Leaf.Corridor->CorridorLength=BeforeLength;Check(TEXT("Existing connection revalidates after restoring scalar"),FRogueliteSeedLayoutAssembler::Revalidate(this,Batch));
    Check(TEXT("Runtime release/cleanup"),ClearRuntime() && FormalCount(W)==0);
    Check(TEXT("Runtime deterministic repeat"),GenerateRuntime() && Batch.LayoutDigest==Digest);Check(TEXT("Repeat cleanup"),ClearRuntime());
    // Inject an independent obstacle AFTER planning/spawn, proving physical failure clears actual actors and earlier reservations.
    const auto Plan=FRogueliteSeedLayoutAssembler::Plan(this,Settings,GetActorTransform());
    auto Failure=FRogueliteRuntimeSeedAssembler::SpawnPlannedActors(this,Plan);
    Check(TEXT("Forced-failure fixture has actual spawned actors"),Plan.bValid && Failure.OwnedActors.Num()==8);
    AActor* Obstacle=nullptr;
    if(Plan.bValid && Plan.Edges.Num()==3)
    {
        FActorSpawnParameters P;P.Owner=this;P.ObjectFlags|=RF_Transient;
        Obstacle=W->SpawnActor<AActor>(AActor::StaticClass(),Plan.Edges.Last().Transform,P);
        if(Obstacle)
        {
            auto* M=NewObject<UStaticMeshComponent>(Obstacle,TEXT("B3A_IndependentObstacle"));Obstacle->AddInstanceComponent(M);Obstacle->SetRootComponent(M);
            M->SetMobility(EComponentMobility::Movable);M->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));
            M->SetCollisionProfileName(TEXT("BlockAll"));M->SetWorldLocation(Plan.Edges.Last().Transform.TransformPosition(FVector(0,20,100)));M->SetWorldScale3D(FVector(.1));M->RegisterComponent();
        }
    }
    const auto BeforeObstacle=Obstacle?Obstacle->GetActorTransform():FTransform::Identity;
    const bool Rejected=!FRogueliteRuntimeSeedAssembler::ValidateAndCommit(this,Plan,Failure);
    Check(TEXT("Post-spawn physical failure rolls back prior reservations/8 actors"),Rejected && Failure.bCleanupComplete && Failure.OwnedActors.IsEmpty()
        && FormalCount(W)==0 && Failure.FailedEdge==2 && !Failure.FailureValidation.Probes.IsEmpty());
    Check(TEXT("Independent blocker protected by batch cleanup"),IsValid(Obstacle) && Obstacle->GetActorTransform().Equals(BeforeObstacle,.0001));
    Evidence.Add(TEXT("B3A_FORCED_FAILURE ")+Failure.Reason);
    // Full original failure probe data is published for MCP; only this explicit test fixture is removed here.
    LastFailure=Failure;
    if(IsValid(Obstacle))W->DestroyActor(Obstacle);
    int32 Successes=0;TSet<FString> Digests;
    for(int32 Seed=0;Seed<20;++Seed)
    {
        Settings.Seed=Seed;const bool Success=GenerateRuntime();Successes+=Success;
        if(Success)Digests.Add(Batch.LayoutDigest);
        Evidence.Add(FString::Printf(TEXT("B3A_SEED Seed=%d Success=%d Rooms=%d Corridors=%d Digest=%s Reason=%s"),
            Seed,Success,Batch.Rooms.Num(),Batch.Edges.Num(),*Batch.LayoutDigest,*Batch.Reason));
        Check(TEXT("Multi-seed runtime cleanup"),ClearRuntime() && FormalCount(W)==0);
    }
    Check(TEXT("20 runtime seeds geometrically successful"),Successes==20);
    Evidence.Add(FString::Printf(TEXT("B3A_STATS Seeds=20 Passed=%d Failed=%d UniqueDigests=%d"),Successes,20-Successes,Digests.Num()));
    for(const auto& Item:Original)Check(TEXT("Original GameWorld actor identity/Transform retained"),IsValid(Item.Key) && Item.Key->GetActorTransform().Equals(Item.Value,.0001));
    Settings=OriginalSettings;Settings.Seed=2;Check(TEXT("Final inspectable runtime layout"),GenerateRuntime());
    bRuntimeChecksPassed=Passed;Evidence.Add(FString::Printf(TEXT("B3A_CHECKS_FINISHED Passed=%d GameWorld=%d Geometry=%d DungeonReady=%d"),Passed,W->IsGameWorld(),bGeometryValidated,bDungeonReady));
    for(const auto& Line:Evidence)UE_LOG(LogRoguelitePrototype,Display,TEXT("%s"),*Line);
}

void ARogueliteRuntimeSeedAssembly::RunEditorRegression()
{
    if(!GetWorld() || GetWorld()->IsGameWorld() || (GetWorld()->GetOutermost()->GetName()!=TEXT("/Game/Roguelite/Maps/L_Roguelite_RuntimeSeedAssembly_Dev")
        && GetWorld()->GetOutermost()->GetName()!=TEXT("/Game/Roguelite/Maps/L_Roguelite_RuntimeNavigation_Dev")))return;
    EditorRegressionEvidence.Reset();bool Passed=true;
    const auto Check=[&](const FString& Name,bool OK){Passed&=OK;EditorRegressionEvidence.Add((OK?TEXT("PASS "):TEXT("FAIL "))+Name);};
    auto S=Settings;S.Seed=2;S.bCheckNavigation=false;
    auto First=FRogueliteSeedLayoutAssembler::Generate(this,S,FTransform::Identity);
    Check(TEXT("B2 Editor generation and exact original fixture digest"),First.bSucceeded && First.Rooms.Num()==4 && First.Edges.Num()==3 && First.LayoutDigest==TEXT("42537d09b94e589a"));
    for(const auto& E:First.Edges)Check(TEXT("C2 Editor connection/capsule regression"),E.Validation.bSucceeded && E.Validation.bCapsulePassed && E.Validation.Probes.Num()==59);
    FString Reason;Check(TEXT("Editor generated batch clears"),FRogueliteSeedLayoutAssembler::Clear(this,First,Reason));
    auto Repeat=FRogueliteSeedLayoutAssembler::Generate(this,S,FTransform::Identity);
    Check(TEXT("B2 Editor repeated ordered candidates"),Repeat.bSucceeded && Repeat.LayoutDigest==TEXT("42537d09b94e589a"));
    Check(TEXT("Repeated Editor batch clears"),FRogueliteSeedLayoutAssembler::Clear(this,Repeat,Reason));
    auto* Class=S.Templates[0].TemplateClass.LoadSynchronous();FActorSpawnParameters P;P.Owner=this;P.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    auto* A=GetWorld()->SpawnActor<ARogueliteRoomTemplate>(Class,FVector(0,-50000,0),FRotator::ZeroRotator,P);
    auto* B=GetWorld()->SpawnActor<ARogueliteRoomTemplate>(Class,FVector(20000,-50000,0),FRotator::ZeroRotator,P);
    auto* Registry=GetWorld()->SpawnActor<ARogueliteRoomAssembly>();Registry->bCheckNavigation=false;
    const auto Direct=Registry->TryAlignRoomToSocket(A,ERogueliteRoomSide::North,B,ERogueliteRoomSide::South,ERogueliteConnectionMode::Direct);
    Check(TEXT("C2 Direct feature retained"),Direct.bSucceeded && Direct.bCapsulePassed);
    if(Direct.ConnectionId.IsValid())Registry->ReleaseConnection(Direct.ConnectionId);
    for(AActor* Actor:{static_cast<AActor*>(A),static_cast<AActor*>(B),static_cast<AActor*>(Registry)})if(IsValid(Actor))GetWorld()->DestroyActor(Actor);
    Check(TEXT("No Editor regression geometry/reservation leftovers"),FormalCount(GetWorld())==0);
    bEditorRegressionPassed=Passed;EditorRegressionEvidence.Add(FString::Printf(TEXT("B3A_EDITOR_REGRESSION Passed=%d"),Passed));
    for(const auto& Line:EditorRegressionEvidence)UE_LOG(LogRoguelitePrototype,Display,TEXT("%s"),*Line);
}
