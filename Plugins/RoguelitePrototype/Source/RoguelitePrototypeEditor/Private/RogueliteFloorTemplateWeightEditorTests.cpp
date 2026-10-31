#include "RogueliteFloorProfile.h"
#include "RogueliteCompactCorridorContract.h"
#include "RogueliteDevGameMode.h"
#include "RogueliteDesignerWorkbench.h"
#include "Editor.h"
#include "FileHelpers.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Engine/DirectionalLight.h"
#include "Engine/SkyLight.h"
#include "GameFramework/PlayerStart.h"
#include "GameFramework/WorldSettings.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/PackageName.h"
#include "Misc/OutputDevice.h"
#include "HAL/FileManager.h"
ROGUELITEPROTOTYPE_API bool ConfigureF1TestNavigation(UWorld*);
#if WITH_DEV_AUTOMATION_TESTS
namespace F1Editor
{
const TCHAR* Map=TEXT("/Game/Roguelite/Maps/L_Roguelite_FloorTemplateWeights_Dev");
FString Dir(){return FPaths::ProjectPluginsDir()/TEXT("RoguelitePrototype/Development/P03F1/Run-20261031-130717");}
void Save(const FString& N,const FString& T){FFileHelper::SaveStringToFile(T,*(Dir()/N),FFileHelper::EEncodingOptions::ForceUTF8);}
bool Own(){return GEditor&&!GEditor->PlayWorld&&GEditor->GetEditorWorldContext().World()&&GEditor->GetEditorWorldContext().World()->GetOutermost()->GetName()==Map;}
int32 Count(UWorld* W){int32 N=0;for(TActorIterator<AActor>I(W);I;++I)N+=!I->IsActorBeingDestroyed();return N;}
URogueliteFloorProfile* Profile(int32 F){const FString N=F==1?TEXT("DA_Floor1_Intro"):F==2?TEXT("DA_Floor2_Expansion"):TEXT("DA_Floor3_Final");return LoadObject<URogueliteFloorProfile>(nullptr,*(TEXT("/Game/Roguelite/FloorProfiles/")+N+TEXT(".")+N));}
FRogueliteFloorDefinition Rules(int32 F){auto D=Profile(F)->Rules;D.SpatialPlanningVersion=3;for(auto& T:D.AllowedTemplates)T.Weight=T.Id==TEXT("Junction3")?(F==1?10:F==2?5:1):(F==3?5:1);return D;}
struct FTrace: FOutputDevice
{
    FString Text;FTrace(){GLog->AddOutputDevice(this);}~FTrace(){GLog->RemoveOutputDevice(this);}
    void Serialize(const TCHAR* V,ELogVerbosity::Type,const FName&)override{if(FCString::Strstr(V,TEXT("F1_")))Text+=FString(V)+TEXT("\n");}
};
FString PlanRow(int32 F,int32 S,const FRogueliteLoopAwareSpatialPlan& P)
{return FString::Printf(TEXT("%d\t%d\t%d\t%s\t%s\t%s\t%s\n"),F,S,P.Tree.bValid,*P.Evidence.Stage,*P.Evidence.StablePlanDigest,*P.Evidence.RoomTypeDigest,*P.Evidence.Reason.Replace(TEXT("\n"),TEXT(" ")));}
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FF1Map,"ProjectAuthoring.F1.CreateMap",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FF1Map::RunTest(const FString&)
{
    using namespace F1Editor;if(!GEditor||GEditor->PlayWorld)return false;
    if(FPaths::FileExists(FPackageName::LongPackageNameToFilename(Map,FPackageName::GetMapPackageExtension()))){AddError(TEXT("F1 map already exists; no overwrite"));return false;}
    auto* W=UEditorLoadingAndSavingUtils::NewBlankMap(false);if(!W)return false;
    W->GetWorldSettings()->DefaultGameMode=ARogueliteDevGameMode::StaticClass();
    W->SpawnActor<ADirectionalLight>(FVector(0,0,1000),FRotator(-45,-30,0));W->SpawnActor<ASkyLight>();W->SpawnActor<APlayerStart>(FVector(0,0,150),FRotator::ZeroRotator);
    // Give the new world its authorized package identity before the map-local guard.
    if(!TestTrue(TEXT("Create only the new F1 package"),UEditorLoadingAndSavingUtils::SaveMap(W,Map)))return false;
    if(!TestTrue(TEXT("F1 map-local navigation configuration"),ConfigureF1TestNavigation(W)))return false;auto* A=W->SpawnActor<ARogueliteRuntimeLoopAssembly>();
    A->SetActorLabel(TEXT("F1_Runtime_Control"));A->FloorProfile=Profile(3);A->bAutoGenerate=false;A->bNavigationEnabled=true;
    FString Why;const auto C=A->FloorProfile->ResolvePreview(0);if(!FRoguelitePreparedTemplateCatalog::CaptureEditor(A,C.Source,A->PreparedTemplates,Why)){AddError(Why);return false;}
    return TestTrue(TEXT("Only new F1 map saved"),UEditorLoadingAndSavingUtils::SaveMap(W,Map));
}
bool F1Legacy(FAutomationTestBase* Test,bool Capture)
{
    using namespace F1Editor;if(!Own())return false;auto* W=GEditor->GetEditorWorldContext().World();const int32 Before=Count(W);auto* A=W->SpawnActor<AActor>();
    FString Rows=TEXT("Floor\tSeed\tValid\tStage\tPlanDigest\tTypeDigest\tReason\n");bool Good=true;
    for(int32 F=1;F<=3;++F)for(int32 S=0;S<20;++S)
    {const auto C=Profile(F)->ResolvePreview(S);if(!C.bValid){Test->AddError(C.ErrorCode);Good=false;break;}const auto P=FRogueliteLoopAwarePlanner::Plan(A,C.Source,C.Planning,FTransform::Identity);Rows+=PlanRow(F,S,P);}
    A->Destroy();Good&=Test->TestEqual(TEXT("Legacy metadata probes leave no actor residue"),Count(W),Before);
    if(Capture)Save(TEXT("legacy-spatial2-before.tsv"),Rows);
    else{FString Old;Good&=FFileHelper::LoadFileToString(Old,*(Dir()/TEXT("legacy-spatial2-before.tsv")));Good&=Test->TestEqual(TEXT("All60 pre-change legacy spatial results remain byte-identical"),Rows,Old);Save(TEXT("legacy-spatial2-after.tsv"),Rows);}
    return Good;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FF1LegacyCapture,"ProjectVerification.F1.CaptureLegacy",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FF1LegacyCapture::RunTest(const FString&){return F1Legacy(this,true);}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FF1LegacyCheck,"ProjectVerification.F1.CheckLegacy",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FF1LegacyCheck::RunTest(const FString&){return F1Legacy(this,false);}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FF1Guards,"ProjectVerification.F1.GuardsAndDeterminism",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FF1Guards::RunTest(const FString&)
{
    using namespace F1Editor;if(!Own())return false;auto* W=GEditor->GetEditorWorldContext().World();const int32 Before=Count(W);auto* Owner=W->SpawnActor<AActor>();bool Good=true;
    auto D=Rules(1);auto C=FRogueliteFloorProfileResolver::Resolve(D,0);
    const auto First=FRogueliteLoopAwarePlanner::Plan(Owner,C.Source,C.Planning,FTransform::Identity),Repeat=FRogueliteLoopAwarePlanner::Plan(Owner,C.Source,C.Planning,FTransform::Identity);
    Good&=TestTrue(TEXT("New weighted planning has repeatable success and stable digest"),First.Tree.bValid&&Repeat.Tree.bValid&&First.Evidence.StablePlanDigest==Repeat.Evidence.StablePlanDigest);
    D.SpatialPlanningVersion=2;auto Old=FRogueliteFloorProfileResolver::Resolve(D,0);const auto Legacy=FRogueliteLoopAwarePlanner::Plan(Owner,Old.Source,Old.Planning,FTransform::Identity);
    Good&=TestTrue(TEXT("Only spatial version/weights change: graph and room type streams stay identical"),First.Evidence.GraphDigest==Legacy.Evidence.GraphDigest&&First.Evidence.RoomTypeDigest==Legacy.Evidence.RoomTypeDigest);
    FString Rows=TEXT("Case\tPlan\tReason\n");D=Rules(1);D.AllowedTemplates.RemoveAll([](const auto& T){return T.Id!=TEXT("Warehouse");});C=FRogueliteFloorProfileResolver::Resolve(D,0);
    const auto Insufficient=FRogueliteLoopAwarePlanner::Plan(Owner,C.Source,C.Planning,FTransform::Identity);Good&=TestFalse(TEXT("Two-socket-only floor cannot borrow Junction3 for a degree3 node"),Insufficient.Tree.bValid);
    Rows+=TEXT("WarehouseOnly\t0\t")+Insufficient.Evidence.Reason+TEXT("\n");
    D=Rules(1);D.AllowedTemplates.RemoveAll([](const auto& T){return T.Id!=TEXT("Junction3");});C=FRogueliteFloorProfileResolver::Resolve(D,0);
    const auto Only=FRogueliteLoopAwarePlanner::Plan(Owner,C.Source,C.Planning,FTransform::Identity);Good&=TestTrue(TEXT("A legal one-template floor remains usable"),Only.Tree.bValid);
    for(const auto& R:Only.Tree.Rooms)Good&=TestEqual(TEXT("No disabled Warehouse is selected"),R.Record.TemplateId,FName(TEXT("Junction3")));
    FRogueliteSeedBatch B;FRogueliteCommittedLoopBatch L;FRogueliteLoopAwareEvidence E;
    Good&=TestTrue(TEXT("Floor whitelist survives real C2 materialization"),FRogueliteLoopAwarePlanner::MaterializeEditor(Owner,C.Source,C.Planning,Only,B,L,E));
    FString Why;Good&=TestTrue(TEXT("Owned materialized floor cleanup"),FRogueliteLoopAwarePlanner::Clear(Owner,B,L,Why));
    auto* Obstacle=W->SpawnActor<ARogueliteParametricRoom>();const auto Pose=Obstacle->GetActorTransform();C=FRogueliteFloorProfileResolver::Resolve(Rules(1),0);
    const auto Blocked=FRogueliteLoopAwarePlanner::Plan(Owner,C.Source,C.Planning,FTransform::Identity);
    Good&=TestFalse(TEXT("Existing room occupancy cannot be bypassed by weight/fallback"),Blocked.Tree.bValid);
    Good&=TestTrue(TEXT("Existing blocker is preserved"),Obstacle->GetActorTransform().Equals(Pose)&&Count(W)==Before+2);
    Rows+=TEXT("ExistingBlocker\t0\t")+Blocked.Evidence.Reason+TEXT("\n");Obstacle->Destroy();
    D=Rules(1);D.SearchBudget.MaxSpatialChecks=1;C=FRogueliteFloorProfileResolver::Resolve(D,0);const auto Exhausted=FRogueliteLoopAwarePlanner::Plan(Owner,C.Source,C.Planning,FTransform::Identity);
    Good&=TestFalse(TEXT("Bounded failure cannot bypass geometry"),Exhausted.Tree.bValid);Rows+=TEXT("BudgetOne\t0\t")+Exhausted.Evidence.Reason+TEXT("\n");
    D=Rules(1);D.AllowedTemplates[0].Weight=0;Good&=TestFalse(TEXT("Zero is not a floor disable representation"),FRogueliteFloorProfileResolver::Resolve(D,0).bValid);
    Save(TEXT("forced-failure-cases.tsv"),Rows);Owner->Destroy();Good&=TestEqual(TEXT("Guards leave no generated Actors/leases"),Count(W),Before);return Good;
}
IMPLEMENT_COMPLEX_AUTOMATION_TEST(FF1Editor,"ProjectVerification.F1.Editor",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
void FF1Editor::GetTests(TArray<FString>& N,TArray<FString>& C)const{for(int32 F=1;F<=3;++F)for(int32 S=0;S<20;++S){N.Add(FString::Printf(TEXT("Floor%d.Seed%02d"),F,S));C.Add(FString::Printf(TEXT("%d,%d"),F,S));}}
bool FF1Editor::RunTest(const FString& Params)
{
    using namespace F1Editor;if(!Own())return false;TArray<FString> V;Params.ParseIntoArray(V,TEXT(","));if(V.Num()!=2)return false;
    const int32 F=FCString::Atoi(*V[0]),S=FCString::Atoi(*V[1]);auto* W=GEditor->GetEditorWorldContext().World();const int32 Before=Count(W);auto* A=W->SpawnActor<AActor>();
    FTrace Trace;const auto D=Rules(F);const auto C=FRogueliteFloorProfileResolver::Resolve(D,S);if(!C.bValid){AddError(C.ErrorCode);A->Destroy();return false;}
    const auto P=FRogueliteLoopAwarePlanner::Plan(A,C.Source,C.Planning,FTransform::Identity);FRogueliteSeedBatch B;FRogueliteCommittedLoopBatch L;FRogueliteLoopAwareEvidence E;
    bool Built=false,Good=true;if(P.Tree.bValid)Built=FRogueliteLoopAwarePlanner::MaterializeEditor(A,C.Source,C.Planning,P,B,L,E);
    const FString Stem=FString::Printf(TEXT("editor-F%d-S%02d"),F,S);FString Result;FRogueliteLoopAwareEvidence::StaticStruct()->ExportText(Result,&(P.Tree.bValid?E:P.Evidence),nullptr,nullptr,PPF_None,nullptr);Save(Stem+TEXT("-evidence.txt"),Result);
    FString Rooms=TEXT("Node\tRoomType\tTemplate\tWeight\tDegree\tCapacity\tTransform\n");
    for(const auto& R:B.Rooms){const auto* T=D.AllowedTemplates.FindByPredicate([&](const auto& X){return X.Id==R.TemplateId;});Good&=T&&T->AllowedTypes.Contains(R.RoomType)&&R.TemplateCapacity>=R.LogicalDegree;Rooms+=FString::Printf(TEXT("%d\t%d\t%s\t%d\t%d\t%d\t%s\n"),R.NodeId,uint8(R.RoomType),*R.TemplateId.ToString(),T?T->Weight:0,R.LogicalDegree,R.TemplateCapacity,*R.PlacedTransform.ToString());}
    TArray<FRogueliteCompactCorridorMeasurement> Measures;FString Why;if(Built)Good&=FRogueliteCompactCorridorContract::Actual(B,L,Measures,Why);
    if(P.Tree.bValid)Good&=TestTrue(TEXT("Valid plan must pass actual C2 materialization"),Built);
    const int32 NR=B.Rooms.Num(),NT=B.Edges.Num(),NL=L.Edges.Num();FString ClearReason;const bool Clear=FRogueliteLoopAwarePlanner::Clear(A,B,L,ClearReason);A->Destroy();Good&=TestTrue(TEXT("Owned cleanup and no map actor changes"),Clear&&Count(W)==Before);
    Save(Stem+TEXT("-rooms.tsv"),Rooms);Save(Stem+TEXT("-choices.log"),Trace.Text);
    Save(Stem+TEXT("-summary.tsv"),FString::Printf(TEXT("Floor\tSeed\tPlan\tBuilt\tRooms\tTree\tLoop\tClean\tNavigation\tReason\n%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\tNotTestedInEditor\t%s\n"),F,S,P.Tree.bValid,Built,NR,NT,NL,Clear&&Count(W)==Before,*(P.Tree.bValid?E.Reason:P.Evidence.Reason)));
    if(!P.Tree.bValid)AddInfo(TEXT("RETAINED_SPATIAL_FAILURE ")+P.Evidence.Reason);return Good;
}
#endif
