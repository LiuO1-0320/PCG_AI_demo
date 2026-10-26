#include "RogueliteFloorProfile.h"
#include "RogueliteWorkbenchPreview.h"
#include "RogueliteCompactCorridorContract.h"
#include "RogueliteDevGameMode.h"
#include "RogueliteDesignerWorkbench.h"
#include "RogueliteOpenPassagePrototypeRig.h"
#include "LevelEditorViewport.h"
#include "Editor.h"
#include "FileHelpers.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Engine/DirectionalLight.h"
#include "Engine/SkyLight.h"
#include "GameFramework/PlayerStart.h"
#include "GameFramework/WorldSettings.h"
#include "Components/BoxComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/PackageName.h"
#include "HAL/FileManager.h"
#include "UObject/UnrealType.h"
ROGUELITEPROTOTYPE_API bool ConfigureHF03CMapNavigation(UWorld* World);
#if WITH_DEV_AUTOMATION_TESTS
namespace
{
const TCHAR* HF03CMap=TEXT("/Game/Roguelite/Maps/L_Roguelite_OpenPassageIntegration_Dev");
bool OwnMap(){return GEditor&&!GEditor->PlayWorld&&GEditor->GetEditorWorldContext().World()&&GEditor->GetEditorWorldContext().World()->GetOutermost()->GetName()==HF03CMap;}
int32 CountActors(UWorld* W){int32 N=0;for(TActorIterator<AActor> I(W);I;++I)N+=!I->IsActorBeingDestroyed();return N;}
FString EvidenceDir(){static FString D=FPaths::ProjectPluginsDir()/TEXT("RoguelitePrototype/Development/P03E2HF03C/StageB-")+FDateTime::UtcNow().ToString(TEXT("%Y%m%d-%H%M%S"));IFileManager::Get().MakeDirectory(*D,true);return D;}
void Evidence(const FString& Name,const FString& Text){FFileHelper::SaveStringToFile(Text,*(EvidenceDir()/Name),FFileHelper::EEncodingOptions::ForceUTF8);}
FString Components(AActor* A){FString Text;TInlineComponentArray<UPrimitiveComponent*> C;A->GetComponents(C);for(auto* X:C)Text+=X->GetName()+X->GetComponentTransform().ToString()+FString::FromInt(int32(X->GetCollisionEnabled()))+FString::FromInt(X->IsVisible());return Text;}
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHF03CCreateMap,"ProjectAuthoring.HF03C.CreateMap",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FHF03CCreateMap::RunTest(const FString&)
{
    if(!GEditor||GEditor->PlayWorld)return false;
    if(FPaths::FileExists(FPackageName::LongPackageNameToFilename(HF03CMap,FPackageName::GetMapPackageExtension()))){AddError(TEXT("Existing map protected; no overwrite"));return false;}
    auto* W=UEditorLoadingAndSavingUtils::NewBlankMap(false);if(!W)return false;
    W->GetWorldSettings()->DefaultGameMode=ARogueliteDevGameMode::StaticClass();
    W->SpawnActor<ADirectionalLight>(FVector(0,0,1000),FRotator(-45,-30,0));W->SpawnActor<ASkyLight>();W->SpawnActor<APlayerStart>(FVector(0,0,150),FRotator::ZeroRotator);
    return TestTrue(TEXT("Only the new HF03-C map saved"),UEditorLoadingAndSavingUtils::SaveMap(W,HF03CMap));
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHF03CEditorMatrix,"ProjectVerification.HF03C.EditorMatrix",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FHF03CEditorMatrix::RunTest(const FString&)
{
    if(!OwnMap()){AddError(TEXT("Dedicated HF03-C map outside PIE required"));return false;}
    UWorld* W=GEditor->GetEditorWorldContext().World();const int32 Initial=CountActors(W);bool Good=true;
    FString Cases=TEXT("Floor\tSeed\tMode\tCeiling\tPlan\tGenerated\tRooms\tTree\tLoops\tC2\tClear\tReason\n");
    FString LengthRows=TEXT("Floor\tSeed\tMode\tCeiling\tLoop\tEdge\tNativeLength\tSocketDistance\tProbes\tComplete\n");
    int32 Generated=0,Rejected=0;int32 Loops[4]={0,0,0,0};
    for(int32 Mode=0;Mode<2&&Good;++Mode)for(int32 Ceiling=0;Ceiling<2&&Good;++Ceiling)
    for(int32 Floor=1;Floor<=3&&Good;++Floor)for(int32 Seed=0;Seed<(Floor==1?20:6)&&Good;++Seed)
    {
        auto* P=NewObject<URogueliteFloorProfile>();P->Rules=FRogueliteFloorProfileResolver::Defaults(Floor);P->Rules.OpeningMode=Mode?ERogueliteOpeningMode::OpenPassage:ERogueliteOpeningMode::Legacy;P->Rules.bGenerateCorridorCeiling=Ceiling!=0;
        const auto Config=P->ResolvePreview(Seed);if(!Config.bValid){AddError(Config.Message);return false;}
        auto* A=W->SpawnActor<ARogueliteWorkbenchPreview>();A->WorkbenchId=FGuid::NewGuid();A->ApplyProfile(P,Config);A->Tags.AddUnique(TEXT("RogueliteLoopAwareTestOnly"));
        const auto Plan=FRogueliteLoopAwarePlanner::Plan(A,Config.Source,Config.Planning,FTransform::Identity);
        const bool Built=Plan.Tree.bValid&&FRogueliteLoopAwarePlanner::MaterializeEditor(A,Config.Source,Config.Planning,Plan,A->Batch,A->CommittedLoops,A->Latest);
        const int32 NR=A->Batch.Rooms.Num(),NT=A->Batch.Edges.Num(),NL=A->CommittedLoops.Edges.Num();bool C2=Built;FString Why=Plan.Tree.bValid?A->Latest.Reason:Plan.Evidence.Reason;
        if(Built)
        {
            ++Generated;Loops[Mode*2+Ceiling]+=NL;
            TArray<FRogueliteCompactCorridorMeasurement> Measures;C2&=FRogueliteCompactCorridorContract::Actual(A->Batch,A->CommittedLoops,Measures,Why);
            TSet<FGuid> Ids;
            for(const auto& Room:A->Batch.Rooms)
            {
                C2&=Room.Room->EvaluateTemplate().bValid&&Room.Room->GetActorScale3D().Equals(FVector::OneVector)&&Room.Room->GetActorTransform().Equals(Room.PlacedTransform,.0001);
                if(Mode)C2&=Room.Room->RoomHeight==500;
            }
            const auto Check=[&](int32 Id,bool Loop,ARogueliteParametricCorridor* C,ARogueliteParametricRoom* RA,ERogueliteRoomSide SA,ARogueliteParametricRoom* RB,ERogueliteRoomSide SB,const FGuid& Connection,const FRogueliteAssemblyResult& V)
            {
                const bool Complete=ARogueliteRoomAssembly::HasCompleteCorridorEvidence(RA,SA,C,RB,SB,V);
                C2&=Complete&&C->bGenerateCeiling==(Ceiling!=0)&&!Ids.Contains(Connection);Ids.Add(Connection);
                const auto* M=Measures.FindByPredicate([&](const auto& X){return X.EdgeId==Id&&X.bLoop==Loop;});
                LengthRows+=FString::Printf(TEXT("%d\t%d\t%d\t%d\t%d\t%d\t%.6f\t%.6f\t%d\t%d\n"),Floor,Seed,Mode,Ceiling,Loop,Id,C->CorridorLength,M?M->SocketDistance:-1,V.Probes.Num(),Complete);
                auto Missing=V;Missing.Probes.RemoveAt(0);C2&=!ARogueliteRoomAssembly::HasCompleteCorridorEvidence(RA,SA,C,RB,SB,Missing);
                auto Replaced=V;Replaced.Probes[0].Name=TEXT("Missing required aperture coverage");C2&=!ARogueliteRoomAssembly::HasCompleteCorridorEvidence(RA,SA,C,RB,SB,Replaced);
            };
            const auto Room=[&](int32 N){const auto* R=A->Batch.Rooms.FindByPredicate([&](const auto& X){return X.NodeId==N;});return R?R->Room.Get():nullptr;};
            for(const auto& E:A->Batch.Edges)Check(E.EdgeId,false,E.Corridor,Room(E.ParentNode),E.ParentSocket,Room(E.ChildNode),E.ChildSocket,E.ConnectionId,E.Validation);
            for(const auto& E:A->CommittedLoops.Edges)Check(E.EdgeId,true,E.Corridor,Room(E.NodeA),E.SocketA,Room(E.NodeB),E.SocketB,E.ConnectionId,E.Validation);
            C2&=Ids.Num()==NT+NL&&A->Batch.Registry->GetConnectionRecords().Num()==NT+NL;
            if(!A->Batch.Edges.IsEmpty())
            {
                const auto& E=A->Batch.Edges[0];FString Before=Components(E.Corridor)+Components(Room(E.ParentNode))+Components(Room(E.ChildNode));
                const auto Again=A->Batch.Registry->RevalidateConfiguredConnection(E.ConnectionId);
                C2&=Again.bSucceeded&&Before==Components(E.Corridor)+Components(Room(E.ParentNode))+Components(Room(E.ChildNode));
            }
            if(Mode&&Floor==1&&Seed==0)
            {
                auto* C=A->Batch.Edges[0].Corridor.Get();const bool Was=C->bGenerateCeiling;const FGuid Id=A->Batch.Edges[0].ConnectionId;
                C->bGenerateCeiling=!Was;C->RebuildCorridor();FString Recheck;
                C2&=FRogueliteLoopCommitQueries::Revalidate(A,A->Batch,A->CommittedLoops,Recheck)&&C->bGenerateCeiling!=Was&&A->Batch.Edges[0].ConnectionId==Id;
                C->bGenerateCeiling=Was;C->RebuildCorridor();C2&=FRogueliteLoopCommitQueries::Revalidate(A,A->Batch,A->CommittedLoops,Recheck);
            }
            Good&=TestTrue(FString::Printf(TEXT("Floor%d Seed%d Mode%d Ceiling%d complete actual Tree/Loop C2"),Floor,Seed,Mode,Ceiling),C2);
        }
        else
        {
            ++Rejected;
            if(Plan.Tree.bValid){Good=false;AddError(FString::Printf(TEXT("Entity/C2 failure Floor%d Seed%d Mode%d Ceiling%d: %s %s"),Floor,Seed,Mode,Ceiling,*A->Latest.Stage,*Why));}
            if(Floor==1){Good=false;AddError(TEXT("Required four-room intro spatial planning rejected: ")+Why);}
        }
        FString ClearReason;const bool Clear=FRogueliteLoopAwarePlanner::Clear(A,A->Batch,A->CommittedLoops,ClearReason);A->Destroy();
        const bool Clean=Clear&&CountActors(W)==Initial;Good&=TestTrue(TEXT("Only this batch cleaned; no residual room/corridor/registry actors"),Clean);
        Cases+=FString::Printf(TEXT("%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%s\n"),Floor,Seed,Mode,Ceiling,Plan.Tree.bValid,Built,NR,NT,NL,C2,Clean,*Why.Replace(TEXT("\n"),TEXT(" ")));
        Evidence(TEXT("cases.tsv"),Cases);Evidence(TEXT("connections.tsv"),LengthRows);
    }
    for(int32 I=0;I<4;++I)Good&=TestTrue(TEXT("Actual committed loop exercised for every mode/ceiling combination"),Loops[I]>0);
    AddInfo(FString::Printf(TEXT("HF03C_EDITOR Generated=%d Rejected=%d Evidence=%s; NavigationNotTested"),Generated,Rejected,*EvidenceDir()));return Good;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHF03CSetupRuntime,"ProjectAuthoring.HF03C.SetupRuntimeMap",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FHF03CSetupRuntime::RunTest(const FString&)
{
    if(!OwnMap())return false;UWorld* W=GEditor->GetEditorWorldContext().World();
    for(TActorIterator<ARogueliteParametricRoom> I(W);I;++I)if(!I->IsActorBeingDestroyed()){AddError(TEXT("Clear only the Workbench-owned preview before Runtime setup"));return false;}
    for(TActorIterator<ARogueliteParametricCorridor> I(W);I;++I)if(!I->IsActorBeingDestroyed())return false;
    if(!ConfigureHF03CMapNavigation(W))return false;
    ARogueliteRuntimeLoopAssembly* Rig=nullptr;for(TActorIterator<ARogueliteRuntimeLoopAssembly> I(W);I;++I)if(!I->IsActorBeingDestroyed()){if(Rig)return false;Rig=*I;}
    if(!Rig)Rig=W->SpawnActor<ARogueliteRuntimeLoopAssembly>();if(!Rig)return false;
    Rig->SetActorLabel(TEXT("HF03C_Runtime_Control"));Rig->FloorProfile=LoadObject<URogueliteFloorProfile>(nullptr,TEXT("/Game/Roguelite/FloorProfiles/DA_Floor3_Final.DA_Floor3_Final"));
    Rig->Seed=0;Rig->bAutoGenerate=false;Rig->bNavigationEnabled=true;Rig->bRunValidationSuite=false;Rig->PrepareTemplatesInEditor();
    return TestTrue(TEXT("Only HF03-C new map saved; no pre-saved dungeon geometry"),!Rig->PreparedTemplates.Digest.IsEmpty()&&UEditorLoadingAndSavingUtils::SaveMap(W,HF03CMap));
}
namespace
{
TWeakObjectPtr<ARogueliteOpenPassagePrototypeRig> VisualRig;
bool Visual(FAutomationTestBase* Test,bool Open,bool Ceiling)
{
    if(!OwnMap())return false;auto* W=GEditor->GetEditorWorldContext().World();
    if(!VisualRig.IsValid())VisualRig=ARogueliteOpenPassagePrototypeRig::CreateEditorPrototype(W,FTransform::Identity);
    auto* R=VisualRig.Get();if(!R)return false;
    if(!R->Configure(Open?ERogueliteOpeningMode::OpenPassage:ERogueliteOpeningMode::Legacy))return false;
    R->Connector->bGenerateCeiling=Ceiling;R->Connector->RebuildCorridor();R->ValidatePrototype();
    for(auto* V:GEditor->GetLevelViewportClients())if(V&&V->Viewport){V->SetGameView(true);V->SetViewLocation(FVector(1770,0,220));V->SetViewRotation(FRotator(0,180,0));V->Invalidate();}
    GEditor->SelectNone(false,true,false);
    return Test->TestTrue(TEXT("Real historical fixture uses original 59/75 (+9 OFF) contract"),R->LastValidation.bSucceeded&&R->LastValidation.Probes.Num()==(Open?75:59)+(Ceiling?0:9));
}
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHF03CVisualLegacyOn,"ProjectVerification.HF03C.Visual.LegacyOn",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FHF03CVisualLegacyOn::RunTest(const FString&){return Visual(this,false,true);}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHF03CVisualLegacyOff,"ProjectVerification.HF03C.Visual.LegacyOff",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FHF03CVisualLegacyOff::RunTest(const FString&){return Visual(this,false,false);}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHF03CVisualOpenOn,"ProjectVerification.HF03C.Visual.OpenOn",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FHF03CVisualOpenOn::RunTest(const FString&){return Visual(this,true,true);}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHF03CVisualOpenOff,"ProjectVerification.HF03C.Visual.OpenOff",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FHF03CVisualOpenOff::RunTest(const FString&){return Visual(this,true,false);}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHF03CVisualClear,"ProjectVerification.HF03C.Visual.Clear",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FHF03CVisualClear::RunTest(const FString&){if(!OwnMap())return false;if(VisualRig.IsValid()){VisualRig->DestroyOwnTestFixture();VisualRig.Reset();}return true;}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHF03CPrototypeRegression,"ProjectVerification.HF03C.HistoricalPrototypeRegression",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FHF03CPrototypeRegression::RunTest(const FString&)
{
    if(!OwnMap())return false;auto* W=GEditor->GetEditorWorldContext().World();const int32 Before=CountActors(W);bool Good=true;
    for(int32 Side=0;Side<4&&Good;++Side)for(int32 Offset=0;Offset<2&&Good;++Offset)
    {
        auto* R=ARogueliteOpenPassagePrototypeRig::CreateEditorPrototype(W,FTransform(FVector(2000000,2000000,0)),static_cast<ERogueliteRoomSide>(Side),Offset?150:0,Offset?-175:0,FVector(3000,2500,400));if(!R)return false;
        const FGuid Id=ARogueliteRoomAssembly::GetCorridorConnectionId(R->Connector);
        for(int32 Mode=0;Mode<2;++Mode)for(int32 Roof=0;Roof<2;++Roof)
        {
            Good&=R->Configure(Mode?ERogueliteOpeningMode::OpenPassage:ERogueliteOpeningMode::Legacy);R->Connector->bGenerateCeiling=Roof!=0;R->Connector->RebuildCorridor();R->ValidatePrototype();
            Good&=TestTrue(TEXT("Historical P01/P02/C1/C2/HF03/B probes and lease identity unchanged"),R->LastValidation.bSucceeded&&R->LastValidation.bGeometryPassed&&R->LastValidation.bCapsulePassed&&R->LastValidation.Probes.Num()==(Mode?75:59)+(Roof?0:9)&&ARogueliteRoomAssembly::GetCorridorConnectionId(R->Connector)==Id);
        }
        R->DestroyOwnTestFixture();Good&=TestEqual(TEXT("Historical transient fixture cleanup"),CountActors(W),Before);
    }
    return Good;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHF03CSavePreview,"ProjectVerification.HF03C.SaveEditorPreview",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FHF03CSavePreview::RunTest(const FString&)
{
    if(!OwnMap()||VisualRig.IsValid())return false;auto* W=GEditor->GetEditorWorldContext().World();
    auto* P=LoadObject<URogueliteFloorProfile>(nullptr,TEXT("/Game/Roguelite/FloorProfiles/DA_Floor2_Expansion.DA_Floor2_Expansion"));
    const auto G=FDesignerWorkbench::Generate(W,P,1);if(!G.bSucceeded){AddError(G.RawReason);return false;}
    auto* A=G.Source.Get();if(!A||A->Batch.Edges.IsEmpty())return false;
    auto* C=A->Batch.Edges[0].Corridor.Get();const FGuid Id=A->Batch.Edges[0].ConnectionId;
    C->bGenerateCeiling=false;C->RebuildCorridor();const auto V=FDesignerWorkbench::Validate(W);
    const bool Good=V.bSucceeded&&!C->bGenerateCeiling&&A->Batch.Edges[0].ConnectionId==Id;
    if(!Good){AddError(V.RawReason);return false;}
    Evidence(TEXT("saved-preview-override.txt"),C->GetPathName()+TEXT(" Ceiling=false Connection=")+Id.ToString()+TEXT(" Layout=")+A->Batch.LayoutDigest);
    return TestTrue(TEXT("Only new HF03-C map saved with manual ceiling override"),UEditorLoadingAndSavingUtils::SaveMap(W,HF03CMap));
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHF03CVerifyReload,"ProjectVerification.HF03C.VerifyReloadAndClear",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FHF03CVerifyReload::RunTest(const FString&)
{
    if(!OwnMap())return false;auto* W=GEditor->GetEditorWorldContext().World();FString Why;auto* A=FDesignerWorkbench::Find(W,Why);if(!A||A->Batch.Edges.IsEmpty())return false;
    auto* C=A->Batch.Edges[0].Corridor.Get();bool Good=TestFalse(TEXT("Actor-level ceiling override persisted after unload/reload"),C->bGenerateCeiling);
    const auto V=FDesignerWorkbench::Validate(W);Good&=TestTrue(TEXT("Validate preserves saved manual override and C2"),V.bSucceeded&&!C->bGenerateCeiling);
    Good&=TestTrue(TEXT("Only reloaded Workbench batch cleared"),FDesignerWorkbench::Clear(W).bSucceeded);return Good;
}
#endif
