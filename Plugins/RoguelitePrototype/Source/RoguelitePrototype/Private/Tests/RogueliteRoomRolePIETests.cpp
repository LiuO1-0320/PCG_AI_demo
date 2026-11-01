#include "Tests/RogueliteRoomRoleTestSupport.h"
#include "RogueliteRuntimeLoopAssembly.h"
#include "RogueliteCompactCorridorContract.h"
#include "Misc/AutomationTest.h"
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Editor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Tests/AutomationEditorCommon.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/OutputDevice.h"
#include "UObject/StrongObjectPtr.h"
namespace G1PIE
{
const TCHAR* Map=TEXT("/Game/Roguelite/Tests/P03G1/L_Roguelite_RoomRoles_Dev");
FString Dir(){return FPaths::ProjectPluginsDir()/TEXT("RoguelitePrototype/Development/P03G1/Run-20261101-2849b0c0");}
void Save(const FString& N,const FString& T){FFileHelper::SaveStringToFile(T,*(Dir()/N),FFileHelper::EEncodingOptions::ForceUTF8);}
template<class T>void Export(const FString& N,const T& V){FString Text;T::StaticStruct()->ExportText(Text,&V,nullptr,nullptr,PPF_None,nullptr);Save(N,Text);}
int32 Count(UWorld* W){int32 N=0;for(TActorIterator<AActor>I(W);I;++I)N+=!I->IsActorBeingDestroyed();return N;}
ARogueliteRoomRoleTestRig* Rig(UWorld* W){if(W)for(TActorIterator<ARogueliteRoomRoleTestRig>I(W);I;++I)if(!I->IsActorBeingDestroyed())return *I;return nullptr;}
struct FContext: FOutputDevice
{
    FAutomationTestBase* Test=nullptr;int32 Floor=1,Seed=0,EditorCount=0,BeforeSeed=0;bool BeforeAuto=false;
    TWeakObjectPtr<ARogueliteRoomRoleTestRig> EditorRig;TStrongObjectPtr<URogueliteFloorProfile> Profile,BeforeProfile;
    FRogueliteSeedLayoutSettings BeforeSettings;FRogueliteLoopAwareSettings BeforePlanning;FRoguelitePreparedTemplateCatalog BeforeCatalog;
    FString Trace;FContext(){GLog->AddOutputDevice(this);}~FContext(){GLog->RemoveOutputDevice(this);}
    void Serialize(const TCHAR* V,ELogVerbosity::Type,const FName&)override{if(FCString::Strstr(V,TEXT("F1_"))||FCString::Strstr(V,TEXT("G1_")))Trace+=FString(V)+TEXT("\n");}
    FString Stem()const{return FString::Printf(TEXT("runtime-F%d-S%02d"),Floor,Seed);}
};
bool Prepare(const TSharedPtr<FContext>& C)
{
    if(!GEditor||GEditor->PlayWorld||!GEditor->GetEditorWorldContext().World()||GEditor->GetEditorWorldContext().World()->GetOutermost()->GetName()!=Map)return false;
    auto* A=Rig(GEditor->GetEditorWorldContext().World());if(!A)return false;
    C->EditorRig=A;C->BeforeProfile=TStrongObjectPtr<URogueliteFloorProfile>(A->FloorProfile.Get());C->BeforeAuto=A->bAutoGenerate;C->BeforeSeed=A->Seed;C->BeforeSettings=A->Settings;C->BeforePlanning=A->Planning;C->BeforeCatalog=A->PreparedTemplates;
    auto* Source=G1Test::Profile(C->Floor);if(!Source)return false;
    C->Profile=TStrongObjectPtr<URogueliteFloorProfile>(NewObject<URogueliteFloorProfile>());C->Profile->Rules=Source->Rules;
    const auto Config=C->Profile->ResolvePreview(C->Seed);FString Why;FRoguelitePreparedTemplateCatalog Catalog;
    if(!Config.bValid||!FRoguelitePreparedTemplateCatalog::CaptureEditor(A,Config.Source,Catalog,Why)){C->Test->AddError(Config.ErrorCode+TEXT(" ")+Why);return false;}
    A->FloorProfile=C->Profile.Get();A->Seed=C->Seed;A->bAutoGenerate=false;A->PreparedTemplates=Catalog;C->EditorCount=Count(A->GetWorld());return true;
}
class FRun:public IAutomationLatentCommand
{
    TSharedPtr<FContext>C;double Start=0;int32 Step=0,BaseCount=0;FString Rooms,Connections;int32 NR=0,NT=0,NL=0;bool Geometry=false,Legal=true;
public:
    explicit FRun(TSharedPtr<FContext> V):C(V){}
    bool Update()override
    {
        UWorld* W=GEditor?GEditor->PlayWorld:nullptr;if(!W)return false;
        if(Start==0){Start=FPlatformTime::Seconds();return false;}if(FPlatformTime::Seconds()-Start<1)return false;
        auto* A=Rig(W);if(!A||!W->IsGameWorld()){C->Test->AddError(TEXT("Actual G1 GameWorld unavailable"));return true;}
        if(Step==0)
        {
            BaseCount=Count(W);Step=1;A->bAutoGenerate=false;A->bNavigationEnabled=true;Geometry=A->GenerateRuntime();
            Export(C->Stem()+TEXT("-geometry.txt"),A->LastGeometry);NR=A->Batch.Rooms.Num();NT=A->Batch.Edges.Num();NL=A->CommittedLoops.Edges.Num();
            Rooms=TEXT("Node\tRoomType\tTemplate\tWeight\tDegree\tCapacity\tTransform\n");
            for(const auto& R:A->Batch.Rooms){const auto* T=C->Profile->Rules.AllowedTemplates.FindByPredicate([&](const auto& V){return V.Id==R.TemplateId;});Legal&=T&&T->AllowedTypes.Contains(R.RoomType)&&R.TemplateCapacity>=R.LogicalDegree&&R.Room->GetActorScale3D().Equals(FVector::OneVector)&&R.Room->RuntimeRegistrationAudit.Passed(R.Room);Rooms+=FString::Printf(TEXT("%d\t%d\t%s\t%d\t%d\t%d\t%s\n"),R.NodeId,uint8(R.RoomType),*R.TemplateId.ToString(),T?T->Weight:0,R.LogicalDegree,R.TemplateCapacity,*R.Room->GetActorTransform().ToString());}
            Connections=TEXT("Edge\tLoop\tLength\tC2\tProbes\tConnectionId\n");TSet<FGuid> Ids;
            for(const auto& E:A->Batch.Edges){Legal&=E.Validation.bSucceeded&&E.Validation.bGeometryPassed&&E.Validation.bCapsulePassed&&!Ids.Contains(E.ConnectionId);Ids.Add(E.ConnectionId);Connections+=FString::Printf(TEXT("%d\t0\t%.3f\t%d\t%d\t%s\n"),E.EdgeId,E.Corridor->CorridorLength,E.Validation.bSucceeded,E.Validation.Probes.Num(),*E.ConnectionId.ToString());}
            for(const auto& E:A->CommittedLoops.Edges){Legal&=E.Validation.bSucceeded&&E.Validation.bGeometryPassed&&E.Validation.bCapsulePassed&&!Ids.Contains(E.ConnectionId);Ids.Add(E.ConnectionId);Connections+=FString::Printf(TEXT("%d\t1\t%.3f\t%d\t%d\t%s\n"),E.EdgeId,E.Corridor->CorridorLength,E.Validation.bSucceeded,E.Validation.Probes.Num(),*E.ConnectionId.ToString());}
            if(Geometry){FString G1Rows,G1Edges;const auto Config=C->Profile->ResolvePreview(C->Seed);const auto Graph=FRogueliteRoomGraphGenerator::Generate(C->Seed,Config.Source.GraphSettings());Legal&=G1Test::Check(C->Profile->Rules,Graph.Graph,A->Batch,A->CommittedLoops,G1Rows,G1Edges);G1Test::Save(C->Stem()+TEXT("-roles.tsv"),G1Rows);TArray<FRogueliteCompactCorridorMeasurement>M;FString Why;Legal&=FRogueliteCompactCorridorContract::Actual(A->Batch,A->CommittedLoops,M,Why)&&Ids.Num()==NT+NL;C->Test->TestFalse(TEXT("Geometry alone is not Ready"),A->IsDungeonReady());return false;}
            C->Test->AddInfo(TEXT("RETAINED_RUNTIME_SPATIAL_FAILURE ")+A->LastGeometry.Reason);
        }
        const auto& R=A->NavigationReadiness->Report;const bool Ready=Geometry&&A->IsDungeonReady();
        const bool Terminal=!A->bGeometryValidated||R.State.Contains(TEXT("Failed"))||R.State.Contains(TEXT("TimedOut"))||R.State.Contains(TEXT("Unavailable"));
        if(Geometry&&!Ready&&!Terminal&&FPlatformTime::Seconds()-Start<420)return false;
        bool Good=Legal;
        if(Geometry){Good&=Ready&&R.bReady&&R.bCoverageComplete&&!R.Paths.IsEmpty()&&!R.Moves.IsEmpty();for(const auto& P:R.Paths)Good&=P.bSucceeded&&!P.bPartial;for(const auto& M:R.Moves)Good&=M.bSucceeded&&(!M.bCorridorRoute||M.bTraversedInterior);}
        Export(C->Stem()+TEXT("-navigation.txt"),R);Save(C->Stem()+TEXT("-rooms.tsv"),Rooms);Save(C->Stem()+TEXT("-connections.tsv"),Connections);Save(C->Stem()+TEXT("-choices.log"),C->Trace);
        const FString Why=Geometry?(Ready?R.Reason:A->LastFailureReason):A->LastGeometry.Reason;const int32 Paths=R.Paths.Num(),Moves=R.Moves.Num();const bool Clean=A->ClearRuntime()&&Count(W)==BaseCount;Good&=Clean;
        if(Geometry&&NL>0&&C->Seed==0){FRogueliteSeedBatch FB;FRogueliteCommittedLoopBatch FL;FRogueliteRuntimeLoopResult FR;const auto CF=C->Profile->ResolvePreview(C->Seed);const bool Unexpected=FRogueliteRuntimeLoopAssembler::Generate(A,CF.Source,CF.Planning,A->PreparedTemplates,FB,FL,FR,ERogueliteRuntimeLoopFault::CancelAfterCommit);Export(C->Stem()+TEXT("-forced-runtime-failure.txt"),FR);Good&=!Unexpected&&FR.Stage==TEXT("CancelledAfterLoopCommit")&&FR.bCleanupComplete&&FB.OwnedActors.IsEmpty()&&FL.Edges.IsEmpty()&&Count(W)==BaseCount;FString Cleanup;FRogueliteRuntimeLoopAssembler::Clear(A,FB,FL,Cleanup);}
        Save(C->Stem()+TEXT("-summary.tsv"),FString::Printf(TEXT("Floor\tSeed\tGenerated\tRooms\tTree\tLoop\tLegal\tReady\tPaths\tMoves\tClean\tReason\n%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%s\n"),C->Floor,C->Seed,Geometry,NR,NT,NL,Legal,Ready,Paths,Moves,Clean,*Why.Replace(TEXT("\n"),TEXT(" "))));
        C->Test->TestTrue(TEXT("Actual geometry/C2/strict navigation if generated, and owned cleanup"),Good);return true;
    }
};
class FRestore:public IAutomationLatentCommand
{
    TSharedPtr<FContext>C;
public:explicit FRestore(TSharedPtr<FContext>V):C(V){}
    bool Update()override{if(GEditor->PlayWorld)return false;if(auto* A=C->EditorRig.Get()){A->FloorProfile=C->BeforeProfile.Get();A->Seed=C->BeforeSeed;A->bAutoGenerate=C->BeforeAuto;A->Settings=C->BeforeSettings;A->Planning=C->BeforePlanning;A->PreparedTemplates=C->BeforeCatalog;C->Test->TestEqual(TEXT("PIE stop did not write runtime actors into Editor map"),Count(A->GetWorld()),C->EditorCount);}return true;}
};
}
IMPLEMENT_COMPLEX_AUTOMATION_TEST(FG1Runtime,"ProjectVerification.G1.Runtime",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
void FG1Runtime::GetTests(TArray<FString>&N,TArray<FString>&C)const{for(int32 F=1;F<=3;++F)for(int32 S=0;S<20;++S){N.Add(FString::Printf(TEXT("Floor%d.Seed%02d"),F,S));C.Add(FString::Printf(TEXT("%d,%d"),F,S));}}
bool FG1Runtime::RunTest(const FString& Params)
{
    using namespace G1PIE;TArray<FString> V;Params.ParseIntoArray(V,TEXT(","));if(V.Num()!=2)return false;auto C=MakeShared<FContext>();C->Test=this;C->Floor=FCString::Atoi(*V[0]);C->Seed=FCString::Atoi(*V[1]);if(!Prepare(C))return false;
    auto& T=FAutomationTestFramework::Get();T.EnqueueLatentCommand(MakeShared<FStartPIECommand>(false));T.EnqueueLatentCommand(MakeShared<FRun>(C));T.EnqueueLatentCommand(MakeShared<FEndPlayMapCommand>());T.EnqueueLatentCommand(MakeShared<FRestore>(C));return true;
}
#endif
