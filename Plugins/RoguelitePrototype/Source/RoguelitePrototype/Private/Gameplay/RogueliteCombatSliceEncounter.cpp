#include "Gameplay/RogueliteCombatSliceEncounter.h"
#include "Gameplay/RogueliteCombatSlicePlayer.h"
#include "Gameplay/RogueliteCombatSliceActors.h"
#include "RogueliteRuntimeLoopAssembly.h"
#include "RogueliteRoomTemplate.h"
#include "RogueliteParametricCorridor.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/World.h"
#include "AIController.h"
#include "NavigationSystem.h"
#include "NavigationPath.h"
#include "Navigation/PathFollowingComponent.h"

namespace RogueliteSliceEncounterImpl
{
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

bool URogueliteSliceEncounter::CurrentIdentity(FString& Reason) const
{
    auto* Source=BoundSource.Get();auto* FPS=BoundPlayer.Get();auto* World=BoundWorld.Get();
    if(!IsInGameThread()||!IsValid(Source)||!IsValid(FPS)||!World||World->bIsTearingDown||!World->IsGameWorld()
        ||GetOwner()!=Source||Source->GetWorld()!=World||FPS->GetWorld()!=World||Source->Batch.BatchId!=BoundBatch
        ||Source->Seed!=SourceSeed||FPS->GetDungeonSource()!=Source||FPS->GetBoundBatch()!=BoundBatch||!FPS->HasLiveBinding())
    {Reason=TEXT("Encounter identity expired: Source/World/Batch/Seed or real player binding changed.");return false;}
    const auto Snapshot=Source->GetTraversalSnapshot();
    if(!Snapshot.bCurrent||Snapshot.BatchId!=BoundBatch||Snapshot.Seed!=GraphSeed
        ||Snapshot.GraphRevision!=GraphRevision||Snapshot.GeometryRevision!=GeometryRevision||Snapshot.StableDigest!=TraversalDigest)
    {Reason=TEXT("Encounter traversal is no longer the captured current graph/geometry revision.");return false;}
    return true;
}

bool URogueliteSliceEncounter::Initialize(ARogueliteRuntimeLoopAssembly* Source,ARogueliteSlicePlayer* FPS,FString& Reason)
{
    if(!IsInGameThread()||!IsValid(Source)||!IsValid(FPS)||GetOwner()!=Source||Source->GetWorld()!=GetWorld()
        ||!GetWorld()||!GetWorld()->IsGameWorld()||!Source->IsDungeonReady()||!FPS->HasLiveBinding()
        ||FPS->GetDungeonSource()!=Source||FPS->GetBoundBatch()!=Source->Batch.BatchId||!FPS->HealthComponent->IsAlive())
    {Reason=TEXT("Encounter requires its owning Source, strict Ready and the actually bound local FPS player.");return false;}
    if(BoundBatch.IsValid()&&BoundBatch==Source->Batch.BatchId&&BoundSource.Get()==Source)
    {
        if(State==ERogueliteSliceEncounterState::Cancelled){Reason=LastReason;return false;}
        Reason=TEXT("Current encounter already exists; no new roster, gates or activation.");return true;
    }
    CleanOwned();
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
    if(!SelectTarget(Reason)){Shutdown(Reason);return false;}
    CurrentRoomNodeId=FindPlayerRoom(FPS->GetActorLocation(),false);
    LastFullRoomNodeId=FindPlayerRoom(FPS->GetActorLocation(),true);
    if(LastFullRoomNodeId!=StartNodeId){Reason=TEXT("Encounter must initialize with the player fully inside the actual StartSafehouse.");Shutdown(Reason);return false;}
    if(!SpawnMembersAndGates(Reason)){Shutdown(Reason);return false;}
    FPS->OnShotAccepted.AddUObject(this,&URogueliteSliceEncounter::AcceptedShot);
    FPS->HealthComponent->OnKilled.AddUObject(this,&URogueliteSliceEncounter::PlayerKilled);
    PreviousPlayerPosition=FPS->GetActorLocation();PreviousPlayerSpeed2D=FPS->GetVelocity().Size2D();bHavePreviousPosition=true;
    State=ERogueliteSliceEncounterState::AwaitingEntry;Reason=TEXT("Two complete Dormant members registered; awaiting a physical committed-connection entry.");
    LastReason=Reason;FPS->GameplaySummary=Describe();return true;
}

bool URogueliteSliceEncounter::SelectTarget(FString& Reason)
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
    TargetNodeId=Candidates[0];
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
            if(!Enemy->Configure(Source,BoundBatch,TargetNodeId,MemberId,FTransform(Record->PlacedTransform.GetRotation(),Center,FVector::OneVector),Reason))return false;
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

void URogueliteSliceEncounter::TrackEntry(float DeltaTime)
{
    auto* FPS=BoundPlayer.Get();auto* Source=BoundSource.Get();if(!FPS||!Source)return;
    const FVector Now=FPS->GetActorLocation();CurrentRoomNodeId=FindPlayerRoom(Now,false);
    if(!bHavePreviousPosition){PreviousPlayerPosition=Now;bHavePreviousPosition=true;return;}
    const FVector Delta=Now-PreviousPlayerPosition;
    const float Limit=FMath::Max(200.f,FPS->GetCharacterMovement()->MaxWalkSpeed*DeltaTime*2.f+40.f);
    if(Delta.Size2D()>Limit)
    {Shutdown(TEXT("Room entry rejected: discontinuous player teleport is not a physical connection crossing."));return;}
    const auto Snapshot=Source->GetTraversalSnapshot();
    if(State==ERogueliteSliceEncounterState::AwaitingEntry)
    {
        const int32 FullRoom=FindPlayerRoom(Now,true);
        if(FullRoom!=INDEX_NONE&&FullRoom!=TargetNodeId)
        {LastFullRoomNodeId=FullRoom;PendingEntryConnection.Invalidate();bCrossedEntryPlane=false;}
        for(const auto Id:IncidentConnectionIds)
        {
            const auto* Edge=Snapshot.Edges.FindByPredicate([&](const auto& Row){return Row.ConnectionId==Id;});
            if(!Edge)continue;const int32 Other=Edge->NodeA==TargetNodeId?Edge->NodeB:Edge->NodeA;
            FRogueliteRoomConnection Connection;if(!ReadConnection(Id,Connection))continue;
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
            if(!Target||!ReadConnection(PendingEntryConnection,Connection)){Shutdown(TEXT("Entry connection became invalid."));return;}
            FRogueliteDoorSocket Socket;
            if(!Target->Room->TryGetDoorSocket(Connection.RoomA==Target->Room.Get()?Connection.SocketA:Connection.SocketB,Socket))
            {Shutdown(TEXT("Target entry socket is no longer legal."));return;}
            const FVector Normal=Socket.WorldOutwardDirection,Tangent=FVector::CrossProduct(FVector::UpVector,Normal);
            const float Before=FVector::DotProduct(PreviousPlayerPosition-Socket.WorldPosition,Normal);
            const float After=FVector::DotProduct(Now-Socket.WorldPosition,Normal);
            const bool Fits=FMath::Abs(FVector::DotProduct(Now-Socket.WorldPosition,Tangent))+34<=Socket.DoorWidth*.5f-2;
            if(Before>=0&&After<0&&FVector::DotProduct(Delta,Normal)<0&&Fits)
            {
                const float Speed=FMath::Max(PreviousPlayerSpeed2D,FPS->GetVelocity().Size2D());
                const float PhysicalLimit=Speed*DeltaTime*2.f+40;
                if(Delta.Size2D()>PhysicalLimit||Speed<1)
                {Shutdown(TEXT("Door-plane crossing lacks actual character movement; teleport entry rejected."));return;}
                bCrossedEntryPlane=true;
            }
            if(After>=0)bCrossedEntryPlane=false;
            const float SafeDepth=Socket.WallThickness+34.f+25.f+2.f;
            if(bCrossedEntryPlane&&Fits&&-After>=SafeDepth&&ContainsRoom(*Target,Now,true))
            {
                FString Why;if(!EnterPreparation(PendingEntryConnection,Why)){Shutdown(Why);return;}
                LastFullRoomNodeId=TargetNodeId;
            }
        }
        if(State==ERogueliteSliceEncounterState::AwaitingEntry&&CurrentRoomNodeId==TargetNodeId&&!PendingEntryConnection.IsValid())
        {Shutdown(TEXT("Target entry source is unknown: no visited committed corridor from a known adjacent room."));return;}
    }
    PreviousPlayerPosition=Now;PreviousPlayerSpeed2D=FPS->GetVelocity().Size2D();
}

bool URogueliteSliceEncounter::ValidateClosedGate(FGuid Id,FString& Reason) const
{
    auto* Gate=GetGate(Id);auto* Source=BoundSource.Get();
    if(!Gate||!Gate->IsClosed()){Reason=TEXT("Incident gate is not physically closed.");return false;}
    const auto Query=FRogueliteDynamicGameplayBridge::QueryConnection(Source,Id,BoundPlayer.Get());
    if(!Query.bStructuralCurrent||!Query.bBaseNavigationReady||Query.bForwardCapsule||Query.bReverseCapsule
        ||Query.State!=ERogueliteGameplayPassage::GameplayBlocked)
    {Reason=TEXT("Closed gate did not retain base Ready and block both real capsule sweep directions.");return false;}
    return true;
}

bool URogueliteSliceEncounter::EnterPreparation(FGuid Connection,FString& Reason)
{
    auto* Source=BoundSource.Get();auto* Gate=GetGate(Connection);
    if(State!=ERogueliteSliceEncounterState::AwaitingEntry||!Source||!Source->IsDungeonReady())
    {Reason=TEXT("Preparation requires an awaiting encounter and unchanged strict base navigation Ready.");return false;}
    if(!CompleteRoster(Reason))return false;
    if(!Gate||!Gate->CanClose(Reason)||!Gate->SetClosed(true,Reason)||!ValidateClosedGate(Connection,Reason))return false;
    EntryConnectionId=Connection;ExitConnectionId=IncidentConnectionIds[0];
    for(const auto Id:IncidentConnectionIds)if(Id!=EntryConnectionId){ExitConnectionId=Id;break;}
    State=ERogueliteSliceEncounterState::Preparation;
    LastReason=TEXT("Player physically entered; actual entry sealed. Unlimited Preparation; both enemies remain Dormant.");return true;
}

void URogueliteSliceEncounter::AcceptedShot(const FRogueliteSliceShot& Shot)
{
    FString Why;auto* FPS=BoundPlayer.Get();FRogueliteSliceShot Recorded;
    if(!CurrentIdentity(Why)||!FPS||Shot.Shooter.Get()!=FPS||Shot.BatchId!=BoundBatch||SeenShots.Contains(Shot.Id)
        ||!FPS->FindAcceptedShot(Shot.Id,Recorded)||Recorded.Id!=Shot.Id||Recorded.BatchId!=BoundBatch
        ||Recorded.Shooter.Get()!=FPS||!Recorded.PawnLocation.Equals(Shot.PawnLocation,1))return;
    SeenShots.Add(Shot.Id);
    const auto* Target=RogueliteSliceEncounterImpl::Room(BoundSource.Get(),TargetNodeId);
    if(State!=ERogueliteSliceEncounterState::Preparation||!Target||!ContainsRoom(*Target,Recorded.PawnLocation,true)
        ||!ContainsRoom(*Target,FPS->GetActorLocation(),true))return;
    if(!CompleteRoster(Why)){Shutdown(Why);return;}
    for(const auto Id:IncidentConnectionIds)
    {
        auto* Gate=GetGate(Id);if(!Gate){Shutdown(TEXT("An incident gate vanished before activation."));return;}
        if(!Gate->IsClosed()&&!Gate->CanClose(Why)){Shutdown(TEXT("Activation cannot safely seal all incident gates: ")+Why);return;}
    }
    for(const auto Id:IncidentConnectionIds)
    {
        auto* Gate=GetGate(Id);
        if(!Gate->SetClosed(true,Why)||!ValidateClosedGate(Id,Why)){Shutdown(TEXT("Activation gate transaction rejected: ")+Why);return;}
    }
    State=ERogueliteSliceEncounterState::CombatActive;
    for(const auto& Enemy:Enemies)if(!Enemy.IsValid()||!Enemy->Activate(FPS))
    {Shutdown(TEXT("A required member could not activate after safe gate closure."));return;}
    ++ActivationCount;LastReason=TEXT("Current room accepted the player's first real shot; all incident gates closed; only this roster active.");
}

void URogueliteSliceEncounter::MemberKilled(URogueliteSliceHealth* Health)
{
    if(bCleaning||!Health)return;
    int32 Index=INDEX_NONE;for(int32 It=0;It<Enemies.Num();++It)if(Enemies[It].IsValid()&&Enemies[It]->Health==Health){Index=It;break;}
    if(Index==INDEX_NONE)return;
    FString Why;
    if(State!=ERogueliteSliceEncounterState::CombatActive||!CurrentIdentity(Why)||Health->GetResolution()!=ERogueliteSliceMemberResolution::Killed)
    {Shutdown(TEXT("Unexpected member terminal event outside current active combat; not a victory."));return;}
    if(KilledMembers.Contains(MemberIds[Index]))return;
    KilledMembers.Add(MemberIds[Index]);
    if(SpawnedEnemyCount==2&&MemberIds.Num()==2&&KilledMembers.Num()==2)CommitCleared();
}

void URogueliteSliceEncounter::PlayerKilled(URogueliteSliceHealth* Health)
{
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
    if(State!=ERogueliteSliceEncounterState::CombatActive||ClearCount!=0)return;
    FString Why;
    for(const auto Id:IncidentConnectionIds)if(auto* Gate=GetGate(Id);!Gate||!Gate->SetClosed(false,Why))
    {Shutdown(TEXT("Clear could not safely reopen an incident gate: ")+Why);return;}
    State=ERogueliteSliceEncounterState::Cleared;++ClearCount;
    ExitConnectionId=IncidentConnectionIds[0];for(const auto Id:IncidentConnectionIds)if(Id!=EntryConnectionId){ExitConnectionId=Id;break;}
    LastReason=TEXT("Exactly two legal member deaths; Cleared once. All gates open; selected exit still requires actual recovery.");
    RecoveryStartedAt=GetWorld()->GetTimeSeconds();NextRecoveryAttemptAt=RecoveryStartedAt;bRecoveryRequested=bParkRequested=bParkFinished=bRecoveryTimedOut=false;
}

FString URogueliteSliceEncounter::RecoveryDiagnostics(const FString& Detail,bool IncludeEvidence,FString* OutBlocker) const
{
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

void URogueliteSliceEncounter::UpdateRecovery()
{
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
        bRecoveryRequested=true;RecoveryStartedAt=Now;LastReason=TEXT("Fresh actual standing-character corridor recovery underway.");
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
        &&FVector::Dist2D(Feet,ParkFeet)<40&&FMath::Abs(Feet.Z-ParkFeet.Z)<50)bParkFinished=true;
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
    Super::TickComponent(DeltaTime,TickType,ThisTickFunction);
    if(State==ERogueliteSliceEncounterState::Uninitialized||State==ERogueliteSliceEncounterState::Cancelled)return;
    FString Why;if(!CurrentIdentity(Why)){Shutdown(Why);return;}
    if(!BoundPlayer->HealthComponent->IsAlive()){Shutdown(TEXT("Player is no longer alive; no victory."));return;}
    if(State!=ERogueliteSliceEncounterState::Cleared&&!CompleteRoster(Why)){Shutdown(Why);return;}
    TrackEntry(DeltaTime);if(State==ERogueliteSliceEncounterState::Cancelled)return;
    if(State==ERogueliteSliceEncounterState::Cleared)UpdateRecovery();
    if(auto* FPS=BoundPlayer.Get())FPS->GameplaySummary=Describe();
}

void URogueliteSliceEncounter::CleanOwned()
{
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
    if(State==ERogueliteSliceEncounterState::Cancelled){CleanOwned();return;}
    State=ERogueliteSliceEncounterState::Cancelled;LastReason=Reason;CleanOwned();
    if(auto* FPS=BoundPlayer.Get())FPS->GameplaySummary=Describe();
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
    if(State==ERogueliteSliceEncounterState::Cancelled||State==ERogueliteSliceEncounterState::Uninitialized)return ERogueliteGameplayPassage::Unavailable;
    if(!ExitConnectionId.IsValid())return ERogueliteGameplayPassage::Unavailable;
    FString Why;if(!CurrentIdentity(Why))return ERogueliteGameplayPassage::Unavailable;
    return FRogueliteDynamicGameplayBridge::QueryConnection(BoundSource.Get(),ExitConnectionId,BoundPlayer.Get()).State;
}
FString URogueliteSliceEncounter::Describe() const
{
    int32 Closed=0;for(const auto& Pair:Gates)if(Pair.Value.IsValid())Closed+=Pair.Value->IsClosed();
    const auto* Source=BoundSource.Get();
    return FString::Printf(TEXT("Node %d / target %d  Batch %s\n%s  Enemies %d  Gates %d/%d closed  BaseNav %s\nEntry %s  Exit %s: %s\n%s"),
        CurrentRoomNodeId,TargetNodeId,*BoundBatch.ToString(),*RogueliteSliceEncounterImpl::StateText(State),GetRemainingEnemyCount(),Closed,IncidentConnectionIds.Num(),
        Source&&Source->IsDungeonReady()?TEXT("Ready"):TEXT("NotReady"),*EntryConnectionId.ToString(),*ExitConnectionId.ToString(),
        *RogueliteSliceEncounterImpl::PassageText(GetExitPassageState()),*LastReason);
}
