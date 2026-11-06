#include "Tests/RogueliteProductionCandidateSupport.h"
#include "Tests/RogueliteProductionCandidateRoom.h"
#include "RogueliteCompactCorridorContract.h"
#include "RogueliteDevGameMode.h"
#include "Editor.h"
#include "FileHelpers.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SCS_Node.h"
#include "Engine/StaticMesh.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/SkyLight.h"
#include "GameFramework/PlayerStart.h"
#include "GameFramework/WorldSettings.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/SavePackage.h"
#include "Misc/PackageName.h"
#include "Misc/AutomationTest.h"
#include "Misc/OutputDevice.h"
ROGUELITEPROTOTYPE_API bool ConfigureM1CandidateNavigation(UWorld*);
#if WITH_DEV_AUTOMATION_TESTS
namespace M1Editor {
using namespace M1Test;
bool Own(){return GEditor&&!GEditor->PlayWorld&&GEditor->GetEditorWorldContext().World()&&GEditor->GetEditorWorldContext().World()->GetOutermost()->GetName()==Map;}
bool SaveNew(UObject* A){A->MarkPackageDirty();FSavePackageArgs Args;Args.TopLevelFlags=RF_Public|RF_Standalone;return UPackage::SavePackage(A->GetOutermost(),A,*FPackageName::LongPackageNameToFilename(A->GetOutermost()->GetName(),FPackageName::GetAssetPackageExtension()),Args);}
FString BPPath(const FString& Id){return FString(Root)+TEXT("Templates/BP_M1_")+Id;}
const TArray<FString> Names={TEXT("Safehouse"),TEXT("Normal_Straight"),TEXT("Normal_Corner"),TEXT("Normal_Junction"),TEXT("FloorExit"),TEXT("FinalBoss")};
UClass* MakeTemplate(const FString& Id)
{
    const FString P=BPPath(Id),N=FPackageName::GetLongPackageAssetName(P);if(FPackageName::DoesPackageExist(P))return nullptr;
    auto* BP=FKismetEditorUtilities::CreateBlueprint(ARogueliteProductionCandidateRoom::StaticClass(),CreatePackage(*P),*N,BPTYPE_Normal,UBlueprint::StaticClass(),UBlueprintGeneratedClass::StaticClass(),TEXT("M1CandidateGreybox"));if(!BP)return nullptr;
    FKismetEditorUtilities::CompileBlueprint(BP);auto* CDO=BP->GeneratedClass->GetDefaultObject<ARogueliteRoomTemplate>();
    auto* Node=BP->SimpleConstructionScript->CreateNode(UStaticMeshComponent::StaticClass(),TEXT("M1_FixedPOI"));auto* Mesh=CastChecked<UStaticMeshComponent>(Node->ComponentTemplate);
    Mesh->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));Mesh->SetRelativeLocation(FVector(300,300,50));Mesh->SetRelativeScale3D(FVector(.5,.5,1));Mesh->SetCollisionProfileName(TEXT("BlockAll"));Mesh->SetMobility(EComponentMobility::Static);
    BP->SimpleConstructionScript->AddNode(Node);Node->SetParent(CDO->TemplateContentRoot);FKismetEditorUtilities::CompileBlueprint(BP);
    CDO=BP->GeneratedClass->GetDefaultObject<ARogueliteRoomTemplate>();CDO->TemplateId=*FString(TEXT("M1_" )+Id);CDO->TemplatePurpose=TEXT("Production Candidate Greybox - No Gameplay / Not Final Art");CDO->Tags.Add(TEXT("M1A_PRODUCTION_CANDIDATE_GREYBOX"));
    CDO->RoomLength=Id==TEXT("FinalBoss")?2400:Id==TEXT("FloorExit")?1800:2000;CDO->RoomWidth=CDO->RoomLength;CDO->RoomHeight=500;CDO->WallThickness=CDO->FloorThickness=20;
    for(auto* Door:{&CDO->NorthDoor,&CDO->SouthDoor,&CDO->EastDoor,&CDO->WestDoor}){Door->bEnabled=false;Door->bAutoCenter=false;Door->ManualOffset=0;Door->DoorWidth=Door->DoorHeight=500;Door->OpeningMode=ERogueliteOpeningMode::OpenPassage;}
    CDO->NorthDoor.bEnabled=true;
    if(Id==TEXT("Safehouse")||Id==TEXT("Normal_Junction")){CDO->SouthDoor.bEnabled=CDO->EastDoor.bEnabled=true;CDO->NorthDoor.ManualOffset=100;CDO->SouthDoor.ManualOffset=-100;CDO->EastDoor.ManualOffset=150;}
    else if(Id==TEXT("Normal_Straight"))CDO->SouthDoor.bEnabled=true;
    else if(Id==TEXT("Normal_Corner")){CDO->EastDoor.bEnabled=true;CDO->NorthDoor.ManualOffset=100;CDO->EastDoor.ManualOffset=-100;}
    FKismetEditorUtilities::CompileBlueprint(BP);FAssetRegistryModule::AssetCreated(BP);return SaveNew(BP)?BP->GeneratedClass:nullptr;
}
struct FTrace:FOutputDevice {FString Text;FTrace(){GLog->AddOutputDevice(this);}~FTrace(){GLog->RemoveOutputDevice(this);}void Serialize(const TCHAR* V,ELogVerbosity::Type,const FName&)override{if(FCString::Strstr(V,TEXT("F1_"))||FCString::Strstr(V,TEXT("G1_")))Text+=FString(V)+TEXT("\n");}};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FM1Create,"ProjectAuthoring.M1A.CreateCandidates",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FM1Create::RunTest(const FString&)
{
    using namespace M1Editor;if(!GEditor||GEditor->PlayWorld)return false;
    if(FPackageName::DoesPackageExist(Map)){AddError(TEXT("M1A map exists; refuse overwrite"));return false;}
    for(const auto& N:Names)if(FPackageName::DoesPackageExist(BPPath(N))){AddError(TEXT("Candidate exists; refuse overwrite: ")+N);return false;}
    for(int32 F=1;F<=3;++F)if(Profile(F)){AddError(TEXT("Candidate profile exists; refuse overwrite"));return false;}
    TMap<FString,UClass*> Classes;for(const auto& N:Names){auto* C=MakeTemplate(N);if(!C){AddError(TEXT("New candidate creation failed: ")+N);return false;}Classes.Add(N,C);}
    for(int32 F=1;F<=3;++F){const FString N=FString::Printf(TEXT("DA_M1_Floor%d_Candidate"),F),P=FString(Root)+TEXT("Profiles/")+N;
        auto* A=NewObject<URogueliteFloorProfile>(CreatePackage(*P),*N,RF_Public|RF_Standalone);A->Rules=FRogueliteFloorProfileResolver::Defaults(F);auto& D=A->Rules;
        D.ProfileId=*FString::Printf(TEXT("M1.Candidate.Floor%d"),F);D.DisplayName=FText::FromString(FString::Printf(TEXT("M1 Production Candidate Greybox / Floor %d"),F));D.ProfileVersion=4;D.SpatialPlanningVersion=3;D.ReliabilityPolicyVersion=2;D.OptionalTreeReservePercent=25;
        if(F==2){D.MinRoomCount=6;for(auto& Q:D.RoomTypes.Rules)if(Q.Type==ERogueliteSeedRoomType::Event){Q.MinCount=0;Q.MaxCount=2;Q.Weight=1;}}
        D.RoomTemplateRoles.bEnabled=true;D.RoomTemplateRoles.StartTemplateIds={TEXT("M1_Safehouse")};D.RoomTemplateRoles.NormalTemplateIds={TEXT("M1_Normal_Straight"),TEXT("M1_Normal_Corner"),TEXT("M1_Normal_Junction")};D.RoomTemplateRoles.FinalTemplateIds={F==3?FName(TEXT("M1_FinalBoss")):FName(TEXT("M1_FloorExit"))};D.AllowedTemplates.Reset();
        for(const auto& Name:Names){if((F==3&&Name==TEXT("FloorExit"))||(F!=3&&Name==TEXT("FinalBoss")))continue;FRogueliteSeedTemplateOption T;T.Id=*FString(TEXT("M1_")+Name);T.TemplateClass=Classes[Name];T.Weight=1;
            T.AllowedTypes=Name==TEXT("Safehouse")?TArray<ERogueliteSeedRoomType>{ERogueliteSeedRoomType::Combat}:Name==TEXT("FloorExit")||Name==TEXT("FinalBoss")?TArray<ERogueliteSeedRoomType>{ERogueliteSeedRoomType::Final}:F==1?TArray<ERogueliteSeedRoomType>{ERogueliteSeedRoomType::Combat}:TArray<ERogueliteSeedRoomType>{ERogueliteSeedRoomType::Combat,ERogueliteSeedRoomType::Challenge,ERogueliteSeedRoomType::SupplyShop,ERogueliteSeedRoomType::Event};D.AllowedTemplates.Add(T);}
        for(int32 S=0;S<100;++S){const auto R=A->ResolvePreview(S);if(!R.bValid){AddError(FString::Printf(TEXT("Approved quotas rejected Floor%d Seed%d: %s %s"),F,S,*R.ErrorCode,*R.Message));return false;}}
        FAssetRegistryModule::AssetCreated(A);if(!SaveNew(A))return false;Export(FString::Printf(TEXT("candidate-profile%d.txt"),F),D);
    }
    auto* W=UEditorLoadingAndSavingUtils::NewBlankMap(false);if(!W)return false;W->GetWorldSettings()->DefaultGameMode=ARogueliteDevGameMode::StaticClass();W->SpawnActor<ADirectionalLight>(FVector(0,0,1000),FRotator(-45,-30,0));W->SpawnActor<ASkyLight>();W->SpawnActor<APlayerStart>(FVector(0,0,150),FRotator::ZeroRotator);
    if(!UEditorLoadingAndSavingUtils::SaveMap(W,Map)||!ConfigureM1CandidateNavigation(W))return false;
    auto* A=W->SpawnActor<ARogueliteProductionCandidateRig>();A->SetActorLabel(TEXT("M1A_ProductionCandidate_Control"));A->FloorProfile=Profile(1);A->Seed=0;A->bAutoGenerate=false;A->M1PrepareTemplates();
    if(!TestEqual(TEXT("Prepared five allowed current-floor templates"),A->PreparedTemplates.Rows.Num(),5))return false;
    for(const auto& Name:Names){FString Why;auto* T=ARogueliteRoomTemplate::CreateTemplateInstance(W,Classes[Name],FTransform(FVector(100000,0,0)),Why);if(!TestNotNull(TEXT("Valid owned candidate: ")+Name,T)){AddError(Why);return false;}Export(TEXT("template-")+Name+TEXT("-validation.txt"),T->EvaluateTemplate());T->Destroy();}
    return TestTrue(TEXT("Only new M1A map saved"),UEditorLoadingAndSavingUtils::SaveMap(W,Map));
}
IMPLEMENT_COMPLEX_AUTOMATION_TEST(FM1Editor,"ProjectVerification.M1A.Editor",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
void FM1Editor::GetTests(TArray<FString>&N,TArray<FString>&C)const{for(int32 Phase=0;Phase<2;++Phase)for(int32 F=1;F<=3;++F)for(int32 S=0;S<(Phase?100:20);++S){N.Add(FString::Printf(TEXT("%s.Floor%d.Seed%03d"),Phase?TEXT("Extended"):TEXT("Smoke"),F,S));C.Add(FString::Printf(TEXT("%d,%d,%d"),Phase,F,S));}}
bool FM1Editor::RunTest(const FString& Params)
{
    using namespace M1Editor;if(!Own())return false;TArray<FString> V;Params.ParseIntoArray(V,TEXT(","));if(V.Num()!=3)return false;const int32 Phase=FCString::Atoi(*V[0]),F=FCString::Atoi(*V[1]),S=FCString::Atoi(*V[2]);
    auto* W=GEditor->GetEditorWorldContext().World();const int32 Before=Count(W);auto* A=W->SpawnActor<AActor>();FTrace Trace;auto* PA=Profile(F);if(!PA){A->Destroy();return false;}const auto D=PA->Rules;const auto C=PA->ResolvePreview(S);
    if(!C.bValid){AddError(C.ErrorCode+TEXT(" ")+C.Message);A->Destroy();return false;}
    const auto P=FRogueliteLoopAwarePlanner::Plan(A,C.Source,C.Planning,FTransform::Identity);FRogueliteSeedBatch B;FRogueliteCommittedLoopBatch L;FRogueliteLoopAwareEvidence E;bool Built=false,Good=true;
    if(P.Tree.bValid)Built=FRogueliteLoopAwarePlanner::MaterializeEditor(A,C.Source,C.Planning,P,B,L,E);
    const FString Stem=FString::Printf(TEXT("%s-F%d-S%03d"),Phase?TEXT("extended"):TEXT("smoke"),F,S);Export(Stem+TEXT("-plan.txt"),P.Evidence);if(P.Tree.bValid)Export(Stem+TEXT("-built.txt"),E);
    FString Rooms,Edges;if(Built){Good&=Check(D,P.Tree.Graph,B,L,Rooms,Edges);TArray<FRogueliteCompactCorridorMeasurement>M;FString Why;Good&=FRogueliteCompactCorridorContract::Actual(B,L,M,Why);}
    if(P.Tree.bValid)Good&=TestTrue(TEXT("Valid plan passes real C2 materialization"),Built);
    const int32 NR=B.Rooms.Num(),NT=B.Edges.Num(),NL=L.Edges.Num();FString Why;const bool Clean=FRogueliteLoopAwarePlanner::Clear(A,B,L,Why);A->Destroy();Good&=Clean&&Count(W)==Before;
    Save(Stem+TEXT("-rooms.tsv"),Rooms);Save(Stem+TEXT("-connections.tsv"),Edges);Save(Stem+TEXT("-choices.log"),Trace.Text);
    Save(Stem+TEXT("-summary.tsv"),FString::Printf(TEXT("Floor\tSeed\tPlan\tBuilt\tRooms\tTree\tLoop\tFallback\tLegal\tClean\tGraphDigest\tPlanDigest\tConfigDigest\tStates\tChecks\tSeconds\tNavigation\tReason\n%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%s\t%s\t%s\t%d\t%d\t%.6f\tNotTestedInEditor\t%s\n"),F,S,P.Tree.bValid,Built,NR,NT,NL,P.Evidence.bUsedOptionalTreeFallback,Good,Clean&&Count(W)==Before,*P.Evidence.GraphDigest,*P.Evidence.StablePlanDigest,*C.ConfigurationDigest,P.Evidence.BacktrackStates,P.Evidence.SpatialChecks,P.Evidence.TotalPlanningSeconds,*(P.Tree.bValid?E.Reason:P.Evidence.Reason).Replace(TEXT("\n"),TEXT(" "))));
    if(!P.Tree.bValid)AddInfo(TEXT("RETAINED_M1_SPATIAL_FAILURE ")+P.Evidence.Reason);return TestTrue(TEXT("All built geometry hard constraints and all owned cleanup; retain bounded search failures"),Good);
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FM1Guards,"ProjectVerification.M1A.QuotasGuardsDeterminism",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FM1Guards::RunTest(const FString&)
{
    using namespace M1Editor;if(!Own())return false;auto* W=GEditor->GetEditorWorldContext().World();const int32 Before=Count(W);auto* A=W->SpawnActor<AActor>();bool Good=true;int32 Six=0,EventPresent=0,EventAbsent=0;FString Rows=TEXT("Seed\tRooms\tEventCount\tOrdinaryCombat\n");
    for(int32 S=0;S<100;++S){const auto C=Profile(2)->ResolvePreview(S);Good&=C.bValid;const auto G=FRogueliteRoomGraphGenerator::Generate(S,C.Source.GraphSettings());const auto P=FRogueliteLoopAwarePlanner::Plan(A,C.Source,C.Planning,FTransform::Identity);if(!P.Tree.bValid)continue;int32 EC=0,CC=0;for(const auto& Item:P.Tree.Rooms){const auto& R=Item.Record;EC+=R.RoomType==ERogueliteSeedRoomType::Event;CC+=R.NodeId!=G.Graph.StartNodeId&&R.RoomType==ERogueliteSeedRoomType::Combat;}Six+=G.Graph.Nodes.Num()==6;EventPresent+=EC>0;EventAbsent+=EC==0;Good&=CC>=2;Rows+=FString::Printf(TEXT("%d\t%d\t%d\t%d\n"),S,G.Graph.Nodes.Num(),EC,CC);}
    Good&=TestTrue(TEXT("Approved Floor2 six rooms and optional Event both feasible"),Six>0&&EventPresent>0&&EventAbsent>0);Save(TEXT("floor2-optional-event.tsv"),Rows);
    for(int32 F=1;F<=3;++F){const auto C=Profile(F)->ResolvePreview(0);const auto P=FRogueliteLoopAwarePlanner::Plan(A,C.Source,C.Planning,FTransform::Identity),Q=FRogueliteLoopAwarePlanner::Plan(A,C.Source,C.Planning,FTransform::Identity);Good&=P.Tree.bValid==Q.Tree.bValid&&P.Evidence.StablePlanDigest==Q.Evidence.StablePlanDigest;
        auto D=Profile(F)->Rules;D.RoomTemplateRoles.StartTemplateIds.Reset();Good&=!FRogueliteFloorProfileResolver::Resolve(D,0).bValid;D=Profile(F)->Rules;D.AllowedTemplates.RemoveAll([](const auto&T){return T.Id==TEXT("M1_Safehouse");});Good&=!FRogueliteFloorProfileResolver::Resolve(D,0).bValid;
        Good&=Profile(F)->ResolveRun(1234).UsedSeed==Profile(F)->ResolveRun(1234).UsedSeed;}
    A->Destroy();Good&=Count(W)==Before;return TestTrue(TEXT("New quotas, missing-role rejection and deterministic original APIs"),Good);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FM1Residual,"ProjectVerification.M1A.OriginalR1ResidualInputs",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FM1Residual::RunTest(const FString&){using namespace M1Editor;if(!Own())return false;auto* W=GEditor->GetEditorWorldContext().World();const int32 Before=Count(W);auto* A=W->SpawnActor<AActor>();auto* RP=LoadObject<URogueliteFloorProfile>(nullptr,TEXT("/Game/Roguelite/Tests/P03R1/Profiles/DA_R1_TestFloor3.DA_R1_TestFloor3"));if(!RP){A->Destroy();return false;}bool Good=true;FString Rows=TEXT("Floor\tSeed\tPlan\tBuilt\tClean\tConfigDigest\tStates\tChecks\tReason\n");for(int32 S:{9,29,59}){const auto C=RP->ResolvePreview(S);if(!C.bValid){Good=false;continue;}const auto P=FRogueliteLoopAwarePlanner::Plan(A,C.Source,C.Planning,FTransform::Identity);FRogueliteSeedBatch B;FRogueliteCommittedLoopBatch L;FRogueliteLoopAwareEvidence E;const bool Built=P.Tree.bValid&&FRogueliteLoopAwarePlanner::MaterializeEditor(A,C.Source,C.Planning,P,B,L,E);if(P.Tree.bValid)Good&=Built;Export(FString::Printf(TEXT("original-R1-F3-S%03d-plan.txt"),S),P.Evidence);FString Why;const bool Clean=FRogueliteLoopAwarePlanner::Clear(A,B,L,Why);Good&=Clean;Rows+=FString::Printf(TEXT("3\t%d\t%d\t%d\t%d\t%s\t%d\t%d\t%s\n"),S,P.Tree.bValid,Built,Clean,*C.ConfigurationDigest,P.Evidence.BacktrackStates,P.Evidence.SpatialChecks,*P.Evidence.Reason.Replace(TEXT("\n"),TEXT(" ")));}A->Destroy();Good&=Count(W)==Before;Save(TEXT("original-R1-residual-inputs.tsv"),Rows);return TestTrue(TEXT("Original R1 inputs retained independently from new candidate results; cleanup always required"),Good);}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FM1Refresh,"ProjectAuthoring.M1A.RefreshOwnMapCatalog",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FM1Refresh::RunTest(const FString&){using namespace M1Editor;if(!Own())return false;auto* W=GEditor->GetEditorWorldContext().World();auto* A=Rig(W);if(!A||!A->Batch.OwnedActors.IsEmpty()||!A->CommittedLoops.Edges.IsEmpty())return false;bool Good=true;for(const auto& N:Names){auto* BP=LoadObject<UBlueprint>(nullptr,*BPPath(N));if(!BP||BP->ParentClass!=ARogueliteProductionCandidateRoom::StaticClass()){AddError(TEXT("Candidate native owned baseline missing: ")+N);return false;}FString Why;auto* T=ARogueliteRoomTemplate::CreateTemplateInstance(W,BP->GeneratedClass.Get(),FTransform(FVector(100000,0,0)),Why);if(!T){AddError(Why);return false;}const auto V=T->EvaluateTemplate();Good&=V.bValid&&V.OwnedGeometryCount==2;Export(TEXT("final-template-")+N+TEXT("-validation.txt"),V);T->Destroy();}A->FloorProfile=Profile(1);A->Seed=0;A->bAutoGenerate=false;A->M1PrepareTemplates();Good&=A->PreparedTemplates.Rows.Num()==5;return TestTrue(TEXT("Actual native and SCS geometry valid; only M1 map catalog saved"),Good&&UEditorLoadingAndSavingUtils::SaveMap(W,Map));}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FM1Readback,"ProjectVerification.M1A.SavedReadback",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FM1Readback::RunTest(const FString&){using namespace M1Editor;if(!Own())return false;bool Good=true;for(int32 F=1;F<=3;++F){auto* P=Profile(F);Good&=P&&P->Rules.ProfileVersion==4&&P->Rules.SpatialPlanningVersion==3&&P->Rules.RoomTemplateRoles.bEnabled&&P->Rules.ReliabilityPolicyVersion==2&&P->Rules.OptionalTreeReservePercent==25;if(P)Export(FString::Printf(TEXT("reloaded-profile%d.txt"),F),P->Rules);}auto* A=Rig(GEditor->GetEditorWorldContext().World());Good&=A&&A->Batch.OwnedActors.IsEmpty()&&A->FloorProfile==Profile(1)&&!A->bAutoGenerate;return TestTrue(TEXT("Fresh process saved candidate profiles and empty sandbox only"),Good);}
#endif
