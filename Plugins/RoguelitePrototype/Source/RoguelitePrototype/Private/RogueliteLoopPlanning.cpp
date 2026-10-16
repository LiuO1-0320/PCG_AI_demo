#include "RogueliteLoopPlanning.h"
#include "RogueliteSeedAssemblyInternal.h"
#include "Components/PrimitiveComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "UObject/Class.h"

namespace
{
using G=FRogueliteRoomGraphGenerator;
// New independent domain; existing five purpose values and all original random draws stay unchanged.
constexpr auto LoopPurpose=static_cast<ERogueliteGraphSeedPurpose>(0x4f13c9d7u);
const TCHAR* SideNames[]={TEXT("North"),TEXT("South"),TEXT("East"),TEXT("West")};
int32 PairKey(int32 A,int32 B){return A*256+B;}
bool Rigid(const FTransform& T)
{const auto R=T.Rotator();return !T.ContainsNaN()&&T.GetScale3D().Equals(FVector::OneVector,.0001)&&FMath::Abs(R.Pitch)<.01&&FMath::Abs(R.Roll)<.01&&FMath::Abs(FMath::FindDeltaAngleDegrees(R.Yaw,FMath::RoundToFloat(R.Yaw/90)*90))<.01;}
bool Intersects(const FBox& A,const FBox& B,double E)
{return A.IsValid&&B.IsValid&&FMath::Min(A.Max.X,B.Max.X)-FMath::Max(A.Min.X,B.Min.X)>E&&FMath::Min(A.Max.Y,B.Max.Y)-FMath::Max(A.Min.Y,B.Min.Y)>E&&FMath::Min(A.Max.Z,B.Max.Z)-FMath::Max(A.Min.Z,B.Min.Z)>E;}
FString Role(ERogueliteGraphNodeRole R)
{return R==ERogueliteGraphNodeRole::Start?TEXT("Start"):R==ERogueliteGraphNodeRole::Final?TEXT("Final"):R==ERogueliteGraphNodeRole::Branch?TEXT("Branch"):TEXT("MainPath");}
const FRogueliteDoorConfig& Config(const ARogueliteParametricRoom* R,ERogueliteRoomSide S)
{return S==ERogueliteRoomSide::North?R->NorthDoor:S==ERogueliteRoomSide::South?R->SouthDoor:S==ERogueliteRoomSide::East?R->EastDoor:R->WestDoor;}
FBox RoomBox(const ARogueliteParametricRoom* R)
{return FBox(FVector(-R->RoomLength*.5-R->WallThickness,-R->RoomWidth*.5-R->WallThickness,-R->FloorThickness),FVector(R->RoomLength*.5+R->WallThickness,R->RoomWidth*.5+R->WallThickness,R->RoomHeight)).TransformBy(R->GetActorTransform());}
FString SeedSettings(const FRogueliteSeedLayoutSettings& S)
{FString Text;FRogueliteSeedLayoutSettings::StaticStruct()->ExportText(Text,&S,nullptr,nullptr,PPF_None,nullptr);return G::StableTextDigest(Text);}
bool Tree(const FRogueliteRoomGraph& Graph,FString& Reason)
{
    const int32 N=Graph.Nodes.Num();if(N<2||N>256||Graph.Edges.Num()!=N-1||!Graph.FindNode(Graph.StartNodeId)||!Graph.FindNode(Graph.FinalNodeId)){Reason=TEXT("Invalid source Tree counts/references.");return false;}
    TArray<int32> Degree;Degree.Init(0,N);TSet<int32> Keys;int32 Starts=0,Finals=0;
    for(int32 I=0;I<N;++I){const auto& Node=Graph.Nodes[I];if(Node.NodeId!=I){Reason=TEXT("Source NodeId/order invalid.");return false;}Starts+=Node.Role==ERogueliteGraphNodeRole::Start;Finals+=Node.Role==ERogueliteGraphNodeRole::Final;}
    for(const auto& E:Graph.Edges)
    {
        if(!Graph.FindNode(E.NodeA)||!Graph.FindNode(E.NodeB)||E.NodeA==E.NodeB||E.Role==ERogueliteGraphEdgeRole::Loop){Reason=TEXT("Source self/reference/committed Loop edge not supported in B4-B1 Tree query.");return false;}
        const int32 Key=PairKey(FMath::Min(E.NodeA,E.NodeB),FMath::Max(E.NodeA,E.NodeB));if(Keys.Contains(Key)){Reason=TEXT("Source duplicate edge.");return false;}Keys.Add(Key);++Degree[E.NodeA];++Degree[E.NodeB];
    }
    TSet<int32> Seen{Graph.StartNodeId};TArray<int32> Queue{Graph.StartNodeId};
    for(int32 H=0;H<Queue.Num();++H)for(const auto& E:Graph.Edges){const int32 Next=E.NodeA==Queue[H]?E.NodeB:E.NodeB==Queue[H]?E.NodeA:INDEX_NONE;if(Next!=INDEX_NONE&&!Seen.Contains(Next)){Seen.Add(Next);Queue.Add(Next);}}
    for(int32 I=0;I<N;++I)if(Degree[I]!=Graph.Nodes[I].Degree||Degree[I]>Graph.Nodes[I].MaxAllowedDegree){Reason=TEXT("Source degree metadata/budget invalid.");return false;}
    if(Seen.Num()!=N||Starts!=1||Finals!=1||Degree[Graph.FinalNodeId]!=1||Graph.Nodes[Graph.StartNodeId].Role!=ERogueliteGraphNodeRole::Start||Graph.Nodes[Graph.FinalNodeId].Role!=ERogueliteGraphNodeRole::Final){Reason=TEXT("Source connectivity/unique Start-Final/final-leaf invariant invalid.");return false;}
    return true;
}
int32 LoopSeed(const FRogueliteRoomGraph& Graph,const FRogueliteLoopPlanningSettings& S)
{return S.bOverrideLoopSeed?S.LoopSelectionSeed:G::DeriveSeed(Graph.Seed,LoopPurpose,S.LoopAlgorithmVersion,S.ConfigVersion);}
void Identity(FRogueliteLoopCandidate& C,const FRogueliteRoomGraph& Graph,const FRogueliteLoopPlanningSettings& S)
{
    const FString Key=FString::Printf(TEXT("loop-v%d|g=%s|pair=%d,%d|ports=%d,%d|profile=%s|socketPair=%d"),S.LoopAlgorithmVersion,*Graph.StableDigest,C.NodeA,C.NodeB,uint8(C.SocketA),uint8(C.SocketB),*C.CorridorProfile.ToString(),C.bHasSocketPair);
    C.CandidateId=FName(*G::StableTextDigest(Key));const int32 Salt=PairKey(C.NodeA,C.NodeB)*128+uint8(C.SocketA)*16+uint8(C.SocketB)*4;
    C.StableRank=uint32(G::DeriveSeed(LoopSeed(Graph,S),LoopPurpose,S.LoopAlgorithmVersion,S.ConfigVersion,Salt));
    C.Score=int64(C.TreeDistance)*S.TreeDistanceWeight+(C.TypeA!=C.TypeB?S.DifferentTypeWeight:0)-FMath::RoundToInt64(C.CorridorLength/100)*S.LengthPerMeterPenalty+(!C.bRequiresEnableA&&!C.bRequiresEnableB?S.AlreadyOpenWeight:0);
}
void Finalize(FRogueliteLoopPlan& P)
{
    FString Text=FString::Printf(TEXT("LoopPlanV1|seed=%d|loopSeed=%d|graph=%s|layout=%s|sourceSettings=%s|sourceState=%s|settings=%s|state=%d|budget=%d|reason=%s"),P.Seed,P.DerivedLoopSeed,*P.GraphDigest,*P.LayoutDigest,*P.SourceSettingsDigest,*P.SourceStateDigest,*P.PlanningSettingsDigest,uint8(P.State),P.bCandidateBudgetExhausted,*P.Reason);
    for(const auto& C:P.Candidates)Text+=FString::Printf(TEXT("|candidate=%s,%d,%d,%d,%d,%s,%d,%d,%d,%d,%lld,%lld,%.3f,%s,%s,%s"),*C.CandidateId.ToString(),C.NodeA,C.NodeB,uint8(C.SocketA),uint8(C.SocketB),*C.CorridorProfile.ToString(),uint8(C.Feasibility),C.bRequiresEnableA,C.bRequiresEnableB,C.bSelected,C.Score,C.StableRank,C.CorridorLength,*C.CorridorTransform.ToString(),*C.Reason,*C.SelectionReason);
    P.StableDigest=G::StableTextDigest(Text);
}
struct Port {FRogueliteDoorSocket Door;bool RequiresEnable=false,Reserved=false;};
struct Room {const FRogueliteSeedRoomRecord* Record=nullptr;TArray<Port> Ports;int32 Capacity=0;};
struct Profile {FName Id;FRogueliteDoorSocket Start,End;FBox Bounds;FVector Dimensions;float Length=0,Wall=0;};
FString StateSignature(AActor* Owner,const FRogueliteSeedBatch& B)
{
    TArray<FString> Rows;const auto Primitive=[&](const UPrimitiveComponent* C,const FString& Prefix)
    {
        if(!C||C->GetCollisionEnabled()==ECollisionEnabled::NoCollision||C->GetCollisionResponseToChannel(ECC_Pawn)!=ECR_Block)return;
        FString Asset;const auto* M=Cast<UStaticMeshComponent>(C);if(M&&M->GetStaticMesh())Asset=M->GetStaticMesh()->GetPathName();
        Rows.Add(Prefix+TEXT("|")+C->GetClass()->GetPathName()+TEXT("|")+Asset+TEXT("|")+C->GetComponentTransform().ToString()+FString::Printf(TEXT("|bounds=%s/%s|collision=%d"),*C->Bounds.Origin.ToString(),*C->Bounds.BoxExtent.ToString(),uint8(C->GetCollisionEnabled())));
    };
    TSet<AActor*> Own;
    for(const auto& R:B.Rooms)if(IsValid(R.Room))
    {
        Own.Add(R.Room);FString Meta=FString::Printf(TEXT("node=%d|template=%s|class=%s|dims=%.6f,%.6f,%.6f,%.6f,%.6f|safety=%.6f,%.6f|pose=%s"),R.NodeId,*R.TemplateId.ToString(),*R.Room->GetClass()->GetPathName(),R.Room->RoomLength,R.Room->RoomWidth,R.Room->RoomHeight,R.Room->WallThickness,R.Room->FloorThickness,R.Room->MinDoorSideWall,R.Room->MinDoorLintelHeight,*R.Room->GetActorTransform().ToString());
        for(int32 I=0;I<4;++I){const auto Side=ERogueliteRoomSide(I);const auto& D=Config(R.Room,Side);Meta+=FString::Printf(TEXT("|door=%d,%d,%d,%.6f,%.6f,%.6f,occupied=%d"),I,D.bEnabled,D.bAutoCenter,D.ManualOffset,D.DoorWidth,D.DoorHeight,ARogueliteRoomAssembly::GetRoomSocketConnectionId(R.Room,Side).IsValid());}
        Rows.Add(Meta);TInlineComponentArray<UPrimitiveComponent*> Cs;R.Room->GetComponents(Cs);for(const auto* C:Cs)Primitive(C,FString::Printf(TEXT("node=%d/component=%s"),R.NodeId,*C->GetFName().ToString()));
    }
    for(const auto& E:B.Edges)if(IsValid(E.Corridor)){Own.Add(E.Corridor);TInlineComponentArray<UPrimitiveComponent*> Cs;E.Corridor->GetComponents(Cs);for(const auto* C:Cs)Primitive(C,FString::Printf(TEXT("edge=%d/component=%s"),E.EdgeId,*C->GetFName().ToString()));}
    for(TActorIterator<AActor> I(Owner->GetWorld());I;++I)if(!I->IsActorBeingDestroyed()&&!Own.Contains(*I)){TInlineComponentArray<UPrimitiveComponent*> Cs;I->GetComponents(Cs);for(const auto* C:Cs)Primitive(C,TEXT("external"));}
    Rows.Sort();FString Text;for(const auto& Row:Rows)Text+=Row+TEXT("\n");return G::StableTextDigest(Text);
}
bool Capture(AActor* Owner,const FRogueliteSeedBatch& B,const FRogueliteSeedLayoutSettings& Source,const FRogueliteLoopPlanningSettings& S,
    FRogueliteRoomGraph& Graph,TArray<Room>& Rooms,TArray<Profile>& Profiles,FString& Reason)
{
    if(!IsValid(Owner)||!Owner->GetWorld()||!B.bSucceeded||!B.bGeometryValid||!B.BatchId.IsValid()||!IsValid(B.Registry)||B.Registry->GetWorld()!=Owner->GetWorld()||B.Rooms.IsEmpty()){Reason=TEXT("Invalid/cleared source batch, registry or World; Tree must actually be assembled first.");return false;}
    const auto Generated=G::Generate(Source.Seed,Source.GraphSettings());if(!Generated.bSucceeded||Generated.Graph.StableDigest!=B.GraphDigest||Source.Seed!=B.Seed||Generated.Graph.Nodes.Num()!=B.Rooms.Num()||Generated.Graph.Edges.Num()!=B.Edges.Num()){Reason=TEXT("Source Seed/config/GraphDigest no longer matches the actual Tree batch.");return false;}Graph=Generated.Graph;
    if(!Tree(Graph,Reason)||G::StableTextDigest(FRogueliteSeedLayoutAssembler::DescribeLayout(B))!=B.LayoutDigest){Reason=TEXT("Source graph/layout records stale: ")+Reason;return false;}
    Rooms.SetNum(Graph.Nodes.Num());TSet<int32> Seen;
    for(const auto& R:B.Rooms)
    {
        if(!Graph.FindNode(R.NodeId)||Seen.Contains(R.NodeId)||!IsValid(R.Room)||R.Room->IsActorBeingDestroyed()||R.Room->GetWorld()!=Owner->GetWorld()||!Rigid(R.Room->GetActorTransform())||!R.Room->GetActorTransform().Equals(R.PlacedTransform,.0001)
            ||!FVector(R.Room->RoomLength,R.Room->RoomWidth,R.Room->RoomHeight).Equals(R.Dimensions,.0001)||R.Room->WallThickness!=R.WallThickness||R.Room->FloorThickness!=R.FloorThickness||!R.Room->OutOfBoundsManualPOIActors.IsEmpty())
        {Reason=FString::Printf(TEXT("Stale/invalid Room Node=%d Transform/dimensions/POI or World."),R.NodeId);return false;}
        const auto* Catalog=Source.Templates.FindByPredicate([&](const auto& T){return T.Id==R.TemplateId&&T.TemplateClass.ToSoftObjectPath()==FSoftObjectPath(R.Room->GetClass());});
        if(!Catalog||R.MemberSignature!=RogueliteSeedAssemblyInternal::MembersText(R.Room)){Reason=FString::Printf(TEXT("Source template/catalog/owned POI signature changed Node=%d."),R.NodeId);return false;}
        Seen.Add(R.NodeId);auto& Out=Rooms[R.NodeId];Out.Record=&R;const auto* Default=R.Room->GetClass()->GetDefaultObject<ARogueliteRoomTemplate>();
        for(int32 I=0;I<4;++I)
        {
            const auto Side=ERogueliteRoomSide(I);FRogueliteDoorSocket Authored,Current;const bool Enabled=R.Room->TryGetDoorSocket(Side,Current);const bool AuthoredValid=Default&&Default->TryGetDoorSocket(Side,Authored);
            if(!Enabled&&!(S.bAllowAuthoredSealedSockets&&AuthoredValid))continue;
            Port P;if(!R.Room->PreviewEnabledDoorSocket(Side,P.Door,P.RequiresEnable))continue;
            ++Out.Capacity;P.Reserved=ARogueliteRoomAssembly::GetRoomSocketConnectionId(R.Room,Side).IsValid();Out.Ports.Add(P);
        }
        Out.Capacity=FMath::Min(Out.Capacity,R.TemplateCapacity);
    }
    const auto Records=B.Registry->GetConnectionRecords();int32 Active=0;for(const auto& R:Records)Active+=R.bActive;
    if(Active!=B.Edges.Num()){Reason=TEXT("Source registry has extra/missing active connections; Tree batch cannot be treated as isolated.");return false;}
    for(const auto& E:B.Edges)
    {
        const auto* R=Records.FindByPredicate([&](const auto& C){return C.Id==E.ConnectionId&&C.bActive;});
        if(!R||!IsValid(E.Corridor)||E.Corridor->GetWorld()!=Owner->GetWorld()||R->Mode!=ERogueliteConnectionMode::StraightCorridor||!R->bGeometryValid||R->ValidationStatus!=ERogueliteConnectionValidation::Passed||!Graph.FindNode(E.ParentNode)||!Graph.FindNode(E.ChildNode)
            ||R->RoomA!=Rooms[E.ParentNode].Record->Room||R->RoomB!=Rooms[E.ChildNode].Record->Room||R->SocketA!=E.ParentSocket||R->SocketB!=E.ChildSocket||R->Corridor!=E.Corridor||E.Corridor->CorridorLength!=E.CorridorLength)
        {Reason=FString::Printf(TEXT("Source Edge=%d C2 reservation/validation/corridor changed."),E.EdgeId);return false;}
        FRogueliteDoorSocket DA,DB;FRogueliteCorridorSocket CA,CB;
        if(!R->RoomA->TryGetDoorSocket(R->SocketA,DA)||!R->RoomB->TryGetDoorSocket(R->SocketB,DB)||!R->Corridor->TryGetCorridorSocket(R->EndpointA,CA)||!R->Corridor->TryGetCorridorSocket(R->EndpointB,CB)
            ||FVector::Distance(DA.WorldPosition,CA.DoorSocket.WorldPosition)>.1||FVector::Distance(DB.WorldPosition,CB.DoorSocket.WorldPosition)>.1
            ||FVector::DotProduct(DA.WorldOutwardDirection,CA.DoorSocket.WorldOutwardDirection)>-.99999||FVector::DotProduct(DB.WorldOutwardDirection,CB.DoorSocket.WorldOutwardDirection)>-.99999
            ||FMath::Abs(DA.DoorWidth-CA.DoorSocket.DoorWidth)>.1||FMath::Abs(DA.DoorHeight-CA.DoorSocket.DoorHeight)>.1||FMath::Abs(DB.DoorWidth-CB.DoorSocket.DoorWidth)>.1||FMath::Abs(DB.DoorHeight-CB.DoorSocket.DoorHeight)>.1)
        {Reason=FString::Printf(TEXT("Source Edge=%d current Tree thresholds/openings changed; no collision/Ready revalidation claim."),E.EdgeId);return false;}
        if(Profiles.ContainsByPredicate([&](const auto& P){return P.Id==E.CorridorProfile;}))continue;
        const auto* ConfigProfile=Source.Corridors.FindByPredicate([&](const auto& P){return P.Id==E.CorridorProfile&&P.bEnabled;});FRogueliteCorridorSocket A,C;
        if(!ConfigProfile||!E.Corridor->TryGetCorridorSocket(ERogueliteCorridorEndpoint::Start,A)||!E.Corridor->TryGetCorridorSocket(ERogueliteCorridorEndpoint::End,C)){Reason=TEXT("Live C1 corridor descriptor/profile invalid.");return false;}
        const auto* Corridor=E.Corridor.Get();
        if(Corridor->CorridorWidth!=ConfigProfile->Width||Corridor->CorridorHeight!=ConfigProfile->Height||Corridor->WallThickness!=ConfigProfile->WallThickness||Corridor->FloorThickness!=ConfigProfile->FloorThickness||Corridor->DoorWidth!=ConfigProfile->DoorWidth||Corridor->DoorHeight!=ConfigProfile->DoorHeight){Reason=TEXT("Live corridor dimensions changed relative to source profile.");return false;}
        Profile P;P.Id=E.CorridorProfile;P.Start=A.DoorSocket;P.End=C.DoorSocket;P.Length=Corridor->CorridorLength;P.Wall=Corridor->WallThickness;P.Bounds=Corridor->GetLocalOccupancyBounds();P.Dimensions=FVector(Corridor->CorridorLength,Corridor->CorridorWidth,Corridor->CorridorHeight);Profiles.Add(P);
    }
    Profiles.Sort([](const auto& A,const auto& B){return A.Id.ToString()<B.Id.ToString();});return !Profiles.IsEmpty();
}
bool Space(AActor* Owner,const FRogueliteSeedBatch& B,const Room& A,const Room& C,const FRogueliteLoopPlanningSettings& S,FRogueliteLoopCandidate& Candidate)
{
    auto* World=Owner->GetWorld();TArray<FString> OccupancyReasons;
    // Actor iteration order is not a deterministic diagnostic order. Sort semantic reasons first.
    for(TActorIterator<ARogueliteParametricRoom> I(World);I;++I)if(!I->IsActorBeingDestroyed()&&Intersects(Candidate.CorridorOccupancy,RoomBox(*I),S.PositionTolerance))
    {const auto* R=B.Rooms.FindByPredicate([&](const auto& Item){return Item.Room==*I;});OccupancyReasons.Add(R?FString::Printf(TEXT("Corridor occupancy intersects parameter Room Node=%d."),R->NodeId):TEXT("Corridor occupancy intersects external parameter room at ")+I->GetActorTransform().ToString());}
    if(!OccupancyReasons.IsEmpty()){OccupancyReasons.Sort();Candidate.Reason=OccupancyReasons[0];return false;}
    for(TActorIterator<ARogueliteParametricCorridor> I(World);I;++I)if(!I->IsActorBeingDestroyed()&&Intersects(Candidate.CorridorOccupancy,I->GetWorldOccupancyBounds(),S.PositionTolerance))
    {const auto* E=B.Edges.FindByPredicate([&](const auto& Item){return Item.Corridor==*I;});OccupancyReasons.Add(E?FString::Printf(TEXT("Corridor occupancy intersects existing Corridor Edge=%d."),E->EdgeId):TEXT("Corridor occupancy intersects external corridor at ")+I->GetActorTransform().ToString());}
    if(!OccupancyReasons.IsEmpty()){OccupancyReasons.Sort();Candidate.Reason=OccupancyReasons[0];return false;}
    FCollisionQueryParams Query(SCENE_QUERY_STAT(RogueliteLoopPrecheck),false);
    const auto IgnoreProspectiveWall=[&](const Room& R,const FRogueliteDoorSocket& D,bool Required)
    {
        if(!Required)return;TInlineComponentArray<UPrimitiveComponent*> Components;R.Record->Room->GetComponents(Components);const FString Name=FString(SideNames[uint8(D.Side)])+TEXT("Wall");
        for(const auto* Component:Components)if(Component->GetFName().ToString()==Name)Query.AddIgnoredComponent(Component);
    };
    IgnoreProspectiveWall(A,Candidate.DoorA,Candidate.bRequiresEnableA);IgnoreProspectiveWall(C,Candidate.DoorB,Candidate.bRequiresEnableB);
    const auto Extent=Candidate.CorridorOccupancy.GetExtent()-FVector(S.PositionTolerance);
    if(Extent.GetMin()<=0||World->OverlapBlockingTestByChannel(Candidate.CorridorOccupancy.GetCenter(),FQuat::Identity,ECC_Pawn,FCollisionShape::MakeBox(Extent),Query)){Candidate.Reason=TEXT("Corridor envelope blocked by actual Pawn collision (including POI/external obstacles).");return false;}
    const FVector Normal=Candidate.DoorA.WorldOutwardDirection,Tangent(-Normal.Y,Normal.X,0),Z(0,0,S.CapsuleHalfHeight+S.CapsuleClearance);
    const FVector InsideA=Candidate.DoorA.WorldPosition-Normal*(Candidate.DoorA.WallThickness+S.CapsuleRadius+S.CapsuleClearance);
    const FVector InsideB=Candidate.DoorB.WorldPosition-Candidate.DoorB.WorldOutwardDirection*(Candidate.DoorB.WallThickness+S.CapsuleRadius+S.CapsuleClearance);
    const float Offset=Candidate.DoorA.DoorWidth*.5-S.CapsuleRadius-S.CapsuleClearance-1;
    const auto Capsule=FCollisionShape::MakeCapsule(S.CapsuleRadius,S.CapsuleHalfHeight);bool Passed=true;
    const auto Probe=[&](const FString& Name,const FVector& Start,const FVector& End,bool Support,AActor* Expected)
    {
        FRogueliteAssemblyProbe P;P.Name=Name;P.Start=Start;P.End=End;P.bExpectedBlocking=Support;FHitResult Hit;
        const bool Blocked=World->SweepSingleByChannel(Hit,Start,End,FQuat::Identity,ECC_Pawn,Capsule,Query);
        const bool OverlapStart=World->OverlapBlockingTestByChannel(Start,FQuat::Identity,ECC_Pawn,Capsule,Query);
        const bool OverlapEnd=!Support&&World->OverlapBlockingTestByChannel(End,FQuat::Identity,ECC_Pawn,Capsule,Query);
        P.bBlockingHit=Blocked||OverlapStart||OverlapEnd;P.HitDistance=Hit.Distance;P.HitObject=Hit.Component.IsValid()?Hit.Component->GetPathName():TEXT("");
        P.bPassed=Support?Blocked&&!OverlapStart&&!Hit.bStartPenetrating&&Hit.GetActor()==Expected:!P.bBlockingHit;Passed&=P.bPassed;Candidate.PrecheckProbes.Add(P);
    };
    for(float Shift:{-Offset,0.f,Offset}){const FVector T=Tangent*Shift;Probe(TEXT("Prospective gap capsule forward"),InsideA+T+Z,InsideB+T+Z,false,nullptr);Probe(TEXT("Prospective gap capsule reverse"),InsideB+T+Z,InsideA+T+Z,false,nullptr);}
    Probe(TEXT("Existing RoomA floor support"),InsideA+Z,InsideA+Z-FVector(0,0,10),true,A.Record->Room);
    Probe(TEXT("Existing RoomB floor support"),InsideB+Z,InsideB+Z-FVector(0,0,10),true,C.Record->Room);
    Candidate.Reason=Passed?TEXT("Straight spatial precheck only: parameter occupancy, actual envelope/capsule clearance and endpoint floors passed. Proposed corridor/floor/end frames are NOT spawned; sealed walls require explicit future opening and C2 validation."):TEXT("Prospective passage blocked or endpoint floor support failed; inspect actual precheck probes.");return Passed;
}
}

