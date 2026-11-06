#include "Tests/RogueliteDynamicBridgeTestRig.h"
#include "RogueliteRuntimeNavigationSystem.h"
#include "RogueliteFloorProfile.h"
#include "RoguelitePrototype.h"
#include "RogueliteDynamicGameplayBridge.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "UObject/ConstructorHelpers.h"
#include "UObject/UnrealType.h"
#include "UObject/Package.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/FileHelper.h"
#include "TimerManager.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
bool StartG1AAdvancedScenario(ARogueliteDynamicBridgeTestRig*,const FString&);
#endif

URogueliteDynamicBridgeNavigationConfig::URogueliteDynamicBridgeNavigationConfig()
{NavigationSystemClass=FSoftClassPath(URogueliteRuntimeNavigationSystem::StaticClass());}

UNavigationSystemBase* URogueliteDynamicBridgeNavigationConfig::CreateAndConfigureNavigationSystem(UWorld& World) const
{
    auto* Base=Super::CreateAndConfigureNavigationSystem(World);
    auto* Nav=Cast<URogueliteRuntimeNavigationSystem>(Base);
    if(!Nav||!World.GetOutermost()->GetName().EndsWith(TEXT("L_Roguelite_DynamicGameplayBridge_Dev")))return Base;
    // Configure only this instance, before world initialization. Never change a CDO or global Config.
    auto* Agents=FindFProperty<FArrayProperty>(Nav->GetClass(),TEXT("SupportedAgents"));
    auto* Default=FindFProperty<FNameProperty>(Nav->GetClass(),TEXT("DefaultAgentName"));
    if(!Agents||!Default)return Base;
    FNavDataConfig Agent;Agent.Name=TEXT("RogueliteB3BStandingTest");Agent.AgentRadius=35;Agent.AgentHeight=180;
    Agent.SetNavDataClass(ARogueliteRuntimeRecastNavMesh::StaticClass());
    *Agents->ContainerPtrToValuePtr<TArray<FNavDataConfig>>(Nav)={Agent};
    Default->SetPropertyValue_InContainer(Nav,Agent.Name);
    for(const auto& Pair:TArray<TPair<FName,bool>>{{TEXT("bAutoCreateNavigationData"),true},{TEXT("bGenerateNavigationOnlyAroundNavigationInvokers"),false},{TEXT("bInitialBuildingLocked"),false},{TEXT("bSkipAgentHeightCheckWhenPickingNavData"),false}})
        if(auto* P=FindFProperty<FBoolProperty>(Nav->GetClass(),Pair.Key))P->SetPropertyValue_InContainer(Nav,Pair.Value);
    UE_LOG(LogRoguelitePrototype,Display,TEXT("G1A_NAV_CONFIG ExistingSystem=%s World=%s Agent=35x180 DynamicRecast=1"),*Nav->GetClass()->GetPathName(),*World.GetPathName());
    return Base;
}

ARogueliteDynamicBridgeTestCharacter::ARogueliteDynamicBridgeTestCharacter()
{
    GetCapsuleComponent()->InitCapsuleSize(34,88);GetCapsuleComponent()->SetCollisionProfileName(TEXT("Pawn"));
    GetCapsuleComponent()->SetCanEverAffectNavigation(false);AutoPossessAI=EAutoPossessAI::Disabled;
    GetCharacterMovement()->bRunPhysicsWithNoController=true;GetCharacterMovement()->MaxFlySpeed=180;
    GetCharacterMovement()->MaxWalkSpeed=600;GetCharacterMovement()->GetNavAgentPropertiesRef().AgentRadius=34;
    GetCharacterMovement()->GetNavAgentPropertiesRef().AgentHeight=176;
    auto* Body=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("TestBody"));Body->SetupAttachment(GetRootComponent());
    static ConstructorHelpers::FObjectFinder<UStaticMesh> TestBodyMesh(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    if(TestBodyMesh.Succeeded())Body->SetStaticMesh(TestBodyMesh.Object);Body->SetRelativeScale3D(FVector(.6,.6,1.5));
    Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);Body->SetCanEverAffectNavigation(false);
}

