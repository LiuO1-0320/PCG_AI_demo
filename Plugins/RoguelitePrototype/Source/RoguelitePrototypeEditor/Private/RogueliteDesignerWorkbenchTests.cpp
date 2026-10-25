#include "RogueliteDesignerWorkbench.h"
#include "RogueliteDevGameMode.h"
#include "Misc/AutomationTest.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "FileHelpers.h"
#include "AssetToolsModule.h"
#include "Factories/DataAssetFactory.h"
#include "Subsystems/EditorAssetSubsystem.h"
#include "Engine/DirectionalLight.h"
#include "Engine/SkyLight.h"
#include "GameFramework/PlayerStart.h"
#include "GameFramework/WorldSettings.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "Tests/AutomationEditorCommon.h"
#if WITH_DEV_AUTOMATION_TESTS
namespace {
const TCHAR* Map=TEXT("/Game/Roguelite/Maps/L_Roguelite_DesignerWorkbench_Dev");
TMap<FString,FTransform> Scene(UWorld* W){TMap<FString,FTransform> R;for(TActorIterator<AActor> I(W);I;++I)if(!I->IsActorBeingDestroyed())R.Add(I->GetPathName(),I->GetActorTransform());return R;}
bool Same(UWorld* W,const TMap<FString,FTransform>& Before){auto Now=Scene(W);if(Now.Num()!=Before.Num())return false;for(const auto& P:Before){auto* T=Now.Find(P.Key);if(!T||!T->Equals(P.Value))return false;}return true;}
bool TestMap(){return GEditor&&!GEditor->PlayWorld&&GEditor->GetEditorWorldContext().World()->GetOutermost()->GetName()==Map;}
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorkbenchAuthor,"ProjectAuthoring.E2.InitializeWorkbench",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FWorkbenchAuthor::RunTest(const FString&)
{
    if(!GEditor||GEditor->PlayWorld)return false;auto Paths=FDesignerWorkbench::ProfilePaths();
    for(int32 I=0;I<Paths.Num();++I)
    {
        if(auto* Existing=LoadObject<URogueliteFloorProfile>(nullptr,*Paths[I])){AddInfo(TEXT("Existing profile preserved: ")+Paths[I]);continue;}
        FString Package=FPackageName::ObjectPathToPackageName(Paths[I]);auto* Factory=NewObject<UDataAssetFactory>();Factory->DataAssetClass=URogueliteFloorProfile::StaticClass();
        auto* P=Cast<URogueliteFloorProfile>(FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get().CreateAsset(FPackageName::GetShortName(Package),FPackageName::GetLongPackagePath(Package),URogueliteFloorProfile::StaticClass(),Factory));
        TestNotNull(TEXT("New profile created via actual Editor asset factory"),P);if(!P)return false;
        P->Rules=FRogueliteFloorProfileResolver::Defaults(I+1);P->MarkPackageDirty();FString Why;TestTrue(TEXT("New profile saved: ")+Why,FDesignerWorkbench::SaveProfile(P,0,Why));
    }
    auto Filename=FPackageName::LongPackageNameToFilename(Map,FPackageName::GetMapPackageExtension());
    if(FPaths::FileExists(Filename)){AddInfo(TEXT("Existing new-map file protected; no overwrite."));return true;}
    auto* W=UEditorLoadingAndSavingUtils::NewBlankMap(false);if(!W)return false;W->GetWorldSettings()->DefaultGameMode=ARogueliteDevGameMode::StaticClass();
    W->SpawnActor<APlayerStart>(FVector(0,0,150),FRotator::ZeroRotator);W->SpawnActor<ADirectionalLight>(FVector(0,0,1000),FRotator(-45,-30,0));W->SpawnActor<ASkyLight>();
    TestTrue(TEXT("Only new Workbench map saved"),UEditorLoadingAndSavingUtils::SaveMap(W,Map));return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorkbenchMulti,"ProjectVerification.E2.EditorProfilesAndOwnership",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FWorkbenchMulti::RunTest(const FString&)
{
    if(!TestMap()){AddError(TEXT("New Workbench test map required; nothing changed."));return false;}auto* W=GEditor->GetEditorWorldContext().World();
    const auto Before=Scene(W);FString Records,FirstLayout,FirstGraph;FRogueliteDungeonMapSnapshot First;
    for(int32 Floor=1;Floor<=3;++Floor)
    {
        auto* P=LoadObject<URogueliteFloorProfile>(nullptr,*FDesignerWorkbench::ProfilePaths()[Floor-1]);if(!P){AddError(TEXT("Profile missing"));return false;}
        const int32 Runs=Floor==1?21:6;int32 Passed=0;
        for(int32 I=0;I<Runs;++I)
        {
            const int32 Seed=I==20?0:I;auto R=FDesignerWorkbench::Generate(W,P,Seed);auto* A=R.Source.Get();
            int32 N=A?A->Batch.Rooms.Num():0,T=A?A->Batch.Edges.Num():0,L=A?A->CommittedLoops.Edges.Num():0;
            const FString Graph=A?A->Batch.GraphDigest:TEXT(""),Layout=A?A->Batch.LayoutDigest:TEXT("");
            if(Floor==1){TestTrue(TEXT("Floor1 actual generation seed ")+FString::FromInt(Seed)+TEXT(": ")+R.RawReason,R.bSucceeded);if(R.bSucceeded){TestEqual(TEXT("Exactly4 rooms"),N,4);TestEqual(TEXT("Exactly3 tree corridors"),T,3);TestEqual(TEXT("No loop"),L,0);}}
            if(R.bSucceeded)
            {
                ++Passed;TestTrue(TEXT("Actual current C2 reservations, capsule probes and traversal"),R.Map.bGeometryValidated&&R.Map.bValid&&!R.Map.bDungeonReady&&R.Map.Connections.Num()==T+L);
                TSet<FGuid> Ids;for(const auto& E:R.Map.Connections){TestTrue(TEXT("Unique authority and actual connection"),E.bReservationActive&&E.bLastGeometryValidated&&E.ConnectionId.IsValid()&&!Ids.Contains(E.ConnectionId));Ids.Add(E.ConnectionId);}
                for(const auto& Room:R.Map.Rooms){auto Local=URogueliteDungeonMapQuery::GetLocalMapView(A,Room.NodeId);TestTrue(TEXT("One hop incidence"),Local.bLocated&&Local.Connections.Num()==Room.Degree);}
                if(Floor==1&&I==0)
                {
                    FirstLayout=Layout;FirstGraph=Graph;First=R.Map;
                    TestFalse(TEXT("Generate refuses stacking"),FDesignerWorkbench::Generate(W,P,Seed).bSucceeded);
                    auto Old=P->Rules;P->Rules.EncounterDifficultyProfileId=TEXT("Unsaved_Freshness_Test");TestFalse(TEXT("Profile changes invalidate old graph"),URogueliteDungeonMapQuery::Capture(A).bValid);P->Rules=Old;
                    TestTrue(TEXT("Restore original in-memory rules without asset save"),FDesignerWorkbench::Validate(W).bSucceeded);
                }
                if(Floor==1&&I==20){TestEqual(TEXT("Layout deterministic across new GUIDs"),Layout,FirstLayout);TestEqual(TEXT("Graph deterministic"),Graph,FirstGraph);TestNotEqual(TEXT("New batch identity"),R.Map.BatchId,First.BatchId);}
            }
            const auto Snapshot=R.Map;auto Cleared=FDesignerWorkbench::Clear(W);TestTrue(TEXT("Own-batch clear"),Cleared.bSucceeded);TestTrue(TEXT("User actors remain"),Same(W,Before));
            if(A)TestFalse(TEXT("Old source invalid after clear"),URogueliteDungeonMapQuery::IsSnapshotCurrent(A,Snapshot));
            const FString Line=FString::Printf(TEXT("E2_CASE Floor=%d Seed=%d Success=%d Rooms=%d Tree=%d Loop=%d Graph=%s Layout=%s Batch=%s C2=%d Clear=%d Reason=%s"),Floor,Seed,R.bSucceeded,N,T,L,*Graph,*Layout,*R.Map.BatchId.ToString(),R.Map.bGeometryValidated,Cleared.bSucceeded,*R.RawReason);
            UE_LOG(LogTemp,Display,TEXT("%s"),*Line);Records+=Line+TEXT("\n");
        }
        AddInfo(FString::Printf(TEXT("E2_FLOOR_SUMMARY Floor=%d Success=%d/%d"),Floor,Passed,Runs));
    }
    // A separate rig and an unattached manual actor must remain untouched.
    auto* Foreign=W->SpawnActor<ARogueliteLoopAwareTestRig>();auto* Manual=W->SpawnActor<AActor>();auto ForeignScene=Scene(W);
    TestTrue(TEXT("Empty workbench clear does not touch foreign rig/actor"),FDesignerWorkbench::Clear(W).bSucceeded&&Same(W,ForeignScene));Foreign->Destroy();Manual->Destroy();
    TestTrue(TEXT("Final original scene unchanged"),Same(W,Before));
    FString Dir=FPaths::ProjectPluginsDir()/TEXT("RoguelitePrototype/Development/P03E2/Native-")+FDateTime::UtcNow().ToString(TEXT("%Y%m%d-%H%M%S"));IFileManager::Get().MakeDirectory(*Dir,true);FFileHelper::SaveStringToFile(Records,*(Dir/TEXT("profile-cases.log")),FFileHelper::EEncodingOptions::ForceUTF8);return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorkbenchFailures,"ProjectVerification.E2.FailuresAndForeignBatch",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FWorkbenchFailures::RunTest(const FString&)
{
    if(!TestMap())return false;auto* W=GEditor->GetEditorWorldContext().World();const auto Before=Scene(W);
    auto* P=LoadObject<URogueliteFloorProfile>(nullptr,*FDesignerWorkbench::ProfilePaths()[0]);if(!P)return false;auto Rules=P->Rules;
    const auto Inventory=FDesignerWorkbench::TemplateInfo(W,P,0);TestTrue(TEXT("Actual authored POI inventory, no scene residue"),Inventory.Contains(TEXT("自有成员"))&&Same(W,Before));
    P->Rules.MaxRoomCount=5;auto Bad=FDesignerWorkbench::Generate(W,P,0);TestTrue(TEXT("Invalid hard constraints rejected with reason"),!Bad.bSucceeded&&!Bad.RawReason.IsEmpty()&&Same(W,Before));P->Rules=Rules;
    P->Rules.SearchBudget.MaxSpatialChecks=1;auto Exhausted=FDesignerWorkbench::Generate(W,P,0);TestTrue(TEXT("Finite-budget failure cleans own attempted source"),!Exhausted.bSucceeded&&Same(W,Before));AddInfo(TEXT("E2_EXPECTED_FAILURE ")+Exhausted.RawReason);P->Rules=Rules;
    auto* Foreign=W->SpawnActor<ARogueliteLoopAwareTestRig>();auto C=P->ResolvePreview(0);Foreign->Settings=C.Source;Foreign->Planning=C.Planning;
    auto Plan=FRogueliteLoopAwarePlanner::Plan(Foreign,C.Source,C.Planning,FTransform::Identity);
    TestTrue(TEXT("Independent foreign actual batch"),FRogueliteLoopAwarePlanner::MaterializeEditor(Foreign,C.Source,C.Planning,Plan,Foreign->Batch,Foreign->CommittedLoops,Foreign->Latest));
    const auto ForeignBefore=Scene(W);const auto Id=Foreign->Batch.BatchId;
    auto Refused=FDesignerWorkbench::Generate(W,P,0);TestTrue(TEXT("Other generator batch not stacked or changed"),!Refused.bSucceeded&&Refused.RawReason==TEXT("ForeignBatchProtected")&&Same(W,ForeignBefore));
    TestTrue(TEXT("Clear leaves foreign actual C2 batch intact"),FDesignerWorkbench::Clear(W).bSucceeded&&Same(W,ForeignBefore)&&Foreign->Batch.BatchId==Id&&URogueliteDungeonMapQuery::Capture(Foreign).bValid);
    FString Why;TestTrue(TEXT("Foreign owner's own cleanup"),FRogueliteLoopAwarePlanner::Clear(Foreign,Foreign->Batch,Foreign->CommittedLoops,Why));Foreign->Destroy();TestTrue(TEXT("All original actors preserved"),Same(W,Before));return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorkbenchLoaded,"ProjectVerification.E2.RevalidateLoadedWorkbench",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FWorkbenchLoaded::RunTest(const FString&)
{
    if(!TestMap())return false;auto R=FDesignerWorkbench::Validate(GEditor->GetEditorWorldContext().World());TestTrue(TEXT("Actual saved loaded workbench validated"),R.bSucceeded);TestTrue(TEXT("Loaded graph remains current Editor geometry, not DungeonReady"),R.Map.bValid&&R.Map.WorldKind==TEXT("EditorWorld")&&!R.Map.bDungeonReady);return true;
}
namespace {FRoguelitePreparedTemplateCatalog RuntimeCatalog;TMap<int32,FRogueliteFloorDefinition> RuntimeRules;TMap<FString,FTransform> EditorBeforePIE;}
class FWorkbenchRuntimeCommand : public IAutomationLatentCommand
{
    FAutomationTestBase* T;double Began=FPlatformTime::Seconds();
public:
    FWorkbenchRuntimeCommand(FAutomationTestBase* In):T(In){}
    virtual bool Update() override
    {
        if(!GEditor||!GEditor->PlayWorld||!GEditor->PlayWorld->HasBegunPlay()){if(FPlatformTime::Seconds()-Began>30){T->AddError(TEXT("PIE failed to start"));return true;}return false;}
        auto* W=GEditor->PlayWorld.Get();const auto Before=Scene(W);auto* Owner=W->SpawnActor<AActor>();
        if(!Owner){T->AddError(TEXT("Runtime owner allocation failed"));return true;}
        for(int32 Floor=1;Floor<=3;++Floor)for(int32 Seed=0;Seed<2;++Seed)
        {
            auto C=FRogueliteFloorProfileResolver::Resolve(RuntimeRules[Floor],Seed);FRogueliteSeedBatch B;FRogueliteCommittedLoopBatch L;FRogueliteRuntimeLoopResult Out;
            const bool Good=FRogueliteFloorProfileResolver::GenerateRuntime(Owner,C,ERogueliteFloorValidationPurpose::DeveloperValidation,RuntimeCatalog,B,L,Out);
            if(Floor==1)T->TestTrue(TEXT("Shared config actual PIE geometry: ")+Out.Reason,Good);
            if(Good){T->TestTrue(TEXT("Actual deferred final registration and traversal"),Out.bRegistrationPassed&&Out.bProbeFreePlanning&&Out.Traversal.bGeometryValidated&&Out.Traversal.bCurrent);for(const auto& R:B.Rooms)T->TestTrue(TEXT("Unit scale at runtime"),R.Room&&R.Room->GetActorScale3D().Equals(FVector::OneVector));}
            UE_LOG(LogTemp,Display,TEXT("E2_RUNTIME Floor=%d Seed=%d Success=%d Rooms=%d Tree=%d Loop=%d Layout=%s Geometry=%d Navigation=NotTested Reason=%s"),Floor,Seed,Good,B.Rooms.Num(),B.Edges.Num(),L.Edges.Num(),*B.LayoutDigest,Out.Traversal.bGeometryValidated,*Out.Reason);
            FString Why;T->TestTrue(TEXT("Runtime own cleanup"),FRogueliteRuntimeLoopAssembler::Clear(Owner,B,L,Why));
        }
        Owner->Destroy();T->TestTrue(TEXT("Runtime scene restored"),Same(W,Before));return true;
    }
};
class FWorkbenchPIEStopCommand : public IAutomationLatentCommand
{
    FAutomationTestBase* T;
public:FWorkbenchPIEStopCommand(FAutomationTestBase* In):T(In){}
    virtual bool Update() override{if(GEditor->PlayWorld)return false;T->TestTrue(TEXT("PIE generated nothing in Editor map"),Same(GEditor->GetEditorWorldContext().World(),EditorBeforePIE));return true;}
};
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorkbenchRuntime,"ProjectVerification.E2.RuntimeSharedConfig",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FWorkbenchRuntime::RunTest(const FString&)
{
    if(!TestMap()){AddError(TEXT("New Workbench map, outside PIE required"));return false;}auto* W=GEditor->GetEditorWorldContext().World();
    EditorBeforePIE=Scene(W);auto* ProbeOwner=W->SpawnActor<AActor>();FString Why;
    for(int32 I=1;I<=3;++I){auto* P=LoadObject<URogueliteFloorProfile>(nullptr,*FDesignerWorkbench::ProfilePaths()[I-1]);if(!P)return false;RuntimeRules.Add(I,P->Rules);}
    auto C=FRogueliteFloorProfileResolver::Resolve(RuntimeRules[1],0);const bool Captured=FRoguelitePreparedTemplateCatalog::CaptureEditor(ProbeOwner,C.Source,RuntimeCatalog,Why);ProbeOwner->Destroy();
    TestTrue(TEXT("Authored prepared template catalog"),Captured);if(!Captured)return false;
    FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FStartPIECommand>(false));
    FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FWorkbenchRuntimeCommand>(this));
    FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FEndPlayMapCommand>());
    FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FWorkbenchPIEStopCommand>(this));return true;
}
#endif
