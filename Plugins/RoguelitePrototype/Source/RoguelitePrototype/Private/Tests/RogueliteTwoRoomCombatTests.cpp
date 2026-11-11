#include "Tests/RogueliteCombatSliceRig.h"
#include "Gameplay/RogueliteCombatSlicePlayer.h"
#include "Gameplay/RogueliteCombatSliceActors.h"
#include "Gameplay/RogueliteCombatSliceEncounter.h"
#include "RogueliteFloorProfile.h"
#include "RogueliteRuntimeNavigationSystem.h"
#include "Components/BoxComponent.h"
#include "Misc/AutomationTest.h"
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Editor.h"
#include "FileHelpers.h"
#include "Tests/AutomationEditorCommon.h"
#include "EngineUtils.h"
#include "Engine/DirectionalLight.h"
#include "Engine/SkyLight.h"
#include "GameFramework/WorldSettings.h"
#include "GameFramework/PlayerStart.h"
#include "Misc/PackageName.h"
#include "Misc/FileHelper.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "HAL/FileManager.h"
#include "UObject/UnrealType.h"

namespace TwoRoomCombatVerification
{
const TCHAR* Map=TEXT("/Game/Roguelite/Tests/P04G2/L_Roguelite_TwoCombatRooms_Dev");
ARogueliteCombatSliceRig* Find(UWorld* W)
{if(W)for(TActorIterator<ARogueliteCombatSliceRig>I(W);I;++I)if(!I->IsActorBeingDestroyed())return *I;return nullptr;}
void Save(const FString& Name,const FString& Text)
{
    FString Dir;if(!FParse::Value(FCommandLine::Get(),TEXT("G2EvidenceDir="),Dir))return;
    IFileManager::Get().MakeDirectory(*Dir,true);FString File=Dir/Name+TEXT(".txt");
    if(IFileManager::Get().FileExists(*File))File+=TEXT(".")+FDateTime::UtcNow().ToString(TEXT("%Y%m%d%H%M%S"))+TEXT(".rerun");
    FFileHelper::SaveStringToFile(Text,*File);
}
class FSmoke:public IAutomationLatentCommand
{
    FAutomationTestBase* Test;FString Case,Rows;double Started=0,ReadyAt=0;
    bool Check(bool Good,const FString& Why)
    {Rows+=FString::Printf(TEXT("Check=%s Pass=%d\n"),*Why,Good);if(!Good)Test->AddError(Why);return Good;}
    bool Finish(bool Good,const FString& Why)
    {if(!Good)Test->AddError(Why);Rows+=FString::Printf(TEXT("Passed=%d\nReason=%s\n"),Good,*Why);Save(TEXT("smoke-")+Case,Rows);return true;}
public:
    FSmoke(FAutomationTestBase* T,FString C):Test(T),Case(MoveTemp(C)){}
    bool Update() override
    {
        auto* W=GEditor?GEditor->PlayWorld.Get():nullptr;if(!W)return false;
        const double Now=FPlatformTime::Seconds();if(!Started)Started=Now;
        auto* R=Find(W);if(!R)return Finish(false,TEXT("G2 Rig missing"));
        if(Now-Started>220)return Finish(false,TEXT("G2 Ready timeout: ")+R->SliceStatus);
        if(!R->SliceFailure.IsEmpty())return Finish(false,R->SliceFailure);
        if(!R->bPlayerBridgeReady)return false;
        if(!ReadyAt){ReadyAt=Now;return false;}if(Now-ReadyAt<1.2)return false;
        auto* P=R->GetSlicePlayer();auto Rooms=R->GetRoomEncounters();
        bool Good=Check(R->bEnableTwoCombatRooms&&Rooms.Num()==2&&P&&P->HasLiveBinding()&&R->IsDungeonReady()&&R->GetTraversalSnapshot().bCurrent,TEXT("Two-room mode has one actually bound local player and original strict Ready"));
        Good&=Check(R->Batch.Rooms.Num()==4&&R->Batch.Edges.Num()==3&&R->CommittedLoops.Edges.IsEmpty(),TEXT("Unchanged M1 Floor1 four-room three-corridor tree"));
        TSet<FString> GateKeys;int32 EnemyCount=0,GateCount=0,Shared=0;
        for(auto* E:Rooms)
        {
            Good&=Check(E&&E->IsCoordinated()&&E->GetTargetNodeId()!=R->PlayerStartNode&&E->GetState()==ERogueliteSliceEncounterState::AwaitingEntry&&E->GetSpawnedEnemyCount()==2&&E->GetRemainingEnemyCount()==2,TEXT("Qualified room owns a complete independent Dormant encounter"));
            if(!E)continue;
            for(const auto& Weak:E->GetEnemies())
            {auto* Enemy=Weak.Get();Good&=Check(Enemy&&Enemy->IsDormant()&&!Enemy->IsActive()&&Enemy->GetNodeId()==E->GetTargetNodeId()&&Enemy->GetBoundBatch()==R->Batch.BatchId,TEXT("Enemy identity uses its actual room/Source/Batch"));++EnemyCount;}
            for(auto Id:E->GetIncidentConnectionIds())
            {
                auto* Gate=E->GetGate(Id);const FString Key=FString::Printf(TEXT("%d/%s"),E->GetTargetNodeId(),*Id.ToString());
                Good&=Check(Gate&&!Gate->IsClosed()&&Gate->GetRoomNodeId()==E->GetTargetNodeId()&&Gate->GetBoundBatch()==R->Batch.BatchId&&!GateKeys.Contains(Key),TEXT("Room-side gate identity is unique and initially open"));
                GateKeys.Add(Key);++GateCount;
                Rows+=FString::Printf(TEXT("Gate Node=%d Connection=%s Actor=%s Pose=%s\n"),E->GetTargetNodeId(),*Id.ToString(),*GetNameSafe(Gate),Gate?*Gate->GetActorTransform().ToHumanReadableString():TEXT("None"));
            }
        }
        for(const auto& Edge:R->GetTraversalSnapshot().Edges)
        {
            auto* A=R->GetEncounterForNode(Edge.NodeA);auto* B=R->GetEncounterForNode(Edge.NodeB);
            Rows+=FString::Printf(TEXT("Edge A=%d B=%d Connection=%s\n"),Edge.NodeA,Edge.NodeB,*Edge.ConnectionId.ToString());
            if(A&&B)
            {auto* Ga=A->GetGate(Edge.ConnectionId);auto* Gb=B->GetGate(Edge.ConnectionId);++Shared;Good&=Check(Ga&&Gb&&Ga!=Gb&&FVector::Dist(Ga->GetActorLocation(),Gb->GetActorLocation())>1000,TEXT("Shared ConnectionId has two distinct actual room-endpoint gates"));}
        }
        int32 Players=0;for(TActorIterator<ARogueliteSlicePlayer>I(W);I;++I)Players+=!I->IsActorBeingDestroyed();
        Good&=Check(Players==1&&EnemyCount==4&&P&&P->Ammo==30&&P->HealthComponent->GetHealth()==100&&R->GetActiveEncounter()==nullptr,TEXT("Both rosters ready before play, one fresh player, no prematurely active encounter"));
        Rows+=FString::Printf(TEXT("Seed=%d Graph=%s Layout=%s Player=%s PC=%s Enemies=%d Gates=%d SharedConnections=%d BridgeLeases=%d\n"),R->Seed,*R->Batch.GraphDigest,*R->Batch.LayoutDigest,*GetNameSafe(P),*GetNameSafe(P?P->GetController():nullptr),EnemyCount,GateCount,Shared,FRogueliteDynamicGameplayBridge::ActiveComponentCount(R));
        return Finish(Good,TEXT("Two-room initialization and independent endpoints verified in actual GameWorld"));
    }
};
class FAfter:public IAutomationLatentCommand
{
    FAutomationTestBase* Test;TWeakObjectPtr<ARogueliteCombatSliceRig> Rig;int32 Seed;FString Case;double Started=0;
public:
    FAfter(FAutomationTestBase* T,ARogueliteCombatSliceRig* R,int32 S,FString C):Test(T),Rig(R),Seed(S),Case(MoveTemp(C)){}
    bool Update()override
    {
        if(!Started)Started=FPlatformTime::Seconds();int32 Worlds=0;if(GEngine)for(const auto& C:GEngine->GetWorldContexts())Worlds+=C.WorldType==EWorldType::PIE;
        if((GEditor&&GEditor->PlayWorld)||Worlds){if(FPlatformTime::Seconds()-Started<15)return false;Test->AddError(TEXT("G2 actual PIE teardown timeout"));return true;}
        if(Rig.IsValid())Rig->Seed=Seed;auto* W=GEditor?GEditor->GetEditorWorldContext().World():nullptr;int32 Gameplay=0;
        if(W)for(TActorIterator<AActor>I(W);I;++I)if(!I->IsActorBeingDestroyed())Gameplay+=I->IsA<ARogueliteSlicePlayer>()||I->IsA<ARogueliteSliceEnemy>()||I->IsA<ARogueliteSliceGate>()||I->IsA<ARogueliteSliceRecoveryProbe>();
        const bool Good=Rig.IsValid()&&Rig->bEnableTwoCombatRooms&&Rig->Batch.OwnedActors.IsEmpty()&&Rig->Batch.Rooms.IsEmpty()&&Rig->Batch.Edges.IsEmpty()&&!Rig->Batch.BatchId.IsValid()&&!Rig->Batch.Registry&&Gameplay==0;
        Test->TestTrue(TEXT("G2 StopPIE restores empty Editor batch without runtime gameplay actors"),Good);Save(TEXT("stop-")+Case,FString::Printf(TEXT("PIEWorlds=%d Gameplay=%d EmptyBatch=%d RestoredSeed=%d\nPassed=%d\n"),Worlds,Gameplay,Good,Seed,Good));return true;
    }
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FG2CreateMap,"ProjectVerification.G2.CreateIndependentMap",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FG2CreateMap::RunTest(const FString&)
{
    using namespace TwoRoomCombatVerification;if(!GEditor||GEditor->PlayWorld)return false;
    if(FPackageName::DoesPackageExist(Map))return TestNotNull(TEXT("Load existing G2 map without overwrite"),UEditorLoadingAndSavingUtils::LoadMap(Map));
    auto* W=UEditorLoadingAndSavingUtils::NewBlankMap(false);if(!W)return false;
    auto* WS=W->GetWorldSettings();WS->DefaultGameMode=ARogueliteSliceGameMode::StaticClass();
    auto* Config=NewObject<URogueliteSliceNavigationConfig>(WS);auto* Property=FindFProperty<FObjectPropertyBase>(AWorldSettings::StaticClass(),TEXT("NavigationSystemConfig"));
    if(!Property)return false;Property->SetObjectPropertyValue_InContainer(WS,Config);
    auto* Bounds=W->SpawnActor<ARogueliteRuntimeNavigationBounds>();Bounds->ConfigureBounds(FBox(FVector(-150,-150,-100),FVector(150,150,300)));
    Bounds->SetActorLabel(TEXT("G2_OriginalNavigationBootstrap"));W->SpawnActor<ADirectionalLight>(FVector(0,0,1000),FRotator(-45,-30,0));W->SpawnActor<ASkyLight>();W->SpawnActor<APlayerStart>(FVector(0,0,300),FRotator::ZeroRotator);
    auto* R=W->SpawnActor<ARogueliteCombatSliceRig>();R->SetActorLabel(TEXT("G2_TwoCombatRooms_Control"));R->bEnableTwoCombatRooms=true;
    R->FloorProfile=LoadObject<URogueliteFloorProfile>(nullptr,TEXT("/Game/Roguelite/ProductionCandidates/P03M1/Profiles/DA_M1_Floor1_Candidate.DA_M1_Floor1_Candidate"));if(!R->FloorProfile)return false;
    R->Seed=0;const auto Configured=R->FloorProfile->ResolvePreview(0);FString Why;
    if(!Configured.bValid||!FRoguelitePreparedTemplateCatalog::CaptureEditor(R,Configured.Source,R->PreparedTemplates,Why))return false;
    W->SpawnActor<AActor>()->SetActorLabel(TEXT("G2_ProtectedManualMarker"));
    return TestTrue(TEXT("Save only new G2 map, explicit two-room mode and empty runtime batch"),UEditorLoadingAndSavingUtils::SaveMap(W,Map));
}
IMPLEMENT_COMPLEX_AUTOMATION_TEST(FG2Smoke,"ProjectVerification.G2.Smoke",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
void FG2Smoke::GetTests(TArray<FString>& Names,TArray<FString>& Commands)const
{Names={TEXT("Seed0"),TEXT("Seed1")};Commands=Names;}
bool FG2Smoke::RunTest(const FString& Case)
{
    using namespace TwoRoomCombatVerification;if(!GEditor||GEditor->PlayWorld)return false;auto* W=GEditor->GetEditorWorldContext().World();
    if(!W||W->GetOutermost()->GetName()!=Map)return false;auto* R=Find(W);if(!R||!R->bEnableTwoCombatRooms)return false;
    const int32 OldSeed=R->Seed;R->Seed=Case==TEXT("Seed1")?1:0;auto& F=FAutomationTestFramework::Get();
    F.EnqueueLatentCommand(MakeShared<FStartPIECommand>(false));F.EnqueueLatentCommand(MakeShared<FSmoke>(this,Case));F.EnqueueLatentCommand(MakeShared<FEndPlayMapCommand>());F.EnqueueLatentCommand(MakeShared<FAfter>(this,R,OldSeed,Case));return true;
}
#endif
