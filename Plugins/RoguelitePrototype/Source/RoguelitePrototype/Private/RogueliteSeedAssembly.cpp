#include "RogueliteSeedAssembly.h"
#include "RogueliteSeedAssemblyInternal.h"
#include "RogueliteRoomTypeAssignment.h"
#include "RoguelitePrototype.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Math/RandomStream.h"

namespace RogueliteSeedAssemblyInternal
{
    using G = FRogueliteRoomGraphGenerator;
    FName BatchTag(const FGuid& Id) { return FName(*(TEXT("RogueliteB2_") + Id.ToString(EGuidFormats::Digits))); }
    FString Pose(const FTransform& T)
    {
        const FVector P=T.GetLocation();
        return FString::Printf(TEXT("%.3f,%.3f,%.3f,%d"),P.X,P.Y,P.Z,FMath::RoundToInt(FRotator::NormalizeAxis(T.Rotator().Yaw)));
    }
    bool Rigid(const FTransform& T)
    {
        const FRotator R=T.Rotator();
        return !T.ContainsNaN() && T.GetScale3D().Equals(FVector::OneVector,.0001)
            && FMath::Abs(R.Pitch)<.01 && FMath::Abs(R.Roll)<.01
            && FMath::Abs(FMath::FindDeltaAngleDegrees(R.Yaw,FMath::RoundToFloat(R.Yaw/90)*90))<.01;
    }
    FBox RoomBox(const ARogueliteParametricRoom* R)
    {
        return FBox(FVector(-R->RoomLength*.5-R->WallThickness,-R->RoomWidth*.5-R->WallThickness,-R->FloorThickness),
            FVector(R->RoomLength*.5+R->WallThickness,R->RoomWidth*.5+R->WallThickness,R->RoomHeight)).TransformBy(R->GetActorTransform());
    }
    bool Overlap(const FBox& A,const FBox& B)
    {
        return FMath::Min(A.Max.X,B.Max.X)-FMath::Max(A.Min.X,B.Min.X)>.1
            && FMath::Min(A.Max.Y,B.Max.Y)-FMath::Max(A.Min.Y,B.Min.Y)>.1
            && FMath::Min(A.Max.Z,B.Max.Z)-FMath::Max(A.Min.Z,B.Min.Z)>.1;
    }
    bool RootFree(ARogueliteRoomTemplate* R,AActor* Owner)
    {
        const FBox Box=RoomBox(R); auto* W=R->GetWorld();
        for(TActorIterator<ARogueliteParametricRoom> I(W);I;++I) if(*I!=R && !I->IsActorBeingDestroyed() && Overlap(Box,RoomBox(*I))) return false;
        for(TActorIterator<ARogueliteParametricCorridor> I(W);I;++I) if(!I->IsActorBeingDestroyed() && Overlap(Box,I->GetWorldOccupancyBounds())) return false;
        FCollisionQueryParams Q; Q.AddIgnoredActor(R); Q.AddIgnoredActor(Owner);
        return !W->OverlapBlockingTestByChannel(Box.GetCenter(),FQuat::Identity,ECC_Pawn,FCollisionShape::MakeBox(Box.GetExtent()-FVector(.1)),Q);
    }
    template<class T> T* SpawnOwned(UWorld* W,UClass* Class,AActor* Owner,const FGuid& Id,const FTransform& Tfm)
    {
        FActorSpawnParameters P; P.Owner=Owner; P.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        if(W->IsGameWorld())P.ObjectFlags|=RF_Transient;
        T* A=W->SpawnActor<T>(Class,Tfm,P); if(A) A->Tags.Add(BatchTag(Id)); return A;
    }
    void DestroyNew(AActor* A) { if(IsValid(A)) A->GetWorld()->DestroyActor(A); }
    ERogueliteRoomSide Opposite(ERogueliteRoomSide S)
    {
        switch(S){case ERogueliteRoomSide::North:return ERogueliteRoomSide::South;case ERogueliteRoomSide::South:return ERogueliteRoomSide::North;
            case ERogueliteRoomSide::East:return ERogueliteRoomSide::West;default:return ERogueliteRoomSide::East;}
    }
    TArray<FRogueliteDoorSocket> Ports(ARogueliteRoomTemplate* R)
    {
        TArray<FRogueliteDoorSocket> Out;
        for(const auto& D:R->GetDoorSockets()) if(D.bValid) Out.Add(D);
        Out.Sort([](const auto& A,const auto& B){return uint8(A.Side)<uint8(B.Side);}); return Out;
    }
    ERogueliteSeedRoomType PickType(const FRogueliteRoomGraphNode& N,const FRogueliteSeedLayoutSettings& S)
    {
        if(N.Role==ERogueliteGraphNodeRole::Final) return ERogueliteSeedRoomType::Final;
        const auto& Weights=N.Role==ERogueliteGraphNodeRole::Branch?S.BranchTypeWeights:S.MainTypeWeights;
        int32 Sum=0; for(int32 W:Weights) Sum+=W;
        FRandomStream R(G::DeriveSeed(S.Seed,ERogueliteGraphSeedPurpose::RoomType,S.AlgorithmVersion,S.ConfigVersion,N.NodeId));
        int32 Pick=R.RandRange(1,Sum); for(int32 I=0;I<3;++I) if((Pick-=Weights[I])<=0) return ERogueliteSeedRoomType(I);
        return ERogueliteSeedRoomType::Combat;
    }
    FName PickEncounter(int32 Node,ERogueliteSeedRoomType Type,const FRogueliteSeedLayoutSettings& S)
    {
        TArray<FRogueliteSeedEncounterOption> Options;
        for(const auto& E:S.Encounters) if(E.AllowedTypes.Contains(Type)) Options.Add(E);
        Options.Sort([](const auto& A,const auto& B){return A.ProfileId.ToString()<B.ProfileId.ToString();});
        int32 Sum=0; for(const auto& E:Options) Sum+=E.Weight;
        if(!Sum) return NAME_None;
        FRandomStream R(G::DeriveSeed(S.Seed,ERogueliteGraphSeedPurpose::Encounter,S.AlgorithmVersion,S.ConfigVersion,Node));
        int32 Pick=R.RandRange(1,Sum); for(const auto& E:Options) if((Pick-=E.Weight)<=0) return E.ProfileId; return NAME_None;
    }
    void SealUnused(ARogueliteRoomTemplate* Room)
    {
        for(auto Side:{ERogueliteRoomSide::North,ERogueliteRoomSide::South,ERogueliteRoomSide::East,ERogueliteRoomSide::West})
        {
            if(ARogueliteRoomAssembly::GetRoomSocketConnectionId(Room,Side).IsValid()) continue;
            switch(Side){case ERogueliteRoomSide::North:Room->NorthDoor.bEnabled=false;break;case ERogueliteRoomSide::South:Room->SouthDoor.bEnabled=false;break;
                case ERogueliteRoomSide::East:Room->EastDoor.bEnabled=false;break;default:Room->WestDoor.bEnabled=false;break;}
        }
        Room->RebuildTemplate();
    }
    FString MembersText(ARogueliteRoomTemplate* R)
    {
        FString Text;
        for(const auto& M:R->GetTemplateMemberStates()) Text+=FString::Printf(TEXT("|member=%s,%s,%s,%s,%s,%s,%d,%d"),
            *M.Name.ToString(),*M.ParentName.ToString(),*M.ParentLocalTransform.ToHumanReadableString(),*M.StaticMeshPath,
            *FString::Join(M.MaterialPaths,TEXT(",")),*M.CollisionProfile.ToString(),M.CollisionEnabled,int32(M.Mobility.GetValue()));
        return Text;
    }
    TArray<int32> TemplateOrder(const TArray<CatalogEntry>& Catalog,ERogueliteSeedRoomType Type,int32 Degree,int32 Node,const FRogueliteSeedLayoutSettings& S)
    {
        TArray<int32> Pool,Out; for(int32 I=0;I<Catalog.Num();++I) if(Catalog[I].Capacity>=Degree && Catalog[I].Option.AllowedTypes.Contains(Type)) Pool.Add(I);
        FRandomStream R(G::DeriveSeed(S.Seed,ERogueliteGraphSeedPurpose::TemplateSelection,S.AlgorithmVersion,S.ConfigVersion,Node));
        while(!Pool.IsEmpty())
        {
            int32 Sum=0; for(int32 I:Pool) Sum+=Catalog[I].Option.Weight;
            int32 Pick=R.RandRange(1,Sum),Chosen=0;
            for(int32 I=0;I<Pool.Num();++I) if((Pick-=Catalog[Pool[I]].Option.Weight)<=0){Chosen=I;break;}
            Out.Add(Pool[Chosen]); Pool.RemoveAt(Chosen);
        }
        return Out;
    }
}

