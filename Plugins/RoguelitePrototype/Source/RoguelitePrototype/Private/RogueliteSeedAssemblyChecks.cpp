#include "RogueliteSeedAssembly.h"
#include "RoguelitePrototype.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Math/RandomStream.h"

namespace
{
    using G=FRogueliteRoomGraphGenerator;
    using Solver=FRogueliteSeedLayoutAssembler;
    void Remove(AActor* A){if(IsValid(A))A->GetWorld()->DestroyActor(A);}
    bool SameMembers(const TArray<FRogueliteTemplateMemberState>& A,const TArray<FRogueliteTemplateMemberState>& B)
    {
        if(A.Num()!=B.Num())return false;
        for(int32 I=0;I<A.Num();++I) if(A[I].Name!=B[I].Name || !A[I].ParentLocalTransform.Equals(B[I].ParentLocalTransform,.0001)
            || A[I].StaticMeshPath!=B[I].StaticMeshPath || A[I].CollisionProfile!=B[I].CollisionProfile || !B[I].bOwnedByInstance)return false;
        return true;
    }
    bool ProbeTemplate(ARogueliteRoomTemplate* R,TArray<FString>& Evidence)
    {
        bool OK=R->ValidateTemplate().bValid; int32 Valid=0;
        FCollisionQueryParams Q; const auto Capsule=FCollisionShape::MakeCapsule(34,88);
        for(const auto& D:R->GetDoorSockets())
        {
            if(!D.bValid){OK=false;continue;} ++Valid;
            const FVector Start=D.WorldPosition-D.WorldOutwardDirection*150+FVector(0,0,90),End=D.WorldPosition+D.WorldOutwardDirection*150+FVector(0,0,90);
            FHitResult Hit;
            const bool Clear=!R->GetWorld()->SweepSingleByChannel(Hit,Start,End,FQuat::Identity,ECC_Pawn,Capsule,Q)
                && !R->GetWorld()->OverlapBlockingTestByChannel(Start,FQuat::Identity,ECC_Pawn,Capsule,Q)
                && !R->GetWorld()->OverlapBlockingTestByChannel(End,FQuat::Identity,ECC_Pawn,Capsule,Q);
            OK&=Clear; Evidence.Add(FString::Printf(TEXT("B2_JUNCTION_PORT Side=%d Offset=%.1f Capsule=%d"),uint8(D.Side),D.AppliedOffset,Clear));
        }
        OK&=Valid==3;
        const auto Original=R->GetTemplateMemberStates();
        R->RoomLength=3000;R->RoomWidth=2500;R->RebuildTemplate();
        OK&=R->ValidateTemplate().bValid && SameMembers(Original,R->GetTemplateMemberStates());
        R->RoomLength=2000;R->RoomWidth=2000;R->RebuildTemplate();
        R->SetActorRotation(FRotator(0,90,0)); R->RebuildTemplate();
        const auto Rotated=R->GetTemplateMemberStates(); OK&=SameMembers(Original,Rotated);
        for(const auto& M:Rotated)OK&=M.WorldTransform.Equals(M.RoomLocalTransform*R->GetActorTransform(),.001);
        Evidence.Add(FString::Printf(TEXT("B2_JUNCTION_DIMENSIONS_AND_RIGID Owned=%d Passed=%d"),Original.Num(),OK));return OK;
    }
    AActor* Block(UWorld* W,const FVector& Position,const FVector& Size)
    {
        auto* A=W->SpawnActor<AActor>(Position,FRotator::ZeroRotator); if(!A)return nullptr;
        auto* M=NewObject<UStaticMeshComponent>(A,TEXT("B2_TestObstacleMesh"));A->AddInstanceComponent(M);A->SetRootComponent(M);
        M->SetMobility(EComponentMobility::Static);M->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));
        M->SetCollisionProfileName(TEXT("BlockAll"));M->SetWorldLocation(Position);M->SetWorldScale3D(Size/100);M->RegisterComponent();return A;
    }
}
void ARogueliteSeedAssembly::ValidateJunction()
{
    bJunctionValidated=false;if(!IsB2EditorWorld())return;
    auto* Option=Settings.Templates.FindByPredicate([](const auto& T){return T.Id==TEXT("Junction3");});
    if(!Option){Evidence.Add(TEXT("B2_JUNCTION_FAIL: Junction3 option missing."));return;}
    auto* Class=Option->TemplateClass.LoadSynchronous();if(!Class)return;
    FActorSpawnParameters P;P.Owner=this;P.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    auto* R=GetWorld()->SpawnActor<ARogueliteRoomTemplate>(Class,FVector(900000,900000,0),FRotator::ZeroRotator,P);
    if(R){bJunctionValidated=ProbeTemplate(R,Evidence);Remove(R);}
    Evidence.Add(FString::Printf(TEXT("B2_JUNCTION_STAGE Passed=%d"),bJunctionValidated));
}

