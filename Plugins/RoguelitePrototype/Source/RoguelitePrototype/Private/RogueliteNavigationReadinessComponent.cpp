#include "RogueliteNavigationReadinessComponent.h"
#include "RogueliteRuntimeSeedAssembly.h"
#include "RogueliteRuntimeNavigationSystem.h"
#include "RogueliteNavigationTestCharacter.h"
#include "RoguelitePrototype.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Navigation/PathFollowingComponent.h"
#include "NavigationPath.h"
#include "Engine/World.h"
#include "TimerManager.h"

namespace
{
    FBox Occupancy(const FRogueliteSeedRoomRecord& R)
    {
        return FBox(FVector(-R.Dimensions.X*.5-R.WallThickness,-R.Dimensions.Y*.5-R.WallThickness,-R.FloorThickness),
            FVector(R.Dimensions.X*.5+R.WallThickness,R.Dimensions.Y*.5+R.WallThickness,R.Dimensions.Z)).TransformBy(R.PlacedTransform);
    }
    FNavAgentProperties TestAgent()
    {return GetDefault<ARogueliteNavigationTestCharacter>()->GetCharacterMovement()->GetNavAgentPropertiesRef();}
}
URogueliteNavigationReadinessComponent::URogueliteNavigationReadinessComponent(){PrimaryComponentTick.bCanEverTick=false;}
ARogueliteRuntimeSeedAssembly* URogueliteNavigationReadinessComponent::Rig() const{return Cast<ARogueliteRuntimeSeedAssembly>(GetOwner());}
bool URogueliteNavigationReadinessComponent::Current() const
{
    return bActive && ActiveWorld.IsValid() && GetWorld()==ActiveWorld.Get() && !GetWorld()->bIsTearingDown
        && IsValid(Rig()) && Rig()->Batch.BatchId==ActiveBatch && Rig()->bGeometryValidated;
}
bool URogueliteNavigationReadinessComponent::IsCurrentBatchReady() const
{
    return Report.bReady && IsValid(Rig()) && GetWorld() && !GetWorld()->bIsTearingDown
        && Rig()->Batch.BatchId==Report.BatchId && Rig()->bGeometryValidated && IsValid(Data)
        && Data->GetWorld()==GetWorld() && !UNavigationSystemV1::IsNavigationBeingBuiltOrLocked(GetWorld());
}
bool URogueliteNavigationReadinessComponent::Start(float GeometrySeconds)
{
    Cancel();Report={};
    auto* R=Rig();if(!R || !GetWorld() || !GetWorld()->IsGameWorld() || !R->bGeometryValidated)return false;
    bActive=true;bPathsValidated=false;AllMovesStart=0;ActiveWorld=GetWorld();ActiveBatch=R->Batch.BatchId;StartSeconds=FPlatformTime::Seconds();
    Report.BatchId=ActiveBatch;Report.Seed=R->Batch.Seed;Report.World=GetWorld()->GetPathName();Report.State=TEXT("NavigationPending");
    Report.LayoutDigest=R->Batch.LayoutDigest;Report.Rooms=R->Batch.Rooms.Num();Report.Corridors=R->Batch.Edges.Num();Report.GeometrySeconds=GeometrySeconds;
    const auto Agent=TestAgent();Report.Agent=FVector(Agent.AgentRadius,Agent.AgentHeight,Agent.AgentStepHeight);
    Navigation=FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
    if(!IsValid(Navigation) || !Navigation->IsA<URogueliteRuntimeNavigationSystem>())
    {Fail(TEXT("NavigationUnavailable"),TEXT("This world has no explicitly configured Roguelite runtime navigation system. No global configuration changed."));return false;}
    if(!FMath::IsFinite(BuildTimeoutSeconds) || BuildTimeoutSeconds<=0 || !FMath::IsFinite(MoveTimeoutSeconds) || MoveTimeoutSeconds<=0
        || !FMath::IsFinite(AcceptanceRadius) || AcceptanceRadius<1 || AcceptanceRadius>100)
    {Fail(TEXT("NavigationFailed"),TEXT("Invalid build/move timeout or acceptance radius (legal 1..100 cm). Original settings retained."));return false;}
    for(const auto& Room:R->Batch.Rooms)Report.Coverage+=Occupancy(Room);
    for(const auto& Edge:R->Batch.Edges)if(IsValid(Edge.Corridor))Report.Coverage+=Edge.Corridor->GetWorldOccupancyBounds();
    if(!Report.Coverage.IsValid){Fail(TEXT("NavigationFailed"),TEXT("No valid parametric geometry coverage."));return false;}
    Report.Coverage=Report.Coverage.ExpandBy(FVector(150,150,300));
    FActorSpawnParameters P;P.Owner=R;P.ObjectFlags|=RF_Transient;
    Bounds=GetWorld()->SpawnActor<ARogueliteRuntimeNavigationBounds>(P);
    if(!Bounds){Fail(TEXT("NavigationFailed"),TEXT("Owned navigation bounds spawn failed."));return false;}
    Bounds->Tags.Add(FName(*ActiveBatch.ToString()));Bounds->ConfigureBounds(Report.Coverage);
    Report.bCoverageComplete=Bounds->GetComponentsBoundingBox(true).ExpandBy(1).IsInsideOrOn(Report.Coverage);
    if(!Report.bCoverageComplete){Fail(TEXT("NavigationFailed"),TEXT("Owned volume does not contain complete parametric geometry coverage."));return false;}
    // Register the generation listener BEFORE requesting new bounds. Build() blocks in this
    // engine version; dynamic bounds/dirty-area updates let UE build asynchronously instead.
    Navigation->OnNavigationGenerationFinishedDelegate.AddUniqueDynamic(this,&URogueliteNavigationReadinessComponent::NavigationGenerated);
    Navigation->OnNavigationBoundsUpdated(Bounds);
    GetWorld()->GetTimerManager().SetTimer(BuildTimer,this,&URogueliteNavigationReadinessComponent::CheckBuild,.25,true);
    UE_LOG(LogRoguelitePrototype,Display,TEXT("B3B_NAV_PENDING Seed=%d Batch=%s Coverage=%s Rooms=%d Corridors=%d Agent=%s"),
        Report.Seed,*Report.BatchId.ToString(),*Report.Coverage.ToString(),Report.Rooms,Report.Corridors,*Report.Agent.ToString());
    return true;
}
void URogueliteNavigationReadinessComponent::NavigationGenerated(ANavigationData* NavData)
{
    if(Current() && IsValid(NavData) && NavData->GetWorld()==ActiveWorld.Get() && NavData->IsA<ARogueliteRuntimeRecastNavMesh>())
    {++Report.GenerationEvents;Report.Diagnostics.Add(TEXT("UE OnNavigationGenerationFinished: ")+NavData->GetPathName());}
}
void URogueliteNavigationReadinessComponent::CheckBuild()
{
    if(!Current()){Cancel();return;}
    ++Report.BoundedChecks;
    const double Elapsed=FPlatformTime::Seconds()-StartSeconds;
    if(Elapsed>BuildTimeoutSeconds){Fail(TEXT("NavigationTimedOut"),FString::Printf(TEXT("Bounded NavMesh wait exceeded %.3f seconds: Events=%d Checks=%d BusyOrLocked=%d NavData=%s. No Ready claim."),
        BuildTimeoutSeconds,Report.GenerationEvents,Report.BoundedChecks,UNavigationSystemV1::IsNavigationBeingBuiltOrLocked(GetWorld()),*GetNameSafe(Data)));return;}
    // Single-agent GetNavDataForProps returns MainNavData directly in UE 5.8.3.
    // Select from registered data only; never create a generic, mismatched fallback.
    Navigation->GetDefaultNavDataInstance(FNavigationSystem::DontCreate);
    Data=Navigation->GetNavDataForProps(TestAgent(),Report.Coverage.GetCenter());
    if(!IsValid(Data) || Report.GenerationEvents==0 || UNavigationSystemV1::IsNavigationBeingBuiltOrLocked(GetWorld()))return;
    if(!Data->IsA<ARogueliteRuntimeRecastNavMesh>() || !Navigation->NavDataSet.Contains(Data) || Data->GetWorld()!=GetWorld()
        || Data->GetRuntimeGenerationMode()!=ERuntimeGenerationType::Dynamic)
    {Fail(TEXT("NavigationUnavailable"),TEXT("Wrong/unregistered/non-dynamic NavData for current world and agent."));return;}
    Report.NavData=Data->GetPathName();Report.NavAgent=FVector(Data->GetConfig().AgentRadius,Data->GetConfig().AgentHeight,Data->GetConfig().AgentStepHeight);
    if(Report.NavAgent.X<Report.Agent.X || Report.NavAgent.Y<Report.Agent.Y)
    {Fail(TEXT("NavigationUnavailable"),TEXT("Registered navigation agent cannot accommodate actual test capsule."));return;}
    GetWorld()->GetTimerManager().ClearTimer(BuildTimer);Report.BuildSeconds=Elapsed;
    const auto PathStart=FPlatformTime::Seconds();const bool Passed=ValidatePaths();Report.PathSeconds=FPlatformTime::Seconds()-PathStart;
    if(!Passed){Fail(TEXT("NavigationFailed"),TEXT("Full-graph path/standing-anchor validation failed; see Node/Edge query evidence."));return;}
    bPathsValidated=true;AllMovesStart=FPlatformTime::Seconds();MoveIndex=0;NextMove();
}
bool URogueliteNavigationReadinessComponent::ClearCapsule(const FVector& Feet) const
{
    const auto Agent=TestAgent();FCollisionQueryParams Params(SCENE_QUERY_STAT(B3BStandingAnchor),false);
    if(Character)Params.AddIgnoredActor(Character);
    return !GetWorld()->OverlapBlockingTestByChannel(Feet+FVector(0,0,Agent.AgentHeight*.5+3),FQuat::Identity,ECC_Pawn,
        FCollisionShape::MakeCapsule(Agent.AgentRadius,Agent.AgentHeight*.5),Params);
}
bool URogueliteNavigationReadinessComponent::ChooseAnchor(int32 Node,FVector& Out)
{
    const auto* Record=Rig()->Batch.Rooms.FindByPredicate([&](const auto& R){return R.NodeId==Node;});if(!Record || !IsValid(Record->Room))return false;
    // A deterministic grid samples the actual interior. Projection is tightly bounded;
    // room origin is merely one candidate, and a POI-blocked center is never accepted.
    TArray<FVector> Candidates;
    for(const auto& Socket:Record->Room->GetDoorSockets())
        if(Socket.bValid)Candidates.Add(Socket.LocalPosition-Socket.LocalOutwardDirection*200+FVector(0,0,50));
    constexpr double Fractions[]={0.,-.25,.25,-.4,.4};
    for(double X:Fractions)for(double Y:Fractions)Candidates.Add(FVector(X*Record->Dimensions.X,Y*Record->Dimensions.Y,50));
    for(const FVector& LocalCandidate:Candidates)
    {
        const auto Candidate=Record->PlacedTransform.TransformPosition(LocalCandidate);
        FNavLocation Projected;
        if(Navigation->ProjectPointToNavigation(Candidate,Projected,FVector(60,60,150),Data)
            && Projected.NodeRef!=INVALID_NAVNODEREF && ClearCapsule(Projected.Location))
        {
            const auto Local=Record->PlacedTransform.InverseTransformPosition(Projected.Location);
            if(FMath::Abs(Local.X)<Record->Dimensions.X*.5-Report.Agent.X && FMath::Abs(Local.Y)<Record->Dimensions.Y*.5-Report.Agent.X)
            {Out=Projected.Location;Report.Diagnostics.Add(FString::Printf(TEXT("Anchor Node=%d Local=%s World=%s projected+standing capsule valid"),Node,*Local.ToString(),*Out.ToString()));return true;}
        }
    }
    Report.Diagnostics.Add(FString::Printf(TEXT("Anchor Node=%d failed: no projected standing capsule among %d door/interior candidates. Agent=%s"),Node,Candidates.Num(),*Report.Agent.ToString()));return false;
}
bool URogueliteNavigationReadinessComponent::Query(const FString& Name,int32 From,int32 To,int32 Edge)
{
    FRogueliteNavigationPathEvidence E;E.Query=Name;E.FromNode=From;E.ToNode=To;E.EdgeId=Edge;
    if(!Anchors.Contains(From) || !Anchors.Contains(To)){E.Reason=TEXT("No legal projected capsule anchor.");Report.Paths.Add(E);return false;}
    E.From=Anchors[From];E.To=Anchors[To];
    FPathFindingQuery Q(this,*Data,E.From,E.To);Q.SetAllowPartialPaths(false);
    const auto Result=Navigation->FindPathSync(TestAgent(),Q);
    E.bPartial=Result.Path.IsValid() && Result.Path->IsPartial();
    E.bSucceeded=Result.IsSuccessful() && Result.Path.IsValid() && !E.bPartial && Result.Path->GetNavigationDataUsed()==Data;
    if(Result.Path.IsValid()){E.Points=Result.Path->GetPathPoints().Num();E.Length=Result.Path->GetLength();}
    E.Reason=FString::Printf(TEXT("QueryResult=%d Partial=%d NavData=%s Agent=%s"),int32(Result.Result),E.bPartial,*Data->GetPathName(),*Report.Agent.ToString());
    Report.Paths.Add(E);
    UE_LOG(LogRoguelitePrototype,Display,TEXT("B3B_PATH Batch=%s %s Node=%d->%d Edge=%d From=%s To=%s Success=%d Partial=%d Points=%d Length=%.1f"),
        *Report.BatchId.ToString(),*Name,From,To,Edge,*E.From.ToString(),*E.To.ToString(),E.bSucceeded,E.bPartial,E.Points,E.Length);
    return E.bSucceeded;
}
bool URogueliteNavigationReadinessComponent::ValidatePaths()
{
    Anchors.Reset();Report.Paths.Reset();Report.Moves.Reset();bool Passed=true;
    const auto Graph=FRogueliteRoomGraphGenerator::Generate(Rig()->Batch.Seed,Rig()->Settings.GraphSettings());
    if(!Graph.bSucceeded || Graph.Graph.StableDigest!=Rig()->Batch.GraphDigest)
    {Report.Diagnostics.Add(TEXT("Current B1 graph does not match spawned batch graph digest."));return false;}
    for(const auto& Room:Rig()->Batch.Rooms){FVector Anchor;if(ChooseAnchor(Room.NodeId,Anchor))Anchors.Add(Room.NodeId,Anchor);else Passed=false;}
    for(const auto& Edge:Rig()->Batch.Edges)
    {
        Passed=Query(TEXT("EdgeForward"),Edge.ParentNode,Edge.ChildNode,Edge.EdgeId) && Passed;
        Passed=Query(TEXT("EdgeReverse"),Edge.ChildNode,Edge.ParentNode,Edge.EdgeId) && Passed;
    }
    Passed=Query(TEXT("StartToFinal"),Graph.Graph.StartNodeId,Graph.Graph.FinalNodeId) && Passed;
    const auto AddMove=[&](const FString& Name,int32 From,int32 To)
    {
        if(!Anchors.Contains(From) || !Anchors.Contains(To))return;
        FRogueliteNavigationMoveEvidence E;E.Test=Name;E.FromNode=From;E.ToNode=To;E.Start=Anchors[From];E.Goal=Anchors[To];Report.Moves.Add(E);
    };
    if(!Graph.Graph.Edges.IsEmpty())AddMove(TEXT("AdjacentCombat"),Graph.Graph.StartNodeId,Graph.Graph.Edges[0].NodeB);
    AddMove(TEXT("StartToFinal_TwoOrMoreCorridors"),Graph.Graph.StartNodeId,Graph.Graph.FinalNodeId);
    for(const auto& Branch:Graph.Graph.Branches)
    {
        const int32 End=Branch.NodeIds.Last();Passed=Query(TEXT("StartToBranchTerminal"),Graph.Graph.StartNodeId,End) && Passed;
        AddMove(TEXT("StartThroughJunctionToBranch"),Graph.Graph.StartNodeId,End);
        for(int32 Node:Branch.NodeIds)
        {
            const auto* Room=Rig()->Batch.Rooms.FindByPredicate([&](const auto& R){return R.NodeId==Node;});
            if(Room && Room->RoomType==ERogueliteSeedRoomType::Challenge)
            {Passed=Query(TEXT("MainToChallenge"),Branch.AttachmentNodeId,Node) && Passed;AddMove(TEXT("MainToChallenge"),Branch.AttachmentNodeId,Node);}
        }
    }
    return Passed && !Report.Moves.IsEmpty();
}
void URogueliteNavigationReadinessComponent::ReleaseTestObjects()
{
    bWaitingForMove=false;ActiveRequest=0;
    if(IsValid(Controller) && Controller->GetOwner()==GetOwner() && Controller->GetWorld()==GetWorld())
    {Controller->Verifier.Reset();Controller->StopMovement();Controller->UnPossess();Controller->Destroy();}
    if(IsValid(Character) && Character->CreatedByVerifier.Get()==this && Character->CreationBatch==Report.BatchId
        && Character->GetWorld()==GetWorld())Character->Destroy();
    Character=nullptr;Controller=nullptr;
}
void URogueliteNavigationReadinessComponent::NextMove()
{
    if(!Current() || !bPathsValidated)return;
    ReleaseTestObjects();
    if(MoveIndex>=Report.Moves.Num())
    {
        GetWorld()->GetTimerManager().ClearTimer(MoveTimer);
        Report.MoveSeconds=FPlatformTime::Seconds()-AllMovesStart;Report.TotalSeconds=FPlatformTime::Seconds()-StartSeconds;
        Report.State=TEXT("NavigationReady");Report.bReady=true;Report.Reason=TEXT("Current-batch Dynamic NavMesh, full graph paths and actual Character MoveTo completions passed. Basic geometry/navigation only; PCG content not included.");
        bActive=false;Navigation->OnNavigationGenerationFinishedDelegate.RemoveDynamic(this,&URogueliteNavigationReadinessComponent::NavigationGenerated);
        Rig()->PublishNavigationState(ActiveBatch,ERogueliteDungeonBuildStage::NavigationReady,Report.Reason);
        UE_LOG(LogRoguelitePrototype,Display,TEXT("B3B_READY Seed=%d Batch=%s Rooms=%d Corridors=%d Paths=%d Moves=%d Build=%.3f Paths=%.3f Move=%.3f Total=%.3f"),
            Report.Seed,*Report.BatchId.ToString(),Report.Rooms,Report.Corridors,Report.Paths.Num(),Report.Moves.Num(),Report.BuildSeconds,Report.PathSeconds,Report.MoveSeconds,Report.TotalSeconds);return;
    }
    auto& E=Report.Moves[MoveIndex];
    if(!ClearCapsule(E.Start)){Fail(TEXT("NavigationFailed"),TEXT("Test Character spawn anchor now blocked: ")+E.Test);return;}
    FActorSpawnParameters P;P.Owner=GetOwner();P.ObjectFlags|=RF_Transient;P.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::DontSpawnIfColliding;
    Character=GetWorld()->SpawnActor<ARogueliteNavigationTestCharacter>(E.Start+FVector(0,0,Report.Agent.Y*.5+3),FRotator::ZeroRotator,P);
    if(Character){Character->CreatedByVerifier=this;Character->CreationBatch=ActiveBatch;Character->Tags.Add(FName(*ActiveBatch.ToString()));}
    Controller=GetWorld()->SpawnActor<ARogueliteNavigationTestController>(P);
    if(!Character || !Controller){Fail(TEXT("NavigationFailed"),TEXT("Test Character/AIController spawn failed; collision was not disabled."));return;}
    Controller->Tags.Add(FName(*ActiveBatch.ToString()));
    Controller->Possess(Character);Controller->Verifier=this;Controller->VerificationBatch=ActiveBatch;
    const auto Actual=Character->GetCharacterMovement()->GetNavAgentPropertiesRef();
    Report.Diagnostics.Add(FString::Printf(TEXT("Actual Character=%s Agent=%.1fx%.1f Capsule=%.1fx%.1f MovementMode=%d CapsuleCollision=%d NavData=%s"),
        *Character->GetPathName(),Actual.AgentRadius,Actual.AgentHeight,Character->GetCapsuleComponent()->GetScaledCapsuleRadius(),Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight(),
        int32(Character->GetCharacterMovement()->MovementMode),int32(Character->GetCapsuleComponent()->GetCollisionEnabled()),*Data->GetPathName()));
    if(!FMath::IsNearlyEqual(Actual.AgentRadius,Report.Agent.X) || !FMath::IsNearlyEqual(Actual.AgentHeight,Report.Agent.Y)
        || Navigation->GetNavDataForProps(Actual,Character->GetActorLocation())!=Data)
    {Fail(TEXT("NavigationUnavailable"),TEXT("Actual possessed Character agent/NavData differs from validated test agent."));return;}
    FAIMoveRequest Request;Request.SetGoalLocation(E.Goal);Request.SetUsePathfinding(true);Request.SetAllowPartialPath(false);
    Request.SetProjectGoalLocation(false);Request.SetAcceptanceRadius(AcceptanceRadius);Request.SetReachTestIncludesAgentRadius(false);Request.SetReachTestIncludesGoalRadius(false);
    MoveStart=FPlatformTime::Seconds();FNavPathSharedPtr Path;
    const auto Result=Controller->MoveTo(Request,&Path);ActiveRequest=Result.MoveId.GetID();E.RequestId=ActiveRequest;
    if(Result.Code!=EPathFollowingRequestResult::RequestSuccessful || !Path.IsValid() || Path->IsPartial() || Path->GetNavigationDataUsed()!=Data)
    {E.Result=FString::Printf(TEXT("Request rejected Code=%d; issuing a request is not arrival success."),int32(Result.Code));Fail(TEXT("NavigationFailed"),E.Result);return;}
    bWaitingForMove=true;GetWorld()->GetTimerManager().SetTimer(MoveTimer,this,&URogueliteNavigationReadinessComponent::CheckMove,.25,true);
    UE_LOG(LogRoguelitePrototype,Display,TEXT("B3B_MOVETO_BEGIN Batch=%s Test=%s Node=%d->%d Request=%u Start=%s Goal=%s PathPoints=%d"),
        *Report.BatchId.ToString(),*E.Test,E.FromNode,E.ToNode,ActiveRequest,*E.Start.ToString(),*E.Goal.ToString(),Path->GetPathPoints().Num());
}
void URogueliteNavigationReadinessComponent::MoveCompleted(ARogueliteNavigationTestController* Source,FGuid Id,uint32 Request,const FPathFollowingResult& Result)
{
    if(!Current() || !bWaitingForMove || Id!=ActiveBatch || Source!=Controller || Source->GetWorld()!=ActiveWorld.Get() || Request!=ActiveRequest)return;
    bWaitingForMove=false;auto& E=Report.Moves[MoveIndex];E.ActualFeet=Character->GetActorLocation()-FVector(0,0,Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
    E.Distance2D=FVector::Dist2D(E.ActualFeet,E.Goal);E.Duration=FPlatformTime::Seconds()-MoveStart;E.Result=Result.ToString();
    E.bSucceeded=Result.IsSuccess() && E.Distance2D<=AcceptanceRadius+.5f && FMath::Abs(E.ActualFeet.Z-E.Goal.Z)<50;
    UE_LOG(LogRoguelitePrototype,Display,TEXT("B3B_MOVETO_END Batch=%s Test=%s Request=%u Result=%s ActualFeet=%s Distance2D=%.3f Acceptance=%.1f Seconds=%.3f Success=%d"),
        *Report.BatchId.ToString(),*E.Test,Request,*E.Result,*E.ActualFeet.ToString(),E.Distance2D,AcceptanceRadius,E.Duration,E.bSucceeded);
    if(!E.bSucceeded){Fail(TEXT("NavigationFailed"),TEXT("Actual MoveTo completion/arrival failed: ")+E.Test+TEXT(" ")+E.Result);return;}
    ++MoveIndex;
    const FGuid ExpectedBatch=ActiveBatch;const TWeakObjectPtr<UWorld> ExpectedWorld=ActiveWorld;
    NextMoveTimer=GetWorld()->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(this,[this,ExpectedBatch,ExpectedWorld]()
        {if(Current() && ExpectedBatch==ActiveBatch && ExpectedWorld==ActiveWorld)NextMove();}));
}
void URogueliteNavigationReadinessComponent::CheckMove()
{
    if(!Current()){Cancel();return;}
    if(!IsValid(Character) || !IsValid(Controller)){Fail(TEXT("NavigationFailed"),TEXT("Test Character/controller disappeared during MoveTo."));return;}
    if(bWaitingForMove && FPlatformTime::Seconds()-MoveStart>MoveTimeoutSeconds)
    {
        auto& E=Report.Moves[MoveIndex];E.Duration=FPlatformTime::Seconds()-MoveStart;E.ActualFeet=Character->GetActorLocation()-FVector(0,0,Report.Agent.Y*.5);
        E.Distance2D=FVector::Dist2D(E.ActualFeet,E.Goal);E.Result=TEXT("TimedOut");Fail(TEXT("NavigationTimedOut"),TEXT("Actual Character MoveTo exceeded timeout: ")+E.Test);
    }
}
void URogueliteNavigationReadinessComponent::Fail(const FString& State,const FString& Reason)
{
    Report.State=State;Report.bReady=false;Report.Reason=Reason;Report.TotalSeconds=FPlatformTime::Seconds()-StartSeconds;
    if(Report.BuildSeconds==0)Report.BuildSeconds=Report.TotalSeconds;
    if(AllMovesStart>0)Report.MoveSeconds=FPlatformTime::Seconds()-AllMovesStart;
    LastFailure=Report;bActive=false;
    if(GetWorld()){GetWorld()->GetTimerManager().ClearTimer(BuildTimer);GetWorld()->GetTimerManager().ClearTimer(MoveTimer);GetWorld()->GetTimerManager().ClearTimer(NextMoveTimer);}
    ReleaseTestObjects();
    if(Navigation)Navigation->OnNavigationGenerationFinishedDelegate.RemoveDynamic(this,&URogueliteNavigationReadinessComponent::NavigationGenerated);
    const auto Stage=State==TEXT("NavigationTimedOut")?ERogueliteDungeonBuildStage::NavigationTimedOut:
        State==TEXT("NavigationUnavailable")?ERogueliteDungeonBuildStage::NavigationUnavailable:ERogueliteDungeonBuildStage::NavigationFailed;
    if(Rig())Rig()->PublishNavigationState(ActiveBatch,Stage,Reason);
    UE_LOG(LogRoguelitePrototype,Warning,TEXT("B3B_NAV_FAILURE Seed=%d Batch=%s State=%s World=%s Reason=%s"),Report.Seed,*Report.BatchId.ToString(),*State,*Report.World,*Reason);
}
void URogueliteNavigationReadinessComponent::Cancel()
{
    bActive=false;bPathsValidated=false;
    if(GetWorld()){GetWorld()->GetTimerManager().ClearTimer(BuildTimer);GetWorld()->GetTimerManager().ClearTimer(MoveTimer);GetWorld()->GetTimerManager().ClearTimer(NextMoveTimer);}
    if(Navigation)Navigation->OnNavigationGenerationFinishedDelegate.RemoveDynamic(this,&URogueliteNavigationReadinessComponent::NavigationGenerated);
    ReleaseTestObjects();
    if(IsValid(Bounds) && Bounds->GetOwner()==GetOwner() && Bounds->GetWorld()==GetWorld())Bounds->Destroy();
    Bounds=nullptr;Report.bReady=false;Report.bCleanupComplete=true;
    if(LastFailure.BatchId==Report.BatchId)LastFailure.bCleanupComplete=true;
    ActiveBatch.Invalidate();ActiveWorld.Reset();
}
bool URogueliteNavigationReadinessComponent::RetryNavigation()
{
    if(!Rig() || !GetWorld() || !GetWorld()->IsGameWorld())return false;
    Rig()->PublishNavigationState(Rig()->Batch.BatchId,ERogueliteDungeonBuildStage::NavigationPending,TEXT("Explicit navigation retry revokes prior Ready; geometry rechecked."));
    if(!FRogueliteSeedLayoutAssembler::Revalidate(Rig(),Rig()->Batch))
    {Cancel();Rig()->PublishNavigationState(Rig()->Batch.BatchId,ERogueliteDungeonBuildStage::NavigationFailed,TEXT("Navigation retry rejected: geometry revalidation failed: ")+Rig()->Batch.Reason);Rig()->bGeometryValidated=false;return false;}
    return Start(Report.GeometrySeconds);
}
void URogueliteNavigationReadinessComponent::EndPlay(const EEndPlayReason::Type Reason){Cancel();Super::EndPlay(Reason);}