using namespace RogueliteSeedAssemblyInternal;

bool RogueliteSeedAssemblyInternal::BuildInputs(AActor* Owner,const FRogueliteSeedLayoutSettings& S,FRogueliteSeedBatch& B,Inputs& Input,FString& Reason)
{
    UWorld* W=IsValid(Owner)?Owner->GetWorld():nullptr;
    const auto Fail=[&](const FString& Why){Reason=Why;return false;};
    if(!W)return Fail(TEXT("Input owner/world unavailable."));
    if(S.TemplateCatalogVersion<1 || S.SolverVersion!=1 || S.MaxCandidatesPerEdge<1 || S.MaxCandidatesPerEdge>256 || S.Templates.IsEmpty()
        || S.Templates.Num()>16 || S.Corridors.IsEmpty() || S.Corridors.Num()>8 || S.Encounters.IsEmpty() || S.Encounters.Num()>32)
        return Fail(TEXT("Invalid catalog/solver version, candidate budget or bounded catalog sizes."));
    if(S.AlgorithmVersion==1)for(const auto& Weights:{S.MainTypeWeights,S.BranchTypeWeights})
    {
        int64 Sum=0; if(Weights.Num()!=3) return Fail(TEXT("Type weights require Combat/Challenge/SupplyShop."));
        for(int32 Weight:Weights){if(Weight<0 || Weight>10000) return Fail(TEXT("Type weight outside 0..10000.")); Sum+=Weight;}
        if(!Sum) return Fail(TEXT("Room type weight sum is zero."));
    }
    TSet<FName> EncounterIds;
    for(const auto& E:S.Encounters)
    {
        if(E.ProfileId.IsNone() || EncounterIds.Contains(E.ProfileId) || E.Weight<1 || E.Weight>10000 || E.AllowedTypes.IsEmpty()) return Fail(TEXT("Invalid/duplicate encounter reference."));
        for(auto Type:E.AllowedTypes) if(uint8(Type)>4) return Fail(TEXT("Unknown encounter room type."));
        EncounterIds.Add(E.ProfileId);
    }
    const auto GraphResult=G::Generate(S.Seed,S.GraphSettings());
    if(!GraphResult.bSucceeded) return Fail(TEXT("B1 graph rejected: ")+GraphResult.Reason);
    Input.Graph=GraphResult.Graph; const auto& Graph=Input.Graph; B.GraphDigest=Graph.StableDigest;
    FString Configuration=G::DescribeSettings(S.GraphSettings())+FString::Printf(TEXT("|B2solver=%d|catalog=%d|budget=%d|nav=%d"),S.SolverVersion,S.TemplateCatalogVersion,S.MaxCandidatesPerEdge,S.bCheckNavigation);
    if(S.AlgorithmVersion==1){for(int32 I:S.MainTypeWeights) Configuration+=FString::Printf(TEXT("|mainWeight=%d"),I);
        for(int32 I:S.BranchTypeWeights) Configuration+=FString::Printf(TEXT("|branchWeight=%d"),I);}
    else Configuration+=TEXT("|")+FRogueliteRoomTypeAssigner::DescribeSettings(S.TypeAssignment);
    TArray<FRogueliteSeedEncounterOption> Encounters=S.Encounters;
    Encounters.Sort([](const auto& A,const auto& C){return A.ProfileId.ToString()<C.ProfileId.ToString();});
    for(const auto& E:Encounters)
    { Configuration+=FString::Printf(TEXT("|encounter=%s,%d"),*E.ProfileId.ToString(),E.Weight); for(auto T:E.AllowedTypes) Configuration+=FString::Printf(TEXT(",%d"),uint8(T)); }
    auto& Profiles=Input.Profiles;
    TSet<FName> ProfileIds;
    for(const auto& P:S.Corridors)
    {
        if(P.Id.IsNone() || ProfileIds.Contains(P.Id)) return Fail(TEXT("Missing/duplicate corridor profile ID.")); ProfileIds.Add(P.Id);
        Configuration+=FString::Printf(TEXT("|profile=%s,%d,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f"),*P.Id.ToString(),P.bEnabled,P.Width,P.Height,P.WallThickness,P.FloorThickness,P.DoorWidth,P.DoorHeight);
        if(P.LengthCandidates.IsEmpty() || P.LengthCandidates.Num()>16) return Fail(TEXT("Corridor lengths must contain 1..16 candidates."));
        for(float L:P.LengthCandidates) Configuration+=FString::Printf(TEXT(",%.3f"),L);
        if(!P.bEnabled) continue;
        for(float V:{P.Width,P.Height,P.WallThickness,P.FloorThickness,P.DoorWidth,P.DoorHeight}) if(!FMath::IsFinite(V) || V<1 || V>100000) return Fail(TEXT("Invalid corridor dimensions."));
        if(P.Width<100 || P.Height<100 || P.DoorWidth>P.Width || P.DoorHeight>P.Height || P.DoorWidth<72 || P.DoorHeight<180) return Fail(TEXT("Corridor opening cannot contain the test capsule or inner shell."));
        for(float L:P.LengthCandidates) if(!FMath::IsFinite(L) || L<100 || L>100000 || L<=2*P.WallThickness) return Fail(TEXT("Invalid corridor candidate length."));
        Profiles.Add(P);
    }
    if(Profiles.IsEmpty()) return Fail(TEXT("No enabled straight corridor profiles."));
    auto& Catalog=Input.Catalog; TSet<FName> TemplateIds;
    TArray<FRogueliteSeedTemplateOption> Options=S.Templates;
    Options.Sort([](const auto& A,const auto& C){return A.Id.ToString()<C.Id.ToString();});
    for(const auto& Option:Options)
    {
        if(Option.Id.IsNone() || TemplateIds.Contains(Option.Id) || Option.Weight<1 || Option.Weight>10000 || Option.AllowedTypes.IsEmpty()) return Fail(TEXT("Invalid/duplicate template option."));
        for(auto Type:Option.AllowedTypes) if(uint8(Type)>4) return Fail(TEXT("Unknown template room type."));
        TemplateIds.Add(Option.Id); auto* Class=Option.TemplateClass.LoadSynchronous();
        if(!Class || Class->HasAnyClassFlags(CLASS_Abstract)) return Fail(TEXT("Template class unavailable: ")+Option.Id.ToString());
        const auto Default=Class->GetDefaultObject<ARogueliteRoomTemplate>()->GetTemplateDescriptor();
        if(Default.Dimensions.ContainsNaN() || Default.Dimensions.GetMin()<100 || !FMath::IsFinite(Default.WallThickness)
            || !FMath::IsFinite(Default.FloorThickness) || Default.WallThickness<1 || Default.FloorThickness<1)
            return Fail(TEXT("Invalid authored template dimensions: ")+Option.Id.ToString());
        const FVector ProbeLocation(1000000+Catalog.Num()*10000,1000000,0);
        const FBox Envelope(FVector(-Default.Dimensions.X*.5-Default.WallThickness,-Default.Dimensions.Y*.5-Default.WallThickness,-Default.FloorThickness)+ProbeLocation,
            FVector(Default.Dimensions.X*.5+Default.WallThickness,Default.Dimensions.Y*.5+Default.WallThickness,Default.Dimensions.Z)+ProbeLocation);
        FCollisionQueryParams ProbeQuery;ProbeQuery.AddIgnoredActor(Owner);
        if(W->OverlapBlockingTestByChannel(Envelope.GetCenter(),FQuat::Identity,ECC_Pawn,FCollisionShape::MakeBox(Envelope.GetExtent()),ProbeQuery))
            return Fail(TEXT("Fixed metadata probe space blocked; existing actors protected."));
        auto* Probe=SpawnOwned<ARogueliteRoomTemplate>(W,Class,Owner,B.BatchId,FTransform(ProbeLocation));
        if(!Probe) return Fail(TEXT("Template probe spawn failed."));
        const auto Validation=Probe->ValidateTemplate(); const auto D=Probe->GetTemplateDescriptor();
        FString Signature=FString::Printf(TEXT("|template=%s,%s,%.3f,%.3f,%.3f,%.3f,%.3f,%d"),*Option.Id.ToString(),*Class->GetPathName(),
            D.Dimensions.X,D.Dimensions.Y,D.Dimensions.Z,D.WallThickness,D.FloorThickness,Option.Weight);
        for(auto Type:Option.AllowedTypes) Signature+=FString::Printf(TEXT(",%d"),uint8(Type));
        const auto Doors=Ports(Probe);
        for(const auto& Port:Doors) Signature+=FString::Printf(TEXT("|port=%d,%.3f,%.3f,%.3f,%.3f,%.3f"),uint8(Port.Side),Port.LocalPosition.X,Port.LocalPosition.Y,Port.AppliedOffset,Port.DoorWidth,Port.DoorHeight);
        const FString MemberSignature=MembersText(Probe);
        Signature+=TEXT("|authoredId=")+D.TemplateId.ToString()+MemberSignature; DestroyNew(Probe);
        if(!Validation.bValid || Validation.bDeferredContent || D.TemplateId.IsNone()) return Fail(TEXT("Template geometry/POI validation failed: ")+Option.Id.ToString()+TEXT(" ")+FString::Join(Validation.Issues,TEXT(" | ")));
        CatalogEntry Entry; Entry.Option=Option; Entry.Class=Class; Entry.Capacity=Doors.Num(); Entry.Signature=Signature; Entry.MemberSignature=MemberSignature; Entry.Descriptor=D; Entry.Sockets=Doors; Catalog.Add(Entry); Configuration+=Signature;
    }
    FRogueliteRoomTypeAssignmentResult Assigned;
    if(S.AlgorithmVersion==2)
    {
        TArray<TArray<ERogueliteSeedRoomType>> Compatible;Compatible.SetNum(Graph.Nodes.Num());
        for(const auto& Node:Graph.Nodes)for(int32 Type=0;Type<5;++Type)
            if(!TemplateOrder(Catalog,ERogueliteSeedRoomType(Type),Node.Degree,Node.NodeId,S).IsEmpty()
                && (Type==4 || !PickEncounter(Node.NodeId,ERogueliteSeedRoomType(Type),S).IsNone()))Compatible[Node.NodeId].Add(ERogueliteSeedRoomType(Type));
        Assigned=FRogueliteRoomTypeAssigner::Assign(Graph,S.TypeAssignment,Compatible);
        if(!Assigned.bSucceeded)return Fail(TEXT("RoomType assignment rejected: ")+Assigned.Reason);
        Configuration+=TEXT("|assignment=")+Assigned.StableDigest;
    }
    B.ConfigurationDigest=G::StableTextDigest(Configuration);
    auto& Types=Input.Types; auto& EncounterRefs=Input.Encounters;
    for(const auto& Node:Graph.Nodes)
    {
        const auto Type=S.AlgorithmVersion==2?Assigned.Types[Node.NodeId]:PickType(Node,S); Types.Add(Type);
        EncounterRefs.Add(Type==ERogueliteSeedRoomType::Event?NAME_None:PickEncounter(Node.NodeId,Type,S));
        Input.EventDefinitions.Add(S.AlgorithmVersion==2?Assigned.EventDefinitionIds[Node.NodeId]:NAME_None);
        if((Type!=ERogueliteSeedRoomType::Event && EncounterRefs.Last().IsNone()) || TemplateOrder(Catalog,Type,Node.Degree,Node.NodeId,S).IsEmpty())
            return Fail(FString::Printf(TEXT("Node=%d Type=%d Degree=%d has no compatible template/encounter reference."),Node.NodeId,uint8(Type),Node.Degree));
    }
    return true;
}

