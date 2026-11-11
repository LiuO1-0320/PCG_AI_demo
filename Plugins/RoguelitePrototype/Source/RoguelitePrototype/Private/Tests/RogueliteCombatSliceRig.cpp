#include "Tests/RogueliteCombatSliceRig.h"

/* PERF1A_BEGIN */
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "ProfilingDebugging/MiscTrace.h"
/* PERF1A_END */
#include "Gameplay/RogueliteCombatSlicePlayer.h"
#include "Gameplay/RogueliteCombatSliceActors.h"
#include "Gameplay/RogueliteCombatSliceEncounter.h"
#include "Components/CapsuleComponent.h"
#include "RogueliteRuntimeNavigationSystem.h"
#include "RogueliteDynamicGameplayBridge.h"
#include "RogueliteFloorProfile.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "GameFramework/PlayerController.h"
#include "UObject/UnrealType.h"
#include "TimerManager.h"

URogueliteSliceNavigationConfig::URogueliteSliceNavigationConfig()
{NavigationSystemClass=FSoftClassPath(URogueliteRuntimeNavigationSystem::StaticClass());}
UNavigationSystemBase* URogueliteSliceNavigationConfig::CreateAndConfigureNavigationSystem(UWorld& World) const
{
    auto* Base=Super::CreateAndConfigureNavigationSystem(World);
    auto* Nav=Cast<URogueliteRuntimeNavigationSystem>(Base);
    if(!Nav||!(World.GetOutermost()->GetName().EndsWith(TEXT("L_Roguelite_SingleCombatSlice_Dev"))||World.GetOutermost()->GetName().EndsWith(TEXT("L_Roguelite_TwoCombatRooms_Dev"))))return Base;
    auto* Agents=FindFProperty<FArrayProperty>(Nav->GetClass(),TEXT("SupportedAgents"));
    auto* Default=FindFProperty<FNameProperty>(Nav->GetClass(),TEXT("DefaultAgentName"));
    if(!Agents||!Default)return Base;
    FNavDataConfig Agent;Agent.Name=TEXT("RogueliteB3BStandingTest");Agent.AgentRadius=35;Agent.AgentHeight=180;
    Agent.SetNavDataClass(ARogueliteRuntimeRecastNavMesh::StaticClass());
    *Agents->ContainerPtrToValuePtr<TArray<FNavDataConfig>>(Nav)={Agent};Default->SetPropertyValue_InContainer(Nav,Agent.Name);
    for(const auto& Pair:TArray<TPair<FName,bool>>{{TEXT("bAutoCreateNavigationData"),true},{TEXT("bGenerateNavigationOnlyAroundNavigationInvokers"),false},{TEXT("bInitialBuildingLocked"),false},{TEXT("bSkipAgentHeightCheckWhenPickingNavData"),false}})
        if(auto* Property=FindFProperty<FBoolProperty>(Nav->GetClass(),Pair.Key))Property->SetPropertyValue_InContainer(Nav,Pair.Value);
    UE_LOG(LogTemp,Display,TEXT("G1B_NAV_CONFIG ExistingSystem=%s World=%s"),*Nav->GetClass()->GetPathName(),*World.GetPathName());
    return Base;
}
ARogueliteCombatSliceRig::ARogueliteCombatSliceRig()
{bAutoGenerate=false;bNavigationEnabled=true;Encounter=CreateDefaultSubobject<URogueliteSliceEncounter>(TEXT("SingleCombatEncounter"));SecondEncounter=CreateDefaultSubobject<URogueliteSliceEncounter>(TEXT("SecondCombatEncounter"));}
ARogueliteSlicePlayer* ARogueliteCombatSliceRig::GetSlicePlayer() const {return Player.Get();}
void ARogueliteCombatSliceRig::BeginPlay()
{
    bAutoGenerate=false;Super::BeginPlay();StartedAt=FPlatformTime::Seconds();
    SliceStatus=TEXT("Generating; waiting for strict navigation and real local player");
    GetWorld()->GetTimerManager().SetTimer(SliceTimer,this,&ARogueliteCombatSliceRig::PollSlice,0.1f,true);
}
void ARogueliteCombatSliceRig::EndPlay(const EEndPlayReason::Type Reason)
{
    GetWorld()->GetTimerManager().ClearTimer(SliceTimer);
    if(Encounter)Encounter->Shutdown(TEXT("PIE/world ended"));
    if(SecondEncounter)SecondEncounter->Shutdown(TEXT("PIE/world ended"));RecoveryOwner.Reset();
    if(Player.IsValid())Player->UnbindFromDungeon();Player.Reset();
    BoundController.Reset();
    FRogueliteDynamicGameplayBridge::Disable(this);Super::EndPlay(Reason);
}
bool ARogueliteCombatSliceRig::ClearRuntime()
{
/* PERF1A_BEGIN */
    TRACE_CPUPROFILER_EVENT_SCOPE(PERF1A_Rig_ClearRuntime);
/* PERF1A_END */

    GetWorld()->GetTimerManager().ClearTimer(SliceTimer);
    if(Encounter)Encounter->Shutdown(SliceFailure.IsEmpty()?TEXT("Runtime Source batch cleared"):SliceFailure);
    if(SecondEncounter)SecondEncounter->Shutdown(SliceFailure.IsEmpty()?TEXT("Runtime Source batch cleared"):SliceFailure);RecoveryOwner.Reset();
    bPlayerBridgeReady=false;GameplayBatchId.Invalidate();
    if(auto* FPS=Player.Get())
    {
        auto* PC=BoundController.Get();
        if(PC&&PC->GetPawn()==FPS&&FPS->GetOwner()==PC&&FPS->GetController()==PC)
        {FPS->UnbindFromDungeon();PC->UnPossess();FPS->Destroy();}
        else FRogueliteDynamicGameplayBridge::UnregisterComponent(this,FPS->GetCapsuleComponent());
    }
    Player.Reset();BoundController.Reset();
    PlayerStartNode=INDEX_NONE;StartedAt=0;bGenerationRequested=false;
    FRogueliteDynamicGameplayBridge::Disable(this);
    const bool Cleared=Super::ClearRuntime();SliceStatus=TEXT("Batch cleared; no current gameplay/player success");return Cleared;
}
bool ARogueliteCombatSliceRig::GenerateRuntime()
{
    if(!GetWorld()||!GetWorld()->IsGameWorld()||!Batch.OwnedActors.IsEmpty())return false;
    SliceFailure.Reset();SliceStatus=TEXT("Waiting for original strict navigation");
    if(auto* PC=Cast<ARogueliteSlicePlayerController>(GetWorld()->GetFirstPlayerController());PC&&PC->IsLocalController()&&!PC->GetPawn())
        if(auto* GM=Cast<ARogueliteSliceGameMode>(GetWorld()->GetAuthGameMode()))GM->RestartPlayer(PC);
    StartedAt=FPlatformTime::Seconds();bGenerationRequested=true;bPlayerBridgeReady=false;GameplayBatchId.Invalidate();PlayerStartNode=INDEX_NONE;
    const bool Generated=Super::GenerateRuntime();
    if(Generated)GetWorld()->GetTimerManager().SetTimer(SliceTimer,this,&ARogueliteCombatSliceRig::PollSlice,0.1f,true);
    return Generated;
}
TArray<URogueliteSliceEncounter*> ARogueliteCombatSliceRig::GetRoomEncounters() const
{
    TArray<URogueliteSliceEncounter*> Out;if(Encounter)Out.Add(Encounter.Get());if(bEnableTwoCombatRooms&&SecondEncounter)Out.Add(SecondEncounter.Get());return Out;
}
URogueliteSliceEncounter* ARogueliteCombatSliceRig::GetEncounterForNode(int32 NodeId) const
{for(auto* E:GetRoomEncounters())if(E&&E->GetTargetNodeId()==NodeId)return E;return nullptr;}
URogueliteSliceEncounter* ARogueliteCombatSliceRig::GetActiveEncounter() const
{
    for(auto* E:GetRoomEncounters())if(E&&(E->GetState()==ERogueliteSliceEncounterState::Preparation||E->GetState()==ERogueliteSliceEncounterState::CombatActive))return E;
    return nullptr;
}
int32 ARogueliteCombatSliceRig::GetCurrentPlayerRoomNode() const
{
/* PERF1A_BEGIN */
    TRACE_CPUPROFILER_EVENT_SCOPE(PERF1A_Rig_GetCurrentPlayerRoomNode);
/* PERF1A_END */

    auto* FPS=Player.Get();if(!FPS||!FPS->HasLiveBinding())return INDEX_NONE;int32 Found=INDEX_NONE;
    for(const auto& Room:Batch.Rooms)
    {
        const FVector L=Room.PlacedTransform.InverseTransformPosition(FPS->GetActorLocation());
        if(FMath::Abs(L.X)<=Room.Dimensions.X*.5&&FMath::Abs(L.Y)<=Room.Dimensions.Y*.5&&L.Z>=0&&L.Z<=Room.Dimensions.Z)
        {if(Found!=INDEX_NONE)return INDEX_NONE;Found=Room.NodeId;}
    }
    return Found;
}
FGuid ARogueliteCombatSliceRig::GetRecoveryConnectionId() const
{return RecoveryOwner.IsValid()?RecoveryOwner->GetExitConnectionId():FGuid();}

