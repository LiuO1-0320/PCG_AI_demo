#include "Tests/RogueliteCombatSliceRig.h"
#include "Gameplay/RogueliteCombatSlicePlayer.h"
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
    if(!Nav||!World.GetOutermost()->GetName().EndsWith(TEXT("L_Roguelite_SingleCombatSlice_Dev")))return Base;
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
{bAutoGenerate=false;bNavigationEnabled=true;Encounter=CreateDefaultSubobject<URogueliteSliceEncounter>(TEXT("SingleCombatEncounter"));}
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
    if(Player.IsValid())Player->UnbindFromDungeon();Player.Reset();
    BoundController.Reset();
    FRogueliteDynamicGameplayBridge::Disable(this);Super::EndPlay(Reason);
}
bool ARogueliteCombatSliceRig::ClearRuntime()
{
    GetWorld()->GetTimerManager().ClearTimer(SliceTimer);
    if(Encounter)Encounter->Shutdown(SliceFailure.IsEmpty()?TEXT("Runtime Source batch cleared"):SliceFailure);
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
            bPlayerBridgeReady=true;SliceStatus=TEXT("Player ready in StartSafehouse");
            if(bEnableCombatEncounter&&(!Encounter||!Encounter->Initialize(this,Player.Get(),Reason)))
            {SliceFailure=TEXT("Single-room encounter initialization refused: ")+Reason;ClearRuntime();}
        }
        else SliceFailure=TEXT("BLOCKED_SCOPE_PLAYER_BINDING: ")+Reason;
    }
    if(bPlayerBridgeReady&&bEnableCombatEncounter&&Encounter&&Encounter->GetState()==ERogueliteSliceEncounterState::Cancelled)
    {
        SliceFailure=Encounter->GetLastReason();
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
    {SliceStatus=Encounter->Describe();if(Player.IsValid())Player->GameplaySummary=SliceStatus;}
    if(GEngine)GEngine->AddOnScreenDebugMessage(uint64(GetUniqueID()),0.2f,SliceFailure.IsEmpty()?FColor::Cyan:FColor::Red,
        FString::Printf(TEXT("G1B | Batch=%s Start=%d | %s"),*GameplayBatchId.ToString(),PlayerStartNode,*SliceStatus));
}