TArray<FRogueliteSeedCandidateRecord> RogueliteSeedAssemblyInternal::Candidates(const Inputs& Input,const FRogueliteSeedLayoutSettings& S,
    int32 Child,int32 EdgeId,TArray<FRogueliteDoorSocket> ParentPorts,ERogueliteRoomSide Preferred)
{
    TArray<FRogueliteSeedCandidateRecord> Out;
    ParentPorts.StableSort([&](const auto& X,const auto& Y){return (X.Side==Preferred?0:uint8(X.Side)+1)<(Y.Side==Preferred?0:uint8(Y.Side)+1);});
    for(const auto& PA:ParentPorts)
        for(int32 Index:TemplateOrder(Input.Catalog,Input.Types[Child],Input.Graph.Nodes[Child].Degree,Child,S))
        {
            auto ChildPorts=Input.Catalog[Index].Sockets;
            ChildPorts.StableSort([](const auto& X,const auto& Y){return (X.Side==ERogueliteRoomSide::South?0:uint8(X.Side)+1)<(Y.Side==ERogueliteRoomSide::South?0:uint8(Y.Side)+1);});
            for(const auto& PC:ChildPorts) for(const auto& Profile:Input.Profiles) for(float Length:Profile.LengthCandidates)
            {
                if(Out.Num()>=S.MaxCandidatesPerEdge)return Out;
                auto& C=Out.AddDefaulted_GetRef();C.EdgeId=EdgeId;C.CandidateIndex=Out.Num()-1;C.TemplateId=Input.Catalog[Index].Option.Id;
                C.ParentSocket=PA.Side;C.ChildSocket=PC.Side;C.ProfileId=Profile.Id;C.Length=Length;
            }
        }
    return Out;
}