bool ARogueliteCombatSliceRig::InitializeTwoRooms(FString& Why)
{
/* PERF1A_BEGIN */
    TRACE_CPUPROFILER_EVENT_SCOPE(PERF1A_Rig_InitializeTwoRooms);
/* PERF1A_END */

    const auto Snapshot=GetTraversalSnapshot();TArray<int32> Nodes;int32 Starts=0,Finals=0;
    if(!Snapshot.bCurrent||Batch.Rooms.Num()!=4||!Settings.RoomTemplateRoles.bEnabled||!bEnableCombatEncounter)
    {Why=TEXT("G2 requires the actual current four-room M1 Floor1 batch and enabled role-qualified combat.");return false;}
    for(const auto& Node:Snapshot.Nodes)
    {
        const auto* Room=Batch.Rooms.FindByPredicate([&](const auto& R){return R.NodeId==Node.NodeId;});
        if(!Room||!IsValid(Room->Room)){Why=TEXT("G2 node has no actual placed room.");return false;}
        if(Node.Role==TEXT("Start")){Starts+=Settings.RoomTemplateRoles.StartTemplateIds.Contains(Room->TemplateId);continue;}
        if(Node.Role==TEXT("Final")){Finals+=Settings.RoomTemplateRoles.FinalTemplateIds.Contains(Room->TemplateId);continue;}
        if(Node.RoomType==ERogueliteSeedRoomType::Combat&&Settings.RoomTemplateRoles.NormalTemplateIds.Contains(Room->TemplateId))Nodes.Add(Node.NodeId);
        else {Why=TEXT("G2 refuses a non-Combat/unqualified normal room; Profile is not modified to fill quotas.");return false;}
    }
    if(Starts!=1||Finals!=1||Nodes.Num()!=2||!Encounter||!SecondEncounter)
    {Why=FString::Printf(TEXT("G2 configuration mismatch: StartSafehouse=%d NormalCombat=%d FloorExit=%d."),Starts,Nodes.Num(),Finals);return false;}
    Nodes.Sort();RecoveryOwner.Reset();
    if(!Encounter->InitializeRoom(this,Player.Get(),Nodes[0],Why)||!SecondEncounter->InitializeRoom(this,Player.Get(),Nodes[1],Why))return false;
    Why=TEXT("Both current Normal Combat rooms have complete independent Dormant rosters and room-side gates.");return true;
}

