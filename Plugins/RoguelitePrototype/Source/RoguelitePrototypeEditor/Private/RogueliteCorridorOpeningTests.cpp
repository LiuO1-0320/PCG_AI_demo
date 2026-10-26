#include "RogueliteDesignerWorkbench.h"
#include "RogueliteCompactCorridorContract.h"
#include "RogueliteDevGameMode.h"
#include "Misc/AutomationTest.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "FileHelpers.h"
#include "Subsystems/EditorAssetSubsystem.h"
#include "GameFramework/WorldSettings.h"
#include "GameFramework/PlayerStart.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/SkyLight.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "Tests/AutomationEditorCommon.h"
#include "UObject/UnrealType.h"
#if WITH_DEV_AUTOMATION_TESTS
namespace {
const TCHAR* HFMap=TEXT("/Game/Roguelite/Maps/L_Roguelite_CorridorOpening_Dev");
FString EvidenceDir(){static FString D=FPaths::ProjectPluginsDir()/TEXT("RoguelitePrototype/Development/P03E2HF02/Native-")+FDateTime::UtcNow().ToString(TEXT("%Y%m%d-%H%M%S"));IFileManager::Get().MakeDirectory(*D,true);return D;}
void Save(const FString& Name,const FString& Text){FFileHelper::SaveStringToFile(Text,*(EvidenceDir()/Name),FFileHelper::EEncodingOptions::ForceUTF8);}
bool HFWorld(){return GEditor&&!GEditor->PlayWorld&&GEditor->GetEditorWorldContext().World()->GetOutermost()->GetName()==HFMap;}
TMap<FString,FTransform> Scene(UWorld* W){TMap<FString,FTransform> R;for(TActorIterator<AActor> I(W);I;++I)if(!I->IsActorBeingDestroyed())R.Add(I->GetPathName(),I->GetActorTransform());return R;}
bool Same(UWorld* W,const TMap<FString,FTransform>& B){auto A=Scene(W);if(A.Num()!=B.Num())return false;for(const auto& P:B){const auto* T=A.Find(P.Key);if(!T||!T->Equals(P.Value,.0001))return false;}return true;}
FRogueliteResolvedFloor LoopConfig;FRoguelitePreparedTemplateCatalog RuntimeCatalog;bool HasLoop=false;TMap<FString,FTransform> BeforePIE;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHF02Initialize,"ProjectAuthoring.HF02.InitializeCompactProfiles",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FHF02Initialize::RunTest(const FString&)
{
    if(!GEditor||GEditor->PlayWorld)return false;
    for(const auto& Path:FDesignerWorkbench::ProfilePaths())
    {
        auto* P=LoadObject<URogueliteFloorProfile>(nullptr,*Path);TestNotNull(TEXT("Existing E2 profile required"),P);if(!P)return false;
        // Preserve every other authored field. No asset factory/default replacement.
        if(P->Rules.MinCorridorLength!=1200||P->Rules.MaxCorridorLength!=1500){P->Rules.MinCorridorLength=1200;P->Rules.MaxCorridorLength=1500;++P->Rules.ProfileVersion;P->MarkPackageDirty();TestTrue(TEXT("Save only existing authorized floor profile"),GEditor->GetEditorSubsystem<UEditorAssetSubsystem>()->SaveLoadedAsset(P,false));}
        TestTrue(TEXT("Compact resolver current"),P->ResolvePreview(0).bValid);
    }
    if(FPaths::FileExists(FPackageName::LongPackageNameToFilename(HFMap,FPackageName::GetMapPackageExtension()))){AddInfo(TEXT("Existing HF map preserved without overwrite."));return true;}
    auto* W=UEditorLoadingAndSavingUtils::NewBlankMap(false);if(!W)return false;auto* WS=W->GetWorldSettings();WS->DefaultGameMode=ARogueliteDevGameMode::StaticClass();
    W->SpawnActor<APlayerStart>(FVector(0,0,150),FRotator::ZeroRotator);W->SpawnActor<ADirectionalLight>(FVector(0,0,1000),FRotator(-45,-30,0));W->SpawnActor<ASkyLight>();
    TestTrue(TEXT("Save new HF-only map"),UEditorLoadingAndSavingUtils::SaveMap(W,HFMap));return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHF02Contract,"Roguelite.CorridorOpening.HF02HardPolicy",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FHF02Contract::RunTest(const FString&)
{
    for(int32 F=1;F<=3;++F){auto D=FRogueliteFloorProfileResolver::Defaults(F);auto C=FRogueliteFloorProfileResolver::Resolve(D,0);FString Why;TestTrue(TEXT("All floors resolve same compact policy"),C.bValid&&FRogueliteCompactCorridorContract::Configuration(C.Source,C.Planning,Why));D.MaxCorridorLength=20000;TestFalse(TEXT("Old long range rejected by floor policy only"),FRogueliteFloorProfileResolver::Resolve(D,0).bValid);auto Old=FRogueliteLoopAwareSettings();Old.bEnableLoopAwarePlanning=true;TestTrue(TEXT("Legacy planning range remains compatible"),FRogueliteLoopAwarePlanner::ValidateSettings(Old,Why));}
    FRogueliteLoopAwareSpatialPlan P;P.Tree.bValid=true;auto& E=P.Tree.Edges.AddDefaulted_GetRef();E.Record.CorridorLength=1501;FString Why;TestFalse(TEXT("Overlong planned Tree fails before spawn"),FRogueliteCompactCorridorContract::Plan(P,Why));E.Record.CorridorLength=1200;auto& I=P.Evidence.Intents.AddDefaulted_GetRef();I.bSpatiallyFeasible=true;I.CorridorLength=1501;TestFalse(TEXT("Overlong provisional Loop fails"),FRogueliteCompactCorridorContract::Plan(P,Why));I.CorridorLength=1500;TestTrue(TEXT("Exact bounds accepted"),FRogueliteCompactCorridorContract::Plan(P,Why));return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHF02Matrix,"ProjectVerification.HF02.EditorCompactMatrix",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FHF02Matrix::RunTest(const FString&)
{
    if(!HFWorld()){AddError(TEXT("HF-only map required; no writes performed."));return false;}auto* W=GEditor->GetEditorWorldContext().World();const auto Before=Scene(W);FString Rows=TEXT("Floor\tStrictLoop\tSeed\tSuccess\tRooms\tTree\tLoop\tMaxCm\tGraph\tLayout\tClear\tReason\n"),Lengths=TEXT("Floor\tStrictLoop\tSeed\tEdge\tLoop\tNativeCm\tSocketCm\tStartError\tEndError\tC2\n");
    HasLoop=false;float SavedMaxLoop=0;FString FirstLayout,FirstPhysical;FString PhysicalRows=TEXT("Floor\tStrictLoop\tSeed\tPhysicalDigest\tGeometry\n");
    for(int32 F=1;F<=3;++F)for(int32 Strict=0;Strict<=(F>1?1:0);++Strict)
    {
        auto* P=LoadObject<URogueliteFloorProfile>(nullptr,*FDesignerWorkbench::ProfilePaths()[F-1]);if(!P)return false;const auto Authored=P->Rules;
        if(Strict)P->Rules.MinLoops=1;const int32 Count=F==1?21:6;int32 Success=0,Loops=0;
        for(int32 Index=0;Index<Count;++Index)
        {
            const int32 Seed=Index==20?0:Index;auto R=FDesignerWorkbench::Generate(W,P,Seed);auto* A=R.Source.Get();int32 N=0,T=0,L=0;double Max=0;FString Graph,Layout,Physical,Geometry;
            if(F==1)TestTrue(TEXT("Floor1 actual compact generation Seed=")+FString::FromInt(Seed)+TEXT(" ")+R.RawReason,R.bSucceeded);
            if(R.bSucceeded&&A)
            {
                ++Success;N=A->Batch.Rooms.Num();T=A->Batch.Edges.Num();L=A->CommittedLoops.Edges.Num();Loops+=L;Graph=A->Batch.GraphDigest;Layout=A->Batch.LayoutDigest;TArray<FRogueliteCompactCorridorMeasurement> M;FString Why;
                TestTrue(TEXT("Every actual corridor satisfies hard bounds"),FRogueliteCompactCorridorContract::Actual(A->Batch,A->CommittedLoops,M,Why));TestTrue(TEXT("Tree count preserved"),T==N-1);if(Strict)TestEqual(TEXT("Hard MinLoops never reduced"),L,1);
                for(const auto& X:M){Max=FMath::Max(Max,X.SocketDistance);Lengths+=FString::Printf(TEXT("%d\t%d\t%d\t%d\t%d\t%.6f\t%.6f\t%.9f\t%.9f\t%d\n"),F,Strict,Seed,X.EdgeId,X.bLoop,X.NativeLength,X.SocketDistance,X.StartError,X.EndError,R.Map.bGeometryValidated);}
                for(const auto& E:A->Batch.Edges)TestTrue(TEXT("Original59 C2 probes and actual capsule"),E.Validation.Probes.Num()==59&&E.Validation.bCapsulePassed);
                for(const auto& E:A->CommittedLoops.Edges)TestTrue(TEXT("Original Loop59 C2 probes and actual capsule"),E.Validation.Probes.Num()==59&&E.Validation.bCapsulePassed);
                TArray<FString> Items;for(const auto& Room:A->Batch.Rooms)Items.Add(TEXT("R|")+Room.Room->GetActorTransform().ToString()+TEXT("|")+Room.Dimensions.ToString());for(const auto& Edge:A->Batch.Edges)Items.Add(TEXT("C|")+Edge.Corridor->GetActorTransform().ToString()+FString::Printf(TEXT("|%.3f,%.3f,%.3f"),Edge.Corridor->CorridorLength,Edge.Corridor->CorridorWidth,Edge.Corridor->CorridorHeight));for(const auto& Edge:A->CommittedLoops.Edges)Items.Add(TEXT("C|")+Edge.Corridor->GetActorTransform().ToString()+FString::Printf(TEXT("|%.3f,%.3f,%.3f"),Edge.Corridor->CorridorLength,Edge.Corridor->CorridorWidth,Edge.Corridor->CorridorHeight));Items.Sort();for(const auto& Item:Items)Geometry+=Item+TEXT(";");Physical=FRogueliteRoomGraphGenerator::StableTextDigest(Geometry);
                if(F==1&&Index==0){FirstLayout=Layout;FirstPhysical=Physical;}if(F==1&&Index==20)TestEqual(TEXT("Same physical geometry independent of GUID/seed text"),Physical,FirstPhysical);if(F==1&&Index==20)TestEqual(TEXT("Compact layout repeat deterministic"),Layout,FirstLayout);
                if(L==1&&A->CommittedLoops.Edges[0].Corridor->CorridorLength>SavedMaxLoop){SavedMaxLoop=A->CommittedLoops.Edges[0].Corridor->CorridorLength;LoopConfig=P->ResolvePreview(Seed);HasLoop=FRoguelitePreparedTemplateCatalog::CaptureEditor(A,LoopConfig.Source,RuntimeCatalog,Why);TestTrue(TEXT("Same actual template set prepared for runtime"),HasLoop);FString Fixture;FRogueliteResolvedFloor::StaticStruct()->ExportText(Fixture,&LoopConfig,nullptr,nullptr,PPF_None,nullptr);Save(TEXT("runtime-config.txt"),Fixture);}
                if(F==1&&Index==0){auto* C=A->Batch.Edges[0].Corridor.Get();const float Old=C->CorridorLength;C->CorridorLength=1501;TArray<FRogueliteCompactCorridorMeasurement> Reject;TestFalse(TEXT("Post-spawn outlier can never Validate success"),FRogueliteCompactCorridorContract::Actual(A->Batch,A->CommittedLoops,Reject,Why));C->CorridorLength=Old;}
            }
            auto Cleared=FDesignerWorkbench::Clear(W);TestTrue(TEXT("Only batch cleanup, original scene intact"),Cleared.bSucceeded&&Same(W,Before));
            const FString Line=FString::Printf(TEXT("%d\t%d\t%d\t%d\t%d\t%d\t%d\t%.6f\t%s\t%s\t%d\t%s\n"),F,Strict,Seed,R.bSucceeded,N,T,L,Max,*Graph,*Layout,Cleared.bSucceeded,*R.RawReason);Rows+=Line;UE_LOG(LogTemp,Display,TEXT("HF02_CASE %s"),*Line);Save(TEXT("cases.tsv"),Rows);Save(TEXT("lengths.tsv"),Lengths);PhysicalRows+=FString::Printf(TEXT("%d\t%d\t%d\t%s\t%s\n"),F,Strict,Seed,*Physical,*Geometry);Save(TEXT("physical.tsv"),PhysicalRows);
        }
        AddInfo(FString::Printf(TEXT("HF02_SUMMARY Floor=%d Strict=%d Success=%d/%d CommittedLoops=%d"),F,Strict,Success,Count,Loops));P->Rules=Authored;
    }
    AddInfo(HasLoop?TEXT("Compact runtime loop fixture selected from actual successful seed."):TEXT("No compact loop succeeded; runtime loop/nav cannot be claimed. Core optimization needs separate authorization."));return true;
}
#endif