FRogueliteGraphGenerationSettings FRogueliteSeedLayoutSettings::GraphSettings() const
{
    FRogueliteGraphGenerationSettings S; S.MinMainPathNodes=MinMainNodes; S.MaxMainPathNodes=MaxMainNodes; S.MinBranches=MinBranches; S.MaxBranches=MaxBranches;
    S.MinBranchDepth=MinBranchDepth; S.MaxBranchDepth=MaxBranchDepth; S.MaxNodes=MaxNodes; S.MaxDegree=MaxDegree; S.MaxAttempts=GraphAttempts;
    S.AlgorithmVersion=AlgorithmVersion; S.ConfigVersion=ConfigVersion;
    S.MinNodes=MinNodes;S.MinDecisionNodes=MinDecisionNodes;S.MaxDecisionNodes=MaxDecisionNodes;S.MinNestedDecisionNodes=MinNestedDecisionNodes;
    S.MaxBranchGenerationDepth=MaxBranchGenerationDepth;S.RecursiveBranchWeight=RecursiveBranchWeight;return S;
}

FRogueliteSeedBatch FRogueliteSeedLayoutAssembler::Generate(AActor* Owner,const FRogueliteSeedLayoutSettings& S,const FTransform& Start)
{
    FRogueliteSeedBatch B; B.Seed=S.Seed; B.BatchId=FGuid::NewGuid();
    UWorld* W=IsValid(Owner)?Owner->GetWorld():nullptr;
    const auto Fail=[&](const FString& Why)
    {
        FString Cleanup; const bool Cleared=Clear(Owner,B,Cleanup); B.bSucceeded=false; B.bGeometryValid=false;
        B.Reason=Why+(Cleared?TEXT(" | Batch actors/connections removed."):TEXT(" | Cleanup refused: ")+Cleanup);
        UE_LOG(LogRoguelitePrototype,Warning,TEXT("B2_FAILED Seed=%d %s"),S.Seed,*B.Reason); return B;
    };
    if(!W || W->IsGameWorld() || !Rigid(Start)) return Fail(TEXT("B2 is Editor authoring only; valid owner and Scale=1 quarter-turn Start required. Runtime is B3."));
    Inputs Input; FString InputReason;
    if(!BuildInputs(Owner,S,B,Input,InputReason))return Fail(InputReason);
    const auto& Graph=Input.Graph; const auto& Catalog=Input.Catalog; const auto& Types=Input.Types; const auto& EncounterRefs=Input.Encounters;
    B.Registry=SpawnOwned<ARogueliteRoomAssembly>(W,ARogueliteRoomAssembly::StaticClass(),Owner,B.BatchId,FTransform::Identity);
    if(!B.Registry) return Fail(TEXT("Connection registry spawn failed."));
    B.OwnedActors.Add(B.Registry); B.Registry->bCheckNavigation=S.bCheckNavigation;
    const auto RecordRoom=[&](int32 Node,int32 Option,ARogueliteRoomTemplate* Room)
    {
        auto& R=B.Rooms.AddDefaulted_GetRef(); R.NodeId=Node; R.RoomType=Types[Node]; R.TemplateId=Catalog[Option].Option.Id;
        R.TemplateClass=Catalog[Option].Class; R.EncounterProfile=EncounterRefs[Node]; R.EventDefinitionId=Input.EventDefinitions[Node]; R.Room=Room; R.PlacedTransform=Room->GetActorTransform();
        R.LogicalDegree=Graph.Nodes[Node].Degree; R.TemplateCapacity=Catalog[Option].Capacity; B.OwnedActors.Add(Room);
        R.Dimensions=FVector(Room->RoomLength,Room->RoomWidth,Room->RoomHeight); R.WallThickness=Room->WallThickness; R.FloorThickness=Room->FloorThickness; R.MemberSignature=MembersText(Room);
    };
    bool RootPlaced=false;
    for(int32 Index:TemplateOrder(Catalog,Types[0],Graph.Nodes[0].Degree,0,S))
    {
        auto* R=SpawnOwned<ARogueliteRoomTemplate>(W,Catalog[Index].Class,Owner,B.BatchId,Start);
        if(R && R->EvaluateTemplate().bValid && RootFree(R,Owner)){RecordRoom(0,Index,R);RootPlaced=true;break;} DestroyNew(R);
    }
    if(!RootPlaced) return Fail(TEXT("Start room parameter footprint or physical occupancy blocked; no existing actor moved."));
    for(const auto& Edge:Graph.Edges)
    {
        const int32 Parent=Edge.NodeA,Child=Edge.NodeB;
        auto* ParentRecord=B.Rooms.FindByPredicate([&](const auto& R){return R.NodeId==Parent;});
        if(!ParentRecord || B.Rooms.ContainsByPredicate([&](const auto& R){return R.NodeId==Child;}))return Fail(TEXT("Graph expansion order invalid."));
        auto* A=ParentRecord->Room.Get();auto FreePorts=Ports(A);
        FreePorts.RemoveAll([&](const auto& D){return ARogueliteRoomAssembly::GetRoomSocketConnectionId(A,D.Side).IsValid();});
        ERogueliteRoomSide Preferred=ERogueliteRoomSide::North;
        for(const auto& Existing:B.Edges)if(Existing.ChildNode==Parent)Preferred=Opposite(Existing.ChildSocket);
        const auto Ordered=Candidates(Input,S,Child,Edge.EdgeId,FreePorts,Preferred);bool Placed=false;
        for(const auto& Choice:Ordered)
        {
            auto& Attempt=B.Candidates.Add_GetRef(Choice);
            const int32 Index=Catalog.IndexOfByPredicate([&](const auto& T){return T.Option.Id==Choice.TemplateId;});
            const auto& Profile=*Input.Profiles.FindByPredicate([&](const auto& P){return P.Id==Choice.ProfileId;});
            auto* ChildRoom=SpawnOwned<ARogueliteRoomTemplate>(W,Catalog[Index].Class,Owner,B.BatchId,FTransform(FVector(1200000+Child*10000,1000000,0)));
            auto* C=SpawnOwned<ARogueliteParametricCorridor>(W,ARogueliteParametricCorridor::StaticClass(),Owner,B.BatchId,FTransform(FVector(1400000,1000000,0)));
            if(!ChildRoom || !C){Attempt.Reason=TEXT("Candidate spawn failed.");DestroyNew(C);DestroyNew(ChildRoom);continue;}
            C->CorridorLength=Choice.Length;C->CorridorWidth=Profile.Width;C->CorridorHeight=Profile.Height;C->WallThickness=Profile.WallThickness;
            C->FloorThickness=Profile.FloorThickness;C->DoorWidth=Profile.DoorWidth;C->DoorHeight=Profile.DoorHeight;C->RebuildCorridor();
            FTransform CT,BT;
            if(!B.Registry->ComputeCorridorTransforms(A,Choice.ParentSocket,C,ERogueliteCorridorEndpoint::Start,ChildRoom,Choice.ChildSocket,CT,BT,Attempt.Reason))
            {DestroyNew(C);DestroyNew(ChildRoom);continue;}
            const auto Check=B.Registry->TryAlignCorridorConnection(A,Choice.ParentSocket,C,ERogueliteCorridorEndpoint::Start,ChildRoom,Choice.ChildSocket);
            Attempt.bSucceeded=Check.bSucceeded && Check.bGeometryPassed && Check.bCapsulePassed;Attempt.Reason=Check.Message;
            if(!Attempt.bSucceeded){DestroyNew(C);DestroyNew(ChildRoom);continue;}
            auto& Record=B.Edges.AddDefaulted_GetRef();Record.EdgeId=Edge.EdgeId;Record.ParentNode=Parent;Record.ChildNode=Child;
            Record.Corridor=C;Record.ConnectionId=Check.ConnectionId;Record.ParentSocket=Choice.ParentSocket;Record.ChildSocket=Choice.ChildSocket;
            Record.CorridorProfile=Profile.Id;Record.CorridorLength=Choice.Length;Record.AcceptedCandidate=Choice.CandidateIndex;Record.Validation=Check;
            B.OwnedActors.Add(C);RecordRoom(Child,Index,ChildRoom);Placed=true;break;
        }
        if(!Placed)return Fail(FString::Printf(TEXT("Edge=%d Parent=%d Child=%d exhausted %d candidates; no subgraph movement/backtracking."),Edge.EdgeId,Parent,Child,Ordered.Num()));
    }
    for(auto& R:B.Rooms) SealUnused(R.Room);
    if(!Revalidate(Owner,B)) return Fail(TEXT("Final batch validation failed: ")+B.Reason);
    B.LayoutDigest=G::StableTextDigest(DescribeLayout(B)); B.bSucceeded=true;
    B.Reason=TEXT("All rooms/straight corridors assembled; geometry/capsule passed. Navigation is a separate status, not Runtime Ready.");
    UE_LOG(LogRoguelitePrototype,Display,TEXT("B2_SUCCEEDED Seed=%d Rooms=%d Edges=%d Candidates=%d Digest=%s Nav=%d"),S.Seed,B.Rooms.Num(),B.Edges.Num(),B.Candidates.Num(),*B.LayoutDigest,int32(B.Navigation));
    return B;
}

