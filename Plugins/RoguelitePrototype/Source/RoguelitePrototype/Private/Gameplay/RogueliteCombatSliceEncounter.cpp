#include "Gameplay/RogueliteCombatSliceEncounter.h"
#include "Gameplay/RogueliteEncounterSynchronousRead.h"
#include "ProfilingDebugging/CountersTrace.h"

/* PERF1A_BEGIN */
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "ProfilingDebugging/MiscTrace.h"
/* PERF1A_END */
#include "Gameplay/RogueliteCombatSlicePlayer.h"
#include "Gameplay/RogueliteCombatSliceActors.h"
#include "Tests/RogueliteCombatSliceRig.h"
#include "RogueliteRuntimeLoopAssembly.h"
#include "RogueliteRuntimeNavigationSystem.h"
#include "RogueliteNavigationReadinessComponent.h"
#include "RogueliteRoomTemplate.h"
#include "RogueliteParametricCorridor.h"
#include "Components/CapsuleComponent.h"
#include "Components/BoxComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "UObject/TopLevelAssetPath.h"
#include "AIController.h"
#include "NavigationSystem.h"
#include "NavigationPath.h"
#include "Navigation/PathFollowingComponent.h"

namespace
{
TRACE_DECLARE_INT_COUNTER(Perf21ReadProduced, TEXT("PERF21A.ReadProduced"));
TRACE_DECLARE_INT_COUNTER(Perf21ReadConsumed, TEXT("PERF21A.ReadConsumed"));
TRACE_DECLARE_INT_COUNTER(Perf21ReadFallback, TEXT("PERF21A.ReadFallback"));
TRACE_DECLARE_INT_COUNTER(Perf21ReadReentry, TEXT("PERF21A.ReadReentry"));
}

namespace RogueliteSliceEncounterImpl
{
bool NativeWorldQueriesAreReadOnly(const UWorld* World,FString* Rejection=nullptr)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(PERF1C_Encounter_NativeWorldAdmission);
    // Immutable type metadata only: no cached World, frame, Snapshot or success.
    static const TSet<FTopLevelAssetPath> Actors={
        FTopLevelAssetPath(TEXT("/Script/Engine.WorldSettings")),
        FTopLevelAssetPath(TEXT("/Script/Engine.Brush")),
        FTopLevelAssetPath(TEXT("/Script/RoguelitePrototype.RogueliteRuntimeNavigationBounds")),
        FTopLevelAssetPath(TEXT("/Script/Engine.DirectionalLight")),
        FTopLevelAssetPath(TEXT("/Script/Engine.SkyLight")),
        FTopLevelAssetPath(TEXT("/Script/Engine.PlayerStart")),
        FTopLevelAssetPath(TEXT("/Script/RoguelitePrototype.RogueliteCombatSliceRig")),
        FTopLevelAssetPath(TEXT("/Script/Engine.Actor")),
        FTopLevelAssetPath(TEXT("/Script/RoguelitePrototype.RogueliteRuntimeRecastNavMesh")),
        FTopLevelAssetPath(TEXT("/Script/Water.BuoyancyManager")),
        FTopLevelAssetPath(TEXT("/Script/Engine.DefaultPhysicsVolume")),
        FTopLevelAssetPath(TEXT("/Script/GameplayDebugger.GameplayDebuggerPlayerManager")),
        FTopLevelAssetPath(TEXT("/Script/RoguelitePrototype.RogueliteSliceGameMode")),
        FTopLevelAssetPath(TEXT("/Script/Engine.GameSession")),
        FTopLevelAssetPath(TEXT("/Script/Engine.ParticleEventManager")),
        FTopLevelAssetPath(TEXT("/Script/Engine.GameNetworkManager")),
        FTopLevelAssetPath(TEXT("/Script/Engine.GameStateBase")),
        FTopLevelAssetPath(TEXT("/Script/AIModule.AIController")),
        FTopLevelAssetPath(TEXT("/Script/NavigationSystem.AbstractNavData")),
        FTopLevelAssetPath(TEXT("/Script/RoguelitePrototype.RogueliteSlicePlayerController")),
        FTopLevelAssetPath(TEXT("/Script/Engine.PlayerState")),
        FTopLevelAssetPath(TEXT("/Script/Engine.PlayerCameraManager")),
        FTopLevelAssetPath(TEXT("/Script/RoguelitePrototype.RogueliteSliceHUD")),
        FTopLevelAssetPath(TEXT("/Script/RoguelitePrototype.RogueliteSlicePlayer")),
        FTopLevelAssetPath(TEXT("/Script/GameplayDebugger.GameplayDebuggerCategoryReplicator")),
        FTopLevelAssetPath(TEXT("/Script/RoguelitePrototype.RogueliteRoomAssembly")),
        FTopLevelAssetPath(TEXT("/Script/RoguelitePrototype.RogueliteParametricCorridor")),
        FTopLevelAssetPath(TEXT("/Script/RoguelitePrototype.RogueliteSliceEnemy")),
        FTopLevelAssetPath(TEXT("/Script/RoguelitePrototype.RogueliteSliceGate")),
        FTopLevelAssetPath(TEXT("/Script/RoguelitePrototype.RogueliteRoomTemplate")),
        FTopLevelAssetPath(TEXT("/Script/RoguelitePrototype.RogueliteProductionCandidateRoom")),
        FTopLevelAssetPath(TEXT("/Script/RoguelitePrototype.RogueliteNavigationTestCharacter")),
        FTopLevelAssetPath(TEXT("/Script/RoguelitePrototype.RogueliteNavigationTestController")),
        FTopLevelAssetPath(TEXT("/Script/RoguelitePrototype.RogueliteSliceRecoveryProbe"))
    };
    static const TSet<FTopLevelAssetPath> Primitives={
        FTopLevelAssetPath(TEXT("/Script/Engine.BrushComponent")),
        FTopLevelAssetPath(TEXT("/Script/Engine.BoxComponent")),
        FTopLevelAssetPath(TEXT("/Script/Engine.ArrowComponent")),
        FTopLevelAssetPath(TEXT("/Script/Engine.BillboardComponent")),
        FTopLevelAssetPath(TEXT("/Script/Engine.CapsuleComponent")),
        FTopLevelAssetPath(TEXT("/Script/NavigationSystem.NavMeshRenderingComponent")),
        FTopLevelAssetPath(TEXT("/Script/Engine.SkeletalMeshComponent")),
        FTopLevelAssetPath(TEXT("/Script/Engine.CameraProxyMeshComponent")),
        FTopLevelAssetPath(TEXT("/Script/Engine.DrawFrustumComponent")),
        FTopLevelAssetPath(TEXT("/Script/GameplayDebugger.GameplayDebuggerRenderingComponent")),
        FTopLevelAssetPath(TEXT("/Script/Engine.StaticMeshComponent"))
    };
    const auto NativeClass=[](const UClass* Class)
    {while(Class&&!Class->HasAnyClassFlags(CLASS_Native))Class=Class->GetSuperClass();return Class;};
    for(TActorIterator<AActor> It(const_cast<UWorld*>(World));It;++It)
    {
        const auto* ActorClass=NativeClass(It->GetClass());
        if(!ActorClass||!Actors.Contains(ActorClass->GetClassPathName()))
        {if(Rejection)*Rejection=TEXT("Unreviewed Actor native class: ")+GetPathNameSafe(ActorClass);return false;}
        TInlineComponentArray<UPrimitiveComponent*> Components;It->GetComponents(Components);
        for(const auto* Component:Components)
        {
            const auto* Class=NativeClass(Component->GetClass());
            // Do not invoke an unreviewed virtual collision getter to decide
            // whether the component can be ignored, even when nonblocking.
            if(!Class||!Primitives.Contains(Class->GetClassPathName()))
            {if(Rejection)*Rejection=TEXT("Unreviewed primitive native class: ")+GetPathNameSafe(Class);return false;}
        }
    }
    return true;
}
// Reuse is confined to the audited native sandbox, in one game-thread stack.
// Unknown Source/World/registry/navigation implementations keep the old calls.
// No token or successful result survives this function or a mutating operation.
bool CanReuseTraversal(const ARogueliteRuntimeLoopAssembly* Source,const ARogueliteSlicePlayer* Player)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(PERF1C_Encounter_ReuseAdmission);
    if(!IsInGameThread()||!IsValid(Source)||Source->GetClass()!=ARogueliteCombatSliceRig::StaticClass()
        ||!IsValid(Player)||Player->GetClass()!=ARogueliteSlicePlayer::StaticClass())return false;
    const auto* World=Source->GetWorld();const auto* Ready=Source->NavigationReadiness.Get();
    if(!IsValid(World)||World->GetClass()!=UWorld::StaticClass()||World->bIsTearingDown||!World->IsGameWorld()
        ||Player->GetWorld()!=World||!IsValid(Ready)||Ready->GetClass()!=URogueliteNavigationReadinessComponent::StaticClass()
        ||Ready->GetOwner()!=Source||!Ready->Report.bReady||Ready->Report.State!=TEXT("NavigationReady")
        ||Ready->Report.BatchId!=Source->Batch.BatchId||!IsValid(Source->Batch.Registry)
        ||Source->Batch.Registry->GetClass()!=ARogueliteRoomAssembly::StaticClass())return false;
    const auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
    if(!IsValid(Nav)||Nav->GetClass()!=URogueliteRuntimeNavigationSystem::StaticClass()||Nav->NavDataSet.IsEmpty())return false;
    bool bReportDataRegistered=false;
    for(const ANavigationData* Data:Nav->NavDataSet)
    {
        if(!IsValid(Data)||Data->GetClass()!=ARogueliteRuntimeRecastNavMesh::StaticClass()||Data->GetWorld()!=World)return false;
        bReportDataRegistered|=Data->GetPathName()==Ready->Report.NavData;
    }
    return bReportDataRegistered&&NativeWorldQueriesAreReadOnly(World);
}
const FRogueliteSeedRoomRecord* Room(const ARogueliteRuntimeLoopAssembly* Source,int32 Node)
{ return Source?Source->Batch.Rooms.FindByPredicate([&](const auto& Row){return Row.NodeId==Node;}):nullptr; }
FString StateText(ERogueliteSliceEncounterState State)
{
    switch(State){case ERogueliteSliceEncounterState::AwaitingEntry:return TEXT("AwaitingEntry");
    case ERogueliteSliceEncounterState::Preparation:return TEXT("Preparation");case ERogueliteSliceEncounterState::CombatActive:return TEXT("CombatActive");
    case ERogueliteSliceEncounterState::Cleared:return TEXT("Cleared");case ERogueliteSliceEncounterState::Cancelled:return TEXT("Cancelled");default:return TEXT("Uninitialized");}
}
FString PassageText(ERogueliteGameplayPassage State)
{
    switch(State){case ERogueliteGameplayPassage::GameplayBlocked:return TEXT("GameplayBlocked");
    case ERogueliteGameplayPassage::Recovering:return TEXT("Recovering");case ERogueliteGameplayPassage::Traversable:return TEXT("Traversable");default:return TEXT("Unavailable");}
}
}

