#include "Tests/RogueliteReadyProofTestTypes.h"

FString ARogueliteReadyProofUnknownSource::GetNavigationEpoch() const
{
    ++EpochCalls;
    if(EpochMode==1)return InjectedEpoch;
    if(EpochMode==2)return TEXT("InvalidRuntimeGeometry");
    if(EpochMode==3)return FString();
    if(EpochMode==4)return IsDungeonReady()?InjectedEpoch:TEXT("InvalidRuntimeGeometry");
    return Super::GetNavigationEpoch();
}

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "RogueliteReadyQueryProof.h"
#include "Tests/RogueliteCombatSliceRig.h"
#include "RogueliteParametricRoom.h"
#include "RogueliteFloorProfile.h"
#include "Components/BoxComponent.h"
#include "Misc/AutomationTest.h"
#include "Tests/AutomationEditorCommon.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Misc/FileHelper.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "HAL/FileManager.h"
#include <type_traits>

static_assert(!std::is_copy_constructible_v<FRogueliteReadyQueryProof>);
static_assert(!std::is_move_constructible_v<FRogueliteReadyQueryProof>);
static_assert(!std::is_copy_assignable_v<FRogueliteReadyQueryProof>);
static_assert(!std::is_move_assignable_v<FRogueliteReadyQueryProof>);

