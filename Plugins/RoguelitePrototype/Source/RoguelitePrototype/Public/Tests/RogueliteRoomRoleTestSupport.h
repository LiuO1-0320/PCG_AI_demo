#pragma once
#include "RogueliteFloorProfile.h"
#include "Tests/RogueliteRoomRoleTestRig.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"
namespace G1Test {
inline const TCHAR* Map=TEXT("/Game/Roguelite/Tests/P03G1/L_Roguelite_RoomRoles_Dev");
inline FString Dir(){return FPaths::ProjectPluginsDir()/TEXT("RoguelitePrototype/Development/P03G1/Run-20261101-2849b0c0");}
inline void Save(const FString& N,const FString& V){FFileHelper::SaveStringToFile(V,*(Dir()/N),FFileHelper::EEncodingOptions::ForceUTF8);}
template<class T>inline void Export(const FString& N,const T& V){FString Text;T::StaticStruct()->ExportText(Text,&V,nullptr,nullptr,PPF_None,nullptr);Save(N,Text);}
inline int32 Count(UWorld* W){int32 N=0;for(TActorIterator<AActor>I(W);I;++I)N+=!I->IsActorBeingDestroyed();return N;}
inline ARogueliteRoomRoleTestRig* Rig(UWorld* W){if(W)for(TActorIterator<ARogueliteRoomRoleTestRig>I(W);I;++I)if(!I->IsActorBeingDestroyed())return *I;return nullptr;}
inline URogueliteFloorProfile* Profile(int32 F){const FString N=FString::Printf(TEXT("DA_G1_TestFloor%d"),F);return LoadObject<URogueliteFloorProfile>(nullptr,*(TEXT("/Game/Roguelite/Tests/P03G1/Profiles/")+N+TEXT(".")+N));}
inline FString Role(const FRogueliteRoomGraph& G,int32 N,int32 F){return N==G.StartNodeId?TEXT("StartSafehouse"):N==G.FinalNodeId?(F==3?TEXT("FinalBoss"):TEXT("FloorExit")):TEXT("NormalRoom");}
inline bool Check(const FRogueliteFloorDefinition& D,const FRogueliteRoomGraph& G,const FRogueliteSeedBatch& B,const FRogueliteCommittedLoopBatch& L,FString& Rows,FString& EdgeRows){
    bool Good=B.bSucceeded&&B.bGeometryValid&&B.Rooms.Num()==G.Nodes.Num();int32 StartCount=0,FinalCount=0,Combat=0;
    Rows=TEXT("Node\tRole\tRoomType\tTemplate\tWeight\tDegree\tCapacity\tTransform\tPOI\n");
    for(const auto& R:B.Rooms){const auto* T=D.AllowedTemplates.FindByPredicate([&](const auto& X){return X.Id==R.TemplateId;});
        const auto& Pool=R.NodeId==G.StartNodeId?D.RoomTemplateRoles.StartTemplateIds:R.NodeId==G.FinalNodeId?D.RoomTemplateRoles.FinalTemplateIds:D.RoomTemplateRoles.NormalTemplateIds;
        bool POI=false;if(IsValid(R.Room)){const auto Members=R.Room->GetTemplateMemberStates();for(const auto& M:Members)if(M.Name.ToString().StartsWith(TEXT("G1_FixedPOI")))POI=M.bOwnedByInstance&&M.RoomLocalTransform.GetLocation().Equals(FVector(300,300,50),.01)&&M.RoomLocalTransform.GetRotation().Equals(FQuat::Identity,.001)&&M.RoomLocalTransform.GetScale3D().Equals(FVector(.5,.5,1),.001)&&M.StaticMeshPath==TEXT("/Engine/BasicShapes/Cube.Cube")&&M.CollisionEnabled!=0;}
        Good&=T&&Pool.Contains(R.TemplateId)&&T->AllowedTypes.Contains(R.RoomType)&&R.TemplateCapacity>=R.LogicalDegree&&IsValid(R.Room)&&R.Room->GetActorScale3D().Equals(FVector::OneVector)&&R.Room->EvaluateTemplate().bValid&&POI;
        StartCount+=R.NodeId==G.StartNodeId;FinalCount+=R.NodeId==G.FinalNodeId;Combat+=R.NodeId!=G.StartNodeId&&R.RoomType==ERogueliteSeedRoomType::Combat;
        Rows+=FString::Printf(TEXT("%d\t%s\t%d\t%s\t%d\t%d\t%d\t%s\t%d\n"),R.NodeId,*Role(G,R.NodeId,D.FloorIndex),uint8(R.RoomType),*R.TemplateId.ToString(),T?T->Weight:0,R.LogicalDegree,R.TemplateCapacity,*R.PlacedTransform.ToString(),POI);
    }
    const auto* Q=D.RoomTypes.Rules.FindByPredicate([](const auto& X){return X.Type==ERogueliteSeedRoomType::Combat;});Good&=Q&&Combat>=Q->MinCount&&Combat<=Q->MaxCount&&StartCount==1&&FinalCount==1;
    EdgeRows=TEXT("Edge\tLoop\tLength\tC2\tCapsule\tProbes\tConnectionId\n");TSet<FGuid> Ids;
    auto ECheck=[&](const auto& E,bool Loop){Good&=IsValid(E.Corridor)&&E.Corridor->GetActorScale3D().Equals(FVector::OneVector)&&E.Validation.bSucceeded&&E.Validation.bGeometryPassed&&E.Validation.bCapsulePassed&&!Ids.Contains(E.ConnectionId)&&E.ConnectionId.IsValid();Ids.Add(E.ConnectionId);for(const auto& P:E.Validation.Probes)Good&=P.bPassed;EdgeRows+=FString::Printf(TEXT("%d\t%d\t%.3f\t%d\t%d\t%d\t%s\n"),E.EdgeId,Loop,E.Corridor?E.Corridor->CorridorLength:0,E.Validation.bSucceeded,E.Validation.bCapsulePassed,E.Validation.Probes.Num(),*E.ConnectionId.ToString());};
    for(const auto& E:B.Edges)ECheck(E,false);for(const auto& E:L.Edges)ECheck(E,true);
    Good&=IsValid(B.Registry)&&B.Registry->Connections.Num()==Ids.Num();if(B.Registry)for(const auto& C:B.Registry->Connections)Good&=Ids.Contains(C.Id)&&C.ValidationStatus==ERogueliteConnectionValidation::Passed;
    return Good;
}
}
