#include "RogueliteDungeonMap.h"
#include "RogueliteRuntimeSeedAssembly.h"
#include "RogueliteParametricCorridor.h"
#include "RogueliteNavigationReadinessComponent.h"
#include "Engine/World.h"
#include "UObject/ObjectKey.h"
#include "Serialization/MemoryWriter.h"

namespace
{
FString NodeRole(ERogueliteGraphNodeRole R)
{
    switch(R){case ERogueliteGraphNodeRole::Start:return TEXT("Start");case ERogueliteGraphNodeRole::Final:return TEXT("Final");case ERogueliteGraphNodeRole::Branch:return TEXT("Branch");default:return TEXT("MainPath");}
}
FString Identity(const UObject* O)
{
    if(!O)return FString();TArray<uint8> Bytes;FMemoryWriter Writer(Bytes);FObjectKey Key(O);Writer<<Key;
    return O->GetPathName()+TEXT("#")+BytesToHex(Bytes.GetData(),Bytes.Num());
}
void AddGraph(FRogueliteDungeonMapSnapshot& S,const FRogueliteRoomGraph& G)
{
    S.Seed=G.Seed;S.AlgorithmVersion=G.AlgorithmVersion;S.ConfigVersion=G.ConfigVersion;S.GraphDigest=G.StableDigest;
    for(const auto& N:G.Nodes){auto& R=S.Rooms.AddDefaulted_GetRef();R.NodeId=N.NodeId;R.Role=NodeRole(N.Role);R.Degree=N.Degree;R.Neighbors=G.GetNeighborIds(N.NodeId);R.Neighbors.Sort();}
    for(const auto& E:G.Edges){auto& C=S.Connections.AddDefaulted_GetRef();C.EdgeId=E.EdgeId;C.NodeA=E.NodeA;C.NodeB=E.NodeB;C.Mode=E.ConnectionMode;C.Role=E.Role==ERogueliteGraphEdgeRole::MainPath?TEXT("MainPath"):TEXT("OptionalBranch");}
}
}
TArray<FVector2D> URogueliteDungeonMapQuery::Rectangle(const FTransform& T,float L,float W,float M)
{
    TArray<FVector2D> Result;
    if(!FMath::IsFinite(L)||!FMath::IsFinite(W)||L<=0||W<=0)return Result;
    for(const FVector& P:TArray<FVector>{FVector(-L/2-M,-W/2-M,0),FVector(L/2+M,-W/2-M,0),FVector(L/2+M,W/2+M,0),FVector(-L/2-M,W/2+M,0)})
    {const FVector V=T.TransformPosition(P);Result.Add(FVector2D(V.X,V.Y));}
    return Result;
}
FRogueliteDungeonMapSnapshot URogueliteDungeonMapQuery::PreviewGraph(int32 Seed,const FRogueliteGraphGenerationSettings& Settings)
{
    FRogueliteDungeonMapSnapshot S;S.Seed=Seed;const auto G=FRogueliteRoomGraphGenerator::Generate(Seed,Settings);
    S.bValid=G.bSucceeded;S.Reason=G.Reason;S.Stage=G.bSucceeded?TEXT("GraphGenerated"):TEXT("Failed");S.WorldKind=TEXT("Logical schematic (not world coordinates)");
    if(G.bSucceeded)AddGraph(S,G.Graph);
    S.StableDataDigest=ComputeStableDigest(S);S.Revision=S.StableDataDigest;return S;
}
FRogueliteDungeonMapSnapshot URogueliteDungeonMapQuery::Capture(const AActor* Source)
{
    check(IsInGameThread());FRogueliteDungeonMapSnapshot S;
    const FRogueliteSeedBatch* B=nullptr;const FRogueliteSeedLayoutSettings* Settings=nullptr;
    const auto* Runtime=Cast<ARogueliteRuntimeSeedAssembly>(Source);const auto* Editor=Cast<ARogueliteSeedAssembly>(Source);
    if(!IsValid(Source)||!IsValid(Source->GetWorld())||Source->GetWorld()->bIsTearingDown){S.Reason=TEXT("Source/world unavailable.");return S;}
    const UWorld* W=Source->GetWorld();S.SourceIdentity=Identity(Source);S.WorldIdentity=Identity(W);S.WorldKind=W->IsGameWorld()?TEXT("GameWorld"):TEXT("EditorWorld");
    if(Runtime){B=&Runtime->Batch;Settings=&Runtime->Settings;S.Stage=StaticEnum<ERogueliteDungeonBuildStage>()->GetNameStringByValue(int64(Runtime->Stage));S.bGeometryValidated=Runtime->bGeometryValidated;S.bDungeonReady=Runtime->IsDungeonReady();
        if(Runtime->NavigationReadiness && Runtime->NavigationReadiness->Report.BatchId==B->BatchId){S.Navigation=Runtime->NavigationReadiness->Report.State;S.Reason=Runtime->NavigationReadiness->Report.Reason;}
        else S.Navigation=Runtime->bEnableRuntimeNavigation?S.Stage:TEXT("NotChecked");
    }
    else if(Editor){B=&Editor->Batch;Settings=&Editor->Settings;S.Stage=B->bGeometryValid?TEXT("GeometryValidated"):TEXT("Empty/Failed");S.bGeometryValidated=B->bGeometryValid;S.Navigation=StaticEnum<ERogueliteNavigationCheck>()->GetNameStringByValue(int64(B->Navigation));}
    else{S.Reason=TEXT("Select a SeedAssembly or RuntimeSeedAssembly source.");return S;}
    if(!W->IsGameWorld()){S.Stage=B->bGeometryValid?TEXT("GeometryValidated"):TEXT("Validation required / last failure");S.bGeometryValidated=B->bGeometryValid;S.bDungeonReady=false;S.Navigation=StaticEnum<ERogueliteNavigationCheck>()->GetNameStringByValue(int64(B->Navigation));}
    S.Seed=B->Seed;S.BatchId=B->BatchId;S.ConfigurationDigest=B->ConfigurationDigest;S.LayoutDigest=B->LayoutDigest;
    if(!B->BatchId.IsValid()||B->bCleanupComplete||B->Rooms.IsEmpty()){S.Reason=TEXT("No live batch. ")+B->Reason;S.bDungeonReady=false;return S;}
    if(S.Reason.IsEmpty())S.Reason=B->Reason;
    if(Settings->Seed!=B->Seed){S.Reason=TEXT("Source Seed changed; batch belongs to the previous Seed.");S.bDungeonReady=false;return S;}
    const auto G=FRogueliteRoomGraphGenerator::Generate(B->Seed,Settings->GraphSettings());
    if(!G.bSucceeded||G.Graph.StableDigest!=B->GraphDigest){S.Reason=TEXT("Source topology settings differ from this batch; graph correspondence unavailable.");S.bDungeonReady=false;return S;}
    AddGraph(S,G.Graph);S.TemplateCatalogVersion=Settings->TemplateCatalogVersion;S.SolverVersion=Settings->SolverVersion;
    const auto Fail=[&](const FString& Reason){S.bValid=false;S.bSpatial=false;S.bDungeonReady=false;S.Rooms.Reset();S.Connections.Reset();S.Reason=Reason;return S;};
    if(B->Rooms.Num()!=S.Rooms.Num()||B->Edges.Num()!=S.Connections.Num()||!IsValid(B->Registry)||B->Registry->GetWorld()!=W)return Fail(TEXT("Batch mapping/registry missing or belongs to another world."));
    for(auto& R:S.Rooms)
    {
        const auto* Actual=B->Rooms.FindByPredicate([&](const auto& V){return V.NodeId==R.NodeId;});
        if(!Actual||!IsValid(Actual->Room)||Actual->Room->GetWorld()!=W)return Fail(TEXT("Live room missing: Node ")+FString::FromInt(R.NodeId));
        const auto* A=Actual->Room.Get();R.bRoomTypeAssigned=true;R.RoomType=Actual->RoomType;R.TemplateId=Actual->TemplateId;R.EventDefinitionId=Actual->EventDefinitionId;R.TemplateClassPath=Actual->TemplateClass?Actual->TemplateClass->GetPathName():FString();
        R.bHasWorldGeometry=true;R.WorldTransform=A->GetActorTransform();R.Dimensions=FVector(A->RoomLength,A->RoomWidth,A->RoomHeight);R.Footprint=Rectangle(R.WorldTransform,A->RoomLength,A->RoomWidth,A->WallThickness);
    }
    for(auto& C:S.Connections)
    {
        const auto* Actual=B->Edges.FindByPredicate([&](const auto& E){return E.EdgeId==C.EdgeId;});
        if(!Actual||!IsValid(Actual->Corridor)||Actual->Corridor->GetWorld()!=W)return Fail(TEXT("Live corridor missing: Edge ")+FString::FromInt(C.EdgeId));
        const auto* Record=B->Registry->Connections.FindByPredicate([&](const auto& V){return V.Id==Actual->ConnectionId;});
        if(!Record||Record->Corridor!=Actual->Corridor)return Fail(TEXT("C2 connection record missing or mismatched."));
        const auto* A=Actual->Corridor.Get();FRogueliteCorridorSocket Start,End;
        if(!A->TryGetCorridorSocket(Record->EndpointA,Start)||!A->TryGetCorridorSocket(Record->EndpointB,End))return Fail(TEXT("Corridor socket unavailable."));
        C.NodeA=Actual->ParentNode;C.NodeB=Actual->ChildNode;C.ConnectionId=Record->Id;C.Mode=StaticEnum<ERogueliteConnectionMode>()->GetNameStringByValue(int64(Record->Mode));C.bReservationActive=Record->bActive;C.bLastGeometryValidated=Record->bGeometryValid;
        C.bHasWorldGeometry=true;C.Start=Start.DoorSocket.WorldPosition;C.End=End.DoorSocket.WorldPosition;C.StartOutward=Start.DoorSocket.WorldOutwardDirection;C.EndOutward=End.DoorSocket.WorldOutwardDirection;
        C.Dimensions=FVector(A->CorridorLength,A->CorridorWidth,A->CorridorHeight);C.Footprint=Rectangle(A->GetActorTransform(),A->CorridorLength,A->CorridorWidth+2*A->WallThickness);
    }
    S.bValid=true;S.bSpatial=true;S.StableDataDigest=ComputeStableDigest(S);
    FString SettingsText;FRogueliteSeedLayoutSettings::StaticStruct()->ExportText(SettingsText,Settings,nullptr,nullptr,PPF_None,nullptr);
    S.Revision=FRogueliteRoomGraphGenerator::StableTextDigest(S.StableDataDigest+S.WorldIdentity+S.SourceIdentity+S.BatchId.ToString()+S.Stage+S.Navigation+S.Reason+SettingsText);
    return S;
}
bool URogueliteDungeonMapQuery::IsSnapshotCurrent(const AActor* Source,const FRogueliteDungeonMapSnapshot& Snapshot)
{
    if(!Snapshot.bValid||!Snapshot.bSpatial)return false;const auto Current=Capture(Source);return Current.bValid&&Current.Revision==Snapshot.Revision;
}
FRogueliteDungeonLocalMapView URogueliteDungeonMapQuery::GetLocalMapView(const AActor* Source,int32 Node){return FilterOneHop(Capture(Source),Node);}
FRogueliteDungeonLocalMapView URogueliteDungeonMapQuery::FilterOneHop(const FRogueliteDungeonMapSnapshot& S,int32 Node)
{
    FRogueliteDungeonLocalMapView V;V.Seed=S.Seed;V.BatchId=S.BatchId;V.SourceRevision=S.Revision;
    if(!S.bValid||Node==INDEX_NONE||!S.Rooms.ContainsByPredicate([&](const auto& R){return R.NodeId==Node;})){V.Reason=TEXT("Current room unlocated; explicit valid NodeId required.");return V;}
    V.bLocated=true;V.CurrentNodeId=Node;TSet<int32> Allowed;Allowed.Add(Node);
    for(const auto& C:S.Connections)if(C.NodeA==Node||C.NodeB==Node){V.Connections.Add(C);Allowed.Add(C.NodeA);Allowed.Add(C.NodeB);}
    for(auto R:S.Rooms)if(Allowed.Contains(R.NodeId)){R.Neighbors.RemoveAll([&](int32 N){return !Allowed.Contains(N);});R.Degree=R.Neighbors.Num();V.Rooms.Add(R);}
    return V;
}
FString URogueliteDungeonMapQuery::ComputeStableDigest(const FRogueliteDungeonMapSnapshot& S)
{
    FString T=FString::Printf(TEXT("E1V1|%d|%d|%d|%d|%d|%s|spatial=%d"),S.Seed,S.AlgorithmVersion,S.ConfigVersion,S.TemplateCatalogVersion,S.SolverVersion,*S.GraphDigest,S.bSpatial);
    for(const auto& R:S.Rooms){T+=FString::Printf(TEXT("|R%d,%s,%d,%d,%d,%s,%s,%s,%.3f,%.3f,%.3f"),R.NodeId,*R.Role,R.Degree,R.bRoomTypeAssigned,uint8(R.RoomType),*R.TemplateId.ToString(),*R.TemplateClassPath,*R.WorldTransform.ToString(),R.Dimensions.X,R.Dimensions.Y,R.Dimensions.Z);if(!R.EventDefinitionId.IsNone())T+=TEXT(",event=")+R.EventDefinitionId.ToString();for(int32 N:R.Neighbors)T+=FString::Printf(TEXT(",N%d"),N);for(const auto& P:R.Footprint)T+=FString::Printf(TEXT(",P%.3f:%.3f"),P.X,P.Y);}
    for(const auto& C:S.Connections){T+=FString::Printf(TEXT("|E%d,%d,%d,%s,%s,%s,%s,%s,%s,%s,%d,%d"),C.EdgeId,C.NodeA,C.NodeB,*C.Role,*C.Mode,*C.Start.ToString(),*C.End.ToString(),*C.StartOutward.ToString(),*C.EndOutward.ToString(),*C.Dimensions.ToString(),C.bReservationActive,C.bLastGeometryValidated);for(const auto& P:C.Footprint)T+=FString::Printf(TEXT(",P%.3f:%.3f"),P.X,P.Y);}
    return FRogueliteRoomGraphGenerator::StableTextDigest(T);
}