URogueliteSliceEncounter::URogueliteSliceEncounter()
{
    PrimaryComponentTick.bCanEverTick=true;PrimaryComponentTick.TickInterval=.02f;
}

bool URogueliteSliceEncounter::CanStartSynchronousRead() const
{
    TRACE_CPUPROFILER_EVENT_SCOPE(PERF21A_Encounter_Admission);
    if(State!=ERogueliteSliceEncounterState::AwaitingEntry||GetClass()!=StaticClass()
        ||Enemies.Num()!=2||MemberIds.Num()!=2||SpawnedEnemyCount!=2||!KilledMembers.IsEmpty())return false;
    auto* Source=BoundSource.Get();auto* FPS=BoundPlayer.Get();
    if(!FRogueliteEncounterSynchronousRead::Admit(this,Source,FPS))return false;
    for(int32 Index=0;Index<Enemies.Num();++Index)
    {
        const auto* Enemy=Enemies[Index].Get();
        if(!IsValid(Enemy)||Enemy->GetClass()!=ARogueliteSliceEnemy::StaticClass()
            ||!IsValid(Enemy->Health)||Enemy->Health->GetClass()!=URogueliteSliceHealth::StaticClass()
            ||Enemy->GetOwner()!=Source||Enemy->GetWorld()!=BoundWorld.Get()
            ||Enemy->Health->GetOwner()!=Enemy||Enemy->Health->GetWorld()!=BoundWorld.Get()
            ||Enemy->GetMemberId()!=MemberIds[Index]||Enemy->GetNodeId()!=TargetNodeId
            ||Enemy->GetBoundBatch()!=BoundBatch||Enemy->GetDungeonSource()!=Source||!Enemy->IsDormant())return false;
    }
    return true;
}

bool URogueliteSliceEncounter::CurrentIdentity(FString& Reason, FRogueliteEncounterSynchronousRead* Read) const
{
/* PERF1A_BEGIN */
    TRACE_CPUPROFILER_EVENT_SCOPE(PERF1A_Encounter_CurrentIdentity);
/* PERF1A_END */

    if(ActiveSynchronousRead&&ActiveSynchronousRead!=Read)ActiveSynchronousRead->Invalidate();
    auto* Source=BoundSource.Get();auto* FPS=BoundPlayer.Get();auto* World=BoundWorld.Get();
    if(!IsInGameThread()||!IsValid(Source)||!IsValid(FPS)||!World||World->bIsTearingDown||!World->IsGameWorld()
        ||GetOwner()!=Source||Source->GetWorld()!=World||FPS->GetWorld()!=World||Source->Batch.BatchId!=BoundBatch
        ||Source->Seed!=SourceSeed||FPS->GetDungeonSource()!=Source||FPS->GetBoundBatch()!=BoundBatch)
    {Reason=TEXT("Encounter identity expired: Source/World/Batch/Seed or real player binding changed.");return false;}
    // HasLiveBinding's cheap identity/lease predicates are checked here; its
    // final bCurrent is supplied by the same strong Snapshot checked below.
    // The native interval contains only const getters and no delegate/world pump.
    const bool bReuse=RogueliteSliceEncounterImpl::CanReuseTraversal(Source,FPS);
    const bool bPlayerBound=bReuse
        ? BoundBatch.IsValid()&&FRogueliteDynamicGameplayBridge::IsComponentIsolated(Source,BoundBatch,FPS->GetCapsuleComponent())
        : FPS->HasLiveBinding();
    if(!bPlayerBound){Reason=TEXT("Encounter real player binding changed.");return false;}
    auto Snapshot=Source->GetTraversalSnapshot();
    if(!Snapshot.bCurrent||Snapshot.BatchId!=BoundBatch||Snapshot.Seed!=GraphSeed
        ||Snapshot.GraphRevision!=GraphRevision||Snapshot.GeometryRevision!=GeometryRevision||Snapshot.StableDigest!=TraversalDigest)
    {Reason=TEXT("Encounter traversal is no longer the captured current graph/geometry revision.");return false;}
    if(Read&&Read==ActiveSynchronousRead&&CanStartSynchronousRead())
    {
        TRACE_CPUPROFILER_EVENT_SCOPE(PERF21A_Encounter_Produce);
        if(Read->Produce(Source,FPS,MoveTemp(Snapshot)))
        {TRACE_COUNTER_INCREMENT(Perf21ReadProduced);}
        else
        {TRACE_COUNTER_INCREMENT(Perf21ReadFallback); }
    }
    return true;
}

bool URogueliteSliceEncounter::Initialize(ARogueliteRuntimeLoopAssembly* Source,ARogueliteSlicePlayer* FPS,FString& Reason)
{return InitializeRoom(Source,FPS,INDEX_NONE,Reason);}

bool URogueliteSliceEncounter::InitializeRoom(ARogueliteRuntimeLoopAssembly* Source,ARogueliteSlicePlayer* FPS,int32 RequestedNode,FString& Reason)
{
    if(ActiveSynchronousRead)ActiveSynchronousRead->Close();
    if(!IsInGameThread()||!IsValid(Source)||!IsValid(FPS)||GetOwner()!=Source||Source->GetWorld()!=GetWorld()
        ||!GetWorld()||!GetWorld()->IsGameWorld()||!Source->IsDungeonReady()||!FPS->HasLiveBinding()
        ||FPS->GetDungeonSource()!=Source||FPS->GetBoundBatch()!=Source->Batch.BatchId||!FPS->HealthComponent->IsAlive())
    {Reason=TEXT("Encounter requires its owning Source, strict Ready and the actually bound local FPS player.");return false;}
    if(BoundBatch.IsValid()&&BoundBatch==Source->Batch.BatchId&&BoundSource.Get()==Source)
    {
        if(State==ERogueliteSliceEncounterState::Cancelled){Reason=LastReason;return false;}
        if(RequestedNode!=INDEX_NONE&&TargetNodeId!=RequestedNode){Reason=TEXT("Cannot rebind an existing encounter to a different node in this batch.");return false;}
        Reason=TEXT("Current encounter already exists; no new roster, gates or activation.");return true;
    }
    CleanOwned();bCoordinated=RequestedNode!=INDEX_NONE;
    const auto Snapshot=Source->GetTraversalSnapshot();
    if(!Snapshot.bCurrent||!Source->Settings.RoomTemplateRoles.bEnabled)
    {Reason=TEXT("Encounter requires current traversal and explicit NormalRoom template roles.");return false;}
    BoundSource=Source;BoundPlayer=FPS;BoundWorld=GetWorld();BoundBatch=Source->Batch.BatchId;SourceSeed=Source->Seed;
    GraphSeed=Snapshot.Seed;GraphRevision=Snapshot.GraphRevision;GeometryRevision=Snapshot.GeometryRevision;TraversalDigest=Snapshot.StableDigest;
    State=ERogueliteSliceEncounterState::Uninitialized;TargetNodeId=StartNodeId=CurrentRoomNodeId=LastFullRoomNodeId=INDEX_NONE;
    EntryConnectionId.Invalidate();ExitConnectionId.Invalidate();PendingEntryConnection.Invalidate();
    IncidentConnectionIds.Empty();MemberIds.Empty();Enemies.Empty();Gates.Empty();KilledMembers.Empty();SeenShots.Empty();
    ActivationCount=ClearCount=SpawnedEnemyCount=0;bHavePreviousPosition=bCrossedEntryPlane=false;
    bRecoveryRequested=bParkRequested=bParkFinished=bRecoveryTimedOut=false;RecoveryAttempts=0;RecoveryProbe.Reset();LastReason.Empty();LastRecoveryDiagnostic.Empty();
    NextPreparationAttemptAt=0;
    if(!SelectTarget(Reason,RequestedNode)){Shutdown(Reason);return false;}
    CurrentRoomNodeId=FindPlayerRoom(FPS->GetActorLocation(),false);
    LastFullRoomNodeId=FindPlayerRoom(FPS->GetActorLocation(),true);
    if(LastFullRoomNodeId!=StartNodeId){Reason=TEXT("Encounter must initialize with the player fully inside the actual StartSafehouse.");Shutdown(Reason);return false;}
    if(!SpawnMembersAndGates(Reason)){Shutdown(Reason);return false;}
    FPS->OnShotAccepted.AddUObject(this,&URogueliteSliceEncounter::AcceptedShot);
    FPS->HealthComponent->OnKilled.AddUObject(this,&URogueliteSliceEncounter::PlayerKilled);
    PreviousPlayerPosition=FPS->GetActorLocation();PreviousPlayerSpeed2D=FPS->GetVelocity().Size2D();bHavePreviousPosition=true;
    State=ERogueliteSliceEncounterState::AwaitingEntry;
/* PERF1A_BEGIN */
    TRACE_BOOKMARK(TEXT("PERF1A Encounter AwaitingEntry Slot=%d"), GetFName()==FName(TEXT("SecondCombatEncounter")) ? 1 : 0);
/* PERF1A_END */
Reason=TEXT("Two complete Dormant members registered; awaiting a physical committed-connection entry.");
    LastReason=Reason;if(!bCoordinated)FPS->GameplaySummary=Describe();return true;
}