ARogueliteDynamicBridgeTestGate::ARogueliteDynamicBridgeTestGate()
{
    PrimaryActorTick.bCanEverTick=false;Blocker=CreateDefaultSubobject<UBoxComponent>(TEXT("DynamicGateBlocker"));
    SetRootComponent(Blocker);Blocker->SetMobility(EComponentMobility::Movable);Blocker->InitBoxExtent(FVector(25,250,250));
    Blocker->SetCollisionObjectType(ECC_WorldDynamic);Blocker->SetCollisionResponseToAllChannels(ECR_Block);
    Blocker->SetCanEverAffectNavigation(false);Blocker->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Blocker->SetHiddenInGame(false);Blocker->ShapeColor=FColor::Orange;
}
void ARogueliteDynamicBridgeTestGate::SetClosed(bool Closed)
{Blocker->SetCollisionEnabled(Closed?ECollisionEnabled::QueryAndPhysics:ECollisionEnabled::NoCollision);FRogueliteDynamicGameplayBridge::NotifyGateCollisionChanged(Blocker);}
void ARogueliteDynamicBridgeTestGate::PlaceAtSocket(const FRogueliteDoorSocket& S)
{Blocker->SetBoxExtent(FVector(25,S.DoorWidth*.5,S.DoorHeight*.5));SetActorLocationAndRotation(S.WorldPosition+FVector::UpVector*S.DoorHeight*.5,FRotationMatrix::MakeFromX(S.WorldOutwardDirection).Rotator());}

ARogueliteDynamicBridgeTestRig::ARogueliteDynamicBridgeTestRig()
{bAutoGenerate=false;bNavigationEnabled=true;}
void ARogueliteDynamicBridgeTestRig::BeginPlay()
{
    bAutoGenerate=false;Super::BeginPlay();
    if(bAutoRunScenario)GetWorld()->GetTimerManager().SetTimer(ScenarioTimer,FTimerDelegate::CreateWeakLambda(this,[this](){StartScenario(Scenario);}),1,false);
}
void ARogueliteDynamicBridgeTestRig::EndPlay(const EEndPlayReason::Type Reason)
{GetWorld()->GetTimerManager().ClearTimer(ScenarioTimer);ClearTestObjects();Super::EndPlay(Reason);}
void ARogueliteDynamicBridgeTestRig::ClearTestObjects()
{if(TestPawn.IsValid())TestPawn->Destroy();if(TestGate.IsValid())TestGate->Destroy();TestPawn.Reset();TestGate.Reset();}