FGuid ARogueliteCombatSliceRig::PreferredRecoveryConnection(int32 FromNode) const
{
/* PERF1A_BEGIN */
    TRACE_CPUPROFILER_EVENT_SCOPE(PERF1A_Rig_PreferredRecoveryConnection);
/* PERF1A_END */

    int32 Other=INDEX_NONE;for(auto* E:GetRoomEncounters())if(E&&E->GetTargetNodeId()!=FromNode){Other=E->GetTargetNodeId();break;}
    const auto Snapshot=GetTraversalSnapshot();TMap<int32,int32> Parent;TMap<int32,FGuid> Via;TArray<int32> Queue{FromNode};Parent.Add(FromNode,FromNode);
    for(int32 I=0;I<Queue.Num()&&!Parent.Contains(Other);++I)for(const auto& Edge:Snapshot.Edges)
    {
        const int32 Next=Edge.NodeA==Queue[I]?Edge.NodeB:(Edge.NodeB==Queue[I]?Edge.NodeA:INDEX_NONE);
        if(Next!=INDEX_NONE&&!Parent.Contains(Next)){Parent.Add(Next,Queue[I]);Via.Add(Next,Edge.ConnectionId);Queue.Add(Next);}
    }
    if(Other!=INDEX_NONE&&Parent.Contains(Other))
    {int32 Child=Other;while(Parent.FindChecked(Child)!=FromNode)Child=Parent.FindChecked(Child);return Via.FindChecked(Child);}
    if(auto* E=GetEncounterForNode(FromNode))return E->GetExitConnectionId();return FGuid();
}