bool URogueliteSliceEncounter::SelectTarget(FString& Reason,int32 RequestedNode)
{
    auto* Source=BoundSource.Get();const auto Snapshot=Source->GetTraversalSnapshot();
    const auto* Start=Snapshot.Nodes.FindByPredicate([](const auto& Node){return Node.Role==TEXT("Start");});
    if(!Start){Reason=TEXT("Traversal has no authoritative Start node.");return false;}StartNodeId=Start->NodeId;
    TArray<int32> Candidates;
    for(const auto& Node:Snapshot.Nodes)
    {
        const auto* Record=RogueliteSliceEncounterImpl::Room(Source,Node.NodeId);
        if(Node.Role!=TEXT("Start")&&Node.Role!=TEXT("Final")&&Node.RoomType==ERogueliteSeedRoomType::Combat
            &&Record&&IsValid(Record->Room)&&Source->Settings.RoomTemplateRoles.NormalTemplateIds.Contains(Record->TemplateId))Candidates.Add(Node.NodeId);
    }
    auto Adjacent=[&](int32 Node){return Snapshot.Edges.ContainsByPredicate([&](const auto& Edge){return (Edge.NodeA==StartNodeId&&Edge.NodeB==Node)||(Edge.NodeB==StartNodeId&&Edge.NodeA==Node);});};
    auto Degree=[&](int32 Node){const auto* Row=Snapshot.Nodes.FindByPredicate([&](const auto& Item){return Item.NodeId==Node;});return Row?Row->Degree:0;};
    Candidates.Sort([&](int32 A,int32 B){if(Adjacent(A)!=Adjacent(B))return Adjacent(A);if(Degree(A)!=Degree(B))return Degree(A)>Degree(B);return A<B;});
    if(Candidates.IsEmpty()){Reason=TEXT("No actual Normal Combat node satisfies role/template qualification.");return false;}
    if(RequestedNode!=INDEX_NONE&&!Candidates.Contains(RequestedNode)){Reason=FString::Printf(TEXT("Requested node %d is not a qualified actual Normal Combat room."),RequestedNode);return false;}
    TargetNodeId=RequestedNode==INDEX_NONE?Candidates[0]:RequestedNode;
    for(const auto& Edge:Snapshot.Edges)if(Edge.NodeA==TargetNodeId||Edge.NodeB==TargetNodeId)
    {
        FRogueliteRoomConnection Connection;if(!Edge.ConnectionId.IsValid()||!ReadConnection(Edge.ConnectionId,Connection))
        {Reason=TEXT("Target incident edge lacks a current authoritative StraightCorridor connection.");return false;}
        IncidentConnectionIds.AddUnique(Edge.ConnectionId);
    }
    if(IncidentConnectionIds.IsEmpty()){Reason=TEXT("Target combat node has no committed entry connection.");return false;}
    return true;
}

bool URogueliteSliceEncounter::ReadConnection(FGuid Id,FRogueliteRoomConnection& Out) const
{
/* PERF1A_BEGIN */
    TRACE_CPUPROFILER_EVENT_SCOPE(PERF1A_Encounter_ReadConnection);
/* PERF1A_END */

    const auto* Source=BoundSource.Get();if(!Source||!IsValid(Source->Batch.Registry))return false;
    const auto Snapshot=Source->GetTraversalSnapshot();
    if(!Snapshot.bCurrent||!Snapshot.Edges.ContainsByPredicate([&](const auto& Edge){return Edge.ConnectionId==Id;}))return false;
    const auto Records=Source->Batch.Registry->GetConnectionRecords();
    const auto* Found=Records.FindByPredicate([&](const auto& Row){return Row.Id==Id&&Row.bActive&&Row.bGeometryValid
        &&Row.ValidationStatus==ERogueliteConnectionValidation::Passed&&Row.Mode==ERogueliteConnectionMode::StraightCorridor;});
    if(!Found||!IsValid(Found->RoomA)||!IsValid(Found->RoomB)||!IsValid(Found->Corridor))return false;
    Out=*Found;return true;
}

bool URogueliteSliceEncounter::ContainsRoom(const FRogueliteSeedRoomRecord& Record,const FVector& Center,bool Full) const
{
    if(!IsValid(Record.Room))return false;
    const FVector Local=Record.PlacedTransform.InverseTransformPosition(Center);const float Radius=Full?34.f:0.f;
    return FMath::Abs(Local.X)+Radius<=Record.Dimensions.X*.5f&&FMath::Abs(Local.Y)+Radius<=Record.Dimensions.Y*.5f
        &&Local.Z>=(Full?85.f:0.f)&&Local.Z+(Full?88.f:0.f)<=Record.Dimensions.Z+3;
}

int32 URogueliteSliceEncounter::FindPlayerRoom(const FVector& Center,bool Full) const
{
    const auto* Source=BoundSource.Get();if(!Source)return INDEX_NONE;int32 Found=INDEX_NONE;
    for(const auto& Record:Source->Batch.Rooms)if(ContainsRoom(Record,Center,Full))
    {if(Found!=INDEX_NONE)return INDEX_NONE;Found=Record.NodeId;}
    return Found;
}

bool URogueliteSliceEncounter::GroundPosition(const FRogueliteSeedRoomRecord& Record,const FVector& Requested,AActor* Ignore,
    bool ProtectDoors,FVector& OutCenter,FString& Reason) const
{
    auto* World=BoundWorld.Get();auto* Nav=World?FNavigationSystem::GetCurrent<UNavigationSystemV1>(World):nullptr;
    if(!World||!Nav||!IsValid(Record.Room)){Reason=TEXT("No current room or navigation system for placement.");return false;}
    FNavLocation Projected;
    if(!Nav->ProjectPointToNavigation(Requested,Projected,FVector(80,80,300)))
    {Reason=TEXT("No nearby navigation XY candidate.");return false;}
    const FVector LocalNav=Record.PlacedTransform.InverseTransformPosition(Projected.Location);
    const FVector FloorXY=Record.PlacedTransform.TransformPosition(FVector(LocalNav.X,LocalNav.Y,0));
    FCollisionQueryParams Query(SCENE_QUERY_STAT(G1B_EncounterGround),false);if(Ignore)Query.AddIgnoredActor(Ignore);
    FHitResult Ground;
    if(!World->LineTraceSingleByChannel(Ground,FloorXY+FVector(0,0,350),FloorXY-FVector(0,0,80),ECC_Visibility,Query)
        ||Ground.GetActor()!=Record.Room.Get()||Ground.ImpactNormal.Z<.9f
        ||FMath::Abs(Record.PlacedTransform.InverseTransformPosition(Ground.ImpactPoint).Z)>1.f)
    {Reason=TEXT("Placement rejected: actual finished room floor was not the first ground hit.");return false;}
    OutCenter=Ground.ImpactPoint+FVector(0,0,90);
    if(!ContainsRoom(Record,OutCenter,true)) {Reason=TEXT("Standing capsule is outside room interior.");return false;}
    if(ProtectDoors)for(const auto& Socket:Record.Room->GetDoorSockets())if(Socket.bValid&&FVector::Dist2D(OutCenter,Socket.WorldPosition)<400)
    {Reason=TEXT("Enemy placement would invade a 400cm door exclusion zone.");return false;}
    for(const auto& POIRef:Record.Room->ManualPOIActors)if(auto* POI=POIRef.Get();IsValid(POI))
    {
        FVector Center,Extent;POI->GetActorBounds(false,Center,Extent);
        if(FBox(Center-Extent,Center+Extent).ExpandBy(FVector(34,34,88)).IsInsideOrOn(OutCenter))
        {Reason=TEXT("Placement overlaps a protected manual POI bound.");return false;}
    }
    if(World->OverlapBlockingTestByChannel(OutCenter,FQuat::Identity,ECC_Pawn,FCollisionShape::MakeCapsule(34,88),Query))
    {Reason=TEXT("Standing capsule overlaps real geometry or a participant.");return false;}
    return true;
}

bool URogueliteSliceEncounter::SpawnMembersAndGates(FString& Reason)
{
    auto* Source=BoundSource.Get();const auto* Record=RogueliteSliceEncounterImpl::Room(Source,TargetNodeId);
    if(!Record){Reason=TEXT("Target room record disappeared.");return false;}
    static const FVector2D Fractions[]={FVector2D(-.45f,-.45f),FVector2D(.45f,.45f),FVector2D(-.45f,.45f),FVector2D(.45f,-.45f),
        FVector2D(.25f,0),FVector2D(-.25f,0),FVector2D(0,.3f),FVector2D(0,-.3f),FVector2D(.6f,0),FVector2D(-.6f,0)};
    for(int32 Member=0;Member<2;++Member)
    {
        if(bFailSecondSpawn&&Member==1){Reason=TEXT("Validation injection: second required enemy was not spawned; no zero-count victory.");return false;}
        bool Placed=false;
        for(const FVector2D Fraction:Fractions)
        {
            const FVector Requested=Record->PlacedTransform.TransformPosition(FVector(Fraction.X*Record->Dimensions.X*.5f,Fraction.Y*Record->Dimensions.Y*.5f,10));
            FVector Center;if(!GroundPosition(*Record,Requested,nullptr,true,Center,Reason))continue;
            FActorSpawnParameters Params;Params.Owner=Source;Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
            auto* Enemy=GetWorld()->SpawnActor<ARogueliteSliceEnemy>(ARogueliteSliceEnemy::StaticClass(),Center,Record->PlacedTransform.Rotator(),Params);
            if(!Enemy){Reason=TEXT("Required enemy Actor spawn failed.");return false;}AddOwned(Enemy);
            const FGuid MemberId=FGuid::NewGuid();
            if(!Enemy->Configure(Source,BoundBatch,TargetNodeId,MemberId,FTransform(Record->PlacedTransform.GetRotation(),Center,FVector::OneVector),Reason,bCoordinated?this:nullptr))return false;
            Enemy->Health->OnKilled.AddUObject(this,&URogueliteSliceEncounter::MemberKilled);
            MemberIds.Add(MemberId);Enemies.Add(Enemy);++SpawnedEnemyCount;Placed=true;break;
        }
        if(!Placed){Reason=TEXT("Finite enemy placement candidates exhausted: ")+Reason;return false;}
    }
    if(!CompleteRoster(Reason))return false;
    for(const auto Id:IncidentConnectionIds)
    {
        FActorSpawnParameters Params;Params.Owner=Source;Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        auto* Gate=GetWorld()->SpawnActor<ARogueliteSliceGate>(ARogueliteSliceGate::StaticClass(),FVector::ZeroVector,FRotator::ZeroRotator,Params);
        if(!Gate){Reason=TEXT("Required incident gate Actor spawn failed.");return false;}AddOwned(Gate);
        if(!Gate->Configure(Source,BoundBatch,TargetNodeId,Id,Reason))return false;
        Gates.Add(Id,Gate);
    }
    return true;
}

