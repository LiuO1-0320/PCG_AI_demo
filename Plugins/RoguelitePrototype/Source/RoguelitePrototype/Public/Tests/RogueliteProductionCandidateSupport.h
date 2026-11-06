#pragma once
#include "RogueliteFloorProfile.h"
#include "Tests/RogueliteProductionCandidateRig.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "UObject/Package.h"
namespace M1Test {
inline const TCHAR* Map=TEXT("/Game/Roguelite/Tests/P03M1/L_Roguelite_ProductionReadiness_Dev");
inline const TCHAR* Root=TEXT("/Game/Roguelite/ProductionCandidates/P03M1/");
inline FString Dir(){return FPaths::ProjectPluginsDir()/TEXT("RoguelitePrototype/Development/P03M1A/Run-20261102T102832");}
inline void Save(const FString& N,const FString& Text){FString P=Dir()/N;if(IFileManager::Get().FileExists(*P))P+=TEXT(".")+FDateTime::UtcNow().ToString(TEXT("%Y%m%d%H%M%S"))+TEXT(".rerun");FFileHelper::SaveStringToFile(Text,*P,FFileHelper::EEncodingOptions::ForceUTF8);}
template<class T>inline void Export(const FString& N,const T& V){FString S;T::StaticStruct()->ExportText(S,&V,nullptr,nullptr,PPF_None,nullptr);Save(N,S);}
inline int32 Count(UWorld* W){int32 N=0;for(TActorIterator<AActor>I(W);I;++I)N+=!I->IsActorBeingDestroyed();return N;}
inline URogueliteFloorProfile* Profile(int32 F){const FString N=FString::Printf(TEXT("DA_M1_Floor%d_Candidate"),F);return LoadObject<URogueliteFloorProfile>(nullptr,*(FString(Root)+TEXT("Profiles/")+N+TEXT(".")+N));}
inline ARogueliteProductionCandidateRig* Rig(UWorld* W){if(W)for(TActorIterator<ARogueliteProductionCandidateRig>I(W);I;++I)if(!I->IsActorBeingDestroyed())return *I;return nullptr;}
inline FString Field(const FString& Text,const FString& Name){const FString Key=Name+TEXT("=\"");const int32 I=Text.Find(Key);if(I<0)return FString();const int32 Start=I+Key.Len(),End=Text.Find(TEXT("\""),ESearchCase::CaseSensitive,ESearchDir::FromStart,Start);return Text.Mid(Start,End-Start);}
inline FString Role(const FRogueliteRoomGraph& G,int32 N,int32 F){return N==G.StartNodeId?TEXT("StartSafehouse"):N==G.FinalNodeId?(F==3?TEXT("FinalBoss"):TEXT("FloorExit")):TEXT("NormalRoom");}
inline bool Check(const FRogueliteFloorDefinition&D,const FRogueliteRoomGraph&G,const FRogueliteSeedBatch&B,const FRogueliteCommittedLoopBatch&L,FString& Rows,FString& Edges)
{
    bool Good=B.bSucceeded&&B.bGeometryValid&&B.Rooms.Num()==G.Nodes.Num();int32 Starts=0,Finals=0;TMap<ERogueliteSeedRoomType,int32> Counts;
    Rows=TEXT("Node\tRole\tRoomType\tTemplateId\tClass\tWeight\tDegree\tCapacity\tTransform\tPOI\n");
    for(const auto& R:B.Rooms){const auto* T=D.AllowedTemplates.FindByPredicate([&](const auto& X){return X.Id==R.TemplateId;});const auto& Pool=R.NodeId==G.StartNodeId?D.RoomTemplateRoles.StartTemplateIds:R.NodeId==G.FinalNodeId?D.RoomTemplateRoles.FinalTemplateIds:D.RoomTemplateRoles.NormalTemplateIds;
        bool POI=false,Baseline=false;if(IsValid(R.Room))for(const auto& M:R.Room->GetTemplateMemberStates()){if(M.Name.ToString()==TEXT("M1_BaselinePOI"))Baseline=M.bOwnedByInstance&&M.RoomLocalTransform.GetLocation().Equals(FVector(-300,-300,50),.01)&&M.RoomLocalTransform.GetScale3D().Equals(FVector(.5,.5,1),.001)&&M.StaticMeshPath==TEXT("/Engine/BasicShapes/Cube.Cube")&&M.CollisionEnabled!=0;if(M.Name.ToString().StartsWith(TEXT("M1_FixedPOI")))POI=M.bOwnedByInstance&&M.RoomLocalTransform.GetLocation().Equals(FVector(300,300,50),.01)&&M.RoomLocalTransform.GetRotation().Equals(FQuat::Identity,.001)&&M.RoomLocalTransform.GetScale3D().Equals(FVector(.5,.5,1),.001)&&M.StaticMeshPath==TEXT("/Engine/BasicShapes/Cube.Cube")&&M.CollisionEnabled!=0;}
        Good&=T&&Pool.Contains(R.TemplateId)&&T->AllowedTypes.Contains(R.RoomType)&&R.TemplateCapacity>=R.LogicalDegree&&IsValid(R.Room)&&R.Room->GetClass()==T->TemplateClass.Get()&&R.Room->GetActorScale3D().Equals(FVector::OneVector)&&R.Room->RoomHeight==500&&R.Room->EvaluateTemplate().bValid&&POI&&Baseline;
        Starts+=R.NodeId==G.StartNodeId;Finals+=R.NodeId==G.FinalNodeId;if(!(R.NodeId==G.StartNodeId&&R.RoomType==ERogueliteSeedRoomType::Combat))++Counts.FindOrAdd(R.RoomType);
        Rows+=FString::Printf(TEXT("%d\t%s\t%d\t%s\t%s\t%d\t%d\t%d\t%s\t%d\n"),R.NodeId,*Role(G,R.NodeId,D.FloorIndex),uint8(R.RoomType),*R.TemplateId.ToString(),*GetPathNameSafe(R.TemplateClass.Get()),T?T->Weight:0,R.LogicalDegree,R.TemplateCapacity,*R.PlacedTransform.ToString(),POI);
    }
    Good&=Starts==1&&Finals==1;for(const auto& Q:D.RoomTypes.Rules){const int32 N=Counts.FindRef(Q.Type);Good&=N>=Q.MinCount&&N<=Q.MaxCount;}
    Edges=TEXT("Edge\tLoop\tLength\tC2\tCapsule\tProbes\tConnectionId\n");TSet<FGuid> Ids;
    auto CheckEdge=[&](const auto& E,bool Loop){Good&=IsValid(E.Corridor)&&E.Corridor->GetActorScale3D().Equals(FVector::OneVector)&&E.Validation.bSucceeded&&E.Validation.bGeometryPassed&&E.Validation.bCapsulePassed&&E.ConnectionId.IsValid()&&!Ids.Contains(E.ConnectionId);Ids.Add(E.ConnectionId);for(const auto& P:E.Validation.Probes)Good&=P.bPassed;Edges+=FString::Printf(TEXT("%d\t%d\t%.3f\t%d\t%d\t%d\t%s\n"),E.EdgeId,Loop,E.Corridor?E.Corridor->CorridorLength:0,E.Validation.bSucceeded,E.Validation.bCapsulePassed,E.Validation.Probes.Num(),*E.ConnectionId.ToString());};
    for(const auto& E:B.Edges)CheckEdge(E,false);for(const auto& E:L.Edges)CheckEdge(E,true);Good&=IsValid(B.Registry)&&B.Registry->Connections.Num()==Ids.Num();if(B.Registry)for(const auto& C:B.Registry->Connections)Good&=Ids.Contains(C.Id)&&C.ValidationStatus==ERogueliteConnectionValidation::Passed;
    return Good;
}
}
