#include "RogueliteDesignerWorkbench.h"
#include "RogueliteDesignerPanel.h"
#include "RogueliteDevGameMode.h"
#include "RogueliteRuntimeLoopAssembly.h"
#include "Tests/RogueliteProductionCandidateSupport.h"
#include "Editor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Engine/DirectionalLight.h"
#include "Engine/SkyLight.h"
#include "GameFramework/PlayerStart.h"
#include "GameFramework/WorldSettings.h"
#include "FileHelpers.h"
#include "PackageTools.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/ScopeExit.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Widgets/Text/STextBlock.h"

ROGUELITEPROTOTYPE_API bool ConfigureM1BWorkbenchNavigation(UWorld*);

#if WITH_DEV_AUTOMATION_TESTS
namespace M1BEditor
{
const TCHAR* Map=TEXT("/Game/Roguelite/Tests/P03M1B/L_Roguelite_WorkbenchCandidates_Dev");
FString Dir(){FString Path;FParse::Value(FCommandLine::Get(),TEXT("M1BEvidenceDir="),Path);return Path.IsEmpty()?FPaths::ProjectPluginsDir()/TEXT("RoguelitePrototype/Development/P03M1B/Native"):Path;}
void Save(const FString& Name,const FString& Text){IFileManager::Get().MakeDirectory(*Dir(),true);FString Path=Dir()/Name;if(IFileManager::Get().FileExists(*Path))Path+=TEXT(".")+FDateTime::UtcNow().ToString(TEXT("%Y%m%d%H%M%S"))+TEXT(".rerun");FFileHelper::SaveStringToFile(Text,*Path,FFileHelper::EEncodingOptions::ForceUTF8);}
URogueliteFloorProfile* Profile(int32 Floor,bool Candidate=true){const auto Paths=FDesignerWorkbench::ProfilePaths();const int32 Index=Floor-1+(Candidate?3:0);return Paths.IsValidIndex(Index)?LoadObject<URogueliteFloorProfile>(nullptr,*Paths[Index]):nullptr;}
bool Own(){return GEditor&&!GEditor->PlayWorld&&GEditor->GetEditorWorldContext().World()&&GEditor->GetEditorWorldContext().World()->GetOutermost()->GetName()==Map;}
TMap<FString,FTransform> Scene(UWorld* W){TMap<FString,FTransform> Out;for(TActorIterator<AActor>I(W);I;++I)if(!I->IsActorBeingDestroyed())Out.Add(I->GetPathName(),I->GetActorTransform());return Out;}
bool Same(UWorld* W,const TMap<FString,FTransform>& Before){const auto Now=Scene(W);if(Now.Num()!=Before.Num())return false;for(const auto& R:Before){const auto* T=Now.Find(R.Key);if(!T||!T->Equals(R.Value))return false;}return true;}
FString Text(const FRogueliteFloorDefinition& Rules){FString Out;FRogueliteFloorDefinition::StaticStruct()->ExportText(Out,&Rules,nullptr,nullptr,PPF_None,nullptr);return Out;}
void WidgetText(const TSharedRef<SWidget>& W,FString& Out,int32 Depth=0){if(Depth>36||Out.Len()>100000)return;if(W->GetTypeAsString()==TEXT("STextBlock"))Out+=StaticCastSharedRef<STextBlock>(W)->GetText().ToString()+TEXT("\n");auto* Children=W->GetChildren();for(int32 I=0;I<Children->Num();++I)WidgetText(Children->GetChildAt(I),Out,Depth+1);}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FM1BCreate,"ProjectVerification.M1B.CreateIndependentMap",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FM1BCreate::RunTest(const FString&)
{
    using namespace M1BEditor;if(!GEditor||GEditor->PlayWorld)return false;
    if(FPackageName::DoesPackageExist(Map))return TestNotNull(TEXT("Read existing M1B map without overwriting"),UEditorLoadingAndSavingUtils::LoadMap(Map));
    auto* W=UEditorLoadingAndSavingUtils::NewBlankMap(false);if(!W)return false;
    W->GetWorldSettings()->DefaultGameMode=ARogueliteDevGameMode::StaticClass();
    W->SpawnActor<ADirectionalLight>(FVector(0,0,1000),FRotator(-45,-30,0));W->SpawnActor<ASkyLight>();W->SpawnActor<APlayerStart>(FVector(0,0,150),FRotator::ZeroRotator);
    if(!UEditorLoadingAndSavingUtils::SaveMap(W,Map)||!ConfigureM1BWorkbenchNavigation(W))return false;
    auto* A=W->SpawnActor<ARogueliteRuntimeLoopAssembly>();A->SetActorLabel(TEXT("M1B_Runtime_Control"));A->FloorProfile=Profile(1);A->Seed=0;A->bAutoGenerate=false;
    auto* Manual=W->SpawnActor<AActor>();Manual->SetActorLabel(TEXT("M1B_Manual_Protected"));
    return TestTrue(TEXT("Only the independent M1B map is saved"),UEditorLoadingAndSavingUtils::SaveMap(W,Map));
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FM1BProfiles,"ProjectVerification.M1B.SixProfilesAndPanel",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FM1BProfiles::RunTest(const FString&)
{
    using namespace M1BEditor;if(!Own())return false;bool Good=TestEqual(TEXT("Six explicit preset paths"),FDesignerWorkbench::ProfilePaths().Num(),6);FString Rows=TEXT("Family\tFloor\tPath\tDigest\tTemplates\tProfileVersion\tSpatial\tReliability\n");
    for(int32 Family=0;Family<2;++Family)for(int32 Floor=1;Floor<=3;++Floor)
    {
        auto* P=Profile(Floor,Family==1);if(!TestNotNull(TEXT("Actual selected profile"),P))return false;const auto Digest=FRogueliteFloorProfileResolver::Digest(P->Rules);
        auto Session=MakeShared<FDesignerWorkbenchSession>();Session->Profile=P;Session->PreviewSeed=0;
        auto Panel=SNew(SDesignerWorkbenchPanel,Session,[](AActor*){});FString Visible;WidgetText(Panel,Visible);
        Good&=TestTrue(TEXT("Construct/reopen preserves exact selected asset"),Session->Profile.Get()==P&&P->Rules.FloorIndex==Floor);
        Good&=TestTrue(TEXT("UI family/floor label present"),Visible.Contains(FString::Printf(TEXT("%s / Floor %d"),Family?TEXT("Production Candidate"):TEXT("Legacy"),Floor)));
        Good&=TestTrue(TEXT("Weight matrix uses selected family"),Family?Visible.Contains(TEXT("M1_Safehouse")):!Visible.Contains(TEXT("M1_Safehouse")));
        Good&=TestTrue(TEXT("Panel does not edit the profile while selecting"),FRogueliteFloorProfileResolver::Digest(P->Rules)==Digest);
        Rows+=FString::Printf(TEXT("%s\t%d\t%s\t%s\t%d\t%d\t%d\t%d\n"),Family?TEXT("Candidate"):TEXT("Legacy"),Floor,*P->GetPathName(),*Digest,P->Rules.AllowedTemplates.Num(),P->Rules.ProfileVersion,P->Rules.SpatialPlanningVersion,P->Rules.ReliabilityPolicyVersion);
        Save(FString::Printf(TEXT("panel-%d-%d.txt"),Family,Floor),Visible);Save(FString::Printf(TEXT("profile-%d-%d.txt"),Family,Floor),Text(P->Rules));
    }
    FString Why;auto* Transient=NewObject<URogueliteFloorProfile>();Transient->Rules=Profile(1)->Rules;
    Good&=TestFalse(TEXT("Non-allowlisted assets cannot be saved"),FDesignerWorkbench::SaveProfile(Transient,0,Why));Save(TEXT("six-profiles.tsv"),Rows);return Good;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FM1BQuotas,"ProjectVerification.M1B.ResolverQuotaSemantics",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FM1BQuotas::RunTest(const FString&)
{
    using namespace M1BEditor;if(!Own())return false;bool Good=true;FString Rows;
    for(int32 Floor=1;Floor<=3;++Floor){auto D=Profile(Floor)->Rules;for(int32 Seed=0;Seed<6;++Seed){const auto C=FRogueliteFloorProfileResolver::Resolve(D,Seed);Good&=TestTrue(TEXT("Actual candidate resolver"),C.bValid);Rows+=FString::Printf(TEXT("Floor%d Seed%d Valid%d %s\n"),Floor,Seed,C.bValid,*C.ErrorCode);}}
    auto D=Profile(1)->Rules;for(auto& Q:D.RoomTypes.Rules)if(Q.Type==ERogueliteSeedRoomType::Combat)Q.MaxCount=2;
    Good&=TestTrue(TEXT("G1 4 rooms = safehouse + two ordinary Combat + Final"),FRogueliteFloorProfileResolver::Resolve(D,0).bValid);
    D.RoomTemplateRoles.bEnabled=false;Good&=TestFalse(TEXT("Legacy Combat max2 cannot fit three Combat-tagged nodes"),FRogueliteFloorProfileResolver::Resolve(D,0).bValid);
    auto F2=Profile(2)->Rules;F2.MinRoomCount=F2.MaxRoomCount=6;Good&=TestTrue(TEXT("Six-room Floor2 with optional Event"),FRogueliteFloorProfileResolver::Resolve(F2,0).bValid);
    auto* Event=F2.RoomTypes.Rules.FindByPredicate([](const auto& Q){return Q.Type==ERogueliteSeedRoomType::Event;});Good&=TestTrue(TEXT("Event stays optional with positive weight"),Event&&Event->MinCount==0&&Event->MaxCount>0&&Event->Weight>0);
    Event->MinCount=1;Good&=TestFalse(TEXT("Seven mandatory roles/types cannot fit six nodes"),FRogueliteFloorProfileResolver::Resolve(F2,0).bValid);
    auto F3=Profile(3)->Rules;for(auto Type:{ERogueliteSeedRoomType::Challenge,ERogueliteSeedRoomType::SupplyShop,ERogueliteSeedRoomType::Event})Good&=TestTrue(TEXT("Floor3 special type minimum"),F3.RoomTypes.Rules.ContainsByPredicate([&](const auto& Q){return Q.Type==Type&&Q.MinCount>=1;}));
    for(auto& Q:F3.RoomTypes.Rules)if(Q.Type==ERogueliteSeedRoomType::Combat)Q.MinCount=Q.MaxCount=20;const auto Bad=FRogueliteFloorProfileResolver::Resolve(F3,0);Good&=TestTrue(TEXT("Impossible ordinary quotas rejected with diagnostic"),!Bad.bValid&&!Bad.ErrorCode.IsEmpty());
    Rows+=TEXT("Expected invalid quota: ")+Bad.ErrorCode+TEXT(" ")+Bad.Message;Save(TEXT("resolver-quotas.txt"),Rows);return Good;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FM1BSave,"ProjectVerification.M1B.SelectedCandidateWeightSaveReloadRestore",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FM1BSave::RunTest(const FString&)
{
    using namespace M1BEditor;if(!Own())return false;auto* P=Profile(1);if(!P)return false;const auto Original=P->Rules;const FString Before=FRogueliteFloorProfileResolver::Digest(Original);FString Why;bool Restored=false;
    ON_SCOPE_EXIT {if(!Restored){if(auto* R=Profile(1)){R->Modify();R->Rules=Original;FString RestoreWhy;if(!FDesignerWorkbench::SaveProfile(R,0,RestoreWhy))AddError(TEXT("Candidate restore failed: ")+RestoreWhy);}}};
    Save(TEXT("weight-before.txt"),Text(Original));P->Modify();auto* Entry=P->Rules.AllowedTemplates.FindByPredicate([](const auto& V){return V.Id==TEXT("M1_Normal_Straight");});if(!Entry)return false;const int32 NewWeight=Entry->Weight+7;Entry->Weight=NewWeight;
    bool Good=TestTrue(TEXT("Workbench saves only the explicitly passed candidate"),FDesignerWorkbench::SaveProfile(P,0,Why));FText Error;
    Good&=TestTrue(TEXT("Actual saved package reloaded"),UPackageTools::ReloadPackages({P->GetOutermost()},Error,EReloadPackagesInteractionMode::AssumePositive));P=Profile(1);
    const auto* Saved=P?P->Rules.AllowedTemplates.FindByPredicate([](const auto& V){return V.Id==TEXT("M1_Normal_Straight");}):nullptr;
    Good&=TestTrue(TEXT("Weight persisted on disk, not merely in Details memory"),Saved&&Saved->Weight==NewWeight);if(P)Save(TEXT("weight-saved-reloaded.txt"),Text(P->Rules));
    if(!P)return false;P->Modify();P->Rules=Original;Good&=TestTrue(TEXT("Original candidate configuration restored by Editor API"),FDesignerWorkbench::SaveProfile(P,0,Why));
    Good&=TestTrue(TEXT("Restored saved package reloaded"),UPackageTools::ReloadPackages({P->GetOutermost()},Error,EReloadPackagesInteractionMode::AssumePositive));P=Profile(1);
    Restored=P&&FRogueliteFloorProfileResolver::Digest(P->Rules)==Before;Good&=TestTrue(TEXT("Final M1A configuration exactly unchanged"),Restored);if(P)Save(TEXT("weight-restored-reloaded.txt"),Text(P->Rules));return Good;
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FM1BEditorCases,"ProjectVerification.M1B.Editor",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
void FM1BEditorCases::GetTests(TArray<FString>& Names,TArray<FString>& Commands)const
{for(const TCHAR* V:{TEXT("CandidateF1"),TEXT("CandidateF2Loop"),TEXT("CandidateF2Fallback"),TEXT("CandidateF3Boss"),TEXT("G1TightCombatMax"),TEXT("LegacyCombatQuota"),TEXT("FailureCleanup"),TEXT("SwitchAndProtect")}){Names.Add(V);Commands.Add(V);}}
bool FM1BEditorCases::RunTest(const FString& Case)
{
    using namespace M1BEditor;if(!Own())return false;auto* W=GEditor->GetEditorWorldContext().World();const auto Before=Scene(W);bool Good=true;FString Rows;
    ON_SCOPE_EXIT {FDesignerWorkbench::Clear(W);};
    int32 Floor=Case.Contains(TEXT("F3"))?3:Case.Contains(TEXT("F2"))?2:1;const int32 Seed=Case.Contains(TEXT("Fallback"))?3:Case==TEXT("FailureCleanup")?75:0;if(Case==TEXT("FailureCleanup"))Floor=2;
    auto* Source=Profile(Floor,Case!=TEXT("LegacyCombatQuota"));if(!Source)return false;TStrongObjectPtr<URogueliteFloorProfile> P(NewObject<URogueliteFloorProfile>());P->Rules=Source->Rules;
    if(Case==TEXT("G1TightCombatMax")||Case==TEXT("LegacyCombatQuota"))for(auto& Q:P->Rules.RoomTypes.Rules)if(Q.Type==ERogueliteSeedRoomType::Combat)Q.MaxCount=Case==TEXT("G1TightCombatMax")?2:3;
    const int32 Runs=Case==TEXT("CandidateF1")?2:1;FString FirstGraph,FirstLayout;
    for(int32 Run=0;Run<Runs;++Run)
    {
        auto R=FDesignerWorkbench::Generate(W,P.Get(),Seed);auto* A=R.Source.Get();
        if(Case==TEXT("FailureCleanup")){Good&=TestTrue(TEXT("Bounded seed75 failure retained and cleaned"),!R.bSucceeded&&Same(W,Before)&&!R.RawReason.IsEmpty());Save(Case+TEXT(".txt"),R.RawReason);return Good;}
        Good&=TestTrue(TEXT("Actual Workbench generate: ")+R.RawReason,R.bSucceeded);if(!A)return false;
        const auto Graph=FRogueliteRoomGraphGenerator::Generate(Seed,A->Settings.GraphSettings());FString RoomRows,Connections;
        if(P->Rules.RoomTemplateRoles.bEnabled)Good&=TestTrue(TEXT("Original G1 roles, authored components and C2"),M1Test::Check(P->Rules,Graph.Graph,A->Batch,A->CommittedLoops,RoomRows,Connections));
        Good&=TestTrue(TEXT("Workbench validate, actual geometry only"),FDesignerWorkbench::Validate(W).bSucceeded&&R.Map.bGeometryValidated&&!R.Map.bDungeonReady);
        int32 Tagged=0,Ordinary=0,Events=0;for(const auto& Node:A->Batch.Rooms){Tagged+=Node.RoomType==ERogueliteSeedRoomType::Combat;Ordinary+=Node.RoomType==ERogueliteSeedRoomType::Combat&&Node.NodeId!=Graph.Graph.StartNodeId;Events+=Node.RoomType==ERogueliteSeedRoomType::Event;}
        if(Case==TEXT("G1TightCombatMax"))Good&=TestTrue(TEXT("Three Combat labels but only two ordinary Combat meet max2"),Tagged==3&&Ordinary==2);
        if(Case==TEXT("LegacyCombatQuota"))Good&=TestEqual(TEXT("Legacy still counts all three Combat labels"),Tagged,3);
        if(Case==TEXT("CandidateF2Loop"))Good&=TestTrue(TEXT("Six-room optional-Event-free real loop"),A->Batch.Rooms.Num()==6&&Events==0&&A->CommittedLoops.Edges.Num()==1);
        if(Case==TEXT("CandidateF2Fallback"))Good&=TestTrue(TEXT("Legal zero-loop Tree fallback"),A->CommittedLoops.Edges.IsEmpty()&&A->Latest.bUsedOptionalTreeFallback);
        if(Case==TEXT("CandidateF3Boss"))Good&=TestTrue(TEXT("Final uses candidate Boss class/id"),A->Batch.Rooms.ContainsByPredicate([&](const auto& N){return N.NodeId==Graph.Graph.FinalNodeId&&N.TemplateId==TEXT("M1_FinalBoss");}));
        if(Run==0){FirstGraph=A->Batch.GraphDigest;FirstLayout=A->Batch.LayoutDigest;}else Good&=TestTrue(TEXT("Fixed seed layout/graph independent of new GUID"),FirstGraph==A->Batch.GraphDigest&&FirstLayout==A->Batch.LayoutDigest);
        if(Case==TEXT("SwitchAndProtect")){const auto Old=Scene(W);const auto Blocked=FDesignerWorkbench::Generate(W,Profile(2),0);Good&=TestTrue(TEXT("Switch cannot overwrite existing batch"),!Blocked.bSucceeded&&Blocked.RawReason==TEXT("ExistingBatchProtected")&&Same(W,Old));}
        Rows+=FString::Printf(TEXT("Run%d Floor%d Seed%d Rooms%d Tree%d Loop%d TaggedCombat%d OrdinaryCombat%d Event%d Graph%s Layout%s Source%s\n"),Run,Floor,Seed,A->Batch.Rooms.Num(),A->Batch.Edges.Num(),A->CommittedLoops.Edges.Num(),Tagged,Ordinary,Events,*A->Batch.GraphDigest,*A->Batch.LayoutDigest,*A->AppliedProfilePath);
        Save(Case+FString::Printf(TEXT("-rooms-%d.tsv"),Run),RoomRows);Save(Case+FString::Printf(TEXT("-connections-%d.tsv"),Run),Connections);
        Good&=TestTrue(TEXT("Clear keeps original user/test actors and releases this batch"),FDesignerWorkbench::Clear(W).bSucceeded&&Same(W,Before));
    }
    if(Case==TEXT("SwitchAndProtect")){auto R=FDesignerWorkbench::Generate(W,Profile(2),0);Good&=TestTrue(TEXT("After Clear the next exact profile is applied"),R.bSucceeded&&R.Source.IsValid()&&R.Source->Profile==Profile(2));Good&=TestTrue(TEXT("Switched batch clean"),FDesignerWorkbench::Clear(W).bSucceeded&&Same(W,Before));}
    Save(Case+TEXT("-summary.txt"),Rows);return Good;
}
#endif