bool ARogueliteCombatSliceRig::RequestRoomRecovery(int32 Node,FGuid Connection,FString& Why)
{
/* PERF1A_BEGIN */
    TRACE_CPUPROFILER_EVENT_SCOPE(PERF1A_Rig_RequestRoomRecovery);
/* PERF1A_END */

    auto* E=GetEncounterForNode(Node);
    if(!bEnableTwoCombatRooms||!bPlayerBridgeReady||!E||GetActiveEncounter())
    {Why=TEXT("Select recovery only for a current Cleared G2 room while no Preparation/Combat is active.");return false;}
    if(RecoveryOwner.IsValid()&&RecoveryOwner->IsRecoveryBusy()&&RecoveryOwner.Get()!=E)
    {Why=TEXT("This Source already has one recovery/parking request; wait for it to settle.");return false;}
    if(RecoveryOwner.Get()==E&&E->NeedsRecoveryService()&&E->GetExitConnectionId()==Connection)
    {Why=TEXT("The same selected recovery request is already queued; deadline and attempts preserved.");return true;}
    if(!E->SelectRecoveryConnection(Connection,Why))return false;RecoveryOwner=E;return true;
}

void ARogueliteCombatSliceRig::PollTwoRoomRecovery()
{
/* PERF1A_BEGIN */
    TRACE_CPUPROFILER_EVENT_SCOPE(PERF1A_Rig_PollTwoRoomRecovery);
/* PERF1A_END */

    if(GetActiveEncounter())return;
    if(RecoveryOwner.IsValid()&&RecoveryOwner->NeedsRecoveryService()){RecoveryOwner->AdvanceScheduledRecovery();return;}
    auto Rooms=GetRoomEncounters();
    // No recovery can be selected here without a pending native encounter.  The
    // player/source lifecycle check in PollSlice still runs on every poll.
    if(!Rooms.ContainsByPredicate([](const auto* E){return E&&E->NeedsRecoveryService();}))return;
    const int32 Current=GetCurrentPlayerRoomNode();
    Rooms.Sort([&](const auto& A,const auto& B){const bool Ac=A.GetTargetNodeId()==Current,Bc=B.GetTargetNodeId()==Current;return Ac!=Bc?Ac:A.GetTargetNodeId()<B.GetTargetNodeId();});
    for(auto* E:Rooms)if(E&&E->NeedsRecoveryService())
    {
        FString Why;if(!E->SelectRecoveryConnection(PreferredRecoveryConnection(E->GetTargetNodeId()),Why))
        {SliceStatus=TEXT("Recovery selection refused: ")+Why;return;}
        RecoveryOwner=E;E->AdvanceScheduledRecovery();return;
    }
}

FString ARogueliteCombatSliceRig::DescribeTwoRooms() const
{
/* PERF1A_BEGIN */
    TRACE_CPUPROFILER_EVENT_SCOPE(PERF1A_Rig_DescribeTwoRooms);
/* PERF1A_END */

    const int32 Current=GetCurrentPlayerRoomNode();const auto* Active=GetActiveEncounter();
    FString Out=FString::Printf(TEXT("G2 Current Room: %s | Preparation/Combat Node: %s\n"),Current==INDEX_NONE?TEXT("Corridor/None"):*FString::FromInt(Current),Active?*FString::FromInt(Active->GetTargetNodeId()):TEXT("None"));
    for(const auto* E:GetRoomEncounters())if(E)
    {
        int32 Closed=0;for(const auto Id:E->GetIncidentConnectionIds())if(auto* Gate=E->GetGate(Id))Closed+=Gate->IsClosed();
        Out+=FString::Printf(TEXT("Room %d: %s | EnemyRemaining %d | Gates %d/%d closed\n"),E->GetTargetNodeId(),*StaticEnum<ERogueliteSliceEncounterState>()->GetNameStringByValue(int64(E->GetState())),E->GetRemainingEnemyCount(),Closed,E->GetIncidentConnectionIds().Num());
    }
    if(RecoveryOwner.IsValid())
    {
        const auto State=RecoveryOwner->GetExitPassageState();
        const TCHAR* Label=State==ERogueliteGameplayPassage::Traversable?TEXT("Traversable"):State==ERogueliteGameplayPassage::GameplayBlocked?TEXT("GameplayBlocked"):State==ERogueliteGameplayPassage::Recovering?TEXT("Recovering"):TEXT("Unavailable");
        Out+=FString::Printf(TEXT("Recovery %s | %s\n%s"),*RecoveryOwner->GetExitConnectionId().ToString(),Label,*RecoveryOwner->GetLastReason());
    }
    else Out+=TEXT("Recovery: None (no connection proof claimed)");return Out;
}

