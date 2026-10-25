#include "RogueliteRuntimeLoopAssembly.h"
#include "RogueliteRuntimeNavigationSystem.h"
#include "RogueliteDevGameMode.h"
#include "Misc/AutomationTest.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Editor.h"
#include "FileHelpers.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Engine/World.h"
#include "Engine/DirectionalLight.h"
#include "Engine/SkyLight.h"
#include "GameFramework/PlayerStart.h"
#include "GameFramework/WorldSettings.h"
#include "EngineUtils.h"
#include "UObject/UnrealType.h"

// Invoked explicitly through the real Epic Automation MCP. Never part of the Roguelite.* regression filter.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRuntimeLoopCreateMap,"ProjectAuthoring.B4B3.CreateRuntimeLoopMap",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FRuntimeLoopCreateMap::RunTest(const FString&)
{
    const FString Name=TEXT("/Game/Roguelite/Maps/L_Roguelite_RuntimeLoopNavigation_Dev");
    const auto File=FPackageName::LongPackageNameToFilename(Name,FPackageName::GetMapPackageExtension());
    if(FPaths::FileExists(File)){AddInfo(TEXT("Existing test map protected; authoring skipped without changes."));return true;}
    if(!GEditor||GEditor->PlayWorld){AddError(TEXT("Editor outside PIE required; no map created."));return false;}
    auto* W=UEditorLoadingAndSavingUtils::NewBlankMap(false);if(!W)return false;
    auto* WS=W->GetWorldSettings();WS->DefaultGameMode=ARogueliteDevGameMode::StaticClass();
    auto* Config=NewObject<UNavigationSystemModuleConfig>(WS);Config->NavigationSystemClass=FSoftClassPath(URogueliteRuntimeNavigationSystem::StaticClass());
    auto* Property=FindFProperty<FObjectPropertyBase>(AWorldSettings::StaticClass(),TEXT("NavigationSystemConfig"));if(!Property)return false;
    Property->SetObjectPropertyValue_InContainer(WS,Config);
    auto* Rig=W->SpawnActor<ARogueliteRuntimeLoopAssembly>();if(!Rig)return false;Rig->SetActorLabel(TEXT("B4B3_RuntimeLoop"));
    FString Reason;if(!FRoguelitePreparedTemplateCatalog::CaptureEditor(Rig,FRogueliteRuntimeLoopAssembler::SourcePreset(0),Rig->PreparedTemplates,Reason))
    {AddError(Reason);return false;}
    auto* Bounds=W->SpawnActor<ARogueliteRuntimeNavigationBounds>();Bounds->ConfigureBounds(FBox(FVector(-150,-150,-100),FVector(150,150,300)));Bounds->SetActorLabel(TEXT("B4B3_NavigationBootstrap"));
    W->SpawnActor<APlayerStart>(FVector(0,-600,150),FRotator::ZeroRotator);
    W->SpawnActor<ADirectionalLight>(FVector(0,0,1000),FRotator(-45,-30,0));W->SpawnActor<ASkyLight>();
    const bool Saved=UEditorLoadingAndSavingUtils::SaveMap(W,Name);TestTrue(TEXT("Only new map saved"),Saved);
    int32 Geometry=0;for(TActorIterator<ARogueliteParametricRoom> I(W);I;++I)++Geometry;for(TActorIterator<ARogueliteParametricCorridor> I(W);I;++I)++Geometry;
    TestEqual(TEXT("No pre-saved dungeon geometry"),Geometry,0);TestTrue(TEXT("Prepared catalog persists without World actor references"),!Rig->PreparedTemplates.Digest.IsEmpty());
    AddInfo(TEXT("B4B3_NEW_MAP_SAVED EmptyDungeon=1 MapLocalDynamicNavigation=1 PreparedTemplateCatalog=1"));return Saved&&Geometry==0;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRuntimeLoopEditorRegression,"ProjectVerification.B4B3.EditorRegressions",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FRuntimeLoopEditorRegression::RunTest(const FString&)
{
    if(!GEditor||GEditor->PlayWorld)return false;auto* W=GEditor->GetEditorWorldContext().World();
    if(!W||W->GetOutermost()->GetName()!=TEXT("/Game/Roguelite/Maps/L_Roguelite_RuntimeLoopNavigation_Dev")){AddError(TEXT("Only new owned test map accepted."));return false;}
    ARogueliteRuntimeLoopAssembly* Rig=nullptr;for(TActorIterator<ARogueliteRuntimeLoopAssembly> I(W);I;++I){Rig=*I;break;}
    if(!Rig)return false;Rig->RunEditorRegressions();TestTrue(TEXT("Original Editor regressions"),Rig->bEditorRegressionsPassed);return Rig->bEditorRegressionsPassed;
}
#endif

void ARogueliteRuntimeLoopAssembly::RunEditorRegressions()
{
#if WITH_EDITOR
    if(!GetWorld()||GetWorld()->WorldType!=EWorldType::Editor||GetWorld()->GetOutermost()->GetName()!=TEXT("/Game/Roguelite/Maps/L_Roguelite_RuntimeLoopNavigation_Dev"))return;
    for(TActorIterator<ARogueliteParametricRoom> I(GetWorld());I;++I)if(!I->IsActorBeingDestroyed())return;
    EditorRegressionResults.Reset();bEditorRegressionsPassed=true;
    auto* Fixed=GetWorld()->SpawnActor<ARogueliteLoopCommitTestRig>();Fixed->RunCommitChecks();
    EditorFixedCases=Fixed->Cases;EditorRegressionResults.Append(Fixed->Evidence);bEditorRegressionsPassed&=Fixed->bChecksPassed;
    if(!Fixed->Batch.OwnedActors.IsEmpty()||!Fixed->CommittedLoops.Edges.IsEmpty()){bEditorRegressionsPassed=false;return;}Fixed->Destroy();
    auto* Loop=GetWorld()->SpawnActor<ARogueliteLoopAwareTestRig>();Loop->RunChecks();
    EditorLoopAwareCases=Loop->Cases;EditorRegressionResults.Append(Loop->Evidence);bEditorRegressionsPassed&=Loop->bChecksPassed;
    if(!Loop->Batch.OwnedActors.IsEmpty()||!Loop->CommittedLoops.Edges.IsEmpty()){bEditorRegressionsPassed=false;return;}Loop->Destroy();
    auto* Control=GetWorld()->SpawnActor<ARogueliteRuntimeSeedAssembly>();Control->RunEditorRegression();
    EditorRegressionResults.Append(Control->EditorRegressionEvidence);bEditorRegressionsPassed&=Control->bEditorRegressionPassed;
    if(!Control->Batch.OwnedActors.IsEmpty()){bEditorRegressionsPassed=false;return;}Control->Destroy();
#endif
}

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRuntimeLoopInputGuard,"Roguelite.RuntimeLoops.PreparedDataAndGuards",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FRuntimeLoopInputGuard::RunTest(const FString&)
{
    FRoguelitePreparedTemplateCatalog C;FString Reason;const auto S=FRogueliteRuntimeLoopAssembler::SourcePreset(0);
    TestFalse(TEXT("Empty catalog rejected, no GameWorld probe fallback"),C.IsCurrent(S,Reason));
    FRogueliteSeedBatch B;FRogueliteCommittedLoopBatch L;FRogueliteRuntimeLoopResult R;FRogueliteLoopAwareSettings P;P.bEnableLoopAwarePlanning=true;
    TestFalse(TEXT("No-world runtime assembly rejected"),FRogueliteRuntimeLoopAssembler::Generate(nullptr,S,P,C,B,L,R));
    TestTrue(TEXT("No actors/leases touched"),B.OwnedActors.IsEmpty()&&L.Edges.IsEmpty());return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRuntimeLoopSmallPreset,"Roguelite.RuntimeLoops.SeparateFourRoomPreset",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FRuntimeLoopSmallPreset::RunTest(const FString&)
{
    const auto Small=FRogueliteRuntimeLoopAssembler::SourcePreset(0,true),Large=FRogueliteRuntimeLoopAssembler::SourcePreset(0);
    const auto A=FRogueliteRoomGraphGenerator::Generate(0,Small.GraphSettings()),B=FRogueliteRoomGraphGenerator::Generate(0,Large.GraphSettings());
    TestTrue(TEXT("Independent small Tree settings legal"),A.bSucceeded&&A.Graph.Nodes.Num()==4&&A.Graph.Edges.Num()==3);
    TestTrue(TEXT("Original normal V2 fixture retained"),B.bSucceeded&&B.Graph.Nodes.Num()==8&&B.Graph.Edges.Num()==7);return true;
}
#endif