bool URogueliteSliceEncounter::CompleteRoster(FString& Reason) const
{
/* PERF1A_BEGIN */
    TRACE_CPUPROFILER_EVENT_SCOPE(PERF1A_Encounter_CompleteRoster);
/* PERF1A_END */

    if(Enemies.Num()!=2||MemberIds.Num()!=2||SpawnedEnemyCount!=2)
    {Reason=TEXT("Encounter requires exactly two fully registered members.");return false;}
    for(int32 Index=0;Index<Enemies.Num();++Index)
    {
        const auto* Enemy=Enemies[Index].Get();
        if(!IsValid(Enemy)||!Enemy->Health||Enemy->GetMemberId()!=MemberIds[Index]||Enemy->GetNodeId()!=TargetNodeId
            ||Enemy->GetBoundBatch()!=BoundBatch||Enemy->GetDungeonSource()!=BoundSource.Get())
        {Reason=TEXT("Encounter member identity/registration is incomplete.");return false;}
        if(!KilledMembers.Contains(MemberIds[Index])&&(!Enemy->Health->IsAlive()||(!Enemy->IsDormant()&&!Enemy->IsActive())))
        {Reason=TEXT("Required member has an unexpected terminal or inactive state.");return false;}
    }
    return true;
}

void URogueliteSliceEncounter::AddOwned(AActor* Actor)
{
    if(!IsValid(Actor))return;OwnedActors.AddUnique(Actor);Actor->OnDestroyed.AddDynamic(this,&URogueliteSliceEncounter::OwnedActorDestroyed);
}

void URogueliteSliceEncounter::TrackEntry(float DeltaTime, FRogueliteEncounterSynchronousRead* Read)
{
/* PERF1A_BEGIN */
    TRACE_CPUPROFILER_EVENT_SCOPE(PERF1A_Encounter_TrackEntry);
/* PERF1A_END */

    TOptional<FRogueliteLoopTraversalSnapshot> EntryTraversal;
    const auto EndRead=[&](){if(Read)Read->Close();EntryTraversal.Reset();};
    auto* FPS=BoundPlayer.Get();auto* Source=BoundSource.Get();if(!FPS||!Source)return;
    const FVector Now=FPS->GetActorLocation();CurrentRoomNodeId=FindPlayerRoom(Now,false);
    if(!bHavePreviousPosition){PreviousPlayerPosition=Now;bHavePreviousPosition=true;return;}
    const FVector Delta=Now-PreviousPlayerPosition;
    const float Limit=FMath::Max(200.f,FPS->GetCharacterMovement()->MaxWalkSpeed*DeltaTime*2.f+40.f);
    if(Delta.Size2D()>Limit)
    {EndRead();Shutdown(TEXT("Room entry rejected: discontinuous player teleport is not a physical connection crossing."));return;}
    const bool bReuse=RogueliteSliceEncounterImpl::CanReuseTraversal(Source,FPS);
    // The native non-entry branch already has Tick's fresh CurrentIdentity.
    // Preserve the original unconditional query for unknown implementations,
    // whose virtual readiness getters have not been proved side-effect free.
    if(!bReuse||State==ERogueliteSliceEncounterState::AwaitingEntry)
    {
        FRogueliteLoopTraversalSnapshot Value;
        TRACE_CPUPROFILER_EVENT_SCOPE(PERF21A_Encounter_Consume);
        if(Read&&Read==ActiveSynchronousRead&&CanStartSynchronousRead()
            &&Read->Consume(Source,FPS,Value))
        {EntryTraversal.Emplace(MoveTemp(Value));TRACE_COUNTER_INCREMENT(Perf21ReadConsumed);}
        else
        {
            if(Read){Read->Invalidate();TRACE_COUNTER_INCREMENT(Perf21ReadFallback);}
            EntryTraversal.Emplace(Source->GetTraversalSnapshot());
        }
    }
    if(State==ERogueliteSliceEncounterState::AwaitingEntry)
    {
        const auto& Snapshot=EntryTraversal.GetValue();
        const auto Records=bReuse?Source->Batch.Registry->GetConnectionRecords():TArray<FRogueliteRoomConnection>();
        const auto ReadEntryConnection=[&](FGuid Id,FRogueliteRoomConnection& Out)
        {
            if(!bReuse)return ReadConnection(Id,Out);
            // Keep the original read-work scope visible when its implementation
            // uses this call-local view instead of constructing another view.
            TRACE_CPUPROFILER_EVENT_SCOPE(PERF1A_Encounter_ReadConnection);
            TRACE_CPUPROFILER_EVENT_SCOPE(PERF1C_Encounter_ReadEntryConnection);
            if(!Snapshot.bCurrent||!Snapshot.Edges.ContainsByPredicate([&](const auto& Edge){return Edge.ConnectionId==Id;}))return false;
            const auto* Found=Records.FindByPredicate([&](const auto& Row){return Row.Id==Id&&Row.bActive&&Row.bGeometryValid
                &&Row.ValidationStatus==ERogueliteConnectionValidation::Passed&&Row.Mode==ERogueliteConnectionMode::StraightCorridor;});
            if(!Found||!IsValid(Found->RoomA)||!IsValid(Found->RoomB)||!IsValid(Found->Corridor))return false;
            Out=*Found;return true;
        };
        const int32 FullRoom=FindPlayerRoom(Now,true);
        if(FullRoom!=INDEX_NONE&&FullRoom!=TargetNodeId)
        {LastFullRoomNodeId=FullRoom;PendingEntryConnection.Invalidate();bCrossedEntryPlane=false;}
        for(const auto Id:IncidentConnectionIds)
        {
            const auto* Edge=Snapshot.Edges.FindByPredicate([&](const auto& Row){return Row.ConnectionId==Id;});
            if(!Edge)continue;const int32 Other=Edge->NodeA==TargetNodeId?Edge->NodeB:Edge->NodeA;
            FRogueliteRoomConnection Connection;if(!ReadEntryConnection(Id,Connection))continue;
            const FVector Local=Connection.Corridor->GetActorTransform().InverseTransformPosition(Now);
            const bool InCorridor=FMath::Abs(Local.X)<=Connection.Corridor->CorridorLength*.5f+1
                &&FMath::Abs(Local.Y)+34<=Connection.Corridor->CorridorWidth*.5f&&Local.Z>=85&&Local.Z+88<=Connection.Corridor->CorridorHeight+3;
            if(InCorridor&&LastFullRoomNodeId==Other)
            {if(PendingEntryConnection!=Id)bCrossedEntryPlane=false;PendingEntryConnection=Id;}
        }
        if(PendingEntryConnection.IsValid())
        {
            FRogueliteRoomConnection Connection;
            const auto* Target=RogueliteSliceEncounterImpl::Room(Source,TargetNodeId);
            if(!Target||!ReadEntryConnection(PendingEntryConnection,Connection)){EndRead();Shutdown(TEXT("Entry connection became invalid."));return;}
            FRogueliteDoorSocket Socket;
            if(!Target->Room->TryGetDoorSocket(Connection.RoomA==Target->Room.Get()?Connection.SocketA:Connection.SocketB,Socket))
            {EndRead();Shutdown(TEXT("Target entry socket is no longer legal."));return;}
            const FVector Normal=Socket.WorldOutwardDirection,Tangent=FVector::CrossProduct(FVector::UpVector,Normal);
            const float Before=FVector::DotProduct(PreviousPlayerPosition-Socket.WorldPosition,Normal);
            const float After=FVector::DotProduct(Now-Socket.WorldPosition,Normal);
            const bool Fits=FMath::Abs(FVector::DotProduct(Now-Socket.WorldPosition,Tangent))+34<=Socket.DoorWidth*.5f-2;
            if(Before>=0&&After<0&&FVector::DotProduct(Delta,Normal)<0&&Fits)
            {
                const float Speed=FMath::Max(PreviousPlayerSpeed2D,FPS->GetVelocity().Size2D());
                const float PhysicalLimit=Speed*DeltaTime*2.f+40;
                if(Delta.Size2D()>PhysicalLimit||Speed<1)
                {EndRead();Shutdown(TEXT("Door-plane crossing lacks actual character movement; teleport entry rejected."));return;}
                bCrossedEntryPlane=true;
            }
            if(After>=0)bCrossedEntryPlane=false;
            const float SafeDepth=Socket.WallThickness+34.f+25.f+2.f;
            if(bCrossedEntryPlane&&Fits&&-After>=SafeDepth&&ContainsRoom(*Target,Now,true))
            {
                if(GetWorld()->GetTimeSeconds()>=NextPreparationAttemptAt)
                {
                    FString Why;
                    EndRead();
                    if(EnterPreparation(PendingEntryConnection,Why))LastFullRoomNodeId=TargetNodeId;
                    else
                    {
                        // A refused/rolled-back lock attempt is not an established Preparation.
                        // Keep the valid batch and retry only while the actual full-body entry holds.
                        if(State==ERogueliteSliceEncounterState::Cancelled)return;
                        LastReason=Why;NextPreparationAttemptAt=GetWorld()->GetTimeSeconds()+.5;
                    }
                }
            }
        }
        if(State==ERogueliteSliceEncounterState::AwaitingEntry&&CurrentRoomNodeId==TargetNodeId&&!PendingEntryConnection.IsValid())
        {EndRead();Shutdown(TEXT("Target entry source is unknown: no visited committed corridor from a known adjacent room."));return;}
    }
    PreviousPlayerPosition=Now;PreviousPlayerSpeed2D=FPS->GetVelocity().Size2D();
}

bool URogueliteSliceEncounter::ValidateClosedGate(FGuid Id,FString& Reason) const
{
/* PERF1A_BEGIN */
    TRACE_CPUPROFILER_EVENT_SCOPE(PERF1A_Encounter_ValidateClosedGate);
/* PERF1A_END */

    auto* Gate=GetGate(Id);auto* Source=BoundSource.Get();
    if(!Gate||Gate->GetOwner()!=Source||Gate->GetWorld()!=GetWorld()||Gate->GetConnectionId()!=Id||!Gate->IsClosed()
        ||!FRogueliteDynamicGameplayBridge::IsComponentIsolated(Source,BoundBatch,Gate->GetBlocker()))
    {Reason=TEXT("Incident gate is not currently bound and physically closed.");return false;}
    const auto Query=FRogueliteDynamicGameplayBridge::QueryConnection(Source,Id,BoundPlayer.Get());
    if(!Query.bStructuralCurrent||!Query.bBaseNavigationReady||Query.bForwardCapsule||Query.bReverseCapsule
        ||Query.State!=ERogueliteGameplayPassage::GameplayBlocked)
    {Reason=TEXT("Closed gate did not retain base Ready and block both real capsule sweep directions.");return false;}
    auto* Blocker=Gate->GetBlocker();if(!Blocker){Reason=TEXT("Closed gate has no actual blocker component.");return false;}
    const FVector Center=Blocker->GetComponentLocation(),Normal=Blocker->GetForwardVector();
    FCollisionQueryParams Params(SCENE_QUERY_STAT(G1B_PreparationGateRay),false,BoundPlayer.Get());FHitResult Forward,Reverse;
    if(!GetWorld()->LineTraceSingleByChannel(Forward,Center-Normal*100,Center+Normal*100,ECC_Visibility,Params)
        ||Forward.GetComponent()!=Blocker
        ||!GetWorld()->LineTraceSingleByChannel(Reverse,Center+Normal*100,Center-Normal*100,ECC_Visibility,Params)
        ||Reverse.GetComponent()!=Blocker)
    {Reason=TEXT("Closed gate did not itself block both actual doorway rays.");return false;}
    return true;
}

