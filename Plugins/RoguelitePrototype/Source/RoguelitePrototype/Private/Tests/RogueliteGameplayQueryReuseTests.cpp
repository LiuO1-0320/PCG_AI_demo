#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Tests/RogueliteCombatSliceRig.h"
#include "Gameplay/RogueliteCombatSlicePlayer.h"
#include "Gameplay/RogueliteCombatSliceEncounter.h"
#include "RogueliteRoomTemplate.h"
#include "Components/BoxComponent.h"
#include "Components/SphereComponent.h"
#include "Engine/TargetPoint.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "Misc/AutomationTest.h"
#include "Tests/AutomationEditorCommon.h"
#include "Misc/FileHelper.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "HAL/FileManager.h"

// Module-private implementation check; no new Runtime/public API or header.
namespace RogueliteSliceEncounterImpl
{bool CanReuseTraversal(const ARogueliteRuntimeLoopAssembly*,const ARogueliteSlicePlayer*);
bool NativeWorldQueriesAreReadOnly(const UWorld*,FString*);}
namespace GameplayQueryReuseTests
{
class FExercise final:public IAutomationLatentCommand
{
    FAutomationTestBase* Test;FString Case,Rows;double Started=0,InjectedAt=0;bool Injected=false;
    TWeakObjectPtr<AActor> External;
    bool Finish(bool Good,const FString& Why)
    {
        if(!Good)Test->AddError(Why);
        if(External.IsValid())External->Destroy();
        Rows+=FString::Printf(TEXT("Passed=%d\nReason=%s\n"),Good,*Why);
        FString Dir;if(FParse::Value(FCommandLine::Get(),TEXT("PERF1CEvidenceDir="),Dir))
        {IFileManager::Get().MakeDirectory(*Dir,true);FFileHelper::SaveStringToFile(Rows,*(Dir/(Case+TEXT("-")+FDateTime::UtcNow().ToString(TEXT("%Y%m%d%H%M%S"))+TEXT(".txt"))));}
        return true;
    }
public:
    FExercise(FAutomationTestBase* T,FString C):Test(T),Case(MoveTemp(C)){}
    bool Update() override
    {
        auto* W=GEditor?GEditor->PlayWorld.Get():nullptr;if(!W)return false;
        if(!Started)Started=FPlatformTime::Seconds();
        ARogueliteCombatSliceRig* R=nullptr;for(TActorIterator<ARogueliteCombatSliceRig>I(W);I;++I){R=*I;break;}
        if(!R)return Finish(false,TEXT("G2 Rig missing"));
        if(FPlatformTime::Seconds()-Started>210)return Finish(false,TEXT("Timed out: ")+R->SliceStatus);
        if(Injected)
        {
            if(R->Stage==ERogueliteDungeonBuildStage::NavigationFailed&&!R->bGeometryValidated&&!R->Batch.bSucceeded&&!R->Batch.bGeometryValid&&R->Batch.bCleanupComplete
                &&R->Batch.OwnedActors.IsEmpty()&&R->Batch.Rooms.IsEmpty()&&R->Batch.Edges.IsEmpty()&&!IsValid(R->Batch.Registry)
                &&FRogueliteDynamicGameplayBridge::ActiveComponentCount(R)==0)
            {
                Rows+=FString::Printf(TEXT("WatchCleanupSeconds=%.4f\n"),FPlatformTime::Seconds()-InjectedAt);
                if(Case==TEXT("ExternalBetweenCalls")&&!Test->TestTrue(TEXT("Owned cleanup preserves the external fixture"),External.IsValid()))
                    return Finish(false,TEXT("External actor was incorrectly removed"));
                return Finish(true,Case==TEXT("StaticParameterWatch")
                    ?TEXT("Parameter-only identity matches Legacy; original strict Ready and Watch reject and clean the owned batch.")
                    :TEXT("Fresh synchronous rejection followed by original Watch cleanup; test-owned external fixtures were preserved."));
            }
            if(FPlatformTime::Seconds()-InjectedAt>2)return Finish(false,TEXT("Original Watch did not finish owned cleanup within two seconds"));
            return false;
        }
        if(!R->SliceFailure.IsEmpty())return Finish(false,R->SliceFailure);
        auto* P=R->GetSlicePlayer();if(!R->bPlayerBridgeReady||!P)return false;
        const bool Admitted=RogueliteSliceEncounterImpl::CanReuseTraversal(R,P);
        Rows+=FString::Printf(TEXT("NativeAdmission=%d World=%s Batch=%s\n"),Admitted,*W->GetPathName(),*R->Batch.BatchId.ToString());
        if(!Test->TestTrue(TEXT("Actual native Ready G2 chain is admitted"),Admitted))
        {FString Why;RogueliteSliceEncounterImpl::NativeWorldQueriesAreReadOnly(W,&Why);Rows+=Why+TEXT("\n");return Finish(false,TEXT("Native reuse unavailable: ")+Why);}
        auto* E=R->Encounter.Get();if(!E)return Finish(false,TEXT("Encounter missing"));
        E->TickComponent(.02f,LEVELTICK_All,&E->PrimaryComponentTick);
        if(!Test->TestTrue(TEXT("First fresh native synchronous check retains AwaitingEntry"),E->GetState()==ERogueliteSliceEncounterState::AwaitingEntry))
            return Finish(false,TEXT("Valid native chain rejected"));
        if(Case==TEXT("NativeChain"))return Finish(R->GetTraversalSnapshot().bCurrent&&R->IsDungeonReady(),TEXT("Native synchronous queries retain original current geometry and strict Ready."));
        if(Case==TEXT("UnknownActorFallback")||Case==TEXT("UnknownPrimitiveFallback"))
        {
            FActorSpawnParameters Args;Args.ObjectFlags|=RF_Transient;
            auto* A=Case==TEXT("UnknownActorFallback")?W->SpawnActor<ATargetPoint>(Args):W->SpawnActor<AActor>(Args);
            if(!A)return Finish(false,TEXT("Unknown-type fixture failed"));External=A;
            if(Case==TEXT("UnknownPrimitiveFallback"))
            {
                auto* Sphere=NewObject<USphereComponent>(A);A->SetRootComponent(Sphere);Sphere->SetMobility(EComponentMobility::Movable);
                Sphere->SetCollisionEnabled(ECollisionEnabled::NoCollision);Sphere->SetCanEverAffectNavigation(false);Sphere->RegisterComponent();
            }
            const bool Refused= !RogueliteSliceEncounterImpl::CanReuseTraversal(R,P);
            const bool LegacyBinding=P->HasLiveBinding();E->TickComponent(.02f,LEVELTICK_All,&E->PrimaryComponentTick);
            const bool Good=Refused&&LegacyBinding&&E->GetState()==ERogueliteSliceEncounterState::AwaitingEntry&&R->IsDungeonReady();
            Rows+=FString::Printf(TEXT("UnknownTypeAdmissionRefused=%d LegacyBinding=%d CurrentEncounter=%d\n"),Refused,LegacyBinding,E->GetState()==ERogueliteSliceEncounterState::AwaitingEntry);
            return Finish(Test->TestTrue(TEXT("Unreviewed nonblocking type uses original full queries and retains valid gameplay"),Good),TEXT("Unknown-type fallback checked without admitting the new type or caching a success."));
        }
        if(Case==TEXT("ExternalBetweenCalls"))
        {
            FActorSpawnParameters Args;Args.ObjectFlags|=RF_Transient;
            auto* A=W->SpawnActor<AActor>(Args);if(!A)return Finish(false,TEXT("External fixture failed"));External=A;
            auto* B=NewObject<UBoxComponent>(A);A->SetRootComponent(B);B->SetMobility(EComponentMobility::Movable);
            B->SetBoxExtent(FVector(50));B->SetCollisionResponseToAllChannels(ECR_Block);B->SetCanEverAffectNavigation(false);
            B->SetWorldLocation(FVector(11000,11000,200));B->RegisterComponent();
        }
        else if(Case==TEXT("StaticParameterWatch"))
        {
            const auto Before=R->GetTraversalSnapshot();R->Batch.Rooms[0].Room->RoomLength+=100;
            // Reproduce the original identity's two bCurrent queries.  A bare
            // dimension edit changes strict Current, not the mesh/door digest.
            const bool LegacyPlayerBinding=P->HasLiveBinding();const auto After=R->GetTraversalSnapshot();
            const bool LegacyDecision=LegacyPlayerBinding&&After.bCurrent&&After.BatchId==Before.BatchId&&After.Seed==Before.Seed
                &&After.GraphRevision==Before.GraphRevision&&After.GeometryRevision==Before.GeometryRevision&&After.StableDigest==Before.StableDigest;
            E->TickComponent(.02f,LEVELTICK_All,&E->PrimaryComponentTick);
            const bool NewDecision=E->GetState()==ERogueliteSliceEncounterState::AwaitingEntry;
            const bool StrictReady=R->IsDungeonReady();
            Rows+=FString::Printf(TEXT("ParameterOnly LegacyIdentity=%d NewIdentity=%d StrictReady=%d\n"),LegacyDecision,NewDecision,StrictReady);
            if(!Test->TestTrue(TEXT("Parameter-only identity matches original; strict Ready immediately rejects before original Watch"),LegacyDecision&&NewDecision&&!StrictReady))
                return Finish(false,TEXT("Parameter-only original/new contract differs"));
            Injected=true;InjectedAt=FPlatformTime::Seconds();return false;
        }
        else
        {
            TInlineComponentArray<UPrimitiveComponent*> Parts;R->Batch.Rooms[0].Room->GetComponents(Parts);bool Changed=false;
            for(auto* Part:Parts)if(Part->GetFName()==TEXT("Floor")&&Part->Mobility==EComponentMobility::Static)
            {Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);Changed=true;break;}
            if(!Test->TestTrue(TEXT("Intentional negative changes an actual registered Static floor collision"),Changed))
                return Finish(false,TEXT("Static collision fixture unavailable"));
        }
        // No frame boundary: a preceding successful query must not authorize a
        // second call after a world mutation, even within this same callback.
        E->TickComponent(.02f,LEVELTICK_All,&E->PrimaryComponentTick);
        const bool Rejected=E->GetState()==ERogueliteSliceEncounterState::Cancelled&&E->GetClearCount()==0;
        Rows+=TEXT("Rejected=")+FString::FromInt(Rejected)+TEXT(" Reason=")+E->GetLastReason()+TEXT("\n");
        if(!Test->TestTrue(TEXT("World change between same-stack checks is freshly rejected, never Cleared"),Rejected))
            return Finish(false,TEXT("Stale synchronous success escaped mutation boundary"));
        Injected=true;InjectedAt=FPlatformTime::Seconds();return false;
    }
};
}
IMPLEMENT_COMPLEX_AUTOMATION_TEST(FGameplayQueryReuseTests,"ProjectVerification.PERF1C.QueryReuse",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
void FGameplayQueryReuseTests::GetTests(TArray<FString>& Names,TArray<FString>& Commands)const
{Names={TEXT("NativeChain"),TEXT("ExternalBetweenCalls"),TEXT("StaticBetweenCalls"),TEXT("StaticParameterWatch"),TEXT("UnknownActorFallback"),TEXT("UnknownPrimitiveFallback")};Commands=Names;}
bool FGameplayQueryReuseTests::RunTest(const FString& Case)
{
    if(!GEditor||GEditor->PlayWorld)return false;auto* W=GEditor->GetEditorWorldContext().World();
    if(!W||W->GetOutermost()->GetName()!=TEXT("/Game/Roguelite/Tests/P04G2/L_Roguelite_TwoCombatRooms_Dev"))return false;
    auto& F=FAutomationTestFramework::Get();F.EnqueueLatentCommand(MakeShared<FStartPIECommand>(false));
    F.EnqueueLatentCommand(MakeShared<GameplayQueryReuseTests::FExercise>(this,Case));F.EnqueueLatentCommand(MakeShared<FEndPlayMapCommand>());return true;
}
#endif
