#include "RogueliteSeedAssemblyInternal.h"
#include "Engine/World.h"
#include "EngineUtils.h"

using namespace RogueliteSeedAssemblyInternal;
namespace
{
    FBox RoomBounds(const FRogueliteTemplateDescriptor& D)
    {
        return FBox(FVector(-D.Dimensions.X*.5-D.WallThickness,-D.Dimensions.Y*.5-D.WallThickness,-D.FloorThickness),
            FVector(D.Dimensions.X*.5+D.WallThickness,D.Dimensions.Y*.5+D.WallThickness,D.Dimensions.Z));
    }
    bool Intersects(const FBox& A,const FBox& B)
    {
        return !A.IsValid || !B.IsValid || (FMath::Min(A.Max.X,B.Max.X)-FMath::Max(A.Min.X,B.Min.X)>.1
            && FMath::Min(A.Max.Y,B.Max.Y)-FMath::Max(A.Min.Y,B.Min.Y)>.1
            && FMath::Min(A.Max.Z,B.Max.Z)-FMath::Max(A.Min.Z,B.Min.Z)>.1);
    }
    bool SpaceFree(AActor* Owner,const FBox& Bounds,const FRogueliteSeedSpatialPlan& Plan,FString& Reason)
    {
        for(const auto& R:Plan.Rooms)if(Intersects(Bounds,R.Occupancy)){Reason=TEXT("Plan occupancy intersects planned room.");return false;}
        for(const auto& E:Plan.Edges)if(Intersects(Bounds,E.Occupancy)){Reason=TEXT("Plan occupancy intersects planned corridor.");return false;}
        UWorld* W=Owner->GetWorld();
        for(TActorIterator<ARogueliteParametricRoom> I(W);I;++I)
            if(!I->IsActorBeingDestroyed())
            {
                FRogueliteTemplateDescriptor D;D.Dimensions=FVector(I->RoomLength,I->RoomWidth,I->RoomHeight);D.WallThickness=I->WallThickness;D.FloorThickness=I->FloorThickness;
                if(Intersects(Bounds,RoomBounds(D).TransformBy(I->GetActorTransform()))){Reason=TEXT("Plan intersects existing room: ")+I->GetPathName();return false;}
            }
        for(TActorIterator<ARogueliteParametricCorridor> I(W);I;++I)
            if(!I->IsActorBeingDestroyed() && Intersects(Bounds,I->GetWorldOccupancyBounds())){Reason=TEXT("Plan intersects existing corridor: ")+I->GetPathName();return false;}
        FCollisionQueryParams Q;Q.AddIgnoredActor(Owner);
        if(W->OverlapBlockingTestByChannel(Bounds.GetCenter(),FQuat::Identity,ECC_Pawn,FCollisionShape::MakeBox(Bounds.GetExtent()-FVector(.1)),Q))
        {Reason=TEXT("Plan footprint blocked by a registered world object.");return false;}
        return true;
    }
    FRogueliteDoorSocket Placed(const FRogueliteDoorSocket& D,const FTransform& T)
    {auto Out=D;Out.WorldPosition=T.TransformPosition(D.LocalPosition);Out.WorldOutwardDirection=T.TransformVectorNoScale(D.LocalOutwardDirection);return Out;}
    ERogueliteRoomSide OppositeSide(ERogueliteRoomSide S)
    {return S==ERogueliteRoomSide::North?ERogueliteRoomSide::South:S==ERogueliteRoomSide::South?ERogueliteRoomSide::North:S==ERogueliteRoomSide::East?ERogueliteRoomSide::West:ERogueliteRoomSide::East;}
    struct CorridorSpec
    {
        FName Profile;float Length=0;
        FRogueliteDoorSocket Start,End;
        FBox LocalBounds;
    };
}