bool ARogueliteDynamicBridgeTestRig::StartScenario(const FString& Name)
{
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
    if(Name!=TEXT("BaselinePawn")&&Name!=TEXT("BaselineGate"))return StartG1AAdvancedScenario(this,Name);
#endif
    if(!GetWorld()||!GetWorld()->IsGameWorld()||!Batch.OwnedActors.IsEmpty()||(Name!=TEXT("BaselinePawn")&&Name!=TEXT("BaselineGate")))return false;
    Scenario=Name;bScenarioFinished=bScenarioPassed=false;ScenarioEvidence.Reset();ScenarioStep=0;ObservedBatch.Invalidate();ObservedDigest.Reset();
    ClearTestObjects();FActorSpawnParameters P;P.Owner=this;P.ObjectFlags|=RF_Transient;
    if(Scenario==TEXT("BaselinePawn"))
    {
        TestPawn=GetWorld()->SpawnActor<ARogueliteDynamicBridgeTestCharacter>(FVector(-6000,-6000,400),FRotator::ZeroRotator,P);
        if(!TestPawn.IsValid())return false;TestPawn->GetCharacterMovement()->SetMovementMode(MOVE_Flying);
    }
    else TestGate=GetWorld()->SpawnActor<ARogueliteDynamicBridgeTestGate>(P);
    ScenarioStarted=FPlatformTime::Seconds();
    if(!GenerateRuntime()){FinishScenario(false,TEXT("Geometry generation failed: ")+LastGeometry.Reason);return false;}
    GetWorld()->GetTimerManager().SetTimer(ScenarioTimer,this,&ARogueliteDynamicBridgeTestRig::PollScenario,.1,true);return true;
}
void ARogueliteDynamicBridgeTestRig::PollScenario()
{
    if(ScenarioStep==0)
    {
        if(!IsDungeonReady())
        {
            const FString State=NavigationReadiness->Report.State;
            if(FPlatformTime::Seconds()-ScenarioStarted>240||State.Contains(TEXT("Failed"))||State.Contains(TEXT("Unavailable"))||State.Contains(TEXT("TimedOut")))FinishScenario(false,TEXT("Initial strict navigation did not become Ready: ")+State+TEXT(" ")+NavigationReadiness->Report.Reason);
            return;
        }
        const auto T=GetTraversalSnapshot();ObservedBatch=Batch.BatchId;ObservedDigest=CommittedLoops.GeometryDigest;
        ScenarioEvidence=FString::Printf(TEXT("Scenario=%s\nWorld=%s\nBatch=%s\nGeometryValidated=%d\nNavigationReady=%d\nTraversalCurrent=%d\nRooms=%d\nTree=%d\nLoop=%d\nDigestBefore=%s\nPaths=%d\nMoves=%d\n"),*Scenario,*GetWorld()->GetPathName(),*ObservedBatch.ToString(),bGeometryValidated,IsDungeonReady(),T.bCurrent,Batch.Rooms.Num(),Batch.Edges.Num(),CommittedLoops.Edges.Num(),*ObservedDigest,NavigationReadiness->Report.Paths.Num(),NavigationReadiness->Report.Moves.Num());
        if(TestPawn.IsValid())
        {PawnStart=TestPawn->GetActorLocation();TestPawn->GetCharacterMovement()->Velocity=FVector(120,0,0);TestPawn->AddMovementInput(FVector::ForwardVector,1,true);}
        else if(TestGate.IsValid())
        {
            const auto& E=Batch.Edges[0];const auto* R=Batch.Rooms.FindByPredicate([&](const auto& X){return X.NodeId==E.ParentNode;});FRogueliteDoorSocket Socket;
            if(!R||!R->Room->TryGetDoorSocket(E.ParentSocket,Socket)){FinishScenario(false,TEXT("Actual Tree socket unavailable"));return;}
            TestGate->PlaceAtSocket(Socket);TestGate->SetClosed(true);
            ScenarioEvidence+=FString::Printf(TEXT("GateConnection=%s\nGateNode=%d\nGateSocket=%d\nGateBlocksPawn=%d\n"),*E.ConnectionId.ToString(),R->NodeId,int32(E.ParentSocket),TestGate->Blocker->GetCollisionResponseToChannel(ECC_Pawn)==ECR_Block);
        }
        ScenarioStep=1;ActionStarted=FPlatformTime::Seconds();return;
    }
    if(TestPawn.IsValid())TestPawn->AddMovementInput(FVector::ForwardVector,1,true);
    const auto CurrentDigest=FRogueliteLoopCommitQueries::GeometryDigest(this,Batch,CommittedLoops);
    const bool Current=GetTraversalSnapshot().bCurrent;const double Elapsed=FPlatformTime::Seconds()-ActionStarted;
    ScenarioEvidence+=FString::Printf(TEXT("Sample t=%.3f Current=%d Geometry=%d Owned=%d Digest=%s Pawn=%s\n"),Elapsed,Current,bGeometryValidated,Batch.OwnedActors.Num(),*CurrentDigest,TestPawn.IsValid()?*TestPawn->GetActorLocation().ToString():TEXT("none"));
    if(Elapsed<1.1)return;
    const float Moved=TestPawn.IsValid()?FVector::Dist(TestPawn->GetActorLocation(),PawnStart):0;
    const bool Cleared=!bGeometryValidated&&Batch.OwnedActors.IsEmpty()&&Stage==ERogueliteDungeonBuildStage::NavigationFailed;
    ScenarioEvidence+=FString::Printf(TEXT("ActualDisplacement=%.3f\nExpectedOldWatchClear=%d\nLastFailure=%s\n"),Moved,Cleared,*LastFailureReason);
    FinishScenario(Cleared&&(Scenario==TEXT("BaselineGate")||Moved>30),TEXT("Observed old runtime after more than four WatchGeometry periods; no bridge core edits."));
}
void ARogueliteDynamicBridgeTestRig::FinishScenario(bool Passed,const FString& Why)
{
    GetWorld()->GetTimerManager().ClearTimer(ScenarioTimer);bScenarioFinished=true;bScenarioPassed=Passed;
    ScenarioEvidence+=FString::Printf(TEXT("Passed=%d\nReason=%s\n"),Passed,*Why);
    FString Dir;FParse::Value(FCommandLine::Get(),TEXT("G1AEvidenceDir="),Dir);
    if(!Dir.IsEmpty())
    {
        IFileManager::Get().MakeDirectory(*Dir,true);FString File=Dir/TEXT("scenario-")+Scenario+TEXT(".txt");
        if(IFileManager::Get().FileExists(*File))File+=TEXT(".")+FDateTime::UtcNow().ToString(TEXT("%Y%m%d%H%M%S"))+TEXT(".rerun");
        FFileHelper::SaveStringToFile(ScenarioEvidence,*File,FFileHelper::EEncodingOptions::ForceUTF8);
        FString Text;FRogueliteNavigationReadinessEvidence::StaticStruct()->ExportText(Text,&NavigationReadiness->Report,nullptr,nullptr,PPF_None,nullptr);
        FFileHelper::SaveStringToFile(Text,*(File+TEXT(".navigation")),FFileHelper::EEncodingOptions::ForceUTF8);
    }
    UE_LOG(LogRoguelitePrototype,Display,TEXT("G1A_SCENARIO Name=%s Passed=%d %s"),*Scenario,Passed,*Why);
    ClearTestObjects();if(!Batch.OwnedActors.IsEmpty())ClearRuntime();
}
