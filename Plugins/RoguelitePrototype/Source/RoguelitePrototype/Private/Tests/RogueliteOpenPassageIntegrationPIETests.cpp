#include "RogueliteFloorProfile.h"
#include "RogueliteRuntimeLoopAssembly.h"
#include "RogueliteCompactCorridorContract.h"
#include "RogueliteDungeonMap.h"
#include "Misc/AutomationTest.h"
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Editor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Tests/AutomationEditorCommon.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "UObject/StrongObjectPtr.h"
namespace
{
const TCHAR* Map=TEXT("/Game/Roguelite/Maps/L_Roguelite_OpenPassageIntegration_Dev");
FString Dir(){static FString D=FPaths::ProjectPluginsDir()/TEXT("RoguelitePrototype/Development/P03E2HF03C/StageC-")+FDateTime::UtcNow().ToString(TEXT("%Y%m%d-%H%M%S"));IFileManager::Get().MakeDirectory(*D,true);return D;}
void Save(const FString& Name,const FString& Text){FFileHelper::SaveStringToFile(Text,*(Dir()/Name),FFileHelper::EEncodingOptions::ForceUTF8);}
template<class T> void SaveStruct(const FString& Name,const T& Value){FString Text;T::StaticStruct()->ExportText(Text,&Value,nullptr,nullptr,PPF_None,nullptr);Save(Name,Text);}
ARogueliteRuntimeLoopAssembly* Rig(UWorld* W){if(!W)return nullptr;for(TActorIterator<ARogueliteRuntimeLoopAssembly> I(W);I;++I)if(!I->IsActorBeingDestroyed())return *I;return nullptr;}
int32 Count(UWorld* W){int32 N=0;for(TActorIterator<AActor> I(W);I;++I)N+=!I->IsActorBeingDestroyed();return N;}
struct FContext
{
    FAutomationTestBase* Test=nullptr;bool Navigation=false,Retry=false;int32 Floor=3,Seed=0,Mode=1,Ceiling=1;
    TArray<TStrongObjectPtr<URogueliteFloorProfile>> Profiles;TArray<FRoguelitePreparedTemplateCatalog> Catalogs;
    FRoguelitePreparedTemplateCatalog LegacyCatalog;
    TWeakObjectPtr<ARogueliteRuntimeLoopAssembly> EditorRig;
    TStrongObjectPtr<URogueliteFloorProfile> BeforeProfile;
    FRogueliteSeedLayoutSettings BeforeSettings;FRogueliteLoopAwareSettings BeforePlanning;FRoguelitePreparedTemplateCatalog BeforeCatalog;
    int32 BeforeSeed=0,BeforeCount=0;bool BeforeAuto=false;
};
bool Prepare(const TSharedPtr<FContext>& C)
{
    if(!GEditor||GEditor->PlayWorld||!GEditor->GetEditorWorldContext().World()||GEditor->GetEditorWorldContext().World()->GetOutermost()->GetName()!=Map)return false;
    auto* A=Rig(GEditor->GetEditorWorldContext().World());if(!A)return false;
    C->EditorRig=A;C->BeforeProfile=TStrongObjectPtr<URogueliteFloorProfile>(A->FloorProfile.Get());C->BeforeSeed=A->Seed;C->BeforeAuto=A->bAutoGenerate;
    C->BeforeSettings=A->Settings;C->BeforePlanning=A->Planning;C->BeforeCatalog=A->PreparedTemplates;C->BeforeCount=Count(A->GetWorld());
    for(int32 M=0;M<2;++M)for(int32 Roof=0;Roof<2;++Roof)for(int32 F=1;F<=3;++F)
    {
        const FString Asset=F==1?TEXT("DA_Floor1_Intro"):F==2?TEXT("DA_Floor2_Expansion"):TEXT("DA_Floor3_Final");
        auto* Original=LoadObject<URogueliteFloorProfile>(nullptr,*(TEXT("/Game/Roguelite/FloorProfiles/")+Asset+TEXT(".")+Asset));if(!Original)return false;
        auto* P=NewObject<URogueliteFloorProfile>();P->Rules=Original->Rules;P->Rules.OpeningMode=M?ERogueliteOpeningMode::OpenPassage:ERogueliteOpeningMode::Legacy;P->Rules.bGenerateCorridorCeiling=Roof!=0;
        C->Profiles.Emplace(P);const auto Config=P->ResolvePreview(0);FRoguelitePreparedTemplateCatalog Catalog;FString Why;
        if(!Config.bValid||!FRoguelitePreparedTemplateCatalog::CaptureEditor(A,Config.Source,Catalog,Why)){C->Test->AddError(TEXT("Prepared catalog: ")+Why);return false;}
        C->Catalogs.Add(Catalog);
    }
    FString Why;if(!FRoguelitePreparedTemplateCatalog::CaptureEditor(A,FRogueliteRuntimeLoopAssembler::SourcePreset(0),C->LegacyCatalog,Why)){C->Test->AddError(Why);return false;}
    const int32 Index=(C->Mode*2+C->Ceiling)*3+C->Floor-1;
    A->FloorProfile=C->Profiles[Index].Get();A->Seed=C->Seed;A->bAutoGenerate=false;A->PreparedTemplates=C->Catalogs[Index];C->BeforeCount=Count(A->GetWorld());return true;
}
class FRestore : public IAutomationLatentCommand
{
    TSharedPtr<FContext> C;
public:
    explicit FRestore(TSharedPtr<FContext> In):C(In){}
    bool Update() override
    {
        if(GEditor->PlayWorld)return false;
        if(auto* A=C->EditorRig.Get()){A->FloorProfile=C->BeforeProfile.Get();A->Seed=C->BeforeSeed;A->bAutoGenerate=C->BeforeAuto;A->Settings=C->BeforeSettings;A->Planning=C->BeforePlanning;A->PreparedTemplates=C->BeforeCatalog;
            C->Test->TestEqual(TEXT("PIE stop leaves no runtime-generated Editor actors"),Count(A->GetWorld()),C->BeforeCount);}
        return true;
    }
};
class FRuntime : public IAutomationLatentCommand
{
    TSharedPtr<FContext> C;bool Started=false;double Start=0;int32 Step=0,BaseCount=0;
    FString Rows=TEXT("Kind\tFloor\tSeed\tMode\tCeiling\tSuccess\tRooms\tTree\tLoop\tRegistration\tCleanup\tReason\n");
    TMap<FString,FString> Digests;
public:
    explicit FRuntime(TSharedPtr<FContext> In):C(In){}
    bool Update() override
    {
        UWorld* W=GEditor?GEditor->PlayWorld:nullptr;if(!W)return false;
        if(!Started){Started=true;Start=FPlatformTime::Seconds();return false;}
        if(FPlatformTime::Seconds()-Start<1)return false;
        auto* A=Rig(W);if(!A||!W->IsGameWorld()){C->Test->AddError(TEXT("Actual PIE GameWorld/controller absent"));return true;}
        if(C->Navigation)return Navigation(A);
        if(Step==0)BaseCount=Count(W);
        if(Step>=154){Save(TEXT("runtime-geometry.tsv"),Rows);return true;}
        const int32 N=Step++;int32 F=1,S=0,M=1,Roof=1;bool Legacy=N>=128&&N<148;
        ERogueliteRuntimeLoopFault Fault=ERogueliteRuntimeLoopFault::None;
        if(N<128)
        {
            int32 V=N%32;const int32 Combo=N/32;M=Combo/2;Roof=Combo%2;
            if(V<20){F=1;S=V;}else if(V<26){F=2;S=V-20;}else{F=3;S=V-26;}
        }
        else if(Legacy){S=N-128;M=0;Roof=1;}
        else {F=3;S=0;if(N>=150)Fault=N==150?ERogueliteRuntimeLoopFault::CancelLoopSpawn:N==151?ERogueliteRuntimeLoopFault::RejectLoopC2:ERogueliteRuntimeLoopFault::CancelAfterCommit;}
        const int32 Index=(M*2+Roof)*3+F-1;auto* P=C->Profiles[Index].Get();const auto Config=P->ResolvePreview(S);
        FRogueliteSeedBatch B;FRogueliteCommittedLoopBatch L;FRogueliteRuntimeLoopResult R;bool Success=false;
        auto Settings=Legacy?FRogueliteRuntimeLoopAssembler::SourcePreset(S):Config.Source;
        auto Planning=Config.Planning;if(Legacy){Planning=FRogueliteLoopAwareSettings();Planning.bEnableLoopAwarePlanning=true;}
        const auto& Catalog=Legacy?C->LegacyCatalog:C->Catalogs[Index];
        if(Fault==ERogueliteRuntimeLoopFault::None&&!Legacy)
            Success=FRogueliteFloorProfileResolver::GenerateRuntime(A,Config,ERogueliteFloorValidationPurpose::DeveloperValidation,Catalog,B,L,R);
        else Success=FRogueliteRuntimeLoopAssembler::Generate(A,Settings,Planning,Catalog,B,L,R,Fault);
        bool Good=Success==(Fault==ERogueliteRuntimeLoopFault::None);
        if(Success)
        {
            Good&=R.bProbeFreePlanning&&R.bRegistrationPassed;
            for(const auto& E:R.Connections)Good&=E.bSucceeded&&E.bGeometryPassed&&E.bCapsulePassed;
            for(const auto& Room:B.Rooms)Good&=Room.Room->GetActorScale3D().Equals(FVector::OneVector)&&Room.Room->GetActorTransform().Equals(Room.PlacedTransform,.0001)&&Room.Room->EvaluateTemplate().bValid;
            if(!Legacy){TArray<FRogueliteCompactCorridorMeasurement> Lengths;FString Why;Good&=FRogueliteCompactCorridorContract::Actual(B,L,Lengths,Why);for(const auto& E:B.Edges)Good&=E.Corridor->bGenerateCeiling==(Roof!=0);for(const auto& E:L.Edges)Good&=E.Corridor->bGenerateCeiling==(Roof!=0);}
            const FString Key=FString::Printf(TEXT("%d,%d,%d,%d"),F,S,M,Roof);
            if(!Legacy){if(const auto* Old=Digests.Find(Key))Good&=C->Test->TestEqual(TEXT("Repeated runtime spatial/layout digest excludes random connection GUIDs"),R.Plan.LayoutDigest,*Old);else Digests.Add(Key,R.Plan.LayoutDigest);}
        }
        else Good&=R.bCleanupComplete&&B.OwnedActors.IsEmpty()&&L.Edges.IsEmpty();
        const int32 NR=B.Rooms.Num(),NT=B.Edges.Num(),NL=L.Edges.Num();
        if(IsValid(R.TestObstacle)){Good&=Fault==ERogueliteRuntimeLoopFault::RejectLoopC2;R.TestObstacle->Destroy();R.TestObstacle=nullptr;}
        FString Why;const bool Clean=FRogueliteRuntimeLoopAssembler::Clear(A,B,L,Why)&&Count(W)==BaseCount;
        Good&=Clean;SaveStruct(FString::Printf(TEXT("geometry-%03d.txt"),N),R);
        Rows+=FString::Printf(TEXT("%s\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%s\n"),Legacy?TEXT("LegacyFixture"):Fault==ERogueliteRuntimeLoopFault::None?TEXT("FloorProfile"):TEXT("ExpectedFailure"),F,S,M,Roof,Success,NR,NT,NL,R.bRegistrationPassed,Clean,*R.Reason.Replace(TEXT("\n"),TEXT(" ")));
        Save(TEXT("runtime-geometry.tsv"),Rows);
        C->Test->TestTrue(FString::Printf(TEXT("Actual GameWorld case%d Mode%d Ceiling%d: %s"),N,M,Roof,*R.Reason),Good);
        if(!Good)return true;return false;
    }
    bool Navigation(ARogueliteRuntimeLoopAssembly* A)
    {
        if(Step==0)
        {
            BaseCount=Count(A->GetWorld());A->bNavigationEnabled=true;A->bAutoGenerate=false;
            const int32 Index=(C->Mode*2+C->Ceiling)*3+C->Floor-1;
            A->FloorProfile=C->Profiles[Index].Get();A->PreparedTemplates=C->Catalogs[Index];A->Seed=C->Seed;
            Step=1;
            if(!A->GenerateRuntime()){SaveStruct(Name()+TEXT("-generation-failed.txt"),A->LastGeometry);C->Test->AddError(A->LastFailureReason);return true;}
            C->Test->TestFalse(TEXT("Geometry success is not NavigationReady"),A->IsDungeonReady());return false;
        }
        const bool Ready=A->IsDungeonReady();
        if(Ready&&C->Retry&&Step==1)
        {
            SaveStruct(Name()+TEXT("-before-retry.txt"),A->NavigationReadiness->Report);
            if(!C->Test->TestTrue(TEXT("New profile explicit RetryNavigation uses real no-rebuild C2 path"),A->NavigationReadiness->RetryNavigation()))return true;
            bool Audit=true;for(const auto& R:A->Batch.Rooms)Audit&=R.Room->RuntimeRegistrationAudit.Passed(R.Room);
            for(const auto& E:A->Batch.Edges)Audit&=E.Corridor->RuntimeRegistrationAudit.Passed(E.Corridor);
            for(const auto& E:A->CommittedLoops.Edges)Audit&=E.Corridor->RuntimeRegistrationAudit.Passed(E.Corridor);
            C->Test->TestTrue(TEXT("Registered Static geometry fingerprints unchanged by explicit retry"),Audit);Step=2;return false;
        }
        if(!Ready&&A->bGeometryValidated&&FPlatformTime::Seconds()-Start<420)return false;
        const auto Report=A->NavigationReadiness->Report;SaveStruct(Name()+TEXT("-navigation.txt"),Report);SaveStruct(Name()+TEXT("-last-failure.txt"),A->NavigationReadiness->LastFailure);SaveStruct(Name()+TEXT("-geometry.txt"),A->LastGeometry);
        bool Good=Ready&&Report.bReady&&Report.bCoverageComplete&&!Report.Paths.IsEmpty()&&!Report.Moves.IsEmpty();
        for(const auto& Q:Report.Paths)Good&=Q.bSucceeded&&!Q.bPartial;
        for(const auto& M:Report.Moves)Good&=M.bSucceeded&&(!M.bCorridorRoute||M.bTraversedInterior);
        const FString Why=Report.Reason;const bool Cleared=A->ClearRuntime();Good&=Cleared&&Count(A->GetWorld())==BaseCount;
        C->Test->TestTrue(FString::Printf(TEXT("%s strict actual paths/Character MoveTo and owned cleanup: %s"),*Name(),*Why),Good);return true;
    }
    FString Name()const{return FString::Printf(TEXT("nav-F%d-Seed%02d-Mode%d-Ceiling%d%s"),C->Floor,C->Seed,C->Mode,C->Ceiling,C->Retry?TEXT("-safeRetry"):TEXT(""));}
};
void Queue(const TSharedPtr<FContext>& C)
{
    auto& F=FAutomationTestFramework::Get();F.EnqueueLatentCommand(MakeShared<FStartPIECommand>(false));F.EnqueueLatentCommand(MakeShared<FRuntime>(C));F.EnqueueLatentCommand(MakeShared<FEndPlayMapCommand>());F.EnqueueLatentCommand(MakeShared<FRestore>(C));
}
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHF03CRuntimeGeometry,"ProjectVerification.HF03C.RuntimeGeometry",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FHF03CRuntimeGeometry::RunTest(const FString&){auto C=MakeShared<FContext>();C->Test=this;if(!Prepare(C)){AddError(TEXT("Prepare Runtime geometry on the dedicated HF03-C map"));return false;}Queue(C);return true;}
IMPLEMENT_COMPLEX_AUTOMATION_TEST(FHF03CRuntimeNavigation,"ProjectVerification.HF03C.RuntimeNavigation",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
void FHF03CRuntimeNavigation::GetTests(TArray<FString>& Names,TArray<FString>& Commands) const
{
    for(int32 F=1;F<=3;++F)for(int32 S=0;S<(F==1?20:6);++S){Names.Add(FString::Printf(TEXT("F%d.Seed%02d.OpenOn"),F,S));Commands.Add(FString::Printf(TEXT("%d,%d,1,1"),F,S));}
    for(const FIntPoint Pair:{FIntPoint(0,1),FIntPoint(0,0),FIntPoint(1,0)}){Names.Add(FString::Printf(TEXT("F3.Seed00.Mode%d.Ceiling%d"),Pair.X,Pair.Y));Commands.Add(FString::Printf(TEXT("3,0,%d,%d"),Pair.X,Pair.Y));}
}
bool FHF03CRuntimeNavigation::RunTest(const FString& Parameters)
{
    TArray<FString> V;Parameters.ParseIntoArray(V,TEXT(","));if(V.Num()!=4)return false;
    auto C=MakeShared<FContext>();C->Test=this;C->Navigation=true;C->Floor=FCString::Atoi(*V[0]);C->Seed=FCString::Atoi(*V[1]);C->Mode=FCString::Atoi(*V[2]);C->Ceiling=FCString::Atoi(*V[3]);
    if(!Prepare(C)){AddError(TEXT("Prepare strict navigation on the dedicated HF03-C map"));return false;}Queue(C);return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHF03CSafeRetry,"ProjectVerification.HF03C.RuntimeSafeRetry",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FHF03CSafeRetry::RunTest(const FString&){auto C=MakeShared<FContext>();C->Test=this;C->Navigation=true;C->Retry=true;C->Floor=3;C->Seed=0;C->Mode=1;C->Ceiling=0;if(!Prepare(C))return false;Queue(C);return true;}
#endif