// Deliberately confined to native development tests. No reflected mutation API,
// callback hook in the production interval, or production Ready setter is added.
struct FRogueliteReadyProofTestAccess
{
    static void BorrowReport(URogueliteNavigationReadinessComponent& To,const URogueliteNavigationReadinessComponent& From)
    {To.Report=From.Report;To.Data=From.Data;To.Navigation=From.Navigation;}
    template<typename Mutator>
    static bool Rejected(ARogueliteRuntimeLoopAssembly& Source,Mutator Change,ERogueliteReadyProofFailure Expected,FString& Rows)
    {
        auto* N=Source.NavigationReadiness.Get();FRogueliteReadyQueryProof P;
        TGuardValue<bool> Q(Source.bReadyQueryInProgress,true);
        TGuardValue<const FRogueliteReadyQueryProof*> Slot(Source.ActiveReadyQueryProof,&P);
        const bool Produced=N->PrepareReadyQuery(Source,P)==ERogueliteReadyProofAdmission::Native&&Source.ProduceReadyQueryProof(P);
        if(!Produced){Rows+=TEXT("Protocol fixture failed to produce an actual strong proof\n");return false;}
        Change(P);const bool Accepted=N->ConsumeReadyQuery(Source,P);
        Rows+=FString::Printf(TEXT("ProtocolReject Accepted=%d Failure=%d Expected=%d\n"),Accepted,int32(P.Failure),int32(Expected));
        return !Accepted&&P.Failure==Expected;
    }
    static bool Protocol(ARogueliteRuntimeLoopAssembly& S,FAutomationTestBase& T,FString& Rows)
    {
        bool Good=true;auto C=[&](const TCHAR* Name,bool Value){Good&=Value;T.TestTrue(Name,Value);Rows+=FString::Printf(TEXT("Check=%s Pass=%d\n"),Name,Value);};
        auto* N=S.NavigationReadiness.Get();
        C(TEXT("Audited native G2 class admitted"),S.IsNativeReadyProofClass(S.GetClass()));
        C(TEXT("Native loop base admitted"),S.IsNativeReadyProofClass(ARogueliteRuntimeLoopAssembly::StaticClass()));
        C(TEXT("Unknown native Source not admitted"),!S.IsNativeReadyProofClass(ARogueliteReadyProofUnknownSource::StaticClass()));
        auto* BlueprintClass=NewObject<UBlueprintGeneratedClass>(GetTransientPackage(),NAME_None,RF_Transient);
        C(TEXT("Actual transient Blueprint UClass not admitted"),!S.IsNativeReadyProofClass(BlueprintClass));
        C(TEXT("Strong current positive control"),S.IsDungeonReady());
        C(TEXT("Source identity"),Rejected(S,[&](auto& P){P.Source=nullptr;},ERogueliteReadyProofFailure::Identity,Rows));
        C(TEXT("Readiness identity"),Rejected(S,[&](auto& P){P.Readiness=nullptr;},ERogueliteReadyProofFailure::Identity,Rows));
        C(TEXT("Different real World identity"),Rejected(S,[&](auto& P){P.World=GEditor->GetEditorWorldContext().World();},ERogueliteReadyProofFailure::World,Rows));
        C(TEXT("Different Batch identity"),Rejected(S,[&](auto& P){P.BatchId=FGuid::NewGuid();},ERogueliteReadyProofFailure::Batch,Rows));
        C(TEXT("Registry identity"),Rejected(S,[&](auto& P){P.Registry=nullptr;},ERogueliteReadyProofFailure::Registry,Rows));
        const int32 GraphRevision=S.CommittedLoops.GraphRevision;
        C(TEXT("Actual GraphRevision changed after production"),Rejected(S,[&](auto& P){++S.CommittedLoops.GraphRevision;},ERogueliteReadyProofFailure::Revision,Rows));
        S.CommittedLoops.GraphRevision=GraphRevision;
        const int32 GeometryRevision=S.CommittedLoops.GeometryRevision;
        C(TEXT("Actual GeometryRevision changed after production"),Rejected(S,[&](auto& P){++S.CommittedLoops.GeometryRevision;},ERogueliteReadyProofFailure::Revision,Rows));
        S.CommittedLoops.GeometryRevision=GeometryRevision;
        C(TEXT("Graph digest identity"),Rejected(S,[&](auto& P){P.GraphDigest+=TEXT("changed");},ERogueliteReadyProofFailure::Digest,Rows));
        C(TEXT("Layout digest identity"),Rejected(S,[&](auto& P){P.LayoutDigest+=TEXT("changed");},ERogueliteReadyProofFailure::Digest,Rows));
        C(TEXT("Committed geometry digest identity"),Rejected(S,[&](auto& P){P.GeometryDigest+=TEXT("changed");},ERogueliteReadyProofFailure::Digest,Rows));
        C(TEXT("Configuration binding"),Rejected(S,[&](auto& P){P.ConfigurationDigest+=TEXT("changed");},ERogueliteReadyProofFailure::Configuration,Rows));
        C(TEXT("Source transform binding"),Rejected(S,[&](auto& P){P.SourceTransform.AddToTranslation(FVector(1,0,0));},ERogueliteReadyProofFailure::Transform,Rows));
        C(TEXT("Actual NavData identity"),Rejected(S,[&](auto& P){P.NavData=nullptr;},ERogueliteReadyProofFailure::Navigation,Rows));
        C(TEXT("Invalid proof Epoch"),Rejected(S,[&](auto& P){P.Epoch=TEXT("InvalidRuntimeGeometry");},ERogueliteReadyProofFailure::Epoch,Rows));
        C(TEXT("Empty proof Epoch"),Rejected(S,[&](auto& P){P.Epoch.Reset();},ERogueliteReadyProofFailure::Epoch,Rows));
        {
            FRogueliteReadyQueryProof P;bool Produced=false;
            {
                TGuardValue<bool> Q(S.bReadyQueryInProgress,true);TGuardValue<const FRogueliteReadyQueryProof*> Slot(S.ActiveReadyQueryProof,&P);
                Produced=N->PrepareReadyQuery(S,P)==ERogueliteReadyProofAdmission::Native&&S.ProduceReadyQueryProof(P);
                C(TEXT("One actual strong proof consumed once"),Produced&&N->ConsumeReadyQuery(S,P));
                C(TEXT("Same-query proof replay refused"),!N->ConsumeReadyQuery(S,P)&&P.Failure==ERogueliteReadyProofFailure::QueryLifetime);
            }
            C(TEXT("Cross-call proof reuse refused"),Produced&&!N->ConsumeReadyQuery(S,P)&&P.Failure==ERogueliteReadyProofFailure::QueryLifetime);
        }
        {
            TGuardValue<bool> Q(S.bReadyQueryInProgress,true);
            C(TEXT("Reentrant Source Ready query refused"),!S.IsDungeonReady());
        }
        const auto Report=N->Report;
        for(const TCHAR* State:{TEXT("NavigationPending"),TEXT("NavigationFailed"),TEXT("NavigationTimedOut"),TEXT("NavigationUnavailable")})
        {N->Report.State=State;N->Report.bReady=false;C(State,!S.IsDungeonReady());N->Report=Report;}
        N->Report.Epoch=TEXT("InvalidRuntimeGeometry");C(TEXT("Invalid Report Epoch fail closed"),!N->IsCurrentBatchReady()&&!S.IsDungeonReady());N->Report=Report;
        N->Report.Epoch.Reset();C(TEXT("Empty Report Epoch fail closed"),!N->IsCurrentBatchReady()&&!S.IsDungeonReady());N->Report=Report;

        // A foreign component which really returns public Ready is a stronger negative
        // than a null/not-ready dummy. Its report/data are deliberately borrowed protocol
        // fixtures from this real NavMesh; no second generated dungeon is claimed.
        const FTransform Pose(FVector(20000,20000,0));
        auto* Foreign=S.GetWorld()->SpawnActorDeferred<ARogueliteReadyProofUnknownSource>(ARogueliteReadyProofUnknownSource::StaticClass(),Pose,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        Foreign->bAutoGenerate=false;Foreign->bNavigationEnabled=false;Foreign->FinishSpawning(Pose);
        Foreign->EpochMode=1;Foreign->InjectedEpoch=Report.Epoch;Foreign->Batch.BatchId=S.Batch.BatchId;Foreign->bGeometryValidated=true;
        BorrowReport(*Foreign->NavigationReadiness,*N);
        C(TEXT("Foreign Owner fixture has actual valid public Readiness"),Foreign->NavigationReadiness->IsCurrentBatchReady());
        S.NavigationReadiness=Foreign->NavigationReadiness;
        C(TEXT("Other Source's effective Ready cannot certify current Source"),!S.IsDungeonReady());S.NavigationReadiness=N;
        Foreign->Batch={};Foreign->bGeometryValidated=false;Foreign->Destroy();
        auto* Proxy=NewObject<URogueliteReadyProofUnknownReadiness>(&S,NAME_None,RF_Transient);
        S.AddInstanceComponent(Proxy);Proxy->RegisterComponent();BorrowReport(*Proxy,*N);S.NavigationReadiness=Proxy;
        C(TEXT("Unknown legitimate Readiness preserves Legacy positive"),Proxy->IsCurrentBatchReady()&&S.IsDungeonReady());
        const auto* R=S.Batch.Rooms[0].Room.Get();const double Length=R->RoomLength;
        S.Batch.Rooms[0].Room->RoomLength+=10;
        C(TEXT("Unknown Readiness Legacy still rejects illegal Static dimensions"),!S.IsDungeonReady());S.Batch.Rooms[0].Room->RoomLength=Length;
        S.NavigationReadiness=N;Proxy->DestroyComponent();
        C(TEXT("Native Source restored after protocol negatives"),S.IsDungeonReady());return Good;
    }
};

namespace Perf1BReadyTests
{
const TCHAR* Map=TEXT("/Game/Roguelite/Tests/P04G2/L_Roguelite_TwoCombatRooms_Dev");
void Save(const FString& Name,const FString& Text)
{
    FString D;if(!FParse::Value(FCommandLine::Get(),TEXT("PERF1BEvidenceDir="),D))return;
    IFileManager::Get().MakeDirectory(*D,true);FString File=D/Name+TEXT(".txt");
    if(IFileManager::Get().FileExists(*File))File+=TEXT(".")+FDateTime::UtcNow().ToString(TEXT("%Y%m%d%H%M%S"))+TEXT(".rerun");
    FFileHelper::SaveStringToFile(Text,*File,FFileHelper::EEncodingOptions::ForceUTF8);
}
struct FRun final:IAutomationLatentCommand
{
    FAutomationTestBase* Test;FString Case,Rows;double Started=FPlatformTime::Seconds();int32 Step=0;
    TWeakObjectPtr<ARogueliteCombatSliceRig> Primary;TWeakObjectPtr<ARogueliteReadyProofUnknownSource> Unknown;
    bool Good=true;
    FRun(FAutomationTestBase* T,const FString& C):Test(T),Case(C){}
    void Check(const TCHAR* Name,bool V){Good&=V;Test->TestTrue(Name,V);Rows+=FString::Printf(TEXT("Check=%s Pass=%d\n"),Name,V);}
    bool Finish(const FString& Why)
    {
        if(Unknown.IsValid()){Unknown->ClearRuntime();Unknown->Destroy();}
        if(Primary.IsValid())Primary->ClearRuntime();
        Rows+=FString::Printf(TEXT("Passed=%d\nReason=%s\n"),Good,*Why);Save(Case,Rows);return true;
    }
    bool Update() override
    {
        UWorld* W=GEditor?GEditor->PlayWorld:nullptr;
        if(!W)return false;
        if(FPlatformTime::Seconds()-Started>240){Check(TEXT("Bounded real PIE safety fixture ready"),false);return Finish(TEXT("Timeout; no fake Ready"));}
        if(Step==0&&!Primary.IsValid())for(TActorIterator<ARogueliteCombatSliceRig>I(W);I;++I)if(!I->IsActorBeingDestroyed()){Primary=*I;break;}
        auto* S=Primary.Get();if(Step==0&&!S)return false;
        if(Step==0)
        {
            if(!S->IsDungeonReady())return false;
            Check(TEXT("Real GameWorld strict navigation positive control"),W->IsGameWorld()&&S->bGeometryValidated&&S->NavigationReadiness->Report.bReady);
            Rows+=S->NavigationReadiness->Report.Reason+TEXT("\n");
            if(Case==TEXT("NativeProtocol"))
            {Good&=FRogueliteReadyProofTestAccess::Protocol(*S,*Test,Rows);return Finish(TEXT("Real full Current producer, private identity/lifetime negatives, and Legacy proxy verified"));}
            if(Case==TEXT("NativeStatic"))
            {
                auto* Room=S->Batch.Rooms[0].Room.Get();const auto Old=Room->RoomLength;Room->RoomLength+=10;
                Check(TEXT("Native fast path rejects actual illegal Static room dimensions"),!S->IsDungeonReady());Room->RoomLength=Old;
                Check(TEXT("Native valid geometry restored without reconstruction"),S->IsDungeonReady());
                FActorSpawnParameters P;P.ObjectFlags|=RF_Transient;auto* BoxActor=W->SpawnActor<AActor>(P);
                auto* Box=NewObject<UBoxComponent>(BoxActor);BoxActor->SetRootComponent(Box);Box->SetBoxExtent(FVector(50));Box->SetCollisionProfileName(TEXT("BlockAll"));Box->SetCanEverAffectNavigation(false);Box->SetMobility(EComponentMobility::Movable);
                Box->RegisterComponent();BoxActor->SetActorLocation(S->Batch.Rooms[0].PlacedTransform.GetLocation()+FVector(100,100,100));
                Check(TEXT("Native fast path rejects actual unregistered external blocking Box"),!S->IsDungeonReady());BoxActor->Destroy();
                Check(TEXT("External Box removal restores original fingerprint"),S->IsDungeonReady());
                TArray<UPrimitiveComponent*> Components;Room->GetComponents(Components);UPrimitiveComponent* Solid=nullptr;
                for(auto* C:Components)if(C->GetCollisionEnabled()!=ECollisionEnabled::NoCollision){Solid=C;break;}
                Check(TEXT("Actual Static collision negative has solid component"),Solid!=nullptr);
                const auto Id=S->Batch.BatchId;S->Batch.BatchId=FGuid::NewGuid();Check(TEXT("Actual Batch replacement rejects old navigation"),!S->IsDungeonReady());S->Batch.BatchId=Id;
                if(Solid){const auto Mode=Solid->GetCollisionEnabled();Solid->SetCollisionEnabled(ECollisionEnabled::NoCollision);Check(TEXT("Actual illegal Static collision rejected by strong Current and Ready"),S->GetNavigationEpoch()==TEXT("InvalidRuntimeGeometry")&&!S->IsDungeonReady());Solid->SetCollisionEnabled(Mode);}
                Check(TEXT("Actual Source Clear succeeds"),S->ClearRuntime());Check(TEXT("Cleared source cannot reuse previous Ready proof"),!S->IsDungeonReady());
                return Finish(TEXT("Actual negative objects/configuration restored or owned batch cleared; no global changes"));
            }
            // Real unknown native Source, generated through unchanged C2/B4-B3 APIs.
            auto Profile=S->FloorProfile;auto Catalog=S->PreparedTemplates;
            Check(TEXT("Clear only original owned PIE batch before unknown Source"),S->ClearRuntime());
            S->Destroy();
            const FTransform Pose(FVector::ZeroVector);
            auto* U=W->SpawnActorDeferred<ARogueliteReadyProofUnknownSource>(ARogueliteReadyProofUnknownSource::StaticClass(),Pose,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
            U->bAutoGenerate=false;U->Seed=0;U->FloorProfile=Profile;U->PreparedTemplates=Catalog;U->FinishSpawning(Pose);Unknown=U;
            Check(TEXT("Unknown Source uses actual original runtime generation"),U->GenerateRuntime());Step=1;return false;
        }
        auto* U=Unknown.Get();if(!U){Check(TEXT("Unknown Source still valid"),false);return Finish(TEXT("Missing unknown Source"));}
        if(!U->IsDungeonReady())return false;
        const int32 Before=U->EpochCalls;
        Check(TEXT("Legitimate unknown Source Legacy positive"),U->IsDungeonReady()&&U->EpochCalls>Before);
        Rows+=U->NavigationReadiness->Report.Reason+TEXT("\n");
        const FString FullEpoch=U->NavigationReadiness->Report.Epoch;
        U->EpochMode=1;U->InjectedEpoch=TEXT("WeakEpoch-PERF1B");U->NavigationReadiness->Report.Epoch=U->InjectedEpoch;
        Check(TEXT("Weak Epoch public Readiness fixture actually true"),U->NavigationReadiness->IsCurrentBatchReady());
        auto* Room=U->Batch.Rooms[0].Room.Get();const auto Length=Room->RoomLength;Room->RoomLength+=10;
        Check(TEXT("Weak virtual Epoch cannot replace Legacy full strong Current"),!U->IsDungeonReady());Room->RoomLength=Length;
        U->EpochMode=2;U->NavigationReadiness->Report.Epoch=FullEpoch;int32 InvalidCalls=U->EpochCalls;Check(TEXT("Invalid virtual Epoch actually invoked and refused"),!U->IsDungeonReady()&&U->EpochCalls>InvalidCalls);
        U->EpochMode=3;U->NavigationReadiness->Report.Epoch=FullEpoch;InvalidCalls=U->EpochCalls;Check(TEXT("Empty virtual Epoch actually invoked and refused"),!U->IsDungeonReady()&&U->EpochCalls>InvalidCalls);
        U->EpochMode=4;U->NavigationReadiness->Report.Epoch=FullEpoch;Check(TEXT("Virtual Epoch reentry cannot borrow outer query proof"),!U->IsDungeonReady());
        U->EpochMode=0;U->NavigationReadiness->Report.Epoch=FullEpoch;Check(TEXT("Original unknown native Ready restored"),U->IsDungeonReady());
        return Finish(TEXT("Actual unknown-source generation/NavMesh/Character MoveTo and retained Legacy virtual path verified"));
    }
};
}
IMPLEMENT_COMPLEX_AUTOMATION_TEST(FRogueliteReadyProofPIETest,"ProjectVerification.PERF1B.ReadyProof",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
void FRogueliteReadyProofPIETest::GetTests(TArray<FString>& Names,TArray<FString>& Commands) const
{Names={TEXT("NativeProtocol"),TEXT("NativeStatic"),TEXT("UnknownSource")};Commands=Names;}
bool FRogueliteReadyProofPIETest::RunTest(const FString& Parameters)
{
    if(GEditor->PlayWorld){AddError(TEXT("Test refuses to replace a running user PIE"));return false;}
    FAutomationEditorCommonUtils::LoadMap(Perf1BReadyTests::Map);
    ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
    FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<Perf1BReadyTests::FRun>(this,Parameters));
    ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());return true;
}
#endif