FString FRogueliteLoopPlanner::DescribeSettings(const FRogueliteLoopPlanningSettings& S)
{FString Text;FRogueliteLoopPlanningSettings::StaticStruct()->ExportText(Text,&S,nullptr,nullptr,PPF_None,nullptr);return TEXT("LoopPlanningV1|domain=4f13c9d7|FinalExcluded=1|LiveC1LengthRange=100..100000|LengthInterpolated=1|")+Text;}
bool FRogueliteLoopPlanner::ValidateSettings(const FRogueliteLoopPlanningSettings& S,FString& Reason)
{
    if(S.LoopAlgorithmVersion!=1||S.ConfigVersion<1||S.MinLoops<0||S.MaxLoops<S.MinLoops||S.MaxLoops>32||S.MaxLoopCandidates<1||S.MaxLoopCandidates>4096||S.MaxCandidateChecks<1||S.MaxCandidateChecks>65536||S.MinLoopTreeDistance<2||S.MinLoopTreeDistance>255
        ||!FMath::IsFinite(S.MinCorridorLength)||!FMath::IsFinite(S.MaxCorridorLength)||S.MinCorridorLength<100||S.MaxCorridorLength<S.MinCorridorLength||S.MaxCorridorLength>100000
        ||!FMath::IsFinite(S.PositionTolerance)||S.PositionTolerance<.001||S.PositionTolerance>1||!FMath::IsFinite(S.AngleToleranceDegrees)||S.AngleToleranceDegrees<.001||S.AngleToleranceDegrees>1
        ||!FMath::IsFinite(S.CapsuleRadius)||!FMath::IsFinite(S.CapsuleHalfHeight)||!FMath::IsFinite(S.CapsuleClearance)||S.CapsuleRadius<1||S.CapsuleRadius>1000||S.CapsuleHalfHeight<S.CapsuleRadius||S.CapsuleHalfHeight>2000||S.CapsuleClearance<.1||S.CapsuleClearance>100
        ||S.TreeDistanceWeight<0||S.TreeDistanceWeight>10000||S.DifferentTypeWeight<0||S.DifferentTypeWeight>10000||S.LengthPerMeterPenalty<0||S.LengthPerMeterPenalty>10000||S.AlreadyOpenWeight<0||S.AlreadyOpenWeight>10000)
    {Reason=TEXT("Invalid loop ranges/version/budget/tolerances/capsule/score weights; input minima retained.");return false;}Reason.Reset();return true;
}
bool FRogueliteLoopPlanner::CheckTopologyPair(const FRogueliteRoomGraph& Graph,const TArray<int32>& Caps,const FRogueliteLoopPlanningSettings& S,int32 A,int32 B,int32& Distance,FString& Reason)
{
    Distance=0;if(!Graph.FindNode(A)||!Graph.FindNode(B)||Caps.Num()!=Graph.Nodes.Num()){Reason=TEXT("Invalid node/capacity reference.");return false;}
    if(A==B){Reason=TEXT("SelfLoop rejected.");return false;}
    if(A==Graph.FinalNodeId||B==Graph.FinalNodeId){Reason=TEXT("FinalEndpointExcluded: no alternate Final admission/Boss bypass contract.");return false;}
    for(const auto& E:Graph.Edges)if((E.NodeA==A&&E.NodeB==B)||(E.NodeA==B&&E.NodeB==A)){Reason=TEXT("AlreadyAdjacent: duplicate Tree connection rejected.");return false;}
    for(int32 Id:{A,B})if(Graph.Nodes[Id].Degree+1>Graph.Nodes[Id].MaxAllowedDegree||Graph.Nodes[Id].Degree+1>Caps[Id]){Reason=FString::Printf(TEXT("DegreeOrTemplateCapacity Node=%d Degree=%d proposed=%d limit=%d capacity=%d"),Id,Graph.Nodes[Id].Degree,Graph.Nodes[Id].Degree+1,Graph.Nodes[Id].MaxAllowedDegree,Caps[Id]);return false;}
    TArray<int32> D;D.Init(-1,Graph.Nodes.Num());D[A]=0;TArray<int32> Queue{A};for(int32 H=0;H<Queue.Num();++H)for(const auto& E:Graph.Edges){int32 Next=E.NodeA==Queue[H]?E.NodeB:E.NodeB==Queue[H]?E.NodeA:-1;if(Next>=0&&D[Next]<0){D[Next]=D[Queue[H]]+1;Queue.Add(Next);}}
    Distance=D[B];if(Distance<S.MinLoopTreeDistance){Reason=FString::Printf(TEXT("TreeSpanTooShort distance=%d required=%d"),Distance,S.MinLoopTreeDistance);return false;}
    Reason=TEXT("TopologyValid: hypothetical nonduplicate edge preserves connectivity and increases E-N+1 by one; not added to Gameplay graph.");return true;
}
FRogueliteLoopPlan FRogueliteLoopPlanner::Topology(const FRogueliteRoomGraph& Graph,const TArray<int32>& Caps,const FRogueliteLoopPlanningSettings& S)
{
    FRogueliteLoopPlan P;P.Seed=Graph.Seed;P.GraphDigest=Graph.StableDigest;P.DerivedLoopSeed=LoopSeed(Graph,S);P.PlanningSettingsDigest=G::StableTextDigest(DescribeSettings(S));
    if(!ValidateSettings(S,P.Reason)){P.State=ERogueliteLoopPlanningState::InvalidSettings;Finalize(P);return P;}
    if(Caps.Num()!=Graph.Nodes.Num()||!Tree(Graph,P.Reason)){P.State=ERogueliteLoopPlanningState::InvalidSource;Finalize(P);return P;}P.bTreeValid=true;
    for(int32 A=0;A<Graph.Nodes.Num();++A)for(int32 B=A+1;B<Graph.Nodes.Num();++B)
    {
        if(P.CandidateChecks>=S.MaxCandidateChecks||P.Candidates.Num()>=S.MaxLoopCandidates){P.bCandidateBudgetExhausted=true;break;}
        FRogueliteLoopCandidate C;C.NodeA=A;C.NodeB=B;C.RoleA=Role(Graph.Nodes[A].Role);C.RoleB=Role(Graph.Nodes[B].Role);++P.CandidateChecks;
        C.bTopologyValid=CheckTopologyPair(Graph,Caps,S,A,B,C.TreeDistance,C.Reason);if(C.bTopologyValid){C.Feasibility=ERogueliteLoopFeasibility::TopologyValid;C.IndependentCycleDelta=1;++P.TopologyPairs;}Identity(C,Graph,S);P.Candidates.Add(C);
    }
    P.State=P.TopologyPairs<S.MinLoops?ERogueliteLoopPlanningState::LoopRequirementUnsatisfied:ERogueliteLoopPlanningState::Complete;
    P.bRequirementsSatisfied=S.MinLoops==0;P.Reason=P.State==ERogueliteLoopPlanningState::LoopRequirementUnsatisfied?TEXT("LoopRequirementUnsatisfied: insufficient topology pairs; no minima reduced."):TEXT("Topology query only. Socket/spatial feasibility and loop budget fulfillment require Analyze; no edges committed.");Finalize(P);return P;
}
bool FRogueliteLoopPlanner::FixedSocketGeometry(const FRogueliteDoorSocket& A,const FRogueliteDoorSocket& B,const FRogueliteDoorSocket& Start,const FRogueliteDoorSocket& End,
    const FTransform& FixedB,const FBox& Bounds,const FRogueliteLoopPlanningSettings& S,FTransform& Transform,float& Length,FBox& Occupancy,FString& Reason)
{
    Transform=FTransform::Identity;Length=0;Occupancy=FBox(ForceInit);
    if(!A.bValid||!B.bValid||!Start.bValid||!End.bValid||!Rigid(FixedB)||!Bounds.IsValid){Reason=TEXT("Invalid Socket/corridor descriptor/quarter-turn transform.");return false;}
    if(FMath::Abs(A.DoorWidth-B.DoorWidth)>.1||FMath::Abs(A.DoorHeight-B.DoorHeight)>.1||FMath::Abs(A.DoorWidth-Start.DoorWidth)>.1||FMath::Abs(A.DoorHeight-Start.DoorHeight)>.1){Reason=TEXT("DoorSpecificationMismatch: equal width/height interface required.");return false;}
    const FVector Delta=B.WorldPosition-A.WorldPosition;Length=FVector::DotProduct(Delta,A.WorldOutwardDirection);
    if(FMath::Abs(Delta.Z)>S.PositionTolerance){Reason=TEXT("HeightDifference: horizontal straight or L corridor cannot repair elevation; no connector proven.");return false;}
    const double Dot=FVector::DotProduct(A.WorldOutwardDirection,B.WorldOutwardDirection);
    if(Dot>-FMath::Cos(FMath::DegreesToRadians(S.AngleToleranceDegrees))||(Delta-A.WorldOutwardDirection*Length).Size()>S.PositionTolerance){Reason=TEXT("RequiresCornerCorridor: fixed thresholds are not collinear/opposed; diagnostic only, no corner route or collision feasibility proven.");return false;}
    if(!FMath::IsFinite(Length)||Length<S.MinCorridorLength||Length>S.MaxCorridorLength||Length<=2*Start.WallThickness){Reason=FString::Printf(TEXT("CorridorLengthOutOfRange %.3f: requested %.3f..%.3f and length>2*wall."),Length,S.MinCorridorLength,S.MaxCorridorLength);return false;}
    auto CA=Start,CB=End;CA.LocalPosition=CA.LocalOutwardDirection*(Length*.5);CB.LocalPosition=CB.LocalOutwardDirection*(Length*.5);
    FTransform ProposedB;if(!ARogueliteRoomAssembly::ComputeCorridorSocketTransforms(A,B,CA,CB,.1f,Transform,ProposedB,Reason))return false;
    if(!ProposedB.GetLocation().Equals(FixedB.GetLocation(),S.PositionTolerance)||!ProposedB.GetRotation().Equals(FixedB.GetRotation(),FMath::DegreesToRadians(S.AngleToleranceDegrees))||!ProposedB.GetScale3D().Equals(FVector::OneVector,.0001)){Reason=TEXT("FixedRoomTransformMismatch: shared C2 formula would move RoomB; refused.");return false;}
    auto Local=Bounds;Local.Min.X=-Length*.5;Local.Max.X=Length*.5;Occupancy=Local.TransformBy(Transform);Reason=TEXT("Fixed endpoint C2 transform calculation passed; no Actor moved.");return true;
}
FRogueliteLoopPlan FRogueliteLoopPlanner::Analyze(AActor* Owner,const FRogueliteSeedBatch& B,const FRogueliteSeedLayoutSettings& Source,const FRogueliteLoopPlanningSettings& S)
{
    FRogueliteLoopPlan P;P.PlanningSettingsDigest=G::StableTextDigest(DescribeSettings(S));
    if(!ValidateSettings(S,P.Reason)){P.State=ERogueliteLoopPlanningState::InvalidSettings;Finalize(P);return P;}
    FRogueliteRoomGraph Graph;TArray<Room> Rooms;TArray<Profile> Profiles;
    if(!Capture(Owner,B,Source,S,Graph,Rooms,Profiles,P.Reason)){P.State=ERogueliteLoopPlanningState::InvalidSource;Finalize(P);return P;}
    TArray<int32> Caps;for(const auto& R:Rooms)Caps.Add(R.Capacity);P=Topology(Graph,Caps,S);const auto Pairs=P.Candidates;P.Candidates.Reset(); // Keep topology checks charged to the single shared budget.
    P.SourceBatchId=B.BatchId;P.SourceWorld=Owner->GetWorld();P.SourceWorldPath=Owner->GetWorld()->GetPathName();P.LayoutDigest=B.LayoutDigest;P.TemplateCatalogVersion=Source.TemplateCatalogVersion;P.SourceSettingsDigest=SeedSettings(Source);P.SourceStateDigest=StateSignature(Owner,B);
    for(const auto& R:Rooms)P.SourceRooms.Add(R.Record->Room);
    TSet<int32> Compatible,Straight,Corner;
    const auto Add=[&](FRogueliteLoopCandidate C){Identity(C,Graph,S);if(P.Candidates.Num()<S.MaxLoopCandidates)P.Candidates.Add(MoveTemp(C));else P.bCandidateBudgetExhausted=true;};
    for(auto Base:Pairs)
    {
        const auto& A=Rooms[Base.NodeA];const auto& C=Rooms[Base.NodeB];Base.TemplateA=A.Record->TemplateId;Base.TemplateB=C.Record->TemplateId;Base.TypeA=A.Record->RoomType;Base.TypeB=C.Record->RoomType;Base.RoomAPath=A.Record->Room->GetPathName();Base.RoomBPath=C.Record->Room->GetPathName();
        for(const auto& Port:A.Ports)if(!Port.Reserved)Base.AvailableSocketsA.Add(Port.Door.Side);for(const auto& Port:C.Ports)if(!Port.Reserved)Base.AvailableSocketsB.Add(Port.Door.Side);
        if(!Base.bTopologyValid){if(Base.AvailableSocketsA.IsEmpty()||Base.AvailableSocketsB.IsEmpty())Base.Reason+=TEXT(" | NoFreeSocket: no unreserved legal enabled/authored port at one endpoint.");Add(Base);continue;}
        if(Base.AvailableSocketsA.IsEmpty()||Base.AvailableSocketsB.IsEmpty()){Base.Feasibility=ERogueliteLoopFeasibility::Rejected;Base.Reason=TEXT("NoFreeSocket: all legal authored/enabled ports reserved or unavailable.");Add(Base);continue;}
        for(const auto& PA:A.Ports)if(!PA.Reserved)for(const auto& PB:C.Ports)if(!PB.Reserved)for(const auto& Profile:Profiles)
        {
            if(P.CandidateChecks>=S.MaxCandidateChecks||P.Candidates.Num()>=S.MaxLoopCandidates){P.bCandidateBudgetExhausted=true;break;}++P.CandidateChecks;
            auto Candidate=Base;Candidate.bHasSocketPair=true;Candidate.SocketA=PA.Door.Side;Candidate.SocketB=PB.Door.Side;Candidate.DoorA=PA.Door;Candidate.DoorB=PB.Door;Candidate.bRequiresEnableA=PA.RequiresEnable;Candidate.bRequiresEnableB=PB.RequiresEnable;Candidate.CorridorProfile=Profile.Id;Candidate.CorridorDimensions=Profile.Dimensions;
            const bool Size=FMath::Abs(PA.Door.DoorWidth-PB.Door.DoorWidth)<=.1&&FMath::Abs(PA.Door.DoorHeight-PB.Door.DoorHeight)<=.1&&FMath::Abs(PA.Door.DoorWidth-Profile.Start.DoorWidth)<=.1&&FMath::Abs(PA.Door.DoorHeight-Profile.Start.DoorHeight)<=.1;
            if(!Size||PA.Door.DoorWidth<2*(S.CapsuleRadius+S.CapsuleClearance)+2||PA.Door.DoorHeight<2*S.CapsuleHalfHeight+S.CapsuleClearance){Candidate.Feasibility=ERogueliteLoopFeasibility::Rejected;Candidate.Reason=TEXT("DoorSpecificationMismatch or standing capsule cannot fit actual opening/profile.");Add(Candidate);continue;}
            Candidate.bSocketCompatible=true;Compatible.Add(PairKey(Base.NodeA,Base.NodeB));Candidate.Feasibility=ERogueliteLoopFeasibility::SocketCompatible;
            if(!FixedSocketGeometry(PA.Door,PB.Door,Profile.Start,Profile.End,C.Record->Room->GetActorTransform(),Profile.Bounds,S,Candidate.CorridorTransform,Candidate.CorridorLength,Candidate.CorridorOccupancy,Candidate.Reason))
            {
                const bool NeedCorner=Candidate.Reason.StartsWith(TEXT("RequiresCornerCorridor:"));Candidate.Feasibility=NeedCorner&&S.bAllowCornerCandidates?ERogueliteLoopFeasibility::RequiresCornerCorridor:ERogueliteLoopFeasibility::Rejected;if(NeedCorner)Corner.Add(PairKey(Base.NodeA,Base.NodeB));Add(Candidate);continue;
            }
            Candidate.CorridorDimensions.X=Candidate.CorridorLength;
            if(Space(Owner,B,A,C,S,Candidate)){Candidate.Feasibility=ERogueliteLoopFeasibility::StraightPrecheckPassed;Straight.Add(PairKey(Base.NodeA,Base.NodeB));}else Candidate.Feasibility=ERogueliteLoopFeasibility::Rejected;Add(Candidate);
        }
    }
    P.SocketCompatiblePairs=Compatible.Num();P.StraightPassedPairs=Straight.Num();P.CornerRequiredPairs=Corner.Num();
    const auto Rank=[](ERogueliteLoopFeasibility F){return F==ERogueliteLoopFeasibility::StraightPrecheckPassed?0:F==ERogueliteLoopFeasibility::RequiresCornerCorridor?1:F==ERogueliteLoopFeasibility::SocketCompatible?2:F==ERogueliteLoopFeasibility::TopologyValid?3:4;};
    P.Candidates.Sort([&](const auto& A,const auto& B){const int32 RA=Rank(A.Feasibility),RB=Rank(B.Feasibility);if(RA!=RB)return RA<RB;if(A.Score!=B.Score)return A.Score>B.Score;if(A.StableRank!=B.StableRank)return A.StableRank<B.StableRank;return A.CandidateId.ToString()<B.CandidateId.ToString();});
    TSet<FString> Used;TMap<int32,int32> Added;TArray<FBox> Boxes;TSet<int32> SelectedPairs;
    for(auto& C:P.Candidates)if(C.Feasibility==ERogueliteLoopFeasibility::StraightPrecheckPassed)
    {
        const FString KA=FString::Printf(TEXT("%d:%d"),C.NodeA,uint8(C.SocketA)),KB=FString::Printf(TEXT("%d:%d"),C.NodeB,uint8(C.SocketB));
        if(P.SelectedCandidates.Num()>=S.MaxLoops){C.SelectionReason=TEXT("MaxLoops provisional budget reached.");continue;}
        if(Used.Contains(KA)||Used.Contains(KB)||SelectedPairs.Contains(PairKey(C.NodeA,C.NodeB))){C.SelectionReason=TEXT("Provisional Socket/node-pair conflict; no C2 reservation exists for any candidate.");continue;}
        if(Graph.Nodes[C.NodeA].Degree+Added.FindRef(C.NodeA)+1>FMath::Min(Caps[C.NodeA],Graph.Nodes[C.NodeA].MaxAllowedDegree)||Graph.Nodes[C.NodeB].Degree+Added.FindRef(C.NodeB)+1>FMath::Min(Caps[C.NodeB],Graph.Nodes[C.NodeB].MaxAllowedDegree)){C.SelectionReason=TEXT("Combined selected-candidate Degree/capacity budget exceeded.");continue;}
        bool Hit=false;for(const auto& Box:Boxes)Hit|=Intersects(C.CorridorOccupancy,Box,S.PositionTolerance);if(Hit){C.SelectionReason=TEXT("Proposed corridor conflicts with an earlier selected candidate; not an actual entity.");continue;}
        C.bSelected=true;C.SelectionReason=TEXT("Provisional plan only; B4-B2 must recheck freshness, open doors safely and atomically commit actual C2 connection.");P.SelectedCandidates.Add(C.CandidateId);Used.Add(KA);Used.Add(KB);SelectedPairs.Add(PairKey(C.NodeA,C.NodeB));++Added.FindOrAdd(C.NodeA);++Added.FindOrAdd(C.NodeB);Boxes.Add(C.CorridorOccupancy);
    }
    P.bRequirementsSatisfied=P.SelectedCandidates.Num()>=S.MinLoops;P.State=P.bRequirementsSatisfied?ERogueliteLoopPlanningState::Complete:ERogueliteLoopPlanningState::LoopRequirementUnsatisfied;
    P.Reason=P.bRequirementsSatisfied?FString::Printf(TEXT("Tree intact. %d provisional straight candidates selected; zero actual loop Actors/Edges/Connections committed. Corner candidates diagnostic only."),P.SelectedCandidates.Num()):FString::Printf(TEXT("LoopRequirementUnsatisfied: MinLoops=%d but only %d straight candidates can be provisionally selected; Tree retained, minimum unchanged."),S.MinLoops,P.SelectedCandidates.Num());Finalize(P);return P;
}
bool FRogueliteLoopPlanner::IsCurrent(AActor* Owner,const FRogueliteSeedBatch& B,const FRogueliteSeedLayoutSettings& Source,const FRogueliteLoopPlanningSettings& S,const FRogueliteLoopPlan& P,FString& Reason)
{
    if(!P.bTreeValid||!IsValid(Owner)||!Owner->GetWorld()||P.SourceWorld.Get()!=Owner->GetWorld()||P.SourceBatchId!=B.BatchId||!B.bSucceeded||!B.bGeometryValid||B.Rooms.Num()!=P.SourceRooms.Num()||P.GraphDigest!=B.GraphDigest||P.LayoutDigest!=B.LayoutDigest||P.TemplateCatalogVersion!=Source.TemplateCatalogVersion||P.SourceSettingsDigest!=SeedSettings(Source)||P.PlanningSettingsDigest!=G::StableTextDigest(DescribeSettings(S)))
    {Reason=TEXT("StaleLoopPlan: batch/World/Seed/config/template version changed or cleared.");return false;}
    for(const auto& R:B.Rooms)if(!P.SourceRooms.IsValidIndex(R.NodeId)||P.SourceRooms[R.NodeId].Get()!=R.Room){Reason=TEXT("StaleLoopPlan: room identity replaced/destroyed.");return false;}
    FRogueliteRoomGraph CurrentGraph;TArray<Room> CurrentRooms;TArray<Profile> CurrentProfiles;
    if(!Capture(Owner,B,Source,S,CurrentGraph,CurrentRooms,CurrentProfiles,Reason)){Reason=TEXT("StaleLoopPlan: ")+Reason;return false;}
    if(P.SourceStateDigest!=StateSignature(Owner,B)){Reason=TEXT("StaleLoopPlan: room/socket/collision/obstacle/reservation state changed; analyze again.");return false;}
    Reason=TEXT("Planning source current. This is not permission or proof of an actual committed loop.");return true;
}
