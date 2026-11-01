#include "Tests/RogueliteRoomRoleTestSupport.h"
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
ROGUELITEPROTOTYPE_API bool ConfigureG1TestNavigation(UWorld*);
#if WITH_DEV_AUTOMATION_TESTS
namespace G1Editor {
using namespace G1Test;
bool Own(){return GEditor&&!GEditor->PlayWorld&&GEditor->GetEditorWorldContext().World()&&GEditor->GetEditorWorldContext().World()->GetOutermost()->GetName()==Map;}
bool SaveNew(UObject* A){A->MarkPackageDirty();FSavePackageArgs Args;Args.TopLevelFlags=RF_Public|RF_Standalone;return UPackage::SavePackage(A->GetOutermost(),A,*FPackageName::LongPackageNameToFilename(A->GetOutermost()->GetName(),FPackageName::GetAssetPackageExtension()),Args);}
FString BPPath(const FString& Id){return TEXT("/Game/Roguelite/Tests/P03G1/Templates/BP_G1_Test_")+Id;}
UClass* MakeTemplate(const FString& Id,int32 Kind){
    const FString P=BPPath(Id),N=FPackageName::GetLongPackageAssetName(P);
    if(FPackageName::DoesPackageExist(P))return nullptr;
    auto* BP=FKismetEditorUtilities::CreateBlueprint(ARogueliteRoomTemplate::StaticClass(),CreatePackage(*P),*N,BPTYPE_Normal,UBlueprint::StaticClass(),UBlueprintGeneratedClass::StaticClass(),TEXT("G1TestOnly"));if(!BP)return nullptr;
    FKismetEditorUtilities::CompileBlueprint(BP);auto* CDO=BP->GeneratedClass->GetDefaultObject<ARogueliteRoomTemplate>();
    auto* Node=BP->SimpleConstructionScript->CreateNode(UStaticMeshComponent::StaticClass(),TEXT("G1_FixedPOI"));
    auto* Mesh=CastChecked<UStaticMeshComponent>(Node->ComponentTemplate);Mesh->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));Mesh->SetRelativeLocation(FVector(300,300,50));Mesh->SetRelativeScale3D(FVector(.5,.5,1));Mesh->SetCollisionProfileName(TEXT("BlockAll"));Mesh->SetMobility(EComponentMobility::Static);
    BP->SimpleConstructionScript->AddNode(Node);Node->SetParent(CDO->TemplateContentRoot);FKismetEditorUtilities::CompileBlueprint(BP);
    CDO=BP->GeneratedClass->GetDefaultObject<ARogueliteRoomTemplate>();CDO->TemplateId=*Id;CDO->TemplatePurpose=TEXT("G1_TestOnly_NotGameplay");CDO->Tags.Add(TEXT("G1_TEST_ONLY_2849b0c0"));
    CDO->RoomLength=Kind==1?3000:2000;CDO->RoomWidth=Kind==1?2500:2000;CDO->RoomHeight=Kind==1?500:400;
    CDO->NorthDoor.bEnabled=true;CDO->NorthDoor.bAutoCenter=false;CDO->NorthDoor.ManualOffset=Kind==1?350:100;
    CDO->SouthDoor.bEnabled=Kind!=2;CDO->SouthDoor.bAutoCenter=false;CDO->SouthDoor.ManualOffset=Kind==1?-300:-100;
    CDO->EastDoor.bEnabled=Kind==0;CDO->EastDoor.bAutoCenter=false;CDO->EastDoor.ManualOffset=150;CDO->WestDoor.bEnabled=false;
    // Rebuild the generated class initialization list after authored CDO changes.
    FKismetEditorUtilities::CompileBlueprint(BP);FAssetRegistryModule::AssetCreated(BP);return SaveNew(BP)?BP->GeneratedClass:nullptr;
}
struct FTrace: FOutputDevice {FString Text;FTrace(){GLog->AddOutputDevice(this);}~FTrace(){GLog->RemoveOutputDevice(this);}void Serialize(const TCHAR* V,ELogVerbosity::Type,const FName&)override{if(FCString::Strstr(V,TEXT("F1_"))||FCString::Strstr(V,TEXT("G1_")))Text+=FString(V)+TEXT("\n");}};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FG1Create,"ProjectAuthoring.G1.CreateFixtures",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FG1Create::RunTest(const FString&){using namespace G1Editor;if(!GEditor||GEditor->PlayWorld)return false;
    if(FPackageName::DoesPackageExist(Map)){AddError(TEXT("G1 map exists: refuse overwriting prior fixture"));return false;}
    const TArray<FString> Names={TEXT("Safehouse"),TEXT("Normal10"),TEXT("Normal5"),TEXT("Normal1"),TEXT("Exit"),TEXT("Boss"),TEXT("SafehouseOneDoor")};
    for(const auto& N:Names)if(FPackageName::DoesPackageExist(BPPath(N))){AddError(TEXT("G1 new template already exists: ")+N);return false;}
    for(int32 F=1;F<=3;++F)if(Profile(F)){AddError(TEXT("G1 profile already exists"));return false;}
    TMap<FString,UClass*> Classes;for(const auto& N:Names){auto* C=MakeTemplate(N,N==TEXT("Normal1")?1:N==TEXT("SafehouseOneDoor")?2:0);if(!C){AddError(TEXT("Creating new test-only template failed: ")+N);return false;}Classes.Add(N,C);}
    for(int32 F=1;F<=3;++F){const FString N=FString::Printf(TEXT("DA_G1_TestFloor%d"),F),P=TEXT("/Game/Roguelite/Tests/P03G1/Profiles/")+N;
        auto* A=NewObject<URogueliteFloorProfile>(CreatePackage(*P),*N,RF_Public|RF_Standalone);A->Rules=FRogueliteFloorProfileResolver::Defaults(F);auto& D=A->Rules;
        D.ProfileId=*FString::Printf(TEXT("G1.TestOnly.Floor%d"),F);D.DisplayName=FText::FromString(FString::Printf(TEXT("G1 TEST ONLY Floor %d Role Isolation"),F));D.SpatialPlanningVersion=3;if(F==2)D.MinRoomCount=7;
        D.RoomTemplateRoles.bEnabled=true;D.RoomTemplateRoles.StartTemplateIds={TEXT("Safehouse")};D.RoomTemplateRoles.NormalTemplateIds={TEXT("Normal10"),TEXT("Normal5"),TEXT("Normal1")};D.RoomTemplateRoles.FinalTemplateIds={F==3?FName(TEXT("Boss")):FName(TEXT("Exit"))};D.AllowedTemplates.Reset();
        for(const auto& Name:Names){if(Name==TEXT("SafehouseOneDoor"))continue;FRogueliteSeedTemplateOption T;T.Id=*Name;T.TemplateClass=Classes[Name];T.Weight=Name==TEXT("Normal10")?10:Name==TEXT("Normal5")?5:1;
            T.AllowedTypes=Name==TEXT("Safehouse")?TArray<ERogueliteSeedRoomType>{ERogueliteSeedRoomType::Combat}:Name==TEXT("Exit")||Name==TEXT("Boss")?TArray<ERogueliteSeedRoomType>{ERogueliteSeedRoomType::Final}:TArray<ERogueliteSeedRoomType>{ERogueliteSeedRoomType::Combat,ERogueliteSeedRoomType::Challenge,ERogueliteSeedRoomType::SupplyShop,ERogueliteSeedRoomType::Event};D.AllowedTemplates.Add(T);}
        const auto R=A->ResolvePreview(0);if(!R.bValid){AddError(R.ErrorCode+TEXT(": ")+R.Message);return false;}FAssetRegistryModule::AssetCreated(A);if(!SaveNew(A))return false;
    }
    auto* W=UEditorLoadingAndSavingUtils::NewBlankMap(false);if(!W)return false;W->GetWorldSettings()->DefaultGameMode=ARogueliteDevGameMode::StaticClass();W->SpawnActor<ADirectionalLight>(FVector(0,0,1000),FRotator(-45,-30,0));W->SpawnActor<ASkyLight>();W->SpawnActor<APlayerStart>(FVector(0,0,150),FRotator::ZeroRotator);
    if(!UEditorLoadingAndSavingUtils::SaveMap(W,Map)||!ConfigureG1TestNavigation(W))return false;auto* A=W->SpawnActor<ARogueliteRoomRoleTestRig>();A->SetActorLabel(TEXT("G1_TEST_ONLY_RoleIsolation_Control"));A->FloorProfile=Profile(3);A->Seed=0;A->bAutoGenerate=false;
    A->G1PrepareTemplates();if(A->PreparedTemplates.Rows.IsEmpty()){AddError(A->LastFailureReason);return false;}
    for(const auto& Name:Names){FActorSpawnParameters P;P.ObjectFlags|=RF_Transient;auto* T=W->SpawnActor<ARogueliteRoomTemplate>(Classes[Name],FVector(100000,0,0),FRotator::ZeroRotator,P);const auto V=T->EvaluateTemplate();TestTrue(TEXT("Test-only template geometry and owned POI valid: ")+Name,V.bValid&&V.OwnedGeometryCount>0);T->Destroy();}
    return TestTrue(TEXT("Only new G1 map saved"),UEditorLoadingAndSavingUtils::SaveMap(W,Map));
}
IMPLEMENT_COMPLEX_AUTOMATION_TEST(FG1Editor,"ProjectVerification.G1.Editor",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
void FG1Editor::GetTests(TArray<FString>& N,TArray<FString>& C)const{for(int32 F=1;F<=3;++F)for(int32 S=0;S<20;++S){N.Add(FString::Printf(TEXT("Floor%d.Seed%02d"),F,S));C.Add(FString::Printf(TEXT("%d,%d"),F,S));}}
bool FG1Editor::RunTest(const FString& Params){using namespace G1Editor;if(!Own())return false;TArray<FString> V;Params.ParseIntoArray(V,TEXT(","));if(V.Num()!=2)return false;
    const int32 F=FCString::Atoi(*V[0]),S=FCString::Atoi(*V[1]);auto* W=GEditor->GetEditorWorldContext().World();const int32 Before=Count(W);auto* A=W->SpawnActor<AActor>();FTrace Trace;auto* ProfileAsset=Profile(F);if(!ProfileAsset){A->Destroy();return false;}const auto D=ProfileAsset->Rules;const auto C=ProfileAsset->ResolvePreview(S);if(!C.bValid){AddError(C.ErrorCode+TEXT(" ")+C.Message);A->Destroy();return false;}
    const auto P=FRogueliteLoopAwarePlanner::Plan(A,C.Source,C.Planning,FTransform::Identity);FRogueliteSeedBatch B;FRogueliteCommittedLoopBatch L;FRogueliteLoopAwareEvidence E;bool Built=false,Good=true;
    if(P.Tree.bValid)Built=FRogueliteLoopAwarePlanner::MaterializeEditor(A,C.Source,C.Planning,P,B,L,E);const FString Stem=FString::Printf(TEXT("editor-F%d-S%02d"),F,S);Export(Stem+TEXT("-evidence.txt"),P.Tree.bValid?E:P.Evidence);
    FString Rooms,Edges;if(Built){Good&=Check(D,P.Tree.Graph,B,L,Rooms,Edges);TArray<FRogueliteCompactCorridorMeasurement>M;FString Why;Good&=FRogueliteCompactCorridorContract::Actual(B,L,M,Why);}if(P.Tree.bValid)Good&=TestTrue(TEXT("Valid plan passes real C2 materialization"),Built);
    const int32 NR=B.Rooms.Num(),NT=B.Edges.Num(),NL=L.Edges.Num();FString ClearWhy;const bool Clean=FRogueliteLoopAwarePlanner::Clear(A,B,L,ClearWhy);A->Destroy();Good&=Clean&&Count(W)==Before;
    Save(Stem+TEXT("-rooms.tsv"),Rooms);Save(Stem+TEXT("-connections.tsv"),Edges);Save(Stem+TEXT("-choices.log"),Trace.Text);Save(Stem+TEXT("-summary.tsv"),FString::Printf(TEXT("Floor\tSeed\tPlan\tBuilt\tRooms\tTree\tLoop\tLegal\tClean\tNavigation\tReason\n%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\tNotTestedInEditor\t%s\n"),F,S,P.Tree.bValid,Built,NR,NT,NL,Good,Clean&&Count(W)==Before,*(P.Tree.bValid?E.Reason:P.Evidence.Reason)));
    if(!P.Tree.bValid)AddInfo(TEXT("RETAINED_G1_SPATIAL_FAILURE ")+P.Evidence.Reason);return TestTrue(TEXT("Roles/POI/C2/owned cleanup; spatial failures retained"),Good);
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FG1Guards,"ProjectVerification.G1.GuardsAndDeterminism",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FG1Guards::RunTest(const FString&){using namespace G1Editor;if(!Own())return false;auto* W=GEditor->GetEditorWorldContext().World();const int32 Before=Count(W);auto* A=W->SpawnActor<AActor>();A->Tags.Add(TEXT("RogueliteLoopAwareTestOnly"));bool Good=true;auto D=Profile(1)->Rules;auto C=FRogueliteFloorProfileResolver::Resolve(D,0);
    const auto P=FRogueliteLoopAwarePlanner::Plan(A,C.Source,C.Planning,FTransform::Identity),Repeat=FRogueliteLoopAwarePlanner::Plan(A,C.Source,C.Planning,FTransform::Identity);Good&=TestTrue(TEXT("Same role config/seed stable actual spatial candidate order"),P.Tree.bValid&&Repeat.Tree.bValid&&P.Evidence.StablePlanDigest==Repeat.Evidence.StablePlanDigest);
    D.RoomTemplateRoles.StartTemplateIds.Reset();Good&=TestFalse(TEXT("Missing safehouse role cannot fall back to Combat"),FRogueliteFloorProfileResolver::Resolve(D,0).bValid);
    D=Profile(1)->Rules;D.AllowedTemplates.RemoveAll([](const auto& T){return T.Id==TEXT("Safehouse");});Good&=TestFalse(TEXT("Outside floor safehouse cannot be borrowed"),FRogueliteFloorProfileResolver::Resolve(D,0).bValid);
    D=Profile(1)->Rules;D.RoomTemplateRoles.StartTemplateIds={TEXT("OneDoor")};FRogueliteSeedTemplateOption T;T.Id=TEXT("OneDoor");T.TemplateClass=FSoftObjectPath(*(BPPath(TEXT("SafehouseOneDoor"))+TEXT(".BP_G1_Test_SafehouseOneDoor_C")));T.AllowedTypes={ERogueliteSeedRoomType::Combat};D.AllowedTemplates.Add(T);
    bool Found=false;FString Rows;for(int32 S=0;S<100&&!Found;++S){const auto R=FRogueliteFloorProfileResolver::Resolve(D,S);if(!R.bValid)continue;const auto G=FRogueliteRoomGraphGenerator::Generate(S,R.Source.GraphSettings());if(G.Graph.FindNode(G.Graph.StartNodeId)->Degree<2)continue;Found=true;const auto Bad=FRogueliteLoopAwarePlanner::Plan(A,R.Source,R.Planning,FTransform::Identity);Good&=TestFalse(TEXT("One-door safehouse cannot borrow normal three-door capacity"),Bad.Tree.bValid);Rows+=TEXT("SafehouseOneDoor\t")+Bad.Evidence.Reason+TEXT("\n");}Good&=TestTrue(TEXT("Actual Start degree2+ negative seed found"),Found);
    D=Profile(1)->Rules;C=FRogueliteFloorProfileResolver::Resolve(D,0);auto* Obstacle=W->SpawnActor<ARogueliteParametricRoom>();const auto Pose=Obstacle->GetActorTransform();const auto Blocked=FRogueliteLoopAwarePlanner::Plan(A,C.Source,C.Planning,FTransform::Identity);Good&=TestFalse(TEXT("Weights/roles do not bypass real occupancy"),Blocked.Tree.bValid);Good&=TestTrue(TEXT("Manual blocker transform retained"),Obstacle->GetActorTransform().Equals(Pose));Rows+=TEXT("ExistingBlocker\t")+Blocked.Evidence.Reason+TEXT("\n");Obstacle->Destroy();
    FRogueliteSeedBatch B;FRogueliteCommittedLoopBatch L;FRogueliteLoopAwareEvidence E;const auto Fresh=FRogueliteLoopAwarePlanner::Plan(A,C.Source,C.Planning,FTransform::Identity);Good&=TestTrue(TEXT("Valid fresh fault fixture"),Fresh.Tree.bValid);if(Fresh.Tree.bValid){const bool Fault=FRogueliteLoopAwarePlanner::MaterializeEditor(A,C.Source,C.Planning,Fresh,B,L,E,ERogueliteLoopAwareTestFault::CancelAfterTree);Good&=TestFalse(TEXT("Fault after Tree rejects whole batch"),Fault);Good&=TestTrue(TEXT("Fault safely clears actor/reservation ownership"),B.OwnedActors.IsEmpty()&&L.Edges.IsEmpty());Rows+=TEXT("CancelAfterTree\t")+E.Reason+TEXT("\n");}FString Why;FRogueliteLoopAwarePlanner::Clear(A,B,L,Why);
    Save(TEXT("guards-and-failures.tsv"),Rows);A->Destroy();return TestTrue(TEXT("Role guards deterministic and manual scene untouched"),Good&&Count(W)==Before);
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FG1Legacy,"ProjectVerification.G1.LegacySpatialBaselines",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FG1Legacy::RunTest(const FString&){using namespace G1Editor;if(!Own())return false;auto* W=GEditor->GetEditorWorldContext().World();const int32 Before=Count(W);auto* A=W->SpawnActor<AActor>();FString Rows=TEXT("Floor\tSeed\tValid\tStage\tPlanDigest\tTypeDigest\tReason\n");bool Good=true;
    for(int32 F=1;F<=3;++F)for(int32 S=0;S<20;++S){const FString N=F==1?TEXT("DA_Floor1_Intro"):F==2?TEXT("DA_Floor2_Expansion"):TEXT("DA_Floor3_Final");const auto* P=LoadObject<URogueliteFloorProfile>(nullptr,*(TEXT("/Game/Roguelite/FloorProfiles/")+N+TEXT(".")+N));if(!P){Good=false;continue;}Good&=!P->Rules.RoomTemplateRoles.bEnabled&&P->Rules.SpatialPlanningVersion==2;const auto C=P->ResolvePreview(S);const auto Plan=FRogueliteLoopAwarePlanner::Plan(A,C.Source,C.Planning,FTransform::Identity);Rows+=FString::Printf(TEXT("%d\t%d\t%d\t%s\t%s\t%s\t%s\n"),F,S,Plan.Tree.bValid,*Plan.Evidence.Stage,*Plan.Evidence.StablePlanDigest,*Plan.Evidence.RoomTypeDigest,*Plan.Evidence.Reason.Replace(TEXT("\n"),TEXT(" ")));}
    FString Expected;Good&=FFileHelper::LoadFileToString(Expected,*(FPaths::ProjectPluginsDir()/TEXT("RoguelitePrototype/Development/P03F1/Run-20261031-130717/legacy-spatial2-before.tsv")));Save(TEXT("legacy-spatial2-G1.tsv"),Rows);Good&=TestEqual(TEXT("Old60 spatial2 plans/failures/digests byte-identical to original F1 baseline"),Rows,Expected);
    FString Risk=TEXT("Seed\tPlan\tStage\tReason\n");for(int32 S:{6,16}){auto D=FRogueliteFloorProfileResolver::Defaults(3);D.ProfileVersion=1;D.SpatialPlanningVersion=3;for(auto& T:D.AllowedTemplates)T.Weight=T.Id==TEXT("Junction3")?1:5;const auto C=FRogueliteFloorProfileResolver::Resolve(D,S);const auto P=FRogueliteLoopAwarePlanner::Plan(A,C.Source,C.Planning,FTransform::Identity);Risk+=FString::Printf(TEXT("%d\t%d\t%s\t%s\n"),S,P.Tree.bValid,*P.Evidence.Stage,*P.Evidence.Reason);}Save(TEXT("historical-floor3-seed6-16.tsv"),Risk);A->Destroy();return TestTrue(TEXT("Old profiles/G1 opt-out and no scene residue"),Good&&Count(W)==Before);
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FG1Readback,"ProjectVerification.G1.SavedFixtureReadback",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FG1Readback::RunTest(const FString&){using namespace G1Editor;if(!Own())return false;bool Good=true;auto* A=Rig(GEditor->GetEditorWorldContext().World());Good&=A&&A->Batch.OwnedActors.IsEmpty();for(int32 F=1;F<=3;++F){auto* P=Profile(F);Good&=P&&P->Rules.RoomTemplateRoles.bEnabled&&P->Rules.RoomTemplateRoles.StartTemplateIds==TArray<FName>{TEXT("Safehouse")};if(P){Good&=P->Rules.RoomTemplateRoles.FinalTemplateIds==TArray<FName>{F==3?FName(TEXT("Boss")):FName(TEXT("Exit"))};Export(FString::Printf(TEXT("saved-profile%d.txt"),F),P->Rules);}}return TestTrue(TEXT("Saved map points to explicit test-only profiles, role fields and weights retain values"),Good);}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FG1Resume,"ProjectAuthoring.G1.CompleteOwnedFixtures",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FG1Resume::RunTest(const FString&){using namespace G1Editor;if(!Own())return false;
    for(const FString Name:{TEXT("Safehouse"),TEXT("Normal10"),TEXT("Normal5"),TEXT("Normal1"),TEXT("Exit"),TEXT("Boss"),TEXT("SafehouseOneDoor")}){const FString P=BPPath(Name);auto* BP=LoadObject<UBlueprint>(nullptr,*(P+TEXT(".")+FPackageName::GetLongPackageAssetName(P)));if(!BP||!BP->GeneratedClass){AddError(TEXT("Missing owned fixture ")+Name);return false;}auto* CDO=BP->GeneratedClass->GetDefaultObject<ARogueliteRoomTemplate>();if(CDO->TemplateId!=FName(*Name)||!CDO->ActorHasTag(TEXT("G1_TEST_ONLY_2849b0c0"))){AddError(TEXT("Ownership/default mismatch: no overwrite ")+Name);return false;}
        FKismetEditorUtilities::CompileBlueprint(BP);if(!SaveNew(BP))return false;FActorSpawnParameters SP;SP.ObjectFlags|=RF_Transient;auto* T=GEditor->GetEditorWorldContext().World()->SpawnActor<ARogueliteRoomTemplate>(BP->GeneratedClass,FVector(100000,0,0),FRotator::ZeroRotator,SP);const auto V=T->EvaluateTemplate();const bool Good=V.bValid&&V.OwnedGeometryCount==1&&T->TemplateId==FName(*Name);T->Destroy();if(!TestTrue(TEXT("Actual saved fixture instance retains ID/POI: ")+Name,Good))return false;
    }
    auto* W=GEditor->GetEditorWorldContext().World();if(!ConfigureG1TestNavigation(W))return false;auto* A=Rig(W);if(!A)A=W->SpawnActor<ARogueliteRoomRoleTestRig>();if(!A->Batch.OwnedActors.IsEmpty())return false;A->SetActorLabel(TEXT("G1_TEST_ONLY_RoleIsolation_Control"));A->FloorProfile=Profile(3);A->Seed=0;A->bAutoGenerate=false;A->G1PrepareTemplates();if(!TestTrue(TEXT("Prepared catalog uses valid test templates"),A->PreparedTemplates.Rows.Num()==6)){AddError(A->LastFailureReason);return false;}return TestTrue(TEXT("Only owned G1 map updated"),UEditorLoadingAndSavingUtils::SaveMap(W,Map));
}
#endif
