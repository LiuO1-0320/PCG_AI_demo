#include "Tests/RogueliteCombatSliceRig.h"
#include "Gameplay/RogueliteCombatSlicePlayer.h"
#include "RogueliteRuntimeNavigationSystem.h"
#include "RogueliteFloorProfile.h"
#include "RogueliteDynamicGameplayBridge.h"
#include "Components/CapsuleComponent.h"
#include "Components/BoxComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/AutomationTest.h"
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Editor.h"
#include "FileHelpers.h"
#include "Tests/AutomationEditorCommon.h"
#include "Engine/World.h"
#include "Engine/LocalPlayer.h"
#include "EngineUtils.h"
#include "Engine/DirectionalLight.h"
#include "Engine/SkyLight.h"
#include "GameFramework/WorldSettings.h"
#include "GameFramework/PlayerStart.h"
#include "EnhancedInputSubsystems.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "InputMappingContext.h"
#include "InputCoreTypes.h"
#include "UObject/UnrealType.h"
#include "Misc/PackageName.h"
#include "Misc/FileHelper.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "HAL/FileManager.h"

namespace CombatSliceVerification
{
const TCHAR* Map=TEXT("/Game/Roguelite/Tests/P04G1B/L_Roguelite_SingleCombatSlice_Dev");
ARogueliteCombatSliceRig* Find(UWorld* W)
{if(W)for(TActorIterator<ARogueliteCombatSliceRig>I(W);I;++I)if(!I->IsActorBeingDestroyed())return *I;return nullptr;}
void Save(const FString& Name,const FString& Rows)
{
    FString Dir;if(!FParse::Value(FCommandLine::Get(),TEXT("G1BEvidenceDir="),Dir))return;
    IFileManager::Get().MakeDirectory(*Dir,true);
    FString File=Dir/TEXT("stage0-")+Name+TEXT(".txt");
    if(IFileManager::Get().FileExists(*File))File+=TEXT(".")+FDateTime::UtcNow().ToString(TEXT("%Y%m%d%H%M%S"))+TEXT(".rerun");
    FFileHelper::SaveStringToFile(Rows,*File);
}
class FPlayerExercise:public IAutomationLatentCommand
{
    FAutomationTestBase* Test;FString Case,Rows;int32 Step=0;double Started=0,Action=0;FVector Initial,WaitingPosition;FRotator Look;float Peak=0;double WaitingDelta=0;bool bSawWaiting=false;
    TWeakObjectPtr<AActor> External;
    bool Check(bool Passed,const FString& What)
    {Rows+=FString::Printf(TEXT("Check=%s\tPass=%d\n"),*What,Passed);if(!Passed)Test->AddError(What);return Passed;}
    bool Finish(bool Passed,const FString& Why)
    {if(!Passed)Test->AddError(Why);Rows+=FString::Printf(TEXT("Passed=%d\nReason=%s\n"),Passed,*Why);Save(Case,Rows);return true;}
public:
    FPlayerExercise(FAutomationTestBase* T,FString C):Test(T),Case(MoveTemp(C)){}
    bool Update() override
    {
        auto* W=GEditor?GEditor->PlayWorld.Get():nullptr;if(!W)return false;
        if(!Started){Started=FPlatformTime::Seconds();Rows=TEXT("World=")+W->GetPathName()+TEXT("\n");}
        const double Now=FPlatformTime::Seconds();auto* R=Find(W);
        if(!R)return Finish(false,TEXT("New independent combat Rig absent from actual GameWorld"));
        if(Now-Started>220)return Finish(false,TEXT("Stage0 timed out: ")+R->SliceStatus+TEXT(" / ")+R->LastFailureReason);
        if(Step==0)
        {
            if(!R->SliceFailure.IsEmpty())return Finish(false,R->SliceFailure);
            if(!R->bPlayerBridgeReady)
            {
                const auto* WaitingPC=W->GetFirstPlayerController();
                if(const auto* WaitingFPS=WaitingPC?Cast<ARogueliteSlicePlayer>(WaitingPC->GetPawn()):nullptr)
                {
                    if(!bSawWaiting){bSawWaiting=true;WaitingPosition=WaitingFPS->GetActorLocation();}
                    WaitingDelta=FMath::Max(WaitingDelta,FVector::Distance(WaitingPosition,WaitingFPS->GetActorLocation()));
                    if(WaitingFPS->GetCharacterMovement()->MovementMode!=MOVE_None||WaitingFPS->GetCapsuleComponent()->GetCollisionEnabled()!=ECollisionEnabled::NoCollision||WaitingDelta>1)
                        return Finish(false,TEXT("Unready local FPS pawn moved/fell or enabled collision before registration"));
                }
                return false;
            }
            if(!Check(bSawWaiting&&WaitingDelta<=1,TEXT("Real possessed FPS pawn remained inert throughout pre-Ready generation")))return Finish(false,TEXT("Waiting lifecycle was not observed"));
            auto* P=R->GetSlicePlayer();auto* C=P?Cast<ARogueliteSlicePlayerController>(P->GetController()):nullptr;
            if(!Check(P&&C&&C->IsLocalController()&&C->GetLocalPlayer()&&C->GetPawn()==P&&P->GetOwner()==C,TEXT("Actual local PlayerController spawned and possesses FPS pawn with unchanged real Owner")))return Finish(false,TEXT("BLOCKED_SCOPE_PLAYER_OWNERSHIP"));
            if(!Check(P->HasLiveBinding()&&P->GetBoundBatch()==R->Batch.BatchId&&FRogueliteDynamicGameplayBridge::IsComponentIsolated(R,R->Batch.BatchId,P->GetCapsuleComponent()),TEXT("Real player root capsule registered under current Source/World/Batch")))return Finish(false,TEXT("BLOCKED_SCOPE_PLAYER_REGISTRATION"));
            if(!Check(R->Batch.Rooms.Num()==4&&R->Batch.Edges.Num()==3&&R->CommittedLoops.Edges.IsEmpty()&&R->IsDungeonReady(),TEXT("Original M1 Floor1 actual four-room tree and strict NavigationReady")))return Finish(false,TEXT("Original generation contract failed"));
            const auto* Start=R->Batch.Rooms.FindByPredicate([&](const auto& A){return A.NodeId==R->PlayerStartNode;});
            if(!Check(Start&&R->Settings.RoomTemplateRoles.StartTemplateIds.Contains(Start->TemplateId)&&FMath::Abs(Start->PlacedTransform.InverseTransformPosition(P->GetActorLocation()).X)<Start->Dimensions.X*.5,TEXT("Player is inside actual qualified StartSafehouse")))return Finish(false,TEXT("Wrong spawn role"));
            Rows+=FString::Printf(TEXT("Batch=%s\nStartNode=%d\nPC=%s\nPawn=%s\nOwner=%s\nGraph=%s\nLayout=%s\n"),*R->Batch.BatchId.ToString(),R->PlayerStartNode,*C->GetPathName(),*P->GetPathName(),*P->GetOwner()->GetPathName(),*R->Batch.GraphDigest,*R->Batch.LayoutDigest);
            Initial=P->GetActorLocation();Look=C->GetControlRotation();Action=Now;Step=1;return false;
        }
        auto* P=R->GetSlicePlayer();
        if(Step==5)
        {
            if(R->Batch.bSucceeded&&Now-Action<3)return false;
            Rows+=TEXT("FinalFailure=")+R->LastFailureReason+TEXT("\n");
            return Finish(Check(!R->Batch.bSucceeded&&R->Batch.OwnedActors.IsEmpty()&&!R->bPlayerBridgeReady&&External.IsValid()
                &&R->Stage==ERogueliteDungeonBuildStage::NavigationFailed&&R->LastFailureReason.Contains(TEXT("Current runtime Graph/Geometry changed")),TEXT("Unregistered external blocker invokes original Watch reason and owned cleanup, without deleting foreign actor")),TEXT("External obstacle safety checked with real local player integration"));
        }
        if(!P||!P->HasLiveBinding()||!R->GetTraversalSnapshot().bCurrent)return Finish(false,TEXT("Real player movement invalidated current structure/batch"));
        auto* C=Cast<ARogueliteSlicePlayerController>(P->GetController());
        auto* Input=C&&C->GetLocalPlayer()?C->GetLocalPlayer()->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>():nullptr;
        if(!Input)return Finish(false,TEXT("Actual LocalPlayer EnhancedInput subsystem missing"));
        const auto* Mapping=C->GetSliceMappingContext();
        if(!Mapping||!Input->HasMappingContext(Mapping))return Finish(false,TEXT("Actual key mapping context was not installed in the LocalPlayer"));
        const auto Mapped=[&](const UInputAction* A,FKey K){return Mapping->GetMappings().ContainsByPredicate([&](const auto& M){return M.Action==A&&M.Key==K;});};
        if(!Mapped(C->GetMoveAction(),EKeys::W)||!Mapped(C->GetMoveAction(),EKeys::A)||!Mapped(C->GetMoveAction(),EKeys::S)||!Mapped(C->GetMoveAction(),EKeys::D)
            ||!Mapped(C->GetLookAction(),EKeys::MouseX)||!Mapped(C->GetLookAction(),EKeys::MouseY)||!Mapped(C->GetJumpAction(),EKeys::SpaceBar))return Finish(false,TEXT("WASD/mouse/Space mappings are incomplete"));
        if(Step==1)
        {
            if(Now-Action<0.4)return false;
            if(Case==TEXT("ExternalBlocker"))
            {
                const FString DigestBefore=FRogueliteLoopCommitQueries::GeometryDigest(R,R->Batch,R->CommittedLoops);
                FActorSpawnParameters Params;Params.ObjectFlags|=RF_Transient;
                auto* A=W->SpawnActor<AActor>(Params);if(!A)return Finish(false,TEXT("Could not create negative obstacle"));
                auto* B=NewObject<UBoxComponent>(A);A->SetRootComponent(B);A->AddInstanceComponent(B);B->SetMobility(EComponentMobility::Movable);
                B->SetCanEverAffectNavigation(false);B->InitBoxExtent(FVector(25));B->SetCollisionEnabled(ECollisionEnabled::NoCollision);B->RegisterComponent();
                A->SetActorLocation(FVector(10000,10000,100));B->SetCollisionResponseToAllChannels(ECR_Block);B->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
                const FString DigestAfter=FRogueliteLoopCommitQueries::GeometryDigest(R,R->Batch,R->CommittedLoops);
                Rows+=TEXT("DigestBeforeExternal=")+DigestBefore+TEXT("\nDigestAfterExternal=")+DigestAfter+TEXT("\n");
                if(!Check(DigestBefore!=DigestAfter&&!R->GetTraversalSnapshot().bCurrent,TEXT("Actual unregistered box changes fingerprint before original Watch cleanup")))return Finish(false,TEXT("External obstacle did not affect original structure checks"));
                External=A;Action=Now;Step=5;return false;
            }
            Initial=P->GetActorLocation();Look=C->GetControlRotation();Action=Now;Step=2;
        }
        if(Step==2)
        {
            if(Now-Action<1.2)
            {
                Input->InjectInputForAction(C->GetMoveAction(),FInputActionValue(FVector2D(0,0.25)),{},{});
                Rows+=FString::Printf(TEXT("Movement t=%.3f Position=%s Current=%d\n"),Now-Action,*P->GetActorLocation().ToString(),R->GetTraversalSnapshot().bCurrent);return false;
            }
            Input->InjectInputForAction(C->GetMoveAction(),FInputActionValue(FVector2D::ZeroVector),{},{});
            if(!Check(FVector::Dist2D(Initial,P->GetActorLocation())>60,TEXT("Real EnhancedInput action drove standing CharacterMovement across more than four Watch cycles")))return Finish(false,TEXT("Player input did not move character"));
            Rows+=TEXT("MovementDistance=")+FString::SanitizeFloat(FVector::Dist2D(Initial,P->GetActorLocation()))+TEXT("\n");
            Input->InjectInputForAction(C->GetLookAction(),FInputActionValue(FVector2D(12,3)),{},{});Action=Now;Step=3;return false;
        }
        if(Step==3)
        {
            if(Now-Action<0.2)return false;
            if(!Check(FMath::Abs(FRotator::NormalizeAxis(C->GetControlRotation().Yaw-Look.Yaw))>1,TEXT("Real EnhancedInput look changes first-person control aim")))return Finish(false,TEXT("Look action did not rotate view"));
            if(!P->GetCharacterMovement()->IsMovingOnGround())return false;
            Initial=P->GetActorLocation();Peak=Initial.Z;Input->InjectInputForAction(C->GetJumpAction(),FInputActionValue(true),{},{});Action=Now;Step=4;return false;
        }
        if(Step==4)
        {
            Peak=FMath::Max(Peak,float(P->GetActorLocation().Z));
            if(Now-Action<0.1)Input->InjectInputForAction(C->GetJumpAction(),FInputActionValue(true),{},{});
            else Input->InjectInputForAction(C->GetJumpAction(),FInputActionValue(false),{},{});
            if(Now-Action<1.4)return false;
            Rows+=TEXT("JumpPeakDelta=")+FString::SanitizeFloat(Peak-Initial.Z)+TEXT("\n");
            const bool Good=Check(Peak-Initial.Z>20,TEXT("EnhancedInput Jump raises the real FPS capsule"))&&Check(R->GetTraversalSnapshot().bCurrent&&R->IsDungeonReady(),TEXT("Movement/look/jump retain original structural and navigation Ready"));
            return Finish(Good,TEXT("G1B-0 actual player ownership, input and bridge verified"));
        }
        return false;
    }
};
class FRestoreEncounterMode:public IAutomationLatentCommand
{
    TWeakObjectPtr<ARogueliteCombatSliceRig> Rig;bool Previous;
public:
    FRestoreEncounterMode(ARogueliteCombatSliceRig* R,bool Value):Rig(R),Previous(Value){}
    bool Update()override {if(Rig.IsValid())Rig->bEnableCombatEncounter=Previous;return true;}
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FG1BCreateMap,"ProjectVerification.G1B.CreateIndependentMap",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FG1BCreateMap::RunTest(const FString&)
{
    using namespace CombatSliceVerification;if(!GEditor||GEditor->PlayWorld)return false;
    if(FPackageName::DoesPackageExist(Map))return TestNotNull(TEXT("Load existing independent map without overwrite"),UEditorLoadingAndSavingUtils::LoadMap(Map));
    auto* W=UEditorLoadingAndSavingUtils::NewBlankMap(false);if(!W)return false;
    auto* WS=W->GetWorldSettings();WS->DefaultGameMode=ARogueliteSliceGameMode::StaticClass();
    auto* Config=NewObject<URogueliteSliceNavigationConfig>(WS);
    auto* Property=FindFProperty<FObjectPropertyBase>(AWorldSettings::StaticClass(),TEXT("NavigationSystemConfig"));if(!Property)return false;Property->SetObjectPropertyValue_InContainer(WS,Config);
    auto* Bounds=W->SpawnActor<ARogueliteRuntimeNavigationBounds>();Bounds->ConfigureBounds(FBox(FVector(-150,-150,-100),FVector(150,150,300)));
    Bounds->SetActorLabel(TEXT("G1B_OriginalNavigationBootstrap"));
    W->SpawnActor<ADirectionalLight>(FVector(0,0,1000),FRotator(-45,-30,0));W->SpawnActor<ASkyLight>();
    W->SpawnActor<APlayerStart>(FVector(0,0,300),FRotator::ZeroRotator);
    auto* R=W->SpawnActor<ARogueliteCombatSliceRig>();R->SetActorLabel(TEXT("G1B_SingleCombatSlice_Control"));
    R->FloorProfile=LoadObject<URogueliteFloorProfile>(nullptr,TEXT("/Game/Roguelite/ProductionCandidates/P03M1/Profiles/DA_M1_Floor1_Candidate.DA_M1_Floor1_Candidate"));
    if(!R->FloorProfile)return false;R->Seed=0;
    const auto Configured=R->FloorProfile->ResolvePreview(R->Seed);FString Why;
    if(!Configured.bValid||!FRoguelitePreparedTemplateCatalog::CaptureEditor(R,Configured.Source,R->PreparedTemplates,Why))return false;
    auto* Manual=W->SpawnActor<AActor>();Manual->SetActorLabel(TEXT("G1B_ProtectedManualMarker"));
    return TestTrue(TEXT("Save only new G1B map, empty runtime batch"),UEditorLoadingAndSavingUtils::SaveMap(W,Map));
}
IMPLEMENT_COMPLEX_AUTOMATION_TEST(FG1BPlayerGate,"ProjectVerification.G1B.Stage0",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
void FG1BPlayerGate::GetTests(TArray<FString>& Names,TArray<FString>& Commands)const
{Names={TEXT("RealLocalPlayer"),TEXT("ExternalBlocker")};Commands=Names;}
bool FG1BPlayerGate::RunTest(const FString& Scenario)
{
    using namespace CombatSliceVerification;
    if(!GEditor||GEditor->PlayWorld||!GEditor->GetEditorWorldContext().World()||GEditor->GetEditorWorldContext().World()->GetOutermost()->GetName()!=Map)return false;
    auto* EditorRig=Find(GEditor->GetEditorWorldContext().World());if(!EditorRig)return false;
    const bool Previous=EditorRig->bEnableCombatEncounter;EditorRig->bEnableCombatEncounter=false;
    auto& Framework=FAutomationTestFramework::Get();Framework.EnqueueLatentCommand(MakeShared<FStartPIECommand>(false));
    Framework.EnqueueLatentCommand(MakeShared<FPlayerExercise>(this,Scenario));Framework.EnqueueLatentCommand(MakeShared<FEndPlayMapCommand>());
    Framework.EnqueueLatentCommand(MakeShared<FRestoreEncounterMode>(EditorRig,Previous));return true;
}
#endif
