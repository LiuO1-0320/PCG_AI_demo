#include "RogueliteBranchingTestRig.h"
#include "RogueliteBranchingTestSettings.h"
#include "RogueliteSeedAssemblyInternal.h"
#include "RogueliteNavigationReadinessComponent.h"
#include "RogueliteDungeonMap.h"
#include "RoguelitePrototype.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "TimerManager.h"

namespace
{
FRogueliteBranchingCaseEvidence Capture(const FString& Name,const FRogueliteSeedBatch& B,const FRogueliteSeedLayoutSettings& S)
{
    FRogueliteBranchingCaseEvidence E;E.Case=Name;E.Seed=B.Seed;E.BatchId=B.BatchId;E.bSucceeded=B.bSucceeded&&B.bGeometryValid;
    E.bCleanupComplete=B.bCleanupComplete;E.Reason=B.Reason;E.GraphDigest=B.GraphDigest;E.ConfigurationDigest=B.ConfigurationDigest;E.LayoutDigest=B.LayoutDigest;
    E.Candidates=B.Candidates;E.FailureValidation=B.FailureValidation;const auto Graph=FRogueliteRoomGraphGenerator::Generate(B.Seed,S.GraphSettings());
    for(const auto& R:B.Rooms)
    {
        auto& V=E.Rooms.AddDefaulted_GetRef();V.NodeId=R.NodeId;V.Degree=R.LogicalDegree;V.Capacity=R.TemplateCapacity;V.RoomType=R.RoomType;
        V.TemplateId=R.TemplateId;V.EventDefinitionId=R.EventDefinitionId;V.Transform=R.PlacedTransform;V.Dimensions=R.Dimensions;V.MemberSignature=R.MemberSignature;
        if(const auto* N=Graph.Graph.FindNode(R.NodeId)){V.ParentNodeId=N->ParentNodeId;V.GenerationDepth=N->BranchGenerationDepth;V.bDecision=N->bDecisionNode;}
    }
    for(const auto& C:B.Edges){auto& V=E.Edges.AddDefaulted_GetRef();V.EdgeId=C.EdgeId;V.ParentNode=C.ParentNode;V.ChildNode=C.ChildNode;V.ConnectionId=C.ConnectionId;
        V.ParentSocket=C.ParentSocket;V.ChildSocket=C.ChildSocket;V.CorridorLength=C.CorridorLength;V.Validation=C.Validation;if(IsValid(C.Corridor))V.CorridorTransform=C.Corridor->GetActorTransform();}
    UE_LOG(LogRoguelitePrototype,Display,TEXT("B4_SPATIAL_CASE %s Seed=%d Success=%d Rooms=%d Edges=%d Candidates=%d Graph=%s Config=%s Layout=%s Reason=%s"),
        *Name,B.Seed,E.bSucceeded,E.Rooms.Num(),E.Edges.Num(),E.Candidates.Num(),*E.GraphDigest,*E.ConfigurationDigest,*E.LayoutDigest,*E.Reason);return E;
}
bool Valid(const FRogueliteSeedBatch& B,const FRogueliteSeedLayoutSettings& S)
{
    if(!B.bSucceeded||!B.bGeometryValid||!IsValid(B.Registry)||B.Edges.Num()!=B.Rooms.Num()-1)return false;
    TSet<FGuid> Ids;int32 Events=0,NestedDecisions=0;const auto Graph=FRogueliteRoomGraphGenerator::Generate(B.Seed,S.GraphSettings());
    if(!Graph.bSucceeded || Graph.Graph.StableDigest!=B.GraphDigest)return false;
    for(const auto& R:B.Rooms)
    {
        if(!IsValid(R.Room)||!R.Room->GetActorTransform().Equals(R.PlacedTransform,.0001)||!R.Room->GetActorScale3D().Equals(FVector::OneVector,.0001)
            || R.TemplateCapacity<R.LogicalDegree || RogueliteSeedAssemblyInternal::MembersText(R.Room)!=R.MemberSignature)return false;
        if(R.RoomType==ERogueliteSeedRoomType::Event){++Events;if(R.EventDefinitionId!=S.TypeAssignment.EventDefinitionId)return false;}
        const auto* Node=Graph.Graph.FindNode(R.NodeId);if(Node&&Node->bDecisionNode&&Node->Role==ERogueliteGraphNodeRole::Branch)++NestedDecisions;
    }
    for(const auto& E:B.Edges)
    {
        if(!E.ConnectionId.IsValid()||Ids.Contains(E.ConnectionId)||!E.Validation.bGeometryPassed||!E.Validation.bCapsulePassed)return false;Ids.Add(E.ConnectionId);
        for(const auto& P:E.Validation.Probes)if(!P.bPassed)return false;
        const auto* A=B.Rooms.FindByPredicate([&](const auto& R){return R.NodeId==E.ParentNode;});const auto* C=B.Rooms.FindByPredicate([&](const auto& R){return R.NodeId==E.ChildNode;});
        if(!A||!C||ARogueliteRoomAssembly::GetRoomSocketConnectionId(A->Room,E.ParentSocket)!=E.ConnectionId
            || ARogueliteRoomAssembly::GetRoomSocketConnectionId(C->Room,E.ChildSocket)!=E.ConnectionId)return false;
    }
    return B.Registry->GetConnectionRecords().Num()==B.Edges.Num() && (S.AlgorithmVersion==1 || (Events>0 && NestedDecisions>=S.MinNestedDecisionNodes));
}
AActor* Blocker(UWorld* W,const FTransform& Transform)
{
    FActorSpawnParameters P;P.ObjectFlags|=RF_Transient;auto* A=W->SpawnActor<AActor>(P);if(!A)return nullptr;
    auto* M=NewObject<UStaticMeshComponent>(A,TEXT("B4_ExplicitTestFixture"));A->AddInstanceComponent(M);A->SetRootComponent(M);
    M->SetMobility(EComponentMobility::Movable);M->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));
    M->SetCollisionProfileName(TEXT("BlockAll"));M->SetWorldTransform(Transform);M->RegisterComponent();return A;
}
}
ARogueliteBranchingEditorTestRig::ARogueliteBranchingEditorTestRig(){Settings=RogueliteBranchingTestSettings();}
bool ARogueliteBranchingEditorTestRig::OwnEditorMap() const
{return GetWorld()&&!GetWorld()->IsGameWorld()&&GetWorld()->GetOutermost()->GetName()==TEXT("/Game/Roguelite/Maps/L_Roguelite_Branching_Dev");}
void ARogueliteBranchingEditorTestRig::GenerateBranchingPreview()
{
    if(!OwnEditorMap()||!Batch.OwnedActors.IsEmpty()){Evidence.Add(TEXT("Refused: B4-A own Editor map and empty owned batch required."));return;}
    Modify();Batch=FRogueliteSeedLayoutAssembler::Generate(this,Settings,GetActorTransform());Evidence.Add(Batch.Reason);
}
void ARogueliteBranchingEditorTestRig::ClearBranchingPreview()
{if(OwnEditorMap()){Modify();FString Reason;FRogueliteSeedLayoutAssembler::Clear(this,Batch,Reason);Evidence.Add(Reason);}}
void ARogueliteBranchingEditorTestRig::RunBranchingChecks()
{
    if(!OwnEditorMap()||!Batch.OwnedActors.IsEmpty()){Evidence.Add(TEXT("Refused: tests never overwrite an existing preview or other map."));return;}
    const auto Original=Settings;Cases.Reset();SuccessfulSeeds.Reset();bChecksPassed=true;TMap<AActor*,FTransform> Existing;
    for(TActorIterator<AActor> I(GetWorld());I;++I)if(!I->IsActorBeingDestroyed())Existing.Add(*I,I->GetActorTransform());
    const auto Check=[&](const FString& Name,bool Passed){bChecksPassed&=Passed;Evidence.Add((Passed?TEXT("PASS "):TEXT("FAIL "))+Name);UE_LOG(LogRoguelitePrototype,Display,TEXT("B4_EDITOR_CHECK %s"),*Evidence.Last());};
    const auto Case=[&](const FString& Name,const FRogueliteSeedLayoutSettings& S,const FTransform& Transform,bool ExpectedFailure=false)
    {
        auto B=FRogueliteSeedLayoutAssembler::Generate(this,S,Transform);Cases.Add(Capture(Name,B,S));
        if(ExpectedFailure)Check(Name+TEXT(" refused and batch cleaned"),!B.bSucceeded&&B.bCleanupComplete&&B.OwnedActors.IsEmpty()&&!B.Reason.IsEmpty());
        else if(B.bSucceeded)Check(Name+TEXT(" geometry/POI/unique reservations"),Valid(B,S));
        else Check(Name+TEXT(" bounded spatial failure retained"),B.bCleanupComplete&&B.OwnedActors.IsEmpty()&&!B.Reason.IsEmpty());
        if(B.bSucceeded){FString Reason;Check(Name+TEXT(" explicit owned batch clear"),FRogueliteSeedLayoutAssembler::Clear(this,B,Reason)&&B.OwnedActors.IsEmpty());}
        return Cases.Last().bSucceeded;
    };
    auto Legacy=GetDefault<ARogueliteSeedAssembly>()->Settings;Legacy.Seed=2;
    Case(TEXT("V1 Seed2 regression"),Legacy,FTransform::Identity);Check(TEXT("Original B2 Seed2 spatial Golden unchanged"),Cases.Last().LayoutDigest==TEXT("42537d09b94e589a"));
    for(int32 Seed=0;Seed<20;++Seed)
        if(Case(FString::Printf(TEXT("V2 Seed%d"),Seed),RogueliteBranchingTestSettings(Seed),FTransform(FRotator(0,(Seed%4)*90,0))))SuccessfulSeeds.Add(Seed);
    Check(TEXT("At least one actual multilevel spatial layout"),!SuccessfulSeeds.IsEmpty());
    if(!SuccessfulSeeds.IsEmpty())
    {
        const auto S=RogueliteBranchingTestSettings(SuccessfulSeeds[0]);const auto Pose=FTransform(FRotator(0,(S.Seed%4)*90,0));
        Case(TEXT("V2 repeat A"),S,Pose);const FString Digest=Cases.Last().LayoutDigest;Case(TEXT("V2 repeat B"),S,Pose);
        Check(TEXT("Same Seed/config/catalog/environment stable spatial digest"),!Digest.IsEmpty()&&Digest==Cases.Last().LayoutDigest);
        auto Bad=S;Bad.Templates.RemoveAll([](const auto& T){return T.Id==TEXT("Junction3");});Case(TEXT("No degree3 template"),Bad,Pose,true);
        Bad=S;Bad.MinDecisionNodes=Bad.MaxDecisionNodes=99;Case(TEXT("Impossible decision quota"),Bad,Pose,true);
        Bad=S;Bad.TypeAssignment.Rules[4].MinCount=Bad.TypeAssignment.Rules[4].MaxCount=9;Case(TEXT("Impossible Event quota"),Bad,Pose,true);
        const auto Plan=FRogueliteSeedLayoutAssembler::Plan(this,S,Pose);
        if(Plan.bValid&&!Plan.Edges.IsEmpty())
        {
            const auto CT=Plan.Edges[0].Transform;auto* Fixture=Blocker(GetWorld(),FTransform(CT.GetRotation(),CT.TransformPosition(FVector(0,0,200)),FVector(1,6,4)));
            if(Fixture){const auto Before=Fixture->GetActorTransform();Bad=S;Bad.MaxCandidatesPerEdge=1;Case(TEXT("Candidate exhausted by independent blocker"),Bad,Pose,true);
                Check(TEXT("Batch cleanup preserves independent blocker"),IsValid(Fixture)&&Fixture->GetActorTransform().Equals(Before,0));Fixture->Destroy();}
            else Check(TEXT("Explicit blocker fixture spawned"),false);
        }
        else Check(TEXT("Plan available for blocked-candidate test"),false);
    }
    for(const auto& P:Existing)Check(TEXT("Preexisting Actor unchanged: ")+P.Key->GetName(),IsValid(P.Key)&&P.Key->GetActorTransform().Equals(P.Value,0));
    Settings=Original;UE_LOG(LogRoguelitePrototype,Display,TEXT("B4_EDITOR_FINISHED Passed=%d Seeds=20 SpatialSuccess=%d Cases=%d"),bChecksPassed,SuccessfulSeeds.Num(),Cases.Num());
}
ARogueliteBranchingRuntimeTestRig::ARogueliteBranchingRuntimeTestRig(){Settings=RogueliteBranchingTestSettings();bEnableRuntimeNavigation=true;}
void ARogueliteBranchingRuntimeTestRig::BeginPlay()
{
    Super::BeginPlay();GetWorldTimerManager().SetTimerForNextTick(this,&ARogueliteBranchingRuntimeTestRig::CaptureGeometry);
}
void ARogueliteBranchingRuntimeTestRig::CaptureGeometry()
{
    RuntimeCase=Capture(TEXT("PIE V2 actual GameWorld"),Batch,Settings);bRuntimeGeometryChecksPassed=Valid(Batch,Settings);
    const auto Snapshot=URogueliteDungeonMapQuery::Capture(this);int32 Events=0;
    for(const auto& R:Snapshot.Rooms)if(R.RoomType==ERogueliteSeedRoomType::Event){++Events;bRuntimeGeometryChecksPassed&=R.EventDefinitionId==Settings.TypeAssignment.EventDefinitionId;}
    bRuntimeGeometryChecksPassed&=GetWorld()->IsGameWorld()&&Events>0;
    UE_LOG(LogRoguelitePrototype,Display,TEXT("B4_RUNTIME_CHECK Geometry=%d Events=%d Snapshot=%s Stage=%s NoPortalOrPCG=1"),bRuntimeGeometryChecksPassed,Events,*Snapshot.StableDataDigest,*UEnum::GetValueAsString(Stage));
}
