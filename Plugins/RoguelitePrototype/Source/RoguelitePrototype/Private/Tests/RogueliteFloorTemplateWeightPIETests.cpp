#include "RogueliteFloorProfile.h"
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
namespace F1PIE
{
const TCHAR* Map=TEXT("/Game/Roguelite/Maps/L_Roguelite_FloorTemplateWeights_Dev");
FString Dir(){return FPaths::ProjectPluginsDir()/TEXT("RoguelitePrototype/Development/P03F1/Run-20261031-130717");}
void Save(const FString& N,const FString& T){FFileHelper::SaveStringToFile(T,*(Dir()/N),FFileHelper::EEncodingOptions::ForceUTF8);}
template<class T>void Export(const FString& N,const T& V){FString Text;T::StaticStruct()->ExportText(Text,&V,nullptr,nullptr,PPF_None,nullptr);Save(N,Text);}
int32 Count(UWorld* W){int32 N=0;for(TActorIterator<AActor>I(W);I;++I)N+=!I->IsActorBeingDestroyed();return N;}
ARogueliteRuntimeLoopAssembly* Rig(UWorld* W){if(W)for(TActorIterator<ARogueliteRuntimeLoopAssembly>I(W);I;++I)if(!I->IsActorBeingDestroyed())return *I;return nullptr;}
struct FContext: FOutputDevice
{
    FAutomationTestBase* Test=nullptr;int32 Floor=1,Seed=0,EditorCount=0,BeforeSeed=0;bool BeforeAuto=false;
    TWeakObjectPtr<ARogueliteRuntimeLoopAssembly> EditorRig;TStrongObjectPtr<URogueliteFloorProfile> Profile,BeforeProfile;
    FRogueliteSeedLayoutSettings BeforeSettings;FRogueliteLoopAwareSettings BeforePlanning;FRoguelitePreparedTemplateCatalog BeforeCatalog;
    FString Trace;FContext(){GLog->AddOutputDevice(this);}~FContext(){GLog->RemoveOutputDevice(this);}
    void Serialize(const TCHAR* V,ELogVerbosity::Type,const FName&)override{if(FCString::Strstr(V,TEXT("F1_")))Trace+=FString(V)+TEXT("\n");}
    FString Stem()const{return FString::Printf(TEXT("runtime-F%d-S%02d"),Floor,Seed);}
};
bool Prepare(const TSharedPtr<FContext>& C)
{
    if(!GEditor||GEditor->PlayWorld||!GEditor->GetEditorWorldContext().World()||GEditor->GetEditorWorldContext().World()->GetOutermost()->GetName()!=Map)return false;
    auto* A=Rig(GEditor->GetEditorWorldContext().World());if(!A)return false;
    C->EditorRig=A;C->BeforeProfile=TStrongObjectPtr<URogueliteFloorProfile>(A->FloorProfile.Get());C->BeforeAuto=A->bAutoGenerate;C->BeforeSeed=A->Seed;C->BeforeSettings=A->Settings;C->BeforePlanning=A->Planning;C->BeforeCatalog=A->PreparedTemplates;
    const FString N=C->Floor==1?TEXT("DA_Floor1_Intro"):C->Floor==2?TEXT("DA_Floor2_Expansion"):TEXT("DA_Floor3_Final");
    auto* Source=LoadObject<URogueliteFloorProfile>(nullptr,*(TEXT("/Game/Roguelite/FloorProfiles/")+N+TEXT(".")+N));if(!Source)return false;
    C->Profile=TStrongObjectPtr<URogueliteFloorProfile>(NewObject<URogueliteFloorProfile>());auto& D=C->Profile->Rules;D=Source->Rules;D.SpatialPlanningVersion=3;
    for(auto& T:D.AllowedTemplates)T.Weight=T.Id==TEXT("Junction3")?(C->Floor==1?10:C->Floor==2?5:1):(C->Floor==3?5:1);
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
        auto* A=Rig(W);if(!A||!W->IsGameWorld()){C->Test->AddError(TEXT("Actual F1 GameWorld unavailable"));return true;}
        if(Step==0)
        {
            BaseCount=Count(W);Step=1;A->bAutoGenerate=false;A->bNavigationEnabled=true;Geometry=A->GenerateRuntime();
            Export(C->Stem()+TEXT("-geometry.txt"),A->LastGeometry);NR=A->Batch.Rooms.Num();NT=A->Batch.Edges.Num();NL=A->CommittedLoops.Edges.Num();
            Rooms=TEXT("Node\tRoomType\tTemplate\tWeight\tDegree\tCapacity\tTransform\n");
            for(const auto& R:A->Batch.Rooms){const auto* T=C->Profile->Rules.AllowedTemplates.FindByPredicate([&](const auto& V){return V.Id==R.TemplateId;});Legal&=T&&T->AllowedTypes.Contains(R.RoomType)&&R.TemplateCapacity>=R.LogicalDegree&&R.Room->GetActorScale3D().Equals(FVector::OneVector)&&R.Room->RuntimeRegistrationAudit.Passed(R.Room);Rooms+=FString::Printf(TEXT("%d\t%d\t%s\t%d\t%d\t%d\t%s\n"),R.NodeId,uint8(R.RoomType),*R.TemplateId.ToString(),T?T->Weight:0,R.LogicalDegree,R.TemplateCapacity,*R.Room->GetActorTransform().ToString());}
            Connections=TEXT("Edge\tLoop\tLength\tC2\tProbes\tConnectionId\n");TSet<FGuid> Ids;
            for(const auto& E:A->Batch.Edges){Legal&=E.Validation.bSucceeded&&E.Validation.bGeometryPassed&&E.Validation.bCapsulePassed&&!Ids.Contains(E.ConnectionId);Ids.Add(E.ConnectionId);Connections+=FString::Printf(TEXT("%d\t0\t%.3f\t%d\t%d\t%s\n"),E.EdgeId,E.Corridor->CorridorLength,E.Validation.bSucceeded,E.Validation.Probes.Num(),*E.ConnectionId.ToString());}
            for(const auto& E:A->CommittedLoops.Edges){Legal&=E.Validation.bSucceeded&&E.Validation.bGeometryPassed&&E.Validation.bCapsulePassed&&!Ids.Contains(E.ConnectionId);Ids.Add(E.ConnectionId);Connections+=FString::Printf(TEXT("%d\t1\t%.3f\t%d\t%d\t%s\n"),E.EdgeId,E.Corridor->CorridorLength,E.Validation.bSucceeded,E.Validation.Probes.Num(),*E.ConnectionId.ToString());}
            if(Geometry){TArray<FRogueliteCompactCorridorMeasurement>M;FString Why;Legal&=FRogueliteCompactCorridorContract::Actual(A->Batch,A->CommittedLoops,M,Why)&&Ids.Num()==NT+NL;C->Test->TestFalse(TEXT("Geometry alone is not Ready"),A->IsDungeonReady());return false;}
            C->Test->AddInfo(TEXT("RETAINED_RUNTIME_SPATIAL_FAILURE ")+A->LastGeometry.Reason);
        }
        const auto& R=A->NavigationReadiness->Report;const bool Ready=Geometry&&A->IsDungeonReady();
        const bool Terminal=!A->bGeometryValidated||R.State.Contains(TEXT("Failed"))||R.State.Contains(TEXT("TimedOut"))||R.State.Contains(TEXT("Unavailable"));
        if(Geometry&&!Ready&&!Terminal&&FPlatformTime::Seconds()-Start<420)return false;
        bool Good=Legal;
        if(Geometry){Good&=Ready&&R.bReady&&R.bCoverageComplete&&!R.Paths.IsEmpty()&&!R.Moves.IsEmpty();for(const auto& P:R.Paths)Good&=P.bSucceeded&&!P.bPartial;for(const auto& M:R.Moves)Good&=M.bSucceeded&&(!M.bCorridorRoute||M.bTraversedInterior);}
        Export(C->Stem()+TEXT("-navigation.txt"),R);Save(C->Stem()+TEXT("-rooms.tsv"),Rooms);Save(C->Stem()+TEXT("-connections.tsv"),Connections);Save(C->Stem()+TEXT("-choices.log"),C->Trace);
        const FString Why=Geometry?(Ready?R.Reason:A->LastFailureReason):A->LastGeometry.Reason;const int32 Paths=R.Paths.Num(),Moves=R.Moves.Num();const bool Clean=A->ClearRuntime()&&Count(W)==BaseCount;Good&=Clean;
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
IMPLEMENT_COMPLEX_AUTOMATION_TEST(FF1Runtime,"ProjectVerification.F1.Runtime",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
void FF1Runtime::GetTests(TArray<FString>&N,TArray<FString>&C)const{for(int32 F=1;F<=3;++F)for(int32 S=0;S<20;++S){N.Add(FString::Printf(TEXT("Floor%d.Seed%02d"),F,S));C.Add(FString::Printf(TEXT("%d,%d"),F,S));}}
bool FF1Runtime::RunTest(const FString& Params)
{
    using namespace F1PIE;TArray<FString> V;Params.ParseIntoArray(V,TEXT(","));if(V.Num()!=2)return false;auto C=MakeShared<FContext>();C->Test=this;C->Floor=FCString::Atoi(*V[0]);C->Seed=FCString::Atoi(*V[1]);if(!Prepare(C))return false;
    auto& T=FAutomationTestFramework::Get();T.EnqueueLatentCommand(MakeShared<FStartPIECommand>(false));T.EnqueueLatentCommand(MakeShared<FRun>(C));T.EnqueueLatentCommand(MakeShared<FEndPlayMapCommand>());T.EnqueueLatentCommand(MakeShared<FRestore>(C));return true;
}
#endif
