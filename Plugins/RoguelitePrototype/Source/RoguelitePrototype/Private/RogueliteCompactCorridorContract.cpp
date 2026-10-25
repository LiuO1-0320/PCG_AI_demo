#include "RogueliteCompactCorridorContract.h"
#include "RogueliteParametricCorridor.h"
namespace {bool Length(double L){return FMath::IsFinite(L)&&L>=500&&L<=1000;}}
bool FRogueliteCompactCorridorContract::Configuration(const FRogueliteSeedLayoutSettings& S,const FRogueliteLoopAwareSettings& P,FString& Why)
{
    if(P.MinTreeCorridorLength!=MinCm||P.MaxTreeCorridorLength!=MaxCm||P.MinLoopCorridorLength!=MinCm||P.MaxLoopCorridorLength!=MaxCm||S.Corridors.IsEmpty())
    {Why=TEXT("CompactConfigurationRejected: Tree and Loop must both use500..1000cm.");return false;}
    for(const auto& C:S.Corridors){if(C.LengthCandidates.IsEmpty()){Why=TEXT("CompactEmptyLengthCandidates");return false;}for(float L:C.LengthCandidates)if(!Length(L)){Why=TEXT("CompactCandidateLengthOutOfRange");return false;}}
    Why=TEXT("Compact shared limits and candidates verified.");return true;
}
bool FRogueliteCompactCorridorContract::Plan(const FRogueliteLoopAwareSpatialPlan& P,FString& Why)
{
    if(!P.Tree.bValid){Why=TEXT("CompactPlanInvalid");return false;}
    for(const auto& E:P.Tree.Edges)if(!Length(E.Record.CorridorLength)){Why=FString::Printf(TEXT("CompactPlannedTreeOutOfRange Edge=%d Length=%.6f"),E.Record.EdgeId,E.Record.CorridorLength);return false;}
    for(const auto& E:P.Evidence.Intents)if(E.bSpatiallyFeasible&&!Length(E.CorridorLength)){Why=TEXT("CompactPlannedLoopOutOfRange");return false;}
    Why=TEXT("All planned Tree/Loop lengths within500..1000cm.");return true;
}
bool FRogueliteCompactCorridorContract::Actual(const FRogueliteSeedBatch& B,const FRogueliteCommittedLoopBatch& L,TArray<FRogueliteCompactCorridorMeasurement>& Out,FString& Why)
{
    Out.Reset();bool Good=!B.Edges.IsEmpty();
    const auto Inspect=[&](int32 Id,bool Loop,int32 NA,ERogueliteRoomSide SA,int32 NB,ERogueliteRoomSide SB,ARogueliteParametricCorridor* C)
    {
        auto& M=Out.AddDefaulted_GetRef();M.EdgeId=Id;M.bLoop=Loop;
        const auto* A=B.Rooms.FindByPredicate([&](const auto& R){return R.NodeId==NA;});const auto* D=B.Rooms.FindByPredicate([&](const auto& R){return R.NodeId==NB;});
        FRogueliteDoorSocket DA,DB;FRogueliteCorridorSocket CA,CB;
        if(!IsValid(C)||!A||!D||!IsValid(A->Room)||!IsValid(D->Room)||!A->Room->TryGetDoorSocket(SA,DA)||!D->Room->TryGetDoorSocket(SB,DB)||!C->TryGetCorridorSocket(ERogueliteCorridorEndpoint::Start,CA)||!C->TryGetCorridorSocket(ERogueliteCorridorEndpoint::End,CB))
        {M.Reason=TEXT("CompactActualSocketMissing");Good=false;return;}
        M.NativeLength=C->CorridorLength;M.SocketDistance=FVector::Distance(DA.WorldPosition,DB.WorldPosition);M.StartError=FVector::Distance(DA.WorldPosition,CA.DoorSocket.WorldPosition);M.EndError=FVector::Distance(DB.WorldPosition,CB.DoorSocket.WorldPosition);
        // C1 parameter has strict bounds. 0.01cm only tolerates world-space quaternion arithmetic.
        M.bPassed=Length(M.NativeLength)&&M.SocketDistance>=MinCm-.01&&M.SocketDistance<=MaxCm+.01&&FMath::Abs(M.SocketDistance-M.NativeLength)<=.01&&M.StartError<=.01&&M.EndError<=.01&&C->GetActorScale3D().Equals(FVector::OneVector,.0001);
        M.Reason=FString::Printf(TEXT("CompactActual Edge=%d Loop=%d C1=%.6f SocketSegment=%.6f SeamErrors=%.6f/%.6f Passed=%d"),Id,Loop,M.NativeLength,M.SocketDistance,M.StartError,M.EndError,M.bPassed);Good&=M.bPassed;
    };
    for(const auto& E:B.Edges)Inspect(E.EdgeId,false,E.ParentNode,E.ParentSocket,E.ChildNode,E.ChildSocket,E.Corridor);
    for(const auto& E:L.Edges)Inspect(E.EdgeId,true,E.NodeA,E.SocketA,E.NodeB,E.SocketB,E.Corridor);
    Why=Good?TEXT("All actual Tree/CommittedLoop corridors within500..1000cm."):TEXT("CompactActualLengthRejected");for(const auto& M:Out)if(!M.bPassed)Why+=TEXT(" | ")+M.Reason;return Good;
}