bool ARogueliteCombatSliceRig::PlaceAndRegisterPlayer(FString& Why)
{
    auto* PC=Cast<ARogueliteSlicePlayerController>(GetWorld()->GetFirstPlayerController());
    auto* FPS=PC?Cast<ARogueliteSlicePlayer>(PC->GetPawn()):nullptr;
    if(!FPS||!PC->IsLocalController()||FPS->GetOwner()!=PC){Why=TEXT("Real local PlayerController/Pawn ownership is not ready");return false;}
    const auto Traversal=GetTraversalSnapshot();if(!Traversal.bCurrent){Why=TEXT("Traversal no longer current");return false;}
    int32 Start=INDEX_NONE;
    for(const auto& Node:Traversal.Nodes)if(Node.Role==TEXT("Start")){if(Start!=INDEX_NONE){Why=TEXT("Multiple Start nodes");return false;}Start=Node.NodeId;}
    const auto* Room=Batch.Rooms.FindByPredicate([&](const auto& R){return R.NodeId==Start;});
    if(!Room||!IsValid(Room->Room)||!Settings.RoomTemplateRoles.bEnabled||!Settings.RoomTemplateRoles.StartTemplateIds.Contains(Room->TemplateId))
    {Why=TEXT("Current topology Start has no qualified safehouse template");return false;}
    auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());if(!Nav){Why=TEXT("Existing NavigationSystem unavailable");return false;}
    FCollisionQueryParams Query(SCENE_QUERY_STAT(G1B_StartCapsule),false,FPS);
    const TArray<FVector> Candidates={{0,-100,10},{0,100,10},{200,0,10},{-200,0,10},{0,-350,10},{350,-350,10}};
    for(const auto& Local:Candidates)
    {
        const FVector Guess=Room->PlacedTransform.TransformPosition(Local);FNavLocation Ground;
        if(!Nav->ProjectPointToNavigation(Guess,Ground,FVector(100,100,100)))continue;
        const FVector InRoom=Room->PlacedTransform.InverseTransformPosition(Ground.Location);
        if(FMath::Abs(InRoom.X)>Room->Dimensions.X*.5-100||FMath::Abs(InRoom.Y)>Room->Dimensions.Y*.5-100)continue;
        const FVector Center=Ground.Location+FVector(0,0,90);
        if(GetWorld()->OverlapBlockingTestByChannel(Center,FQuat::Identity,ECC_Pawn,FCollisionShape::MakeCapsule(34,88),Query))continue;
        FRotator Facing=Room->PlacedTransform.Rotator();
        for(const auto& Edge:Traversal.Edges)if(Edge.NodeA==Start||Edge.NodeB==Start)
        {
            const int32 Other=Edge.NodeA==Start?Edge.NodeB:Edge.NodeA;
            if(const auto* Next=Batch.Rooms.FindByPredicate([&](const auto& R){return R.NodeId==Other;}))Facing=(Next->PlacedTransform.GetLocation()-Center).Rotation();
            Facing.Pitch=Facing.Roll=0;break;
        }
        if(!FPS->BindToDungeon(this,FTransform(Facing,Center),Why))return false;
        Player=FPS;BoundController=PC;PlayerStartNode=Start;GameplayBatchId=Batch.BatchId;return true;
    }
    Why=TEXT("No standing capsule-safe navigation location inside actual Start safehouse");return false;
}
void ARogueliteCombatSliceRig::PollSlice()
{
/* PERF1A_BEGIN */
    TRACE_CPUPROFILER_EVENT_SCOPE(PERF1A_Rig_PollSlice);
/* PERF1A_END */

    const double Elapsed=FPlatformTime::Seconds()-StartedAt;
    if(!bGenerationRequested&&Elapsed>0.75)
    {
        bGenerationRequested=true;
        if(!FloorProfile){SliceFailure=TEXT("Independent M1 Floor1 candidate Profile is missing");}
        else if(!GenerateRuntime())SliceFailure=TEXT("Original Runtime generation refused: ")+LastGeometry.Reason;
    }
    if(bGenerationRequested&&!bPlayerBridgeReady&&SliceFailure.IsEmpty()&&IsDungeonReady())
    {
        FString Reason;
        if(PlaceAndRegisterPlayer(Reason))
        {
            bTwoRoomBatch=bEnableTwoCombatRooms;
            if(bEnableTwoCombatRooms)
            {
                if(!InitializeTwoRooms(Reason)){SliceFailure=TEXT("G2 two-room initialization refused: ")+Reason;ClearRuntime();}
                else {bPlayerBridgeReady=true;SliceStatus=Reason;}
            }
            else
            {
                bPlayerBridgeReady=true;SliceStatus=TEXT("Player ready in StartSafehouse");
                if(bEnableCombatEncounter&&(!Encounter||!Encounter->Initialize(this,Player.Get(),Reason)))
                {SliceFailure=TEXT("Single-room encounter initialization refused: ")+Reason;ClearRuntime();}
            }
        }
        else SliceFailure=TEXT("BLOCKED_SCOPE_PLAYER_BINDING: ")+Reason;
    }
    if(bPlayerBridgeReady&&bEnableTwoCombatRooms!=bTwoRoomBatch)
    {SliceFailure=TEXT("Encounter mode changed during a bound batch; restart PIE rather than rebind/reset the player.");ClearRuntime();}
    URogueliteSliceEncounter* Cancelled=nullptr;for(auto* E:GetRoomEncounters())if(E&&E->GetState()==ERogueliteSliceEncounterState::Cancelled){Cancelled=E;break;}
    if(bPlayerBridgeReady&&bEnableCombatEncounter&&Cancelled)
    {
        SliceFailure=Cancelled->GetLastReason();
        if(Batch.bSucceeded&&Batch.BatchId==GameplayBatchId&&!GetTraversalSnapshot().bCurrent)bPlayerBridgeReady=false;
        else ClearRuntime();
    }
    if(bPlayerBridgeReady&&(!Player.IsValid()||!Player->HasLiveBinding()||Batch.BatchId!=GameplayBatchId))
    {
        SliceFailure=TEXT("Current World/Source/Batch/player binding invalidated");
        // Structural rejection belongs to the original WatchGeometry timer. Do not preempt
        // its failure classification/owned cleanup with this sandbox's player lifecycle.
        if(Batch.bSucceeded&&Batch.BatchId==GameplayBatchId&&!GetTraversalSnapshot().bCurrent)
        {bPlayerBridgeReady=false;SliceStatus=TEXT("Structural mismatch; original WatchGeometry rejection pending");}
        else ClearRuntime();
    }
    if(!bPlayerBridgeReady&&Elapsed>200&&SliceFailure.IsEmpty())SliceFailure=TEXT("Timed out waiting for original strict NavigationReady");
    if(!SliceFailure.IsEmpty()){SliceStatus=SliceFailure;GetWorld()->GetTimerManager().ClearTimer(SliceTimer);}
    if(bPlayerBridgeReady&&bEnableCombatEncounter&&Encounter)
    {if(bEnableTwoCombatRooms){PollTwoRoomRecovery();SliceStatus=DescribeTwoRooms();}else SliceStatus=Encounter->Describe();if(Player.IsValid())Player->GameplaySummary=SliceStatus;}
    if(GEngine)GEngine->AddOnScreenDebugMessage(uint64(GetUniqueID()),0.2f,SliceFailure.IsEmpty()?FColor::Cyan:FColor::Red,
        FString::Printf(TEXT("G1B | Batch=%s Start=%d | %s"),*GameplayBatchId.ToString(),PlayerStartNode,*SliceStatus));
}