bool FRogueliteSeedLayoutAssembler::Clear(AActor* Owner,FRogueliteSeedBatch& B,FString& Reason)
{
    TSet<AActor*> Owned; for(auto A:B.OwnedActors) if(IsValid(A)) Owned.Add(A);
    for(auto A:Owned)
    {
        TArray<AActor*> Attached; A->GetAttachedActors(Attached);
        if(!IsValid(Owner) || A->GetWorld()!=Owner->GetWorld() || A->GetOwner()!=Owner || !A->Tags.Contains(BatchTag(B.BatchId))) {Reason=TEXT("Ownership changed; no cleanup performed.");return false;}
        if(auto* Room=Cast<ARogueliteParametricRoom>(A);Room && !Room->ManualPOIActors.IsEmpty()){Reason=TEXT("ManualPOIActors present; cleanup refused.");return false;}
        for(auto Child:Attached) if(!Owned.Contains(Child)){Reason=TEXT("External attached actor protected; cleanup refused.");return false;}
    }
    if(IsValid(Owner) && Owner->GetWorld())
        for(TActorIterator<ARogueliteRoomAssembly> I(Owner->GetWorld());I;++I)
            for(const auto& C:I->GetConnectionRecords())
                if(C.bActive && (Owned.Contains(C.RoomA) || Owned.Contains(C.RoomB) || Owned.Contains(C.Corridor))
                    && !(*I==B.Registry && B.Edges.ContainsByPredicate([&](const auto& E){return E.ConnectionId==C.Id;})))
                {Reason=TEXT("An external connection references this batch; cleanup refused.");return false;}
    if(IsValid(B.Registry)) for(const auto& E:B.Edges) B.Registry->ReleaseConnection(E.ConnectionId);
    for(int32 I=B.OwnedActors.Num()-1;I>=0;--I) DestroyNew(B.OwnedActors[I]);
    B.OwnedActors.Reset(); B.Rooms.Reset(); B.Edges.Reset(); B.Registry=nullptr; B.bSucceeded=false; B.bGeometryValid=false; B.bCleanupComplete=true;
    B.LayoutDigest.Reset(); B.Navigation=ERogueliteNavigationCheck::NotChecked;
    Reason=TEXT("Only tagged, owned batch actors/connections cleared; preexisting actors preserved."); B.Reason=Reason; return true;
}
bool FRogueliteSeedLayoutAssembler::Revalidate(AActor* Owner,FRogueliteSeedBatch& B)
{
    B.bSucceeded=false; B.bGeometryValid=false; B.Navigation=ERogueliteNavigationCheck::NotChecked;
    if(!IsValid(Owner) || !IsValid(B.Registry) || B.Registry->GetOwner()!=Owner || B.Rooms.IsEmpty()) {B.Reason=TEXT("Missing owned batch/registry.");return false;}
    int32 PassedNav=0; bool Unavailable=false,FailedNav=false;
    for(auto& R:B.Rooms)
    {
        if(!IsValid(R.Room) || R.Room->GetOwner()!=Owner || !R.Room->GetActorTransform().Equals(R.PlacedTransform,.0001)
            || !R.Room->EvaluateTemplate().bValid || !FVector(R.Room->RoomLength,R.Room->RoomWidth,R.Room->RoomHeight).Equals(R.Dimensions,.0001)
            || R.Room->WallThickness!=R.WallThickness || R.Room->FloorThickness!=R.FloorThickness || MembersText(R.Room)!=R.MemberSignature)
            {B.Reason=FString::Printf(TEXT("Node=%d moved, dimensions/content changed, missing or template invalid."),R.NodeId);return false;}
        if(Ports(R.Room).Num()!=R.LogicalDegree){B.Reason=TEXT("Unused socket sealing or degree mismatch.");return false;}
    }
    if(B.Registry->GetConnectionRecords().Num()!=B.Edges.Num()){B.Reason=TEXT("Registry record count mismatch.");return false;}
    for(auto& E:B.Edges)
    {
        E.Validation=B.Registry->RevalidateConnection(E.ConnectionId);
        if(!E.Validation.bSucceeded || !E.Validation.bGeometryPassed || !E.Validation.bCapsulePassed)
        { B.Navigation=E.Validation.Navigation; B.Reason=E.Validation.Message; return false; }
        PassedNav+=E.Validation.Navigation==ERogueliteNavigationCheck::Passed;
        Unavailable|=E.Validation.Navigation==ERogueliteNavigationCheck::Unavailable;
        FailedNav|=E.Validation.Navigation==ERogueliteNavigationCheck::Failed;
    }
    B.Navigation=FailedNav?ERogueliteNavigationCheck::Failed:Unavailable?ERogueliteNavigationCheck::Unavailable:
        PassedNav==B.Edges.Num()?ERogueliteNavigationCheck::Passed:ERogueliteNavigationCheck::NotChecked;
    B.bSucceeded=true; B.bGeometryValid=true; B.Reason=TEXT("Geometry/capsule revalidated; read Navigation separately.");return true;
}
FString FRogueliteSeedLayoutAssembler::DescribeLayout(const FRogueliteSeedBatch& B)
{
    FString Text=FString::Printf(TEXT("B2LayoutV1|seed=%d|configuration=%s|graph=%s"),B.Seed,*B.ConfigurationDigest,*B.GraphDigest);
    for(const auto& R:B.Rooms){Text+=FString::Printf(TEXT("|node=%d,%d,%s,%s,%s,%s,%d,%d"),R.NodeId,uint8(R.RoomType),*R.TemplateId.ToString(),
        *GetPathNameSafe(R.TemplateClass.Get()),*R.EncounterProfile.ToString(),*Pose(R.PlacedTransform),R.LogicalDegree,R.TemplateCapacity);
        if(!R.EventDefinitionId.IsNone())Text+=FString::Printf(TEXT("|eventdef=%d,%s"),R.NodeId,*R.EventDefinitionId.ToString());}
    for(const auto& E:B.Edges) Text+=FString::Printf(TEXT("|edge=%d,%d,%d,%d,%d,%s,%.3f,%d,%s"),E.EdgeId,E.ParentNode,E.ChildNode,
        uint8(E.ParentSocket),uint8(E.ChildSocket),*E.CorridorProfile.ToString(),E.CorridorLength,E.AcceptedCandidate,
        IsValid(E.Corridor)?*Pose(E.Corridor->GetActorTransform()):TEXT("missing"));
    return Text;
}

