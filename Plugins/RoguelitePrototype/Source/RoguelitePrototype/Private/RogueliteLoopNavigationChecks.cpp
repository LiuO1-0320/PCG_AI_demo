#include "RogueliteNavigationReadinessComponent.h"
#include "RogueliteRuntimeSeedAssembly.h"
#include "RogueliteNavigationTestCharacter.h"
#include "Components/CapsuleComponent.h"
#include "NavigationPath.h"
#include "NavigationSystem.h"
#include "NavMesh/RecastNavMesh.h"
#include "Engine/World.h"

bool URogueliteNavigationReadinessComponent::QueryLocated(const FString& Name,const FVector& From,const FVector& To,
    int32 Edge,ARogueliteParametricCorridor* Corridor,bool Interior)
{
    FRogueliteNavigationPathEvidence E;E.Query=Name;E.EdgeId=Edge;E.From=From;E.To=To;
    FPathFindingQuery Q(this,*Data,From,To);Q.SetAllowPartialPaths(false);
    const auto Result=Navigation->FindPathSync(Data->GetConfig(),Q);
    E.bPartial=Result.Path.IsValid()&&Result.Path->IsPartial();
    E.bSucceeded=Result.IsSuccessful()&&Result.Path.IsValid()&&!E.bPartial&&Result.Path->GetNavigationDataUsed()==Data;
    if(Result.Path.IsValid())
    {
        E.Points=Result.Path->GetPathPoints().Num();E.Length=Result.Path->GetLength();
        for(const auto& P:Result.Path->GetPathPoints())
        {
            E.Waypoints.Add(P.Location);
            if(Interior)
            {
                const auto L=Corridor->GetActorTransform().InverseTransformPosition(P.Location);
                E.bSucceeded&=FMath::Abs(L.X)<=Corridor->CorridorLength*.5+2&&FMath::Abs(L.Y)<=Corridor->CorridorWidth*.5-Report.Agent.X+20;
            }
        }
        // Reject the Tree/Loop alternative route disguising a disconnected physical seam.
        E.bSucceeded&=E.Length<=FVector::Distance(From,To)+240;
    }
    E.Reason=E.bSucceeded?TEXT("Full current NavData path through actual local corridor/seam; alternative-route detour rejected."):
        TEXT("Physical corridor segment disconnected, partial, outside its envelope or using a detour.");
    Report.Paths.Add(E);return E.bSucceeded;
}

bool URogueliteNavigationReadinessComponent::AppendPhysicalCorridor(const FRogueliteSeedEdgeRecord& Edge,bool Loop,bool AddMovement)
{
    auto* C=Edge.Corridor.Get();if(!IsValid(C))return false;
    const auto* A=Rig()->Batch.Rooms.FindByPredicate([&](const auto& R){return R.NodeId==Edge.ParentNode;});
    const auto* B=Rig()->Batch.Rooms.FindByPredicate([&](const auto& R){return R.NodeId==Edge.ChildNode;});
    FRogueliteDoorSocket DA,DB;
    if(!A||!B||!IsValid(A->Room)||!IsValid(B->Room)||!A->Room->TryGetDoorSocket(Edge.ParentSocket,DA)||!B->Room->TryGetDoorSocket(Edge.ChildSocket,DB))return false;
    const float Padding=FMath::Min(200.f,C->CorridorLength*.2f);
    TArray<FVector> Raw{DA.WorldPosition-DA.WorldOutwardDirection*200,
        C->GetActorTransform().TransformPosition(FVector(-C->CorridorLength*.5+Padding,0,0)),
        C->GetActorLocation(),C->GetActorTransform().TransformPosition(FVector(C->CorridorLength*.5-Padding,0,0)),
        DB.WorldPosition-DB.WorldOutwardDirection*200};
    TArray<FVector> Points;
    for(const auto& Position:Raw)
    {
        FNavLocation Projected;
        if(!Navigation->ProjectPointToNavigation(Position+FVector(0,0,50),Projected,FVector(40,40,100),Data)
            ||Projected.NodeRef==INVALID_NAVNODEREF||!ClearCapsule(Projected.Location))
        {Report.Diagnostics.Add(FString::Printf(TEXT("Physical corridor checkpoint invalid Edge=%d Position=%s"),Edge.EdgeId,*Position.ToString()));return false;}
        Points.Add(Projected.Location);
    }
    bool Good=true;
    for(int32 I=0;I+1<Points.Num();++I)
    {
        const auto Prefix=FString::Printf(TEXT("%sPhysical Edge%d Segment%d"),Loop?TEXT("Loop"):TEXT("Tree"),Edge.EdgeId,I);
        Good=QueryLocated(Prefix+TEXT(" Forward"),Points[I],Points[I+1],Edge.EdgeId,C,I==1||I==2)&&Good;
        Good=QueryLocated(Prefix+TEXT(" Reverse"),Points[I+1],Points[I],Edge.EdgeId,C,I==1||I==2)&&Good;
    }
    if(!Good||!AddMovement)return Good;
    for(int32 Reverse=0;Reverse<(Loop?2:1);++Reverse)
    {
        auto& E=Report.Moves.AddDefaulted_GetRef();E.Test=FString::Printf(TEXT("%sCorridorPhysical_%s_Edge%d"),Loop?TEXT("Loop"):TEXT("Tree"),Reverse?TEXT("Reverse"):TEXT("Forward"),Edge.EdgeId);
        E.FromNode=Reverse?Edge.ChildNode:Edge.ParentNode;E.ToNode=Reverse?Edge.ParentNode:Edge.ChildNode;
        E.bCorridorRoute=true;E.bLoopRoute=Loop;E.EdgeId=Edge.EdgeId;E.CorridorTransform=C->GetActorTransform();
        E.CorridorHalfSize=FVector(C->CorridorLength*.5,C->CorridorWidth*.5,C->CorridorHeight*.5);
        E.Start=Reverse?Points.Last():Points[0];E.Goal=Reverse?Points[0]:Points.Last();
        if(Reverse)for(int32 I=Points.Num()-2;I>=0;--I)E.RequiredCheckpoints.Add(Points[I]);
        else for(int32 I=1;I<Points.Num();++I)E.RequiredCheckpoints.Add(Points[I]);
    }
    return true;
}

void URogueliteNavigationReadinessComponent::SampleCorridor()
{
    if(!IsValid(Character)||!Report.Moves.IsValidIndex(MoveIndex))return;
    auto& E=Report.Moves[MoveIndex];if(!E.bCorridorRoute)return;
    const FVector Feet=Character->GetActorLocation()-FVector(0,0,Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
    const auto Local=E.CorridorTransform.InverseTransformPosition(Feet);
    if(FMath::Abs(Local.X)<=E.CorridorHalfSize.X&&FMath::Abs(Local.Y)<=E.CorridorHalfSize.Y-Report.Agent.X+1&&Local.Z>=-5&&Local.Z<50)
        if(E.InteriorSamples.IsEmpty()||FVector::DistSquared(E.InteriorSamples.Last(),Feet)>25)E.InteriorSamples.Add(Feet);
}