FRogueliteSeedSpatialPlan FRogueliteSeedLayoutAssembler::Plan(AActor* Owner,const FRogueliteSeedLayoutSettings& S,const FTransform& Start)
{
    FRogueliteSeedSpatialPlan Plan;Plan.Metadata.Seed=S.Seed;Plan.Metadata.BatchId=FGuid::NewGuid();
    if(!IsValid(Owner) || !Owner->GetWorld() || !Rigid(Start)){Plan.Reason=TEXT("Plan requires valid owner/world and rigid quarter-turn Transform.");return Plan;}
    if(Owner->GetWorld()->IsGameWorld() && S.bCheckNavigation){Plan.Reason=TEXT("B3A supports geometry only; navigation readiness requires B3B. Set CheckNavigation=false explicitly.");return Plan;}
    Inputs Input;
    if(!BuildInputs(Owner,S,Plan.Metadata,Input,Plan.Reason)){Plan.bGraphGenerated=!Plan.Metadata.GraphDigest.IsEmpty();return Plan;}
    Plan.Graph=Input.Graph;Plan.bGraphGenerated=true;
    for(const auto& T:Input.Catalog)Plan.RetainedClasses.Add(TStrongObjectPtr<UClass>(T.Class));
    UWorld* W=Owner->GetWorld();TArray<CorridorSpec> Corridors;
    // Finite metadata probes at fixed, empty positions; never relocate registered geometry.
    // No formal dungeon actors or reservations exist during this planning query.
    for(const auto& P:Input.Profiles)for(float Length:P.LengthCandidates)
    {
        FActorSpawnParameters Args;Args.Owner=Owner;Args.bDeferConstruction=true;Args.ObjectFlags|=RF_Transient;
        Args.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        Args.CustomPreSpawnInitialization=[P,Length](AActor* Actor)
        {
            auto* C=CastChecked<ARogueliteParametricCorridor>(Actor);C->CorridorLength=Length;C->CorridorWidth=P.Width;C->CorridorHeight=P.Height;
            C->WallThickness=P.WallThickness;C->FloorThickness=P.FloorThickness;C->DoorWidth=P.DoorWidth;C->DoorHeight=P.DoorHeight;
        };
        const FTransform Position(FVector(1800000+Corridors.Num()*200000,1500000,0));
        const FBox Envelope(FVector(-Length*.5,-P.Width*.5-P.WallThickness,-P.FloorThickness),FVector(Length*.5,P.Width*.5+P.WallThickness,P.Height+P.WallThickness));
        FString Reason;FRogueliteSeedSpatialPlan Empty;
        if(!SpaceFree(Owner,Envelope.TransformBy(Position),Empty,Reason)){Plan.Reason=TEXT("Corridor metadata probe space blocked: ")+Reason;return Plan;}
        auto* C=W->SpawnActor<ARogueliteParametricCorridor>(ARogueliteParametricCorridor::StaticClass(),Position,Args);
        if(!C){Plan.Reason=TEXT("Corridor descriptor probe unavailable.");return Plan;}
        C->FinishSpawning(Position);FRogueliteCorridorSocket A,B;
        const bool Valid=C->TryGetCorridorSocket(ERogueliteCorridorEndpoint::Start,A) && C->TryGetCorridorSocket(ERogueliteCorridorEndpoint::End,B);
        const FBox Bounds=C->GetLocalOccupancyBounds();W->DestroyActor(C);
        if(!Valid || !Bounds.IsValid){Plan.Reason=TEXT("Configured corridor descriptor invalid.");return Plan;}
        CorridorSpec Spec;Spec.Profile=P.Id;Spec.Length=Length;Spec.Start=A.DoorSocket;Spec.End=B.DoorSocket;Spec.LocalBounds=Bounds;Corridors.Add(Spec);
    }
    const auto MakeRoom=[&](int32 Node,const CatalogEntry& T,const FTransform& Transform)
    {
        FRogueliteSeedPlannedRoom R;R.Record.NodeId=Node;R.Record.RoomType=Input.Types[Node];R.Record.TemplateId=T.Option.Id;R.Record.TemplateClass=T.Class;
        R.Record.EncounterProfile=Input.Encounters[Node];R.Record.EventDefinitionId=Input.EventDefinitions[Node];R.Record.PlacedTransform=Transform;R.Record.Dimensions=T.Descriptor.Dimensions;
        R.Record.WallThickness=T.Descriptor.WallThickness;R.Record.FloorThickness=T.Descriptor.FloorThickness;R.Record.MemberSignature=T.MemberSignature;
        R.Record.LogicalDegree=Input.Graph.Nodes[Node].Degree;R.Record.TemplateCapacity=T.Capacity;R.Sockets=T.Sockets;R.Occupancy=RoomBounds(T.Descriptor).TransformBy(Transform);return R;
    };
    bool Root=false;
    for(int32 Index:TemplateOrder(Input.Catalog,Input.Types[0],Input.Graph.Nodes[0].Degree,0,S))
    {
        auto R=MakeRoom(0,Input.Catalog[Index],Start);
        if(SpaceFree(Owner,R.Occupancy,Plan,Plan.Reason)){Plan.Rooms.Add(R);Root=true;break;}
    }
    if(!Root){Plan.Reason=TEXT("Start footprint blocked; no formal actors spawned. ")+Plan.Reason;return Plan;}
    for(const auto& Edge:Input.Graph.Edges)
    {
        const auto* Parent=Plan.Rooms.FindByPredicate([&](const auto& R){return R.Record.NodeId==Edge.NodeA;});
        if(!Parent){Plan.Reason=TEXT("Invalid expansion order.");return Plan;}
        auto Ports=Parent->Sockets;Ports.RemoveAll([&](const auto& D){return Parent->UsedSockets.Contains(D.Side);});
        ERogueliteRoomSide Preferred=ERogueliteRoomSide::North;
        for(const auto& E:Plan.Edges)if(E.Record.ChildNode==Edge.NodeA)Preferred=OppositeSide(E.Record.ChildSocket);
        const auto Ordered=Candidates(Input,S,Edge.NodeB,Edge.EdgeId,Ports,Preferred);bool Accepted=false;
        for(const auto& Choice:Ordered)
        {
            auto& Attempt=Plan.Metadata.Candidates.Add_GetRef(Choice);
            const auto& Template=*Input.Catalog.FindByPredicate([&](const auto& T){return T.Option.Id==Choice.TemplateId;});
            const auto& Profile=*Input.Profiles.FindByPredicate([&](const auto& P){return P.Id==Choice.ProfileId;});
            const auto& Corridor=*Corridors.FindByPredicate([&](const auto& C){return C.Profile==Choice.ProfileId && C.Length==Choice.Length;});
            const auto DA=Placed(*Parent->Sockets.FindByPredicate([&](const auto& D){return D.Side==Choice.ParentSocket;}),Parent->Record.PlacedTransform);
            const auto& DB=*Template.Sockets.FindByPredicate([&](const auto& D){return D.Side==Choice.ChildSocket;});
            FTransform CT,BT;
            if(!ARogueliteRoomAssembly::ComputeCorridorSocketTransforms(DA,DB,Corridor.Start,Corridor.End,.1f,CT,BT,Attempt.Reason))continue;
            auto R=MakeRoom(Edge.NodeB,Template,BT);const FBox CB=Corridor.LocalBounds.TransformBy(CT);
            if(Intersects(CB,R.Occupancy) || !SpaceFree(Owner,R.Occupancy,Plan,Attempt.Reason) || !SpaceFree(Owner,CB,Plan,Attempt.Reason))continue;
            // A complete spatial plan assigns endpoints; only materialization commits reservations.
            auto* MutableParent=Plan.Rooms.FindByPredicate([&](const auto& V){return V.Record.NodeId==Edge.NodeA;});
            MutableParent->UsedSockets.Add(Choice.ParentSocket);R.UsedSockets.Add(Choice.ChildSocket);Plan.Rooms.Add(R);
            auto& E=Plan.Edges.AddDefaulted_GetRef();E.Record.EdgeId=Edge.EdgeId;E.Record.ParentNode=Edge.NodeA;E.Record.ChildNode=Edge.NodeB;
            E.Record.ParentSocket=Choice.ParentSocket;E.Record.ChildSocket=Choice.ChildSocket;E.Record.CorridorProfile=Profile.Id;
            E.Record.CorridorLength=Choice.Length;E.Record.AcceptedCandidate=Choice.CandidateIndex;E.Profile=Profile;E.Transform=CT;E.Occupancy=CB;
            Attempt.bSucceeded=true;Attempt.Reason=TEXT("Spatial candidate only; collision/registered template validation remains pending.");Accepted=true;break;
        }
        if(!Accepted){Plan.Reason=FString::Printf(TEXT("Spatial edge %d exhausted %d candidates; no formal actors/reservations created."),Edge.EdgeId,Ordered.Num());return Plan;}
    }
    Plan.bValid=true;Plan.Reason=TEXT("SpatialPlanValid; formal actors and geometry validation still pending.");return Plan;
}