bool URogueliteSliceEncounter::ValidateIncidentConnections(FString& Reason) const
{
/* PERF1A_BEGIN */
    TRACE_CPUPROFILER_EVENT_SCOPE(PERF1A_Encounter_ValidateIncidentConnections);
/* PERF1A_END */

    auto* Source=BoundSource.Get();const auto* Target=RogueliteSliceEncounterImpl::Room(Source,TargetNodeId);
    if(!Source||!Target||!IsValid(Target->Room)){Reason=TEXT("Target room is no longer current.");return false;}
    const auto Snapshot=Source->GetTraversalSnapshot();TSet<FGuid> Actual;
    if(!Snapshot.bCurrent){Reason=TEXT("No current authoritative traversal for the gate set.");return false;}
    // Traversal contains BaseTree plus actual CommittedLoops, never intents or unused sockets.
    for(const auto& Edge:Snapshot.Edges)if(Edge.NodeA==TargetNodeId||Edge.NodeB==TargetNodeId)
    {
        if(!Edge.ConnectionId.IsValid()||Actual.Contains(Edge.ConnectionId))
        {Reason=TEXT("Actual incident traversal has an invalid or repeated ConnectionId.");return false;}
        Actual.Add(Edge.ConnectionId);
    }
    if(Actual.IsEmpty()||Actual.Num()!=IncidentConnectionIds.Num()||Gates.Num()!=Actual.Num())
    {Reason=FString::Printf(TEXT("Node %d incident gate count mismatch: actual=%d recorded=%d gates=%d."),TargetNodeId,Actual.Num(),IncidentConnectionIds.Num(),Gates.Num());return false;}
    TSet<FGuid> Recorded;
    for(const auto Id:IncidentConnectionIds)
    {
        FRogueliteRoomConnection Record;auto* Gate=GetGate(Id);
        if(!Actual.Contains(Id)||Recorded.Contains(Id)||!ReadConnection(Id,Record)
            ||(Record.RoomA!=Target->Room&&Record.RoomB!=Target->Room)
            ||!Gate||Gate->GetConnectionId()!=Id||Gate->GetOwner()!=Source||Gate->GetWorld()!=GetWorld()
            ||!FRogueliteDynamicGameplayBridge::IsComponentIsolated(Source,BoundBatch,Gate->GetBlocker()))
        {Reason=FString::Printf(TEXT("Node %d connection %s has missing/stale incident identity or gate binding."),TargetNodeId,*Id.ToString());return false;}
        Recorded.Add(Id);
    }
    return true;
}

bool URogueliteSliceEncounter::EnterPreparation(FGuid Connection,FString& Reason)
{
    if(ActiveSynchronousRead)ActiveSynchronousRead->Close();
/* PERF1A_BEGIN */
    TRACE_CPUPROFILER_EVENT_SCOPE(PERF1A_Encounter_EnterPreparation);
/* PERF1A_END */

    auto* Source=BoundSource.Get();
    if(State!=ERogueliteSliceEncounterState::AwaitingEntry||!Source||!Source->IsDungeonReady())
    {Reason=TEXT("Preparation requires an awaiting encounter and unchanged strict base navigation Ready.");return false;}
    if(!CurrentIdentity(Reason)||!CompleteRoster(Reason)||!ValidateIncidentConnections(Reason))return false;
    if(bCoordinated)
    {
        TInlineComponentArray<URogueliteSliceEncounter*> Siblings;Source->GetComponents(Siblings);
        for(const auto* Other:Siblings)if(Other&&Other!=this&&Other->IsCoordinated())
        {
            if(Other->GetState()==ERogueliteSliceEncounterState::Preparation||Other->GetState()==ERogueliteSliceEncounterState::CombatActive)
            {Reason=FString::Printf(TEXT("Node %d must wait: node %d already owns current Preparation/Combat."),TargetNodeId,Other->GetTargetNodeId());return false;}
            if(Other->IsRecoveryBusy())
            {Reason=FString::Printf(TEXT("Node %d must wait for node %d's existing recovery agent to finish safe parking."),TargetNodeId,Other->GetTargetNodeId());return false;}
        }
    }
    if(!IncidentConnectionIds.Contains(Connection)){Reason=TEXT("Entered ConnectionId is not an actual target incident passage.");return false;}
    // All occupancy prechecks precede any collision change: do not trap a doorway occupant.
    for(const auto Id:IncidentConnectionIds)if(!GetGate(Id)->CanClose(Reason))
    {Reason=FString::Printf(TEXT("Node %d connection %s refused Preparation preflight: %s"),TargetNodeId,*Id.ToString(),*Reason);return false;}
    TArray<FGuid> Changed;
    const auto Reject=[&](FGuid Failed,const FString& Detail)
    {
        bool Restored=true;FString RollbackReason;
        for(int32 I=Changed.Num()-1;I>=0;--I)
        {
            auto* Gate=GetGate(Changed[I]);FString Why;
            if(!Gate||!Gate->SetClosed(false,Why)){Restored=false;RollbackReason+=Changed[I].ToString()+TEXT(": ")+Why+TEXT("; ");}
        }
        Reason=FString::Printf(TEXT("Node %d connection %s refused Preparation: %s; rollback reopened %d changed gates, restored=%d. %s"),
            TargetNodeId,*Failed.ToString(),*Detail,Changed.Num(),Restored,*RollbackReason);
        if(!Restored)Shutdown(Reason); // Only owned gameplay cleanup; never claim a partial lock succeeded.
        return false;
    };
    int32 Verified=0;
    for(const auto Id:IncidentConnectionIds)
    {
        auto* Gate=GetGate(Id);FString Why;
        if(!Gate->IsClosed())
        {
            if(!Gate->SetClosed(true,Why))return Reject(Id,Why);
            Changed.Add(Id);
        }
        if(!ValidateClosedGate(Id,Why))return Reject(Id,Why);
        ++Verified;
    }
    if(Verified!=IncidentConnectionIds.Num())return Reject(Connection,TEXT("Not every actual incident passage was verified closed."));
    EntryConnectionId=Connection;ExitConnectionId=IncidentConnectionIds[0];
    for(const auto Id:IncidentConnectionIds)if(Id!=EntryConnectionId){ExitConnectionId=Id;break;}
    State=ERogueliteSliceEncounterState::Preparation;
/* PERF1A_BEGIN */
    TRACE_BOOKMARK(TEXT("PERF1A Encounter Preparation Slot=%d"), GetFName()==FName(TEXT("SecondCombatEncounter")) ? 1 : 0);
/* PERF1A_END */

    LastReason=FString::Printf(TEXT("Player fully entered; all %d actual passages sealed and verified. Unlimited Preparation; both enemies remain Dormant."),Verified);return true;
}

void URogueliteSliceEncounter::AcceptedShot(const FRogueliteSliceShot& Shot)
{
    if(ActiveSynchronousRead)ActiveSynchronousRead->Close();
/* PERF1A_BEGIN */
    TRACE_CPUPROFILER_EVENT_SCOPE(PERF1A_Encounter_AcceptedShot);
/* PERF1A_END */

    FString Why;auto* FPS=BoundPlayer.Get();FRogueliteSliceShot Recorded;
    if(!CurrentIdentity(Why)||!FPS||Shot.Shooter.Get()!=FPS||Shot.BatchId!=BoundBatch||SeenShots.Contains(Shot.Id)
        ||!FPS->FindAcceptedShot(Shot.Id,Recorded)||Recorded.Id!=Shot.Id||Recorded.BatchId!=BoundBatch
        ||Recorded.Shooter.Get()!=FPS||!Recorded.PawnLocation.Equals(Shot.PawnLocation,1))return;
    SeenShots.Add(Shot.Id);
    const auto* Target=RogueliteSliceEncounterImpl::Room(BoundSource.Get(),TargetNodeId);
    if(State!=ERogueliteSliceEncounterState::Preparation||!Target||!ContainsRoom(*Target,Recorded.PawnLocation,true)
        ||!ContainsRoom(*Target,FPS->GetActorLocation(),true))return;
    if(!CompleteRoster(Why)||!ValidateIncidentConnections(Why)){Shutdown(Why);return;}
    for(const auto Id:IncidentConnectionIds)
    {
        // Activation validates the established lock; it never repairs/recloses a bypassed gate
        // or causes a redundant G1A gate revision.
        if(!ValidateClosedGate(Id,Why))
        {Shutdown(FString::Printf(TEXT("Node %d connection %s lost its Preparation lock before activation: %s"),TargetNodeId,*Id.ToString(),*Why));return;}
    }
    State=ERogueliteSliceEncounterState::CombatActive;
/* PERF1A_BEGIN */
    TRACE_BOOKMARK(TEXT("PERF1A Encounter CombatActive Slot=%d"), GetFName()==FName(TEXT("SecondCombatEncounter")) ? 1 : 0);
/* PERF1A_END */

    for(const auto& Enemy:Enemies)if(!Enemy.IsValid()||!Enemy->Activate(FPS))
    {Shutdown(TEXT("A required member could not activate after safe gate closure."));return;}
    ++ActivationCount;LastReason=TEXT("Current room accepted the player's first real shot; all incident gates closed; only this roster active.");
}

bool URogueliteSliceEncounter::AuthorizesMemberDamage(const ARogueliteSliceEnemy* Enemy,const FRogueliteSliceShot& Shot) const
{
/* PERF1A_BEGIN */
    TRACE_CPUPROFILER_EVENT_SCOPE(PERF1A_Encounter_AuthorizesMemberDamage);
/* PERF1A_END */

    FString Why;const auto* FPS=BoundPlayer.Get();const auto* Record=RogueliteSliceEncounterImpl::Room(BoundSource.Get(),TargetNodeId);
    if(!bCoordinated||State!=ERogueliteSliceEncounterState::CombatActive||!CurrentIdentity(Why)||!Enemy||!FPS||!Record
        ||Enemy->GetDungeonSource()!=BoundSource.Get()||Enemy->GetWorld()!=BoundWorld.Get()||Enemy->GetBoundBatch()!=BoundBatch||Enemy->GetNodeId()!=TargetNodeId
        ||Shot.Shooter.Get()!=FPS||Shot.BatchId!=BoundBatch||!SeenShots.Contains(Shot.Id)
        ||!ContainsRoom(*Record,Shot.PawnLocation,true)||!ContainsRoom(*Record,FPS->GetActorLocation(),true))return false;
    for(int32 I=0;I<Enemies.Num();++I)if(Enemies[I].Get()==Enemy)
        return MemberIds.IsValidIndex(I)&&MemberIds[I]==Enemy->GetMemberId()&&!KilledMembers.Contains(MemberIds[I]);
    return false;
}