ARogueliteSeedAssembly::ARogueliteSeedAssembly()
{
    PrimaryActorTick.bCanEverTick=false; auto* Root=CreateDefaultSubobject<USceneComponent>(TEXT("SeedRoot")); SetRootComponent(Root);
    const TArray<ERogueliteSeedRoomType> Types={ERogueliteSeedRoomType::Combat,ERogueliteSeedRoomType::Challenge,ERogueliteSeedRoomType::SupplyShop,ERogueliteSeedRoomType::Final};
    FRogueliteSeedTemplateOption J; J.Id=TEXT("Junction3"); J.TemplateClass=FSoftObjectPath(TEXT("/Game/Roguelite/RoomTemplates/BP_Room_Junction3.BP_Room_Junction3_C")); J.AllowedTypes=Types;
    FRogueliteSeedTemplateOption H; H.Id=TEXT("Warehouse"); H.TemplateClass=FSoftObjectPath(TEXT("/Game/Roguelite/RoomTemplates/BP_Room_Warehouse.BP_Room_Warehouse_C")); H.AllowedTypes=Types;
    Settings.Templates={J,H}; FRogueliteSeedEncounterOption E; E.ProfileId=TEXT("Test_NoEncounter"); E.AllowedTypes=Types; Settings.Encounters.Add(E);
    Settings.Corridors.Add(FRogueliteSeedCorridorProfile());
    FRogueliteSeedCorridorProfile Fog; Fog.Id=TEXT("FogTransition_Reserved"); Fog.bEnabled=false; Fog.LengthCandidates={3000,5000}; Settings.Corridors.Add(Fog);
    FRogueliteSeedCorridorProfile Special; Special.Id=TEXT("SpecialConnector_Reserved"); Special.bEnabled=false; Settings.Corridors.Add(Special);
}
bool ARogueliteSeedAssembly::IsB2EditorWorld() const
{return GetWorld() && !GetWorld()->IsGameWorld() && GetWorld()->GetOutermost()->GetName()==TEXT("/Game/Roguelite/Maps/L_Roguelite_SeedAssembly_Dev");}
void ARogueliteSeedAssembly::PostLoad()
{
    Super::PostLoad(); Batch.bSucceeded=false; Batch.bGeometryValid=false; Batch.Navigation=ERogueliteNavigationCheck::NotChecked;
    Batch.Reason=TEXT("Reloaded: mappings retained; Revalidate Batch required.");
    for(auto& E:Batch.Edges){E.Validation.bSucceeded=false;E.Validation.bGeometryPassed=false;E.Validation.bCapsulePassed=false;E.Validation.Navigation=ERogueliteNavigationCheck::NotChecked;}
}
void ARogueliteSeedAssembly::GenerateLayout()
{
    if(!IsB2EditorWorld()){Evidence.Add(TEXT("Refused: B2 authoring map outside PIE required."));return;}
    if(!Batch.OwnedActors.IsEmpty()){Evidence.Add(TEXT("Existing batch protected. Clear Owned Batch explicitly before generating."));return;}
    ValidateJunction(); if(!bJunctionValidated){Evidence.Add(TEXT("Three-door stage failed; multi-room generation stopped."));return;}
    Modify(); Batch=FRogueliteSeedLayoutAssembler::Generate(this,Settings,GetActorTransform()); Evidence.Add(Batch.Reason);
}
void ARogueliteSeedAssembly::RevalidateBatch()
{if(IsB2EditorWorld()){Modify(); FRogueliteSeedLayoutAssembler::Revalidate(this,Batch); Evidence.Add(Batch.Reason);}}
void ARogueliteSeedAssembly::ClearBatch()
{if(IsB2EditorWorld()){Modify(); FString Reason;FRogueliteSeedLayoutAssembler::Clear(this,Batch,Reason);Evidence.Add(Reason);}}
