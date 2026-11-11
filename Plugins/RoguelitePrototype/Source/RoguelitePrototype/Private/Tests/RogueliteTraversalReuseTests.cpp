#include "Tests/RogueliteTraversalReuseTestTypes.h"
#include "RogueliteRuntimeLoopAssembly.h"
#include "RogueliteRoomTemplate.h"

ECollisionEnabled::Type URogueliteTraversalGetterTestComponent::GetCollisionEnabled() const
{
    ++QueryCount;
    if(bArmed&&!bChanged&&Source.IsValid()&&!Source->Batch.Rooms.IsEmpty())
    {bChanged=true;Source->Batch.Rooms[0].Room->RoomLength+=10;}
    return Super::GetCollisionEnabled();
}

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "RogueliteReadyQueryProof.h"
#include "Tests/RogueliteCombatSliceRig.h"
#include "Components/BoxComponent.h"
#include "Components/SphereComponent.h"
#include "Engine/TargetPoint.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "Misc/AutomationTest.h"
#include "Tests/AutomationEditorCommon.h"
#include "Misc/FileHelper.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "HAL/FileManager.h"
#include "UObject/StrongObjectPtr.h"

struct FRogueliteTraversalReuseTestAccess
{
    static bool Admission(const ARogueliteRuntimeLoopAssembly& S,FString& Why)
    {return S.CanReuseNativeTraversal(&Why);}
    static FRogueliteLoopTraversalSnapshot Legacy(const ARogueliteRuntimeLoopAssembly& S)
    {
        auto V=FRogueliteLoopCommitQueries::Capture(const_cast<ARogueliteRuntimeLoopAssembly*>(&S),S.Batch,S.Settings,S.CommittedLoops);
        V.bNavigationReady=V.bCurrent&&S.IsDungeonReady();
        V.NavigationState=V.bNavigationReady?TEXT("NavigationReady"):TEXT("NavigationPending");return V;
    }
    template<typename Mutation>
    static bool Reject(ARogueliteRuntimeLoopAssembly& S,Mutation Change,FString& Rows)
    {
        FRogueliteReadyQueryProof Q;Q.Source=&S;Q.bTraversalAdmitted=S.CanReuseNativeTraversal();
        TGuardValue<FRogueliteReadyQueryProof*> Scope(S.ActiveTraversalRequest,&Q);
        const bool Ready=S.IsDungeonReady();
        if(!Ready||!Q.bProduced||!Q.bTraversalDelivered){Rows+=TEXT("Handoff positive fixture did not produce consumed strong proof\n");return false;}
        Change(Q);FRogueliteLoopTraversalSnapshot Value;
        return !S.TakeCurrentTraversal(Q,Value);
    }
    static bool Protocol(ARogueliteRuntimeLoopAssembly& S,FAutomationTestBase& T,FString& Rows)
    {
        bool Good=true;auto Check=[&](const TCHAR* N,bool V){Good&=V;T.TestTrue(N,V);Rows+=FString::Printf(TEXT("%s=%d\n"),N,V);};
        Check(TEXT("Source"),Reject(S,[](auto& Q){Q.Source=nullptr;},Rows));
        Check(TEXT("World"),Reject(S,[](auto& Q){Q.World=GEditor->GetEditorWorldContext().World();},Rows));
        Check(TEXT("Readiness"),Reject(S,[](auto& Q){Q.Readiness=nullptr;},Rows));
        Check(TEXT("Batch"),Reject(S,[](auto& Q){Q.BatchId=FGuid::NewGuid();},Rows));
        Check(TEXT("Registry"),Reject(S,[](auto& Q){Q.Registry=nullptr;},Rows));
        Check(TEXT("GraphRevision"),Reject(S,[](auto& Q){++Q.GraphRevision;},Rows));
        Check(TEXT("GeometryRevision"),Reject(S,[](auto& Q){++Q.GeometryRevision;},Rows));
        Check(TEXT("GraphDigest"),Reject(S,[](auto& Q){Q.GraphDigest+=TEXT("changed");},Rows));
        Check(TEXT("LayoutDigest"),Reject(S,[](auto& Q){Q.LayoutDigest+=TEXT("changed");},Rows));
        Check(TEXT("GeometryDigest"),Reject(S,[](auto& Q){Q.GeometryDigest+=TEXT("changed");},Rows));
        Check(TEXT("ConfigDigest"),Reject(S,[](auto& Q){Q.ConfigurationDigest+=TEXT("changed");},Rows));
        Check(TEXT("Transform"),Reject(S,[](auto& Q){Q.SourceTransform.AddToTranslation(FVector(1,0,0));},Rows));
        Check(TEXT("NavData"),Reject(S,[](auto& Q){Q.NavData=nullptr;},Rows));
        Check(TEXT("NavigationSystem"),Reject(S,[](auto& Q){Q.Navigation=nullptr;},Rows));
        Check(TEXT("InvalidEpoch"),Reject(S,[](auto& Q){Q.Epoch=TEXT("InvalidRuntimeGeometry");},Rows));
        Check(TEXT("EmptyEpoch"),Reject(S,[](auto& Q){Q.Epoch.Reset();},Rows));
        Check(TEXT("PayloadRevision"),Reject(S,[](auto& Q){++Q.Traversal.GetValue().GeometryRevision;},Rows));
        Check(TEXT("NestedSnapshotInvalidatesOuter"),Reject(S,[&](auto& Q){S.GetTraversalSnapshot();Check(TEXT("NestedSnapshot marked invalid"),Q.bTraversalInvalidated);},Rows));
        Check(TEXT("NestedReadyInvalidatesOuter"),Reject(S,[&](auto& Q){TGuardValue<bool> Busy(S.bReadyQueryInProgress,true);Check(TEXT("Nested Ready refused"),!S.IsDungeonReady());},Rows));
        FRogueliteReadyQueryProof Q;bool Produced=false;
        {
            Q.Source=&S;Q.bTraversalAdmitted=S.CanReuseNativeTraversal();
            TGuardValue<FRogueliteReadyQueryProof*> Scope(S.ActiveTraversalRequest,&Q);
            Produced=S.IsDungeonReady()&&Q.bProduced;FRogueliteLoopTraversalSnapshot Value;
            Check(TEXT("One current handoff consumed"),Produced&&S.TakeCurrentTraversal(Q,Value));
            Check(TEXT("Same scope replay refused"),!S.TakeCurrentTraversal(Q,Value));
        }
        FRogueliteLoopTraversalSnapshot Value;Check(TEXT("Cross-scope replay refused"),Produced&&!S.TakeCurrentTraversal(Q,Value));
        return Good;
    }
    static bool ClearDuringHandoff(ARogueliteRuntimeLoopAssembly& S,FString& Rows)
    {return Reject(S,[&](auto&){FString Unused;Rows+=TEXT("Actual Source Clear=")+FString::FromInt(S.ClearRuntime())+TEXT("\n");},Rows);}
};