void ARogueliteSeedAssembly::RunB2Checks()
{
    if(!IsB2EditorWorld() || !Batch.OwnedActors.IsEmpty()){Evidence.Add(TEXT("Refused: blank owned B2 batch outside PIE required."));return;}
    Modify(); Evidence.Reset(); ValidateJunction(); if(!bJunctionValidated)return;
    bool Passed=true;
    const auto Check=[&](const FString& Name,bool Value){Passed&=Value;Evidence.Add((Value?TEXT("PASS "):TEXT("FAIL "))+Name);};
    TMap<AActor*,FTransform> Original;
    for(TActorIterator<AActor> I(GetWorld());I;++I)if(!I->IsActorBeingDestroyed())Original.Add(*I,I->GetActorTransform());
    auto Small=Settings;Small.MinMainNodes=Small.MaxMainNodes=3;Small.MinBranches=Small.MaxBranches=1;Small.MinBranchDepth=Small.MaxBranchDepth=1;Small.MaxNodes=4;Small.MaxDegree=3;
    int32 Fixture=INDEX_NONE;
    for(int32 Seed=0;Seed<256;++Seed)
    {
        Small.Seed=Seed;const auto Graph=G::Generate(Seed,Small.GraphSettings());
        FRandomStream Select(G::DeriveSeed(Seed,ERogueliteGraphSeedPurpose::TemplateSelection,Small.AlgorithmVersion,Small.ConfigVersion,0));
        if(Graph.bSucceeded && Graph.Graph.Nodes[1].Degree==3 && Select.RandRange(1,2)==2){Fixture=Seed;break;}
    }
    Check(TEXT("Small fixture middle node degree3 found"),Fixture!=INDEX_NONE); if(Fixture==INDEX_NONE)return;Small.Seed=Fixture;
    auto First=Solver::Generate(this,Small,GetActorTransform());Check(TEXT("Small 4-room/3-edge assembly"),First.bSucceeded && First.Rooms.Num()==4 && First.Edges.Num()==3);
    if(!First.bSucceeded){Evidence.Add(First.Reason);return;}
    Check(TEXT("Middle Junction3 and different Warehouse dimensions"),First.Rooms[1].TemplateId==TEXT("Junction3") && First.Rooms[0].TemplateId==TEXT("Warehouse"));
    int32 Probes=0;TSet<FGuid> Ids;
    for(const auto& E:First.Edges){Probes+=E.Validation.Probes.Num();Ids.Add(E.ConnectionId);Check(TEXT("Actual edge capsules"),E.Validation.bCapsulePassed);}
    Check(TEXT("Unique reservations"),Ids.Num()==3 && First.Registry->GetConnectionRecords().Num()==3);
    const FString Digest=First.LayoutDigest; const FVector ShortEntrance=First.Rooms[1].Room->GetActorLocation()
        -First.Edges[0].Corridor->GetActorForwardVector()*(First.Rooms[1].Room->RoomLength*.5-100)
        +First.Edges[0].Corridor->GetActorRightVector()*600+FVector(0,0,100);
    const FVector PassageBlock=First.Edges.Last().Corridor->GetActorTransform().TransformPosition(FVector(0,20,100));
    Evidence.Add(FString::Printf(TEXT("B2_SMALL Seed=%d Nodes=4 Edges=3 Probes=%d Digest=%s"),Fixture,Probes,*Digest));
    FString Reason;Check(TEXT("Explicit release/owned cleanup"),Solver::Clear(this,First,Reason));
    auto Repeat=Solver::Generate(this,Small,GetActorTransform());Check(TEXT("Repeat same seed spatial digest"),Repeat.bSucceeded && Repeat.LayoutDigest==Digest);
    Solver::Clear(this,Repeat,Reason);
    // A thin off-axis obstruction forces physical rejection and whole-batch cleanup.
    auto* Obstacle=Block(GetWorld(),PassageBlock,FVector(10)); const FTransform ObstacleBefore=Obstacle?Obstacle->GetActorTransform():FTransform::Identity;
    auto Tight=Small;Tight.MaxCandidatesPerEdge=1;
    auto Failure=Solver::Generate(this,Tight,GetActorTransform());
    Check(TEXT("Forced later-edge failure releases partial batch"),!Failure.bSucceeded && Failure.bCleanupComplete && Failure.OwnedActors.IsEmpty() && Failure.Edges.IsEmpty());
    Check(TEXT("Preexisting obstruction identity/transform protected"),IsValid(Obstacle) && Obstacle->GetActorTransform().Equals(ObstacleBefore,.0001));
    Evidence.Add(TEXT("B2_FORCED_FAILURE ")+Failure.Reason);Remove(Obstacle);
    // Off-path obstacle lies in the first short child footprint, outside a longer corridor.
    Obstacle=Block(GetWorld(),ShortEntrance,FVector(20));
    auto LengthRetry=Solver::Generate(this,Small,GetActorTransform());
    bool LengthChanged=false,Rejected=false;
    for(const auto& C:LengthRetry.Candidates){Rejected|=!C.bSucceeded;LengthChanged|=C.bSucceeded && C.Length>1000;}
    Check(TEXT("Corridor length retry avoids occupied short footprint"),LengthRetry.bSucceeded && Rejected && LengthChanged);
    Evidence.Add(FString::Printf(TEXT("B2_LENGTH_RETRY Success=%d Candidates=%d Longer=%d"),LengthRetry.bSucceeded,LengthRetry.Candidates.Num(),LengthChanged));
    Solver::Clear(this,LengthRetry,Reason);Remove(Obstacle);
    // C2 Direct/D template regression, on fresh actors only.
    auto* Class=Small.Templates[0].TemplateClass.LoadSynchronous();
    FActorSpawnParameters P;P.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    auto* A=GetWorld()->SpawnActor<ARogueliteRoomTemplate>(Class,FVector(200000,200000,0),FRotator::ZeroRotator,P);
    auto* B=GetWorld()->SpawnActor<ARogueliteRoomTemplate>(Class,FVector(220000,200000,0),FRotator::ZeroRotator,P);
    auto* Registry=GetWorld()->SpawnActor<ARogueliteRoomAssembly>();Registry->bCheckNavigation=false;
    const auto Before=A->GetTemplateMemberStates();
    const auto Direct=Registry->TryAlignRoomToSocket(A,ERogueliteRoomSide::North,B,ERogueliteRoomSide::South,ERogueliteConnectionMode::Direct);
    Check(TEXT("C2 Direct and D members regression"),Direct.bSucceeded && SameMembers(Before,A->GetTemplateMemberStates()));
    if(Direct.ConnectionId.IsValid())Registry->ReleaseConnection(Direct.ConnectionId);Remove(B);Remove(A);Remove(Registry);
    // Larger sample has bounded retries; report failures honestly rather than dropping rooms.
    auto Large=Small;Large.MinMainNodes=5;Large.MaxMainNodes=8;Large.MinBranches=2;Large.MaxBranches=4;Large.MaxBranchDepth=3;Large.MaxNodes=24;
    int32 Successes=0;
    for(int32 Seed=0;Seed<20;++Seed)
    {
        Large.Seed=Seed;auto Result=Solver::Generate(this,Large,GetActorTransform());Successes+=Result.bSucceeded;
        Evidence.Add(FString::Printf(TEXT("B2_LARGE Seed=%d Success=%d Rooms=%d Edges=%d Candidates=%d Reason=%s"),Seed,Result.bSucceeded,Result.Rooms.Num(),Result.Edges.Num(),Result.Candidates.Num(),*Result.Reason));
        if(Result.bSucceeded)Check(TEXT("Larger batch cleanup"),Solver::Clear(this,Result,Reason));else Check(TEXT("Failed large batch cleanup"),Result.bCleanupComplete);
    }
    Evidence.Add(FString::Printf(TEXT("B2_LARGE_STATS Seeds=20 Success=%d Failed=%d"),Successes,20-Successes));
    for(const auto& Pair:Original)Check(TEXT("Original actor preserved"),IsValid(Pair.Key) && Pair.Key->GetActorTransform().Equals(Pair.Value,.0001));
    Check(TEXT("No orphan connection registries"),[&](){int32 Count=0;for(TActorIterator<ARogueliteRoomAssembly> I(GetWorld());I;++I)if(!I->IsActorBeingDestroyed())++Count;return Count==0;}());
    Settings=Small;Batch=Solver::Generate(this,Settings,GetActorTransform());Check(TEXT("Final saved small layout"),Batch.bSucceeded);
    Evidence.Add(FString::Printf(TEXT("B2_CHECKS_FINISHED Passed=%d Navigation=%d"),Passed,int32(Batch.Navigation)));
    for(const auto& Line:Evidence)UE_LOG(LogRoguelitePrototype,Display,TEXT("%s"),*Line);
}
