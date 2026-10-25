#include "RogueliteDungeonMap.h"
#include "RogueliteWorkbenchPreview.h"
#include "RogueliteRuntimeSeedAssembly.h"
#include "RogueliteParametricCorridor.h"
#include "RogueliteNavigationReadinessComponent.h"
#include "RogueliteRuntimeLoopAssembly.h"
#include "RogueliteLoopAwareTestRig.h"
#include "RogueliteLoopCommitTestRig.h"
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
struct FMapSource
{
    const FRogueliteSeedBatch* Batch=nullptr;
    const FRogueliteSeedLayoutSettings* Settings=nullptr;
    const FRogueliteCommittedLoopBatch* Loops=nullptr;
    const ARogueliteRuntimeSeedAssembly* Runtime=nullptr;
};
FMapSource ReadSource(const AActor* A)
{
    FMapSource V;
    if(const auto* R=Cast<ARogueliteRuntimeSeedAssembly>(A)){V.Batch=&R->Batch;V.Settings=&R->Settings;V.Runtime=R;
        if(const auto* L=Cast<ARogueliteRuntimeLoopAssembly>(R))V.Loops=&L->CommittedLoops;}
    else if(const auto* L=Cast<ARogueliteLoopAwareTestRig>(A)){V.Batch=&L->Batch;V.Settings=&L->Settings;V.Loops=&L->CommittedLoops;}
    else if(const auto* Fixed=Cast<ARogueliteLoopCommitTestRig>(A)){V.Batch=&Fixed->Batch;V.Settings=&Fixed->Settings;V.Loops=&Fixed->CommittedLoops;}
    else if(const auto* E=Cast<ARogueliteSeedAssembly>(A)){V.Batch=&E->Batch;V.Settings=&E->Settings;}
    return V;
}
}
bool URogueliteDungeonMapQuery::IsSupportedSource(const AActor* Source)
{return IsValid(Source)&&ReadSource(Source).Batch!=nullptr;}
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
    const auto Fail=[&](const FString& Reason){S.bValid=false;S.bSpatial=false;S.bGeometryValidated=false;S.bDungeonReady=false;S.LoopCount=0;S.Stage=TEXT("Unavailable");S.Navigation=TEXT("Unavailable");S.Rooms.Reset();S.Connections.Reset();S.Reason=Reason;return S;};
    if(!IsValid(Source)||Source->IsActorBeingDestroyed()||!IsValid(Source->GetWorld())||Source->GetWorld()->bIsTearingDown){S.Reason=TEXT("Source/world unavailable.");return S;}
    if(const auto* Workbench=Cast<ARogueliteWorkbenchPreview>(Source)){FString Why;if(!Workbench->IsProfileCurrent(Why))return Fail(Why);}
    const auto Input=ReadSource(Source);const auto* B=Input.Batch;const auto* Settings=Input.Settings;const auto* Runtime=Input.Runtime;
    if(!B||!Settings)return Fail(TEXT("No supported live SeedAssembly/CommittedLoop source selected."));
    const UWorld* W=Source->GetWorld();S.SourceIdentity=Identity(Source);S.WorldIdentity=Identity(W);S.WorldKind=W->IsGameWorld()?TEXT("GameWorld"):TEXT("EditorWorld");
    if(Runtime){S.Stage=StaticEnum<ERogueliteDungeonBuildStage>()->GetNameStringByValue(int64(Runtime->Stage));S.bGeometryValidated=Runtime->bGeometryValidated;S.bDungeonReady=Runtime->IsDungeonReady();
        if(Runtime->NavigationReadiness && Runtime->NavigationReadiness->Report.BatchId==B->BatchId){S.Navigation=Runtime->NavigationReadiness->Report.State;S.Reason=Runtime->NavigationReadiness->Report.Reason;}
        else S.Navigation=Runtime->bEnableRuntimeNavigation?S.Stage:TEXT("NotChecked");
        if(const auto* L=Cast<ARogueliteRuntimeLoopAssembly>(Runtime)){S.Stage=L->RuntimeStatus;if(S.Navigation==TEXT("NotChecked"))S.Navigation=TEXT("NavigationPending");if(L->LastGeometry.bSucceeded&&(S.Reason.IsEmpty()||S.Navigation==TEXT("NavigationPending")))S.Reason=L->LastGeometry.Reason;}
    }
    else{S.Stage=B->bGeometryValid?TEXT("GeometryValidated"):TEXT("Empty/Failed");S.bGeometryValidated=B->bGeometryValid;S.Navigation=StaticEnum<ERogueliteNavigationCheck>()->GetNameStringByValue(int64(B->Navigation));}
    if(!W->IsGameWorld()){S.Stage=B->bGeometryValid?TEXT("GeometryValidated"):TEXT("Validation required / last failure");S.bGeometryValidated=B->bGeometryValid;S.bDungeonReady=false;S.Navigation=StaticEnum<ERogueliteNavigationCheck>()->GetNameStringByValue(int64(B->Navigation));}
    S.Seed=B->Seed;S.BatchId=B->BatchId;S.ConfigurationDigest=B->ConfigurationDigest;S.LayoutDigest=B->LayoutDigest;
    if(!B->BatchId.IsValid()||B->bCleanupComplete||B->Rooms.IsEmpty())return Fail(TEXT("No live batch. ")+B->Reason);
    if(S.Reason.IsEmpty())S.Reason=B->Reason;
    if(const auto* LoopSource=Cast<ARogueliteRuntimeLoopAssembly>(Runtime))if(LoopSource->Seed!=B->Seed)return Fail(TEXT("Runtime source Seed changed; previous batch is not current."));
    if(Settings->Seed!=B->Seed)return Fail(TEXT("Source Seed changed; batch belongs to the previous Seed."));
    const auto G=FRogueliteRoomGraphGenerator::Generate(B->Seed,Settings->GraphSettings());
    if(!G.bSucceeded||G.Graph.StableDigest!=B->GraphDigest)return Fail(TEXT("Source topology settings differ from this batch; graph correspondence unavailable."));
    AddGraph(S,G.Graph);S.TemplateCatalogVersion=Settings->TemplateCatalogVersion;S.SolverVersion=Settings->SolverVersion;
    if(B->Rooms.Num()!=S.Rooms.Num()||B->Edges.Num()!=S.Connections.Num()||!IsValid(B->Registry)||B->Registry->GetWorld()!=W||B->Registry->GetOwner()!=Source)return Fail(TEXT("Batch mapping/registry missing or belongs to another world/owner."));
    // Only the existing authority can attest an Overlay. Never read Intent/Candidate arrays.
    if(Input.Loops)
    {
        const auto T=FRogueliteLoopCommitQueries::Capture(const_cast<AActor*>(Source),*B,*Settings,*Input.Loops);
        if(!T.bCurrent)return Fail(TEXT("Committed traversal unavailable: ")+T.Reason);
        S.GraphRevision=T.GraphRevision;S.GeometryRevision=T.GeometryRevision;S.LoopCount=Input.Loops->Edges.Num();
        for(const auto& E:T.Edges)if(E.bCommittedLoop)
        {auto& C=S.Connections.AddDefaulted_GetRef();C.EdgeId=E.EdgeId;C.NodeA=E.NodeA;C.NodeB=E.NodeB;C.Role=TEXT("CommittedLoop");C.Source=TEXT("CommittedLoop");C.bCommittedLoop=true;C.ConnectionId=E.ConnectionId;}
    }
    const auto Records=B->Registry->GetConnectionRecords();int32 Active=0;for(const auto& R:Records)Active+=R.bActive;
    if(Active!=S.Connections.Num())return Fail(TEXT("Active C2 authority count differs from actual Tree + CommittedLoop; no candidate fallback."));
    for(auto& R:S.Rooms)
    {
        const auto* Actual=B->Rooms.FindByPredicate([&](const auto& V){return V.NodeId==R.NodeId;});
        if(!Actual||!IsValid(Actual->Room)||Actual->Room->IsActorBeingDestroyed()||Actual->Room->GetWorld()!=W||Actual->Room->GetOwner()!=Source)return Fail(TEXT("Live owned room missing: Node ")+FString::FromInt(R.NodeId));
        const auto* A=Actual->Room.Get();R.bRoomTypeAssigned=true;R.RoomType=Actual->RoomType;R.TemplateId=Actual->TemplateId;R.EventDefinitionId=Actual->EventDefinitionId;R.TemplateClassPath=Actual->TemplateClass?Actual->TemplateClass->GetPathName():FString();
        R.BatchId=B->BatchId;R.Neighbors.Reset();R.Degree=0;
        R.bHasWorldGeometry=true;R.WorldTransform=A->GetActorTransform();R.Dimensions=FVector(A->RoomLength,A->RoomWidth,A->RoomHeight);R.Footprint=Rectangle(R.WorldTransform,A->RoomLength,A->RoomWidth,A->WallThickness);
    }
    TSet<int32> EdgeIds;TSet<FGuid> ConnectionIds;
    for(auto& C:S.Connections)
    {
        const auto* Actual=B->Edges.FindByPredicate([&](const auto& E){return E.EdgeId==C.EdgeId;});
        const auto* Loop=Input.Loops?Input.Loops->Edges.FindByPredicate([&](const auto& E){return E.EdgeId==C.EdgeId;}):nullptr;
        if((C.bCommittedLoop&&!Loop)||(!C.bCommittedLoop&&!Actual))return Fail(TEXT("Actual edge mapping missing."));
        if(Actual&&Loop)return Fail(TEXT("Tree/Loop EdgeId collision."));
        const FGuid Id=C.bCommittedLoop?Loop->ConnectionId:Actual->ConnectionId;
        const auto* A=C.bCommittedLoop?Loop->Corridor.Get():Actual->Corridor.Get();
        const auto* Record=Records.FindByPredicate([&](const auto& V){return V.Id==Id;});
        if(!IsValid(A)||A->IsActorBeingDestroyed()||A->GetWorld()!=W||!Record||Record->Corridor!=A||!Record->bActive
            ||EdgeIds.Contains(C.EdgeId)||ConnectionIds.Contains(Id)||!Id.IsValid())return Fail(TEXT("C2/edge/corridor missing, duplicate, released or mismatched."));
        EdgeIds.Add(C.EdgeId);ConnectionIds.Add(Id);
        C.NodeA=C.bCommittedLoop?Loop->NodeA:Actual->ParentNode;C.NodeB=C.bCommittedLoop?Loop->NodeB:Actual->ChildNode;
        auto* RA=S.Rooms.FindByPredicate([&](const auto& R){return R.NodeId==C.NodeA;});auto* RB=S.Rooms.FindByPredicate([&](const auto& R){return R.NodeId==C.NodeB;});
        const auto* BA=B->Rooms.FindByPredicate([&](const auto& R){return R.NodeId==C.NodeA;});const auto* BB=B->Rooms.FindByPredicate([&](const auto& R){return R.NodeId==C.NodeB;});
        if(!RA||!RB||RA==RB||!BA||!BB||Record->RoomA!=BA->Room||Record->RoomB!=BB->Room)return Fail(TEXT("Actual C2 endpoint mapping disagrees with NodeId."));
        RA->Neighbors.AddUnique(C.NodeB);RB->Neighbors.AddUnique(C.NodeA);++RA->Degree;++RB->Degree;
        FRogueliteCorridorSocket Start,End;
        if(!A->TryGetCorridorSocket(Record->EndpointA,Start)||!A->TryGetCorridorSocket(Record->EndpointB,End))return Fail(TEXT("Corridor socket unavailable."));
        C.ConnectionId=Record->Id;C.Mode=StaticEnum<ERogueliteConnectionMode>()->GetNameStringByValue(int64(Record->Mode));C.bReservationActive=Record->bActive;C.bLastGeometryValidated=Record->bGeometryValid;C.bCommitted=true;
        C.CorridorIdentity=A->GetPathName();C.Length=A->CorridorLength;S.bGeometryValidated&=Record->bGeometryValid;
        C.bHasWorldGeometry=true;C.Start=Start.DoorSocket.WorldPosition;C.End=End.DoorSocket.WorldPosition;C.StartOutward=Start.DoorSocket.WorldOutwardDirection;C.EndOutward=End.DoorSocket.WorldOutwardDirection;
        C.Dimensions=FVector(A->CorridorLength,A->CorridorWidth,A->CorridorHeight);C.Footprint=Rectangle(A->GetActorTransform(),A->CorridorLength,A->CorridorWidth+2*A->WallThickness);
    }
    for(auto& R:S.Rooms)R.Neighbors.Sort();S.Rooms.Sort([](const auto& A,const auto& B){return A.NodeId<B.NodeId;});S.Connections.Sort([](const auto& A,const auto& B){return A.EdgeId<B.EdgeId;});
    S.bDungeonReady&=S.bGeometryValidated;
    S.bValid=true;S.bSpatial=true;S.StableDataDigest=ComputeStableDigest(S);
    FString SettingsText;FRogueliteSeedLayoutSettings::StaticStruct()->ExportText(SettingsText,Settings,nullptr,nullptr,PPF_None,nullptr);
    FString AuthorityText;for(const auto& C:S.Connections)AuthorityText+=C.ConnectionId.ToString();
    S.Revision=FRogueliteRoomGraphGenerator::StableTextDigest(S.StableDataDigest+S.WorldIdentity+S.SourceIdentity+S.BatchId.ToString()+S.Stage+S.Navigation+S.Reason+SettingsText+AuthorityText+FString::Printf(TEXT("|%d|%d|Ready%d"),S.GraphRevision,S.GeometryRevision,S.bDungeonReady));
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
    for(const auto& C:S.Connections)if((!S.bSpatial||C.bCommitted)&&(C.NodeA==Node||C.NodeB==Node)){V.Connections.Add(C);Allowed.Add(C.NodeA);Allowed.Add(C.NodeB);}
    for(auto R:S.Rooms)if(Allowed.Contains(R.NodeId))
    {R.Neighbors.Reset();for(const auto& C:V.Connections){if(C.NodeA==R.NodeId)R.Neighbors.AddUnique(C.NodeB);else if(C.NodeB==R.NodeId)R.Neighbors.AddUnique(C.NodeA);}R.Neighbors.Sort();R.Degree=R.Neighbors.Num();V.Rooms.Add(R);}
    return V;
}
FString URogueliteDungeonMapQuery::ComputeStableDigest(const FRogueliteDungeonMapSnapshot& S)
{
    FString T=FString::Printf(TEXT("E1V1|%d|%d|%d|%d|%d|%s|spatial=%d"),S.Seed,S.AlgorithmVersion,S.ConfigVersion,S.TemplateCatalogVersion,S.SolverVersion,*S.GraphDigest,S.bSpatial);
    for(const auto& R:S.Rooms){T+=FString::Printf(TEXT("|R%d,%s,%d,%d,%d,%s,%s,%s,%.3f,%.3f,%.3f"),R.NodeId,*R.Role,R.Degree,R.bRoomTypeAssigned,uint8(R.RoomType),*R.TemplateId.ToString(),*R.TemplateClassPath,*R.WorldTransform.ToString(),R.Dimensions.X,R.Dimensions.Y,R.Dimensions.Z);if(!R.EventDefinitionId.IsNone())T+=TEXT(",event=")+R.EventDefinitionId.ToString();for(int32 N:R.Neighbors)T+=FString::Printf(TEXT(",N%d"),N);for(const auto& P:R.Footprint)T+=FString::Printf(TEXT(",P%.3f:%.3f"),P.X,P.Y);}
    for(const auto& C:S.Connections){T+=FString::Printf(TEXT("|E%d,%d,%d,%s,%s,%s,%s,%s,%s,%s,%d,%d"),C.EdgeId,C.NodeA,C.NodeB,*C.Role,*C.Mode,*C.Start.ToString(),*C.End.ToString(),*C.StartOutward.ToString(),*C.EndOutward.ToString(),*C.Dimensions.ToString(),C.bReservationActive,C.bLastGeometryValidated);if(C.bCommittedLoop)T+=TEXT("|CommittedLoop");for(const auto& P:C.Footprint)T+=FString::Printf(TEXT(",P%.3f:%.3f"),P.X,P.Y);}
    return FRogueliteRoomGraphGenerator::StableTextDigest(T);
}