void URogueliteSliceEncounter::MemberKilled(URogueliteSliceHealth* Health)
{
    if(ActiveSynchronousRead)ActiveSynchronousRead->Close();
    if(bCleaning||!Health)return;
    int32 Index=INDEX_NONE;for(int32 It=0;It<Enemies.Num();++It)if(Enemies[It].IsValid()&&Enemies[It]->Health==Health){Index=It;break;}
    if(Index==INDEX_NONE)return;
    if(bCoordinated&&KilledMembers.Contains(MemberIds[Index]))return; // A recorded room-local death replay is not another room event.
    FString Why;
    if(State!=ERogueliteSliceEncounterState::CombatActive||!CurrentIdentity(Why)||Health->GetResolution()!=ERogueliteSliceMemberResolution::Killed)
    {Shutdown(TEXT("Unexpected member terminal event outside current active combat; not a victory."));return;}
    if(KilledMembers.Contains(MemberIds[Index]))return;
    KilledMembers.Add(MemberIds[Index]);
    if(SpawnedEnemyCount==2&&MemberIds.Num()==2&&KilledMembers.Num()==2)CommitCleared();
}

void URogueliteSliceEncounter::PlayerKilled(URogueliteSliceHealth* Health)
{
    if(ActiveSynchronousRead)ActiveSynchronousRead->Close();
    if(bCleaning)return;
    auto* FPS=BoundPlayer.Get();if(FPS&&Health==FPS->HealthComponent)
        Shutdown(TEXT("Player died; this encounter was cancelled without a clear victory."));
}

void URogueliteSliceEncounter::OwnedActorDestroyed(AActor* Actor)
{
    if(!bCleaning&&State!=ERogueliteSliceEncounterState::Cancelled&&State!=ERogueliteSliceEncounterState::Uninitialized
        &&OwnedActors.ContainsByPredicate([&](const auto& Item){return Item.Get()==Actor;}))
        Shutdown(TEXT("An encounter-owned Actor was unexpectedly destroyed; never counted as a kill."));
}

void URogueliteSliceEncounter::CommitCleared()
{
    if(ActiveSynchronousRead)ActiveSynchronousRead->Close();
/* PERF1A_BEGIN */
    TRACE_CPUPROFILER_EVENT_SCOPE(PERF1A_Encounter_CommitCleared);
/* PERF1A_END */

    if(State!=ERogueliteSliceEncounterState::CombatActive||ClearCount!=0)return;
    FString Why;
    for(const auto Id:IncidentConnectionIds)if(auto* Gate=GetGate(Id);!Gate||!Gate->SetClosed(false,Why))
    {Shutdown(TEXT("Clear could not safely reopen an incident gate: ")+Why);return;}
    State=ERogueliteSliceEncounterState::Cleared;
/* PERF1A_BEGIN */
    TRACE_BOOKMARK(TEXT("PERF1A Encounter Cleared Slot=%d"), GetFName()==FName(TEXT("SecondCombatEncounter")) ? 1 : 0);
/* PERF1A_END */
++ClearCount;
    ExitConnectionId=IncidentConnectionIds[0];for(const auto Id:IncidentConnectionIds)if(Id!=EntryConnectionId){ExitConnectionId=Id;break;}
    LastReason=TEXT("Exactly two legal member deaths; Cleared once. All gates open; selected exit still requires actual recovery.");
    RecoveryStartedAt=GetWorld()->GetTimeSeconds();NextRecoveryAttemptAt=RecoveryStartedAt;bRecoveryRequested=bParkRequested=bParkFinished=bRecoveryTimedOut=false;
}

FString URogueliteSliceEncounter::RecoveryDiagnostics(const FString& Detail,bool IncludeEvidence,FString* OutBlocker) const
{
/* PERF1A_BEGIN */
    TRACE_CPUPROFILER_EVENT_SCOPE(PERF1A_Encounter_RecoveryDiagnostics);
/* PERF1A_END */

    auto* Source=BoundSource.Get();auto* FPS=BoundPlayer.Get();auto* Probe=RecoveryProbe.Get();auto* World=BoundWorld.Get();
    const auto Query=FRogueliteDynamicGameplayBridge::QueryConnection(Source,ExitConnectionId,Probe);
    FVector Start=FVector::ZeroVector,Goal=FVector::ZeroVector;bool EndpointsValid=false;
    if(Probe){Start=Probe->GetStartFeet();Goal=Probe->GetGoalFeet();EndpointsValid=true;}
    else
    {
        FRogueliteRoomConnection Connection;FRogueliteDoorSocket A,B;
        if(ReadConnection(ExitConnectionId,Connection)&&Connection.RoomA->TryGetDoorSocket(Connection.SocketA,A)
            &&Connection.RoomB->TryGetDoorSocket(Connection.SocketB,B))
        {Start=A.WorldPosition-A.WorldOutwardDirection*300;Goal=B.WorldPosition-B.WorldOutwardDirection*300;EndpointsValid=true;}
    }
    FHitResult ForwardHit,ReverseHit;bool ForwardBlocked=false,ReverseBlocked=false;
    if(World&&EndpointsValid)
    {
        FCollisionQueryParams Collision(SCENE_QUERY_STAT(G1B_RecoveryDiagnostics),false);if(Probe)Collision.AddIgnoredActor(Probe);
        // Match the bridge's standing-capsule query. The real FPS player is deliberately not ignored.
        ForwardBlocked=World->SweepSingleByChannel(ForwardHit,Start+FVector(0,0,90),Goal+FVector(0,0,90),FQuat::Identity,ECC_Pawn,
            FCollisionShape::MakeCapsule(34,88),Collision);
        ReverseBlocked=World->SweepSingleByChannel(ReverseHit,Goal+FVector(0,0,90),Start+FVector(0,0,90),FQuat::Identity,ECC_Pawn,
            FCollisionShape::MakeCapsule(34,88),Collision);
    }
    const auto* Controller=Probe?Probe->GetRecoveryController():nullptr;
    if(OutBlocker)*OutBlocker=GetNameSafe(ForwardHit.GetActor()?ForwardHit.GetActor():ReverseHit.GetActor());
    FString Result=FString::Printf(TEXT("%s\nRecovery connection=%s Requested=%d ParkRequested=%d ParkFinished=%d Endpoints=%d Start=%s Goal=%s\nFPS=%s at %s; Probe=%s at %s; ControllerMove=%d\nG1A State=%d Structural=%d BaseReady=%d Forward=%d Reverse=%d FullPath=%d MoveVerified=%d Reason=%s\nActualSweep ForwardBlocked=%d Actor=%s Component=%s; ReverseBlocked=%d Actor=%s Component=%s"),
        *Detail,*ExitConnectionId.ToString(),bRecoveryRequested,bParkRequested,bParkFinished,EndpointsValid,*Start.ToString(),*Goal.ToString(),
        *GetNameSafe(FPS),*(FPS?FPS->GetActorLocation():FVector::ZeroVector).ToString(),*GetNameSafe(Probe),*(Probe?Probe->GetActorLocation():FVector::ZeroVector).ToString(),
        Controller?static_cast<int32>(Controller->GetMoveStatus()):-1,static_cast<int32>(Query.State),Query.bStructuralCurrent,Query.bBaseNavigationReady,
        Query.bForwardCapsule,Query.bReverseCapsule,Query.bFullPath,Query.bMoveVerified,*Query.Reason,
        ForwardBlocked,*GetNameSafe(ForwardHit.GetActor()),*GetNameSafe(ForwardHit.GetComponent()),
        ReverseBlocked,*GetNameSafe(ReverseHit.GetActor()),*GetNameSafe(ReverseHit.GetComponent()));
    if(IncludeEvidence)Result+=TEXT("\nBridgeRecoveryEvidence:\n")+FRogueliteDynamicGameplayBridge::RecoveryEvidence(Source);
    return Result;
}

bool URogueliteSliceEncounter::StartParking(FString& Reason)
{
    auto* Probe=RecoveryProbe.Get();auto* Source=BoundSource.Get();FRogueliteRoomConnection Connection;
    if(!Probe||!Source||!ReadConnection(ExitConnectionId,Connection)||!Probe->GetRecoveryController())return false;
    const auto* GoalRoom=Source->Batch.Rooms.FindByPredicate([&](const auto& Record){return Record.Room.Get()==Connection.RoomB.Get();});
    if(!GoalRoom){Reason=TEXT("Recovery goal room disappeared.");return false;}
    const FVector Goal=Probe->GetGoalFeet(),Start=Probe->GetStartFeet();
    FVector Tangent=FVector::CrossProduct(FVector::UpVector,(Goal-Start).GetSafeNormal());Tangent.Z=0;Tangent.Normalize();
    static const float Offsets[]={220,-220,320,-320,440,-440};
    for(float Offset:Offsets)
    {
        FVector Center;if(!GroundPosition(*GoalRoom,Goal+Tangent*Offset+FVector(0,0,10),Probe,false,Center,Reason))continue;
        const FVector Feet=Center-FVector(0,0,88);
        if(FMath::PointDistToSegment(Feet,Start,Goal)<160)continue;
        const auto* Path=UNavigationSystemV1::FindPathToLocationSynchronously(GetWorld(),Probe->GetActorLocation()-FVector(0,0,78),Feet+FVector(0,0,10),Probe);
        if(!IsValid(Path)||!Path->IsValid()||Path->IsPartial()||Path->PathPoints.Num()<2)continue;
        FAIMoveRequest Request(Feet);Request.SetAcceptanceRadius(25);Request.SetReachTestIncludesAgentRadius(false);
        Request.SetReachTestIncludesGoalRadius(false);Request.SetAllowPartialPath(false);Request.SetUsePathfinding(true);Request.SetProjectGoalLocation(false);
        if(Probe->GetRecoveryController()->MoveTo(Request).Code==EPathFollowingRequestResult::RequestSuccessful)
        {ParkFeet=Feet;bParkRequested=true;Reason=TEXT("Recovery proof obtained; actual Walking MoveTo parking begins, without teleport or collision removal.");return true;}
    }
    Reason=TEXT("No finite legal side-parking point/path for the recovery probe.");return false;
}

