#include "Tests/RogueliteDynamicBridgeTestRig.h"
#include "RogueliteRuntimeNavigationSystem.h"
#include "RogueliteFloorProfile.h"
#include "RogueliteDevGameMode.h"
#include "Misc/AutomationTest.h"
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Editor.h"
#include "FileHelpers.h"
#include "Tests/AutomationEditorCommon.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Engine/DirectionalLight.h"
#include "Engine/SkyLight.h"
#include "GameFramework/WorldSettings.h"
#include "GameFramework/PlayerStart.h"
#include "UObject/UnrealType.h"
#include "UObject/Package.h"
#include "Misc/PackageName.h"

namespace G1ATest
{
const TCHAR* Map=TEXT("/Game/Roguelite/Tests/P04G1A/L_Roguelite_DynamicGameplayBridge_Dev");
const TCHAR* Profile=TEXT("/Game/Roguelite/ProductionCandidates/P03M1/Profiles/DA_M1_Floor1_Candidate.DA_M1_Floor1_Candidate");
ARogueliteDynamicBridgeTestRig* Rig(UWorld* W){if(W)for(TActorIterator<ARogueliteDynamicBridgeTestRig>I(W);I;++I)if(!I->IsActorBeingDestroyed())return *I;return nullptr;}
class FObserve:public IAutomationLatentCommand
{
    FAutomationTestBase* Test;FString Scenario;double Started=0;bool Invoked=false;
public:
    FObserve(FAutomationTestBase* T,FString S):Test(T),Scenario(MoveTemp(S)){}
    bool Update()override
    {
        auto* W=GEditor?GEditor->PlayWorld.Get():nullptr;if(!W)return false;
        if(!Started){Started=FPlatformTime::Seconds();return false;}if(FPlatformTime::Seconds()-Started<1)return false;
        auto* A=Rig(W);if(!A){Test->AddError(TEXT("Independent G1A GameWorld rig missing"));return true;}
        if(!Invoked){Invoked=true;if(!A->StartScenario(Scenario)){Test->AddError(TEXT("G1A scenario could not start"));return true;}}
        if(!A->bScenarioFinished&&FPlatformTime::Seconds()-Started<255)return false;
        Test->TestTrue(TEXT("Actual scenario outcome and owned cleanup"),A->bScenarioFinished&&A->bScenarioPassed&&A->Batch.OwnedActors.IsEmpty());return true;
    }
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FG1ACreate,"ProjectVerification.G1A.CreateIndependentMap",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FG1ACreate::RunTest(const FString&)
{
    using namespace G1ATest;if(!GEditor||GEditor->PlayWorld)return false;
    if(FPackageName::DoesPackageExist(Map))return TestNotNull(TEXT("Load existing G1A map without overwriting"),UEditorLoadingAndSavingUtils::LoadMap(Map));
    auto* W=UEditorLoadingAndSavingUtils::NewBlankMap(false);if(!W)return false;
    auto* WS=W->GetWorldSettings();WS->DefaultGameMode=ARogueliteDevGameMode::StaticClass();
    auto* Config=NewObject<URogueliteDynamicBridgeNavigationConfig>(WS);
    auto* Property=FindFProperty<FObjectPropertyBase>(AWorldSettings::StaticClass(),TEXT("NavigationSystemConfig"));if(!Property)return false;
    Property->SetObjectPropertyValue_InContainer(WS,Config);
    auto* Bounds=W->SpawnActor<ARogueliteRuntimeNavigationBounds>();Bounds->ConfigureBounds(FBox(FVector(-150,-150,-100),FVector(150,150,300)));
    Bounds->SetActorLabel(TEXT("G1A_ExistingNavigationBootstrap"));
    W->SpawnActor<ADirectionalLight>(FVector(0,0,1000),FRotator(-45,-30,0));W->SpawnActor<ASkyLight>();
    W->SpawnActor<APlayerStart>(FVector(0,0,150),FRotator::ZeroRotator);
    auto* A=W->SpawnActor<ARogueliteDynamicBridgeTestRig>();A->SetActorLabel(TEXT("G1A_SafetyTest_Control"));
    A->FloorProfile=LoadObject<URogueliteFloorProfile>(nullptr,Profile);if(!A->FloorProfile)return false;A->Seed=0;A->bAutoGenerate=false;
    const auto C=A->FloorProfile->ResolvePreview(A->Seed);FString Reason;
    if(!C.bValid||!FRoguelitePreparedTemplateCatalog::CaptureEditor(A,C.Source,A->PreparedTemplates,Reason))return false;
    auto* Manual=W->SpawnActor<AActor>();Manual->SetActorLabel(TEXT("G1A_Manual_Protected"));
    return TestTrue(TEXT("Save only new G1A empty runtime map"),UEditorLoadingAndSavingUtils::SaveMap(W,Map));
}
IMPLEMENT_COMPLEX_AUTOMATION_TEST(FG1ABaseline,"ProjectVerification.G1A.Baseline",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
void FG1ABaseline::GetTests(TArray<FString>& Names,TArray<FString>& Commands)const
{Names={TEXT("PawnMovement"),TEXT("GateCollision")};Commands={TEXT("BaselinePawn"),TEXT("BaselineGate")};}
bool FG1ABaseline::RunTest(const FString& Scenario)
{
    using namespace G1ATest;if(!GEditor||GEditor->PlayWorld||!GEditor->GetEditorWorldContext().World()||GEditor->GetEditorWorldContext().World()->GetOutermost()->GetName()!=Map)return false;
    auto& F=FAutomationTestFramework::Get();F.EnqueueLatentCommand(MakeShared<FStartPIECommand>(false));F.EnqueueLatentCommand(MakeShared<FObserve>(this,Scenario));F.EnqueueLatentCommand(MakeShared<FEndPlayMapCommand>());return true;
}
#endif