namespace TraversalReuseTests
{
const TCHAR* Map=TEXT("/Game/Roguelite/Tests/P04G2/L_Roguelite_TwoCombatRooms_Dev");
struct FRun final:IAutomationLatentCommand
{
    FAutomationTestBase* Test;FString Case,Rows;double Started=FPlatformTime::Seconds();bool Good=true;
    FRun(FAutomationTestBase* T,FString C):Test(T),Case(MoveTemp(C)){}
    void Check(const TCHAR* Name,bool V){Good&=V;Test->TestTrue(Name,V);Rows+=FString::Printf(TEXT("Check=%s Pass=%d\n"),Name,V);}
    bool Equal(ARogueliteRuntimeLoopAssembly& S,const TCHAR* Name)
    {
        const auto Expected=FRogueliteTraversalReuseTestAccess::Legacy(S);const auto Actual=S.GetTraversalSnapshot();
        const bool Same=FRogueliteLoopTraversalSnapshot::StaticStruct()->CompareScriptStruct(&Expected,&Actual,0);
        Rows+=FString::Printf(TEXT("%s Current=%d Ready=%d Nodes=%d Edges=%d Reason=%s\n"),Name,Actual.bCurrent,Actual.bNavigationReady,Actual.Nodes.Num(),Actual.Edges.Num(),*Actual.Reason);
        Check(Name,Same);return Same;
    }
    bool Finish(ARogueliteCombatSliceRig* R,const FString& Why)
    {
        if(R&&R->Batch.bSucceeded)Check(TEXT("Owned batch cleanup"),R->ClearRuntime());
        Rows+=FString::Printf(TEXT("Passed=%d\nReason=%s\n"),Good,*Why);
        FString D;if(FParse::Value(FCommandLine::Get(),TEXT("PERF1D1EvidenceDir="),D))
        {IFileManager::Get().MakeDirectory(*D,true);FFileHelper::SaveStringToFile(Rows,*(D/(Case+TEXT("-")+FDateTime::UtcNow().ToString(TEXT("%Y%m%d%H%M%S"))+TEXT(".txt"))));}
        return true;
    }
    bool Update() override
    {
        UWorld* W=GEditor?GEditor->PlayWorld.Get():nullptr;if(!W)return false;
        ARogueliteCombatSliceRig* R=nullptr;for(TActorIterator<ARogueliteCombatSliceRig> I(W);I;++I){R=*I;break;}
        if(!R)return false;
        if(FPlatformTime::Seconds()-Started>210){Check(TEXT("Bounded real Ready fixture"),false);return Finish(R,R->SliceStatus);}
        if(!R->bPlayerBridgeReady||!R->IsDungeonReady())return false;
        FString Why;Check(TEXT("Exact native fixture admission"),FRogueliteTraversalReuseTestAccess::Admission(*R,Why));
        Rows+=TEXT("Admission=")+Why+TEXT(" World=")+W->GetPathName()+TEXT(" Batch=")+R->Batch.BatchId.ToString()+TEXT("\n");
        if(!Good)return Finish(R,TEXT("BLOCKED_PROOF: fixture not admitted"));
        Equal(*R,TEXT("Every reflected Snapshot field equals original"));
        if(Case==TEXT("NativeSemanticAndMove"))
        {
            for(int32 I=0;I<4;++I)Equal(*R,TEXT("Fresh independent full-value query"));
        }
        else if(Case==TEXT("FailureSemantics"))
        {
            auto* Room=R->Batch.Rooms[0].Room.Get();const double Length=Room->RoomLength,Wall=Room->WallThickness;
            Room->RoomLength+=10;Equal(*R,TEXT("Raw dimensions without mesh rebuild"));
            const auto V=R->GetTraversalSnapshot();Check(TEXT("Current and Ready remain distinct"),V.bCurrent&&!V.bNavigationReady);Room->RoomLength=Length;
            Room->WallThickness+=1;Equal(*R,TEXT("Raw wall thickness without rebuild"));Room->WallThickness=Wall;
            const auto Report=R->NavigationReadiness->Report;
            for(const TCHAR* State:{TEXT("NavigationPending"),TEXT("NavigationFailed"),TEXT("NavigationTimedOut"),TEXT("NavigationUnavailable")})
            {R->NavigationReadiness->Report.State=State;R->NavigationReadiness->Report.bReady=false;Equal(*R,State);R->NavigationReadiness->Report=Report;}
            R->NavigationReadiness->Report.Epoch.Reset();Equal(*R,TEXT("Empty Epoch"));R->NavigationReadiness->Report=Report;
            R->NavigationReadiness->Report.Epoch=TEXT("InvalidRuntimeGeometry");Equal(*R,TEXT("Invalid Epoch"));R->NavigationReadiness->Report=Report;
            ++R->CommittedLoops.GraphRevision;Equal(*R,TEXT("Changed GraphRevision"));--R->CommittedLoops.GraphRevision;
            ++R->CommittedLoops.GeometryRevision;Equal(*R,TEXT("Changed GeometryRevision"));--R->CommittedLoops.GeometryRevision;
            const auto Id=R->Batch.BatchId;R->Batch.BatchId=FGuid::NewGuid();Equal(*R,TEXT("Actual Batch replacement"));R->Batch.BatchId=Id;
            const auto Graph=R->Batch.GraphDigest;R->Batch.GraphDigest+=TEXT("bad");Equal(*R,TEXT("Changed graph summary"));R->Batch.GraphDigest=Graph;
            const auto Registry=R->Batch.Registry;R->Batch.Registry=nullptr;Equal(*R,TEXT("Missing registry"));R->Batch.Registry=Registry;
            const bool Geometry=R->Batch.bGeometryValid;R->Batch.bGeometryValid=false;Equal(*R,TEXT("Geometry failure"));R->Batch.bGeometryValid=Geometry;
            auto Nodes=R->Settings.MinNodes;R->Settings.MinNodes=0;Equal(*R,TEXT("Invalid/empty graph settings"));R->Settings.MinNodes=Nodes;
            TInlineComponentArray<UPrimitiveComponent*> Parts;Room->GetComponents(Parts);
            for(auto* Part:Parts)if(Part->GetFName()==TEXT("Floor"))
            {
                const auto C=Part->GetCollisionEnabled();Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
                Equal(*R,TEXT("Actual Static collision changed"));Part->SetCollisionEnabled(C);
                if(Part->IsRegistered())
                {
                    Part->UnregisterComponent();
                    Rows+=FString::Printf(TEXT("Registration fault: original strong Current=%d\n"),
                        FRogueliteRuntimeLoopAssembler::Current(R,R->Batch,R->Settings,R->CommittedLoops));
                    Equal(*R,TEXT("Actual registration state changed; preserve original semantics"));Part->RegisterComponent();
                }
                break;
            }
            for(auto* Member:Room->GetTemplateOwnedComponents())if(auto* Part=Cast<UPrimitiveComponent>(Member))
            {const auto C=Part->GetCollisionEnabled();Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
                Equal(*R,TEXT("Actual owned template member collision changed"));Part->SetCollisionEnabled(C);break;}
            const auto Pose=R->Batch.Rooms[0].PlacedTransform;R->Batch.Rooms[0].PlacedTransform.AddToTranslation(FVector(1,0,0));
            Equal(*R,TEXT("Recorded Transform mismatch"));R->Batch.Rooms[0].PlacedTransform=Pose;
            const auto Owner=Room->GetOwner();Room->SetOwner(nullptr);Equal(*R,TEXT("Actual room Owner changed"));Room->SetOwner(Owner);
            Equal(*R,TEXT("All failure fixtures restored"));
        }
        else if(Case==TEXT("UnknownTypesAndGetter"))
        {
            FActorSpawnParameters Args;Args.ObjectFlags|=RF_Transient;
            auto* Unknown=W->SpawnActor<ATargetPoint>(Args);Check(TEXT("Unknown native Actor rejected from reuse"),!FRogueliteTraversalReuseTestAccess::Admission(*R,Why));
            Equal(*R,TEXT("Unknown Actor preserves Legacy full value"));Unknown->Destroy();
            auto* BP=FKismetEditorUtilities::CreateBlueprint(AActor::StaticClass(),GetTransientPackage(),MakeUniqueObjectName(GetTransientPackage(),UBlueprint::StaticClass(),TEXT("PERF1D1_Unknown")),BPTYPE_Normal,UBlueprint::StaticClass(),UBlueprintGeneratedClass::StaticClass());
            TStrongObjectPtr<UBlueprint> Keep(BP);
            if(BP&&BP->GeneratedClass)
            {auto* A=W->SpawnActor<AActor>(BP->GeneratedClass,Args);Check(TEXT("Actual unknown Blueprint Actor uses Legacy"),A&&!FRogueliteTraversalReuseTestAccess::Admission(*R,Why));Equal(*R,TEXT("Actual Blueprint fallback full value"));if(A)A->Destroy();}
            else Check(TEXT("Transient Blueprint fixture created"),false);
            auto* A=W->SpawnActor<AActor>(Args);auto* Getter=NewObject<URogueliteTraversalGetterTestComponent>(A);
            A->SetRootComponent(Getter);Getter->SetMobility(EComponentMobility::Movable);Getter->SetCanEverAffectNavigation(false);
            Getter->SetCollisionEnabled(ECollisionEnabled::NoCollision);Getter->RegisterComponent();Getter->Source=R;
            const int32 Before=Getter->QueryCount;Check(TEXT("Unknown virtual getter refused before invocation"),!FRogueliteTraversalReuseTestAccess::Admission(*R,Why)&&Getter->QueryCount==Before);
            const double Length=R->Batch.Rooms[0].Room->RoomLength;Getter->bArmed=true;
            const auto V=R->GetTraversalSnapshot();Check(TEXT("Legacy Capture before Ready sees getter mutation"),Getter->bChanged&&V.bCurrent&&!V.bNavigationReady);
            Getter->bArmed=false;R->Batch.Rooms[0].Room->RoomLength=Length;A->Destroy();
            Equal(*R,TEXT("Unknown getter fixture restored"));
            A=W->SpawnActor<AActor>(Args);auto* Box=NewObject<UBoxComponent>(A);A->SetRootComponent(Box);Box->SetMobility(EComponentMobility::Movable);
            Box->SetCanEverAffectNavigation(false);Box->SetBoxExtent(FVector(50));Box->SetCollisionProfileName(TEXT("BlockAll"));
            Box->SetWorldLocation(FVector(11000,11000,200));Box->RegisterComponent();
            Equal(*R,TEXT("Unregistered external BlockAll after prior success"));Check(TEXT("External Box cannot borrow success"),!R->GetTraversalSnapshot().bCurrent);
            A->Destroy();Equal(*R,TEXT("External fixture restored"));
        }
        else if(Case==TEXT("ProofIdentityAndLifetime"))Good&=FRogueliteTraversalReuseTestAccess::Protocol(*R,*Test,Rows);
        else if(Case==TEXT("ClearDuringHandoff"))
        {Check(TEXT("Actual mid-handoff Clear invalidates payload"),FRogueliteTraversalReuseTestAccess::ClearDuringHandoff(*R,Rows));Equal(*R,TEXT("Cleared empty source equals Legacy"));}
        return Finish(R,TEXT("Original complete snapshots and current synchronous handoff tested; no asset saved"));
    }
};
}
IMPLEMENT_COMPLEX_AUTOMATION_TEST(FRogueliteTraversalReusePIETest,"ProjectVerification.PERF1D1.TraversalReuse",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
void FRogueliteTraversalReusePIETest::GetTests(TArray<FString>& Names,TArray<FString>& Commands) const
{Names={TEXT("NativeSemanticAndMove"),TEXT("FailureSemantics"),TEXT("UnknownTypesAndGetter"),TEXT("ProofIdentityAndLifetime"),TEXT("ClearDuringHandoff")};Commands=Names;}
bool FRogueliteTraversalReusePIETest::RunTest(const FString& Parameters)
{
    if(GEditor->PlayWorld){AddError(TEXT("Refuse to replace user PIE"));return false;}
    FAutomationEditorCommonUtils::LoadMap(TraversalReuseTests::Map);
    ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
    FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<TraversalReuseTests::FRun>(this,Parameters));
    ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());return true;
}
#endif