bool URogueliteSliceEncounter::SelectRecoveryConnection(FGuid Connection,FString& Reason)
{
/* PERF1A_BEGIN */
    TRACE_CPUPROFILER_EVENT_SCOPE(PERF1A_Encounter_SelectRecoveryConnection);
/* PERF1A_END */

    if(!bCoordinated||State!=ERogueliteSliceEncounterState::Cleared||!CurrentIdentity(Reason)||!IncidentConnectionIds.Contains(Connection))
    {Reason=TEXT("Recovery selection requires this current Cleared room and an actual incident ConnectionId.");return false;}
    FRogueliteRoomConnection Record;if(!ReadConnection(Connection,Record)){Reason=TEXT("Recovery connection no longer belongs to current C2 traversal.");return false;}
    if(IsRecoveryBusy())
    {Reason=TEXT("Existing recovery/parking is still running; no concurrent or replacement request.");return Connection==ExitConnectionId;}
    if(Connection==ExitConnectionId&&bParkFinished&&GetExitPassageState()==ERogueliteGameplayPassage::Traversable)
    {Reason=TEXT("Current selected connection already has its latest valid parked recovery proof.");return true;}
    if(auto* Probe=RecoveryProbe.Get())
    {
        auto* Controller=Probe->GetRecoveryController();auto* Source=BoundSource.Get();
        if(!bParkFinished||!Controller||Controller->GetMoveStatus()!=EPathFollowingStatus::Idle||Controller->GetOwner()!=Source
            ||Probe->GetWorld()!=BoundWorld.Get()||Probe->GetOwner()!=Controller||Probe->GetController()!=Controller||Controller->GetPawn()!=Probe)
        {Reason=TEXT("Old probe is not safely parked under its actual owned possession chain; preserve it and refuse replacement.");return false;}
        // Retire only this already parked verifier when a new proof is explicitly requested.
        TGuardValue<bool> Guard(bCleaning,true);
        Probe->OnDestroyed.RemoveDynamic(this,&URogueliteSliceEncounter::OwnedActorDestroyed);
        Controller->OnDestroyed.RemoveDynamic(this,&URogueliteSliceEncounter::OwnedActorDestroyed);
        Probe->Quiesce();
        if(Probe->GetOwner()!=Source||Probe->GetController()||!Probe->Destroy())
        {Reason=TEXT("Owned parked probe retirement failed; no new proof claimed.");return false;}
        OwnedActors.RemoveAll([&](const auto& Weak){return !Weak.IsValid()||Weak.Get()==Probe||Weak.Get()==Controller;});RecoveryProbe.Reset();
    }
    ExitConnectionId=Connection;bRecoveryRequested=bParkRequested=bParkFinished=bRecoveryTimedOut=false;RecoveryAttempts=0;
    RecoveryStartedAt=GetWorld()->GetTimeSeconds();NextRecoveryAttemptAt=RecoveryStartedAt;
    Reason=TEXT("One selected current connection queued for original physical/path/Walking recovery.");LastReason=Reason;return true;
}

void URogueliteSliceEncounter::AdvanceScheduledRecovery()
{
/* PERF1A_BEGIN */
    TRACE_CPUPROFILER_EVENT_SCOPE(PERF1A_Encounter_AdvanceScheduledRecovery);
/* PERF1A_END */

    if(!bCoordinated||State!=ERogueliteSliceEncounterState::Cleared)return;
    FString Why;if(!CurrentIdentity(Why)){Shutdown(Why);return;}
    TInlineComponentArray<URogueliteSliceEncounter*> Siblings;GetOwner()->GetComponents(Siblings);
    for(const auto* Other:Siblings)if(Other&&Other!=this&&Other->IsCoordinated()
        &&(Other->GetState()==ERogueliteSliceEncounterState::Preparation||Other->GetState()==ERogueliteSliceEncounterState::CombatActive))return;
    UpdateRecovery();
}

void URogueliteSliceEncounter::UpdateRecovery()
{
/* PERF1A_BEGIN */
    TRACE_CPUPROFILER_EVENT_SCOPE(PERF1A_Encounter_UpdateRecovery);
/* PERF1A_END */

    auto* Source=BoundSource.Get();auto* FPS=BoundPlayer.Get();if(!Source||!FPS||!ExitConnectionId.IsValid())return;
    const double Now=GetWorld()->GetTimeSeconds();
    if(bRecoveryTimedOut)return;
    if(bRecoveryRequested&&!bParkFinished&&Now-RecoveryStartedAt>45)
    {
        bRecoveryTimedOut=true;
        const FString PreviousReason=LastReason;
        LastReason=TEXT("Actual recovery/parking timed out; no new MoveTo requested. See recovery diagnostic.");
        LastRecoveryDiagnostic=RecoveryDiagnostics(LastReason+TEXT(" Previous: ")+PreviousReason,true);
        UE_LOG(LogTemp,Warning,TEXT("G1B_RECOVERY_TIMEOUT %s"),*LastRecoveryDiagnostic);return;
    }
    FString Why;
    if(!RecoveryProbe.IsValid())
    {
        if(Now<NextRecoveryAttemptAt||RecoveryAttempts>=6)return;
        NextRecoveryAttemptAt=Now+.5;
        FRogueliteRoomConnection EndpointConnection;FRogueliteDoorSocket A,B;
        if(!ReadConnection(ExitConnectionId,EndpointConnection)||!EndpointConnection.RoomA->TryGetDoorSocket(EndpointConnection.SocketA,A)
            ||!EndpointConnection.RoomB->TryGetDoorSocket(EndpointConnection.SocketB,B))
        {Shutdown(TEXT("Exit recovery lost its authoritative endpoints."));return;}
        FCollisionQueryParams EndpointQuery(SCENE_QUERY_STAT(G1B_RecoveryEndpointWait),false);
        const FVector EndpointA=A.WorldPosition-A.WorldOutwardDirection*300+FVector(0,0,90);
        const FVector EndpointB=B.WorldPosition-B.WorldOutwardDirection*300+FVector(0,0,90);
        if(GetWorld()->OverlapBlockingTestByChannel(EndpointA,FQuat::Identity,ECC_Pawn,FCollisionShape::MakeCapsule(34,88),EndpointQuery)
            ||GetWorld()->OverlapBlockingTestByChannel(EndpointB,FQuat::Identity,ECC_Pawn,FCollisionShape::MakeCapsule(34,88),EndpointQuery))
        {
            FString Blocker;LastRecoveryDiagnostic=RecoveryDiagnostics(TEXT("Waiting for real Pawn endpoint clearance."),false,&Blocker);
            LastReason=TEXT("Recovery endpoint occupied by ")+Blocker+TEXT(". Please step aside.");
            return;
        }
        ++RecoveryAttempts;
        FActorSpawnParameters Params;Params.Owner=Source;Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        auto* Probe=GetWorld()->SpawnActor<ARogueliteSliceRecoveryProbe>(ARogueliteSliceRecoveryProbe::StaticClass(),FVector::ZeroVector,FRotator::ZeroRotator,Params);
        if(!Probe){LastReason=TEXT("Recovery probe spawn failed; exit not verified.");return;}AddOwned(Probe);
        if(!Probe->Configure(Source,BoundBatch,ExitConnectionId,Why))
        {
            bool Destroyed=false;
            if(Probe->GetOwner()==Source&&Probe->GetWorld()==BoundWorld.Get())
            {
                Probe->Quiesce();
                if(Probe->GetOwner()==Source&&Probe->GetWorld()==BoundWorld.Get())
                {
                    TGuardValue<bool> Guard(bCleaning,true);
                    Destroyed=Probe->Destroy();
                }
            }
            if(Destroyed)
            {
                OwnedActors.Remove(Probe);LastReason=TEXT("Exit recovery deferred; failed owned probe destroyed: ")+Why;return;
            }
            Shutdown(TEXT("Recovery configuration failed with an unresolved probe ownership/destruction; weak ledger retained: ")+Why);return;
        }
        RecoveryProbe=Probe;AddOwned(Probe->GetRecoveryController());
    }
    auto* Probe=RecoveryProbe.Get();if(!Probe)return;
    if(!bRecoveryRequested)
    {
        if(Now<NextRecoveryAttemptAt)return;NextRecoveryAttemptAt=Now+.5;
        if(!FRogueliteDynamicGameplayBridge::BeginRecovery(Source,ExitConnectionId,Probe,Probe->GetRecoveryController(),Why))
        {
            FString Blocker;LastRecoveryDiagnostic=RecoveryDiagnostics(Why,false,&Blocker);
            LastReason=Blocker!=TEXT("None")?TEXT("Recovery blocked by ")+Blocker+TEXT(". Please step aside.")
                :TEXT("Recovery path/agent is not ready; waiting for validation.");return;
        }
        bRecoveryRequested=true;RecoveryStartedAt=Now;
/* PERF1A_BEGIN */
    TRACE_BOOKMARK(TEXT("PERF1A RecoveryStarted Slot=%d"), GetFName()==FName(TEXT("SecondCombatEncounter")) ? 1 : 0);
/* PERF1A_END */
LastReason=TEXT("Fresh actual standing-character corridor recovery underway.");
    }
    if(!bParkRequested)
    {
        const auto Query=FRogueliteDynamicGameplayBridge::QueryConnection(Source,ExitConnectionId,Probe);
        if(Query.bMoveVerified&&Query.State==ERogueliteGameplayPassage::Traversable
            &&Probe->GetRecoveryController()->GetMoveStatus()==EPathFollowingStatus::Idle)
        {if(!StartParking(Why))LastRecoveryDiagnostic=RecoveryDiagnostics(Why);LastReason=Why;}
        return;
    }
    const FVector Feet=Probe->GetActorLocation()-FVector(0,0,88);
    if(!bParkFinished&&Probe->GetRecoveryController()->GetMoveStatus()==EPathFollowingStatus::Idle
        &&FVector::Dist2D(Feet,ParkFeet)<40&&FMath::Abs(Feet.Z-ParkFeet.Z)<50)
/* PERF1A_BEGIN */
{
/* PERF1A_END */
bParkFinished=true;
/* PERF1A_BEGIN */
TRACE_BOOKMARK(TEXT("PERF1A RecoveryParked Slot=%d"), GetFName()==FName(TEXT("SecondCombatEncounter")) ? 1 : 0); }
/* PERF1A_END */

    if(bParkFinished)
    {
        const auto Query=FRogueliteDynamicGameplayBridge::QueryConnection(Source,ExitConnectionId,FPS);
        LastReason=Query.State==ERogueliteGameplayPassage::Traversable
            ?TEXT("Selected exit Traversable: proof retained and recovery pawn actually parked clear of the connection.")
            :TEXT("Recovery pawn parked; current exit query still rejects passage: ")+Query.Reason;
    }
}

void URogueliteSliceEncounter::TickComponent(float DeltaTime,ELevelTick TickType,FActorComponentTickFunction* ThisTickFunction)
{
/* PERF1A_BEGIN */
    TRACE_CPUPROFILER_EVENT_SCOPE(PERF1A_Encounter_Tick);
    TRACE_CPUPROFILER_EVENT_SCOPE_CONDITIONAL(PERF1A_Encounter_Primary, TRACE_CPUPROFILER_EVENT_MANUAL_IS_ENABLED() && GetFName()!=FName(TEXT("SecondCombatEncounter")));
    TRACE_CPUPROFILER_EVENT_SCOPE_CONDITIONAL(PERF1A_Encounter_Secondary, TRACE_CPUPROFILER_EVENT_MANUAL_IS_ENABLED() && GetFName()==FName(TEXT("SecondCombatEncounter")));
    TRACE_CPUPROFILER_EVENT_SCOPE_CONDITIONAL(PERF1A_Encounter_AwaitingEntry, State==ERogueliteSliceEncounterState::AwaitingEntry);
    TRACE_CPUPROFILER_EVENT_SCOPE_CONDITIONAL(PERF1A_Encounter_Preparation, State==ERogueliteSliceEncounterState::Preparation);
    TRACE_CPUPROFILER_EVENT_SCOPE_CONDITIONAL(PERF1A_Encounter_CombatActive, State==ERogueliteSliceEncounterState::CombatActive);
    TRACE_CPUPROFILER_EVENT_SCOPE_CONDITIONAL(PERF1A_Encounter_Cleared, State==ERogueliteSliceEncounterState::Cleared);
/* PERF1A_END */

    const bool bNested=ActiveSynchronousRead!=nullptr;
    if(bNested){ActiveSynchronousRead->Invalidate();TRACE_COUNTER_INCREMENT(Perf21ReadReentry);}
    Super::TickComponent(DeltaTime,TickType,ThisTickFunction);
    FRogueliteEncounterSynchronousRead Read(this);
    TGuardValue<FRogueliteEncounterSynchronousRead*> ReadGuard(ActiveSynchronousRead,bNested?ActiveSynchronousRead:&Read);
    auto* Request=!bNested&&CanStartSynchronousRead()?&Read:nullptr;
    if(State==ERogueliteSliceEncounterState::Uninitialized||State==ERogueliteSliceEncounterState::Cancelled)return;
    FString Why;if(!CurrentIdentity(Why,Request)){Read.Close();Shutdown(Why);return;}
    if(!BoundPlayer->HealthComponent->IsAlive()){Read.Close();Shutdown(TEXT("Player is no longer alive; no victory."));return;}
    if(State!=ERogueliteSliceEncounterState::Cleared&&!CompleteRoster(Why)){Read.Close();Shutdown(Why);return;}
    TrackEntry(DeltaTime,Request);Read.Close();if(State==ERogueliteSliceEncounterState::Cancelled)return;
    if(State==ERogueliteSliceEncounterState::Cleared&&!bCoordinated)UpdateRecovery();
    if(auto* FPS=BoundPlayer.Get())if(!bCoordinated)FPS->GameplaySummary=Describe();
}

void URogueliteSliceEncounter::CleanOwned()
{
    if(ActiveSynchronousRead)ActiveSynchronousRead->Close();
    if(bCleaning)return;TGuardValue<bool> Guard(bCleaning,true);
    if(auto* FPS=BoundPlayer.Get())
    {FPS->OnShotAccepted.RemoveAll(this);if(FPS->HealthComponent)FPS->HealthComponent->OnKilled.RemoveAll(this);}
    auto* Source=BoundSource.Get();if(!Source)Source=Cast<ARogueliteRuntimeLoopAssembly>(GetOwner());
    for(const auto& Weak:OwnedActors)if(auto* Actor=Weak.Get())
    {
        Actor->OnDestroyed.RemoveDynamic(this,&URogueliteSliceEncounter::OwnedActorDestroyed);
        if(auto* Enemy=Cast<ARogueliteSliceEnemy>(Actor);Enemy&&Enemy->Health)Enemy->Health->OnKilled.RemoveAll(this);
    }
    TArray<TWeakObjectPtr<AActor>> Remaining;
    for(const auto& Weak:OwnedActors)if(auto* Actor=Weak.Get())
    {
        if(Actor->GetWorld()!=BoundWorld.Get()){Remaining.Add(Actor);continue;}
        bool Owned=Actor->GetOwner()==Source;
        if(auto* Probe=Cast<ARogueliteSliceRecoveryProbe>(Actor);Probe&&!Owned)
        {
            const auto* Controller=Probe->GetRecoveryController();
            Owned=Controller&&Controller->GetOwner()==Source&&Controller->GetPawn()==Probe
                &&Probe->GetController()==Controller&&Probe->GetOwner()==Controller;
        }
        if(!Owned){Remaining.Add(Actor);continue;}
        if(auto* Controller=Cast<AAIController>(Actor);Controller&&Controller->GetPawn())
        {
            APawn* Pawn=Controller->GetPawn();auto* Probe=Cast<ARogueliteSliceRecoveryProbe>(Pawn);
            const bool OwnPossession=IsValid(Probe)&&OwnedActors.ContainsByPredicate([&](const auto& Entry){return Entry.Get()==Probe;})
                &&Probe->GetWorld()==BoundWorld.Get()&&Probe->GetRecoveryController()==Controller
                &&Probe->GetController()==Controller&&Probe->GetOwner()==Controller;
            if(!OwnPossession)
            {
                Remaining.Add(Actor);
                const FString GuardReason=FString::Printf(TEXT("Cleanup preserved controller %s: its current Pawn %s is external or no longer has the owned recovery possession chain."),
                    *Controller->GetPathName(),*GetPathNameSafe(Pawn));
                if(!LastReason.Contains(GuardReason)){LastReason+=TEXT("\n")+GuardReason;UE_LOG(LogTemp,Warning,TEXT("%s"),*GuardReason);}
                continue;
            }
        }
        if(auto* Enemy=Cast<ARogueliteSliceEnemy>(Actor))Enemy->Quiesce();
        if(auto* Gate=Cast<ARogueliteSliceGate>(Actor))Gate->Quiesce();
        if(auto* Probe=Cast<ARogueliteSliceRecoveryProbe>(Actor))Probe->Quiesce();
        if(!Actor->Destroy())Remaining.Add(Actor);
    }
    OwnedActors=MoveTemp(Remaining);RecoveryProbe.Reset();
}

void URogueliteSliceEncounter::Shutdown(const FString& Reason)
{
    if(ActiveSynchronousRead)ActiveSynchronousRead->Close();
/* PERF1A_BEGIN */
    TRACE_CPUPROFILER_EVENT_SCOPE(PERF1A_Encounter_Shutdown);
/* PERF1A_END */

    if(State==ERogueliteSliceEncounterState::Cancelled){CleanOwned();return;}
    State=ERogueliteSliceEncounterState::Cancelled;
/* PERF1A_BEGIN */
    TRACE_BOOKMARK(TEXT("PERF1A Encounter Cancelled Slot=%d"), GetFName()==FName(TEXT("SecondCombatEncounter")) ? 1 : 0);
/* PERF1A_END */
LastReason=Reason;CleanOwned();
    if(auto* FPS=BoundPlayer.Get())if(!bCoordinated)FPS->GameplaySummary=Describe();
}

void URogueliteSliceEncounter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{Shutdown(TEXT("Encounter World/PIE ended."));Super::EndPlay(EndPlayReason);}

int32 URogueliteSliceEncounter::GetRemainingEnemyCount() const
{return FMath::Max(0,MemberIds.Num()-KilledMembers.Num());}
int32 URogueliteSliceEncounter::GetGameplayActorCount() const
{int32 Count=0;for(const auto& Actor:OwnedActors)Count+=Actor.IsValid();return Count;}
ARogueliteSliceGate* URogueliteSliceEncounter::GetGate(FGuid Connection) const
{const auto* Found=Gates.Find(Connection);return Found?Found->Get():nullptr;}
ERogueliteGameplayPassage URogueliteSliceEncounter::GetExitPassageState() const
{
/* PERF1A_BEGIN */
    TRACE_CPUPROFILER_EVENT_SCOPE(PERF1A_Encounter_GetExitPassageState);
/* PERF1A_END */

    if(State==ERogueliteSliceEncounterState::Cancelled||State==ERogueliteSliceEncounterState::Uninitialized)return ERogueliteGameplayPassage::Unavailable;
    if(!ExitConnectionId.IsValid())return ERogueliteGameplayPassage::Unavailable;
    FString Why;if(!CurrentIdentity(Why))return ERogueliteGameplayPassage::Unavailable;
    return FRogueliteDynamicGameplayBridge::QueryConnection(BoundSource.Get(),ExitConnectionId,BoundPlayer.Get()).State;
}
FString URogueliteSliceEncounter::Describe() const
{
/* PERF1A_BEGIN */
    TRACE_CPUPROFILER_EVENT_SCOPE(PERF1A_Encounter_Describe);
/* PERF1A_END */

    int32 Closed=0;for(const auto& Pair:Gates)if(Pair.Value.IsValid())Closed+=Pair.Value->IsClosed();
    const auto* Source=BoundSource.Get();
    return FString::Printf(TEXT("Node %d / target %d  Batch %s\n%s  Enemies %d  Gates %d/%d closed  BaseNav %s\nEntry %s  Exit %s: %s\n%s"),
        CurrentRoomNodeId,TargetNodeId,*BoundBatch.ToString(),*RogueliteSliceEncounterImpl::StateText(State),GetRemainingEnemyCount(),Closed,IncidentConnectionIds.Num(),
        Source&&Source->IsDungeonReady()?TEXT("Ready"):TEXT("NotReady"),*EntryConnectionId.ToString(),*ExitConnectionId.ToString(),
        *RogueliteSliceEncounterImpl::PassageText(GetExitPassageState()),*LastReason);
}
