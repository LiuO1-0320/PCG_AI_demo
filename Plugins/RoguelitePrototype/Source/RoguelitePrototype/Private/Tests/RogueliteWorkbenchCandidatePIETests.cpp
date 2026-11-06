#include "RogueliteRuntimeLoopAssembly.h"
#include "RogueliteCompactCorridorContract.h"
#include "Tests/RogueliteProductionCandidateSupport.h"
#include "Misc/AutomationTest.h"
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Editor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Tests/AutomationEditorCommon.h"
#include "Misc/FileHelper.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "UObject/StrongObjectPtr.h"

namespace M1BPIE
{
const TCHAR* Map=TEXT("/Game/Roguelite/Tests/P03M1B/L_Roguelite_WorkbenchCandidates_Dev");
FString Dir(){FString Path;FParse::Value(FCommandLine::Get(),TEXT("M1BEvidenceDir="),Path);return Path.IsEmpty()?FPaths::ProjectPluginsDir()/TEXT("RoguelitePrototype/Development/P03M1B/Native"):Path;}
void Save(const FString& Name,const FString& Text){IFileManager::Get().MakeDirectory(*Dir(),true);FString Path=Dir()/Name;if(IFileManager::Get().FileExists(*Path))Path+=TEXT(".")+FDateTime::UtcNow().ToString(TEXT("%Y%m%d%H%M%S"))+TEXT(".rerun");FFileHelper::SaveStringToFile(Text,*Path,FFileHelper::EEncodingOptions::ForceUTF8);}
template<class T>void Export(const FString& Name,const T& Value){FString Text;T::StaticStruct()->ExportText(Text,&Value,nullptr,nullptr,PPF_None,nullptr);Save(Name,Text);}
int32 Count(UWorld* W){int32 N=0;for(TActorIterator<AActor>I(W);I;++I)N+=!I->IsActorBeingDestroyed();return N;}
ARogueliteRuntimeLoopAssembly* Rig(UWorld* W){if(W)for(TActorIterator<ARogueliteRuntimeLoopAssembly>I(W);I;++I)if(!I->IsActorBeingDestroyed())return *I;return nullptr;}
struct FContext
{
    FAutomationTestBase* Test=nullptr;int32 Floor=1,Seed=0,BeforeSeed=0,EditorCount=0;bool Fault=false,BeforeAuto=false,BeforeNav=false;
    FString ExpectedPlan;TWeakObjectPtr<ARogueliteRuntimeLoopAssembly> EditorRig;TStrongObjectPtr<URogueliteFloorProfile> Profile,BeforeProfile;
    FRogueliteSeedLayoutSettings BeforeSettings;FRogueliteLoopAwareSettings BeforePlanning;FRoguelitePreparedTemplateCatalog BeforeCatalog;
    FString Stem()const{return FString::Printf(TEXT("runtime-F%d-S%02d%s"),Floor,Seed,Fault?TEXT("-controlled-failure"):TEXT(""));}
};
bool Prepare(const TSharedPtr<FContext>& C)
{
    if(!GEditor||GEditor->PlayWorld||!GEditor->GetEditorWorldContext().World()||GEditor->GetEditorWorldContext().World()->GetOutermost()->GetName()!=Map)return false;
    auto* A=Rig(GEditor->GetEditorWorldContext().World());if(!A)return false;
    C->EditorRig=A;C->BeforeProfile=TStrongObjectPtr<URogueliteFloorProfile>(A->FloorProfile.Get());C->BeforeSeed=A->Seed;C->BeforeAuto=A->bAutoGenerate;C->BeforeNav=A->bNavigationEnabled;C->BeforeSettings=A->Settings;C->BeforePlanning=A->Planning;C->BeforeCatalog=A->PreparedTemplates;
    auto* Source=M1Test::Profile(C->Floor);if(!Source)return false;
    C->Profile=TStrongObjectPtr<URogueliteFloorProfile>(NewObject<URogueliteFloorProfile>());C->Profile->Rules=Source->Rules;
    if(C->Fault){C->Profile->Rules.MinLoops=1;C->Profile->Rules.SearchBudget.MaxBacktrackStates=2;}
    const auto Config=C->Profile->ResolvePreview(C->Seed);FString Why;FRoguelitePreparedTemplateCatalog Catalog;
    if(!Config.bValid||!FRoguelitePreparedTemplateCatalog::CaptureEditor(A,Config.Source,Catalog,Why)){C->Test->AddError(Config.ErrorCode+TEXT(" ")+Why);return false;}
    const auto Plan=FRogueliteLoopAwarePlanner::Plan(A,Config.Source,Config.Planning,FTransform::Identity);C->ExpectedPlan=Plan.Evidence.StablePlanDigest;Export(C->Stem()+TEXT("-editor-plan.txt"),Plan.Evidence);
    A->FloorProfile=C->Profile.Get();A->Seed=C->Seed;A->bAutoGenerate=false;A->PreparedTemplates=Catalog;C->EditorCount=Count(A->GetWorld());return true;
}
class FRun:public IAutomationLatentCommand
{
    TSharedPtr<FContext> C;double Started=0;int32 Step=0,BeforeCount=0,Rooms=0,Tree=0,Loop=0;bool Geometry=false,Legal=true;
public:
    explicit FRun(TSharedPtr<FContext> In):C(In){}
    bool Update()override
    {
        auto* W=GEditor?GEditor->PlayWorld.Get():nullptr;if(!W)return false;
        if(!Started){Started=FPlatformTime::Seconds();return false;}if(FPlatformTime::Seconds()-Started<1)return false;
        auto* A=Rig(W);if(!A||!W->IsGameWorld()){C->Test->AddError(TEXT("Actual M1B GameWorld required"));return true;}
        if(Step==0)
        {
            Step=1;BeforeCount=Count(W);A->bAutoGenerate=false;A->bNavigationEnabled=true;Geometry=A->GenerateRuntime();
            Export(C->Stem()+TEXT("-geometry.txt"),A->LastGeometry);Rooms=A->Batch.Rooms.Num();Tree=A->Batch.Edges.Num();Loop=A->CommittedLoops.Edges.Num();
            if(Geometry)
            {
                Legal&=C->Test->TestEqual(TEXT("Editor/Runtime exact seed/config spatial plan"),A->LastGeometry.Plan.StablePlanDigest,C->ExpectedPlan);
                const auto Graph=FRogueliteRoomGraphGenerator::Generate(A->Settings.Seed,A->Settings.GraphSettings());FString RoomRows,Edges;
                Legal&=M1Test::Check(C->Profile->Rules,Graph.Graph,A->Batch,A->CommittedLoops,RoomRows,Edges);Save(C->Stem()+TEXT("-rooms.tsv"),RoomRows);Save(C->Stem()+TEXT("-connections.tsv"),Edges);
                for(const auto& Room:A->Batch.Rooms)Legal&=IsValid(Room.Room)&&Room.Room->RuntimeRegistrationAudit.Passed(Room.Room);
                TArray<FRogueliteCompactCorridorMeasurement> Measurements;FString Why;Legal&=FRogueliteCompactCorridorContract::Actual(A->Batch,A->CommittedLoops,Measurements,Why);
                C->Test->TestFalse(TEXT("Geometry alone is not DungeonReady"),A->IsDungeonReady());return false;
            }
        }
        const auto& Nav=A->NavigationReadiness->Report;const bool Ready=Geometry&&A->IsDungeonReady();
        const bool Terminal=!Geometry||Nav.State.Contains(TEXT("Failed"))||Nav.State.Contains(TEXT("TimedOut"))||Nav.State.Contains(TEXT("Unavailable"));
        if(Geometry&&!Ready&&!Terminal&&FPlatformTime::Seconds()-Started<420)return false;
        bool Good=Legal&&(C->Fault?!Geometry:Geometry);
        if(Geometry){Good&=Ready&&Nav.bReady&&Nav.bCoverageComplete&&!Nav.Paths.IsEmpty()&&!Nav.Moves.IsEmpty();for(const auto& P:Nav.Paths)Good&=P.bSucceeded&&!P.bPartial;for(const auto& M:Nav.Moves)Good&=M.bSucceeded&&(!M.bCorridorRoute||M.bTraversedInterior);}
        Export(C->Stem()+TEXT("-navigation.txt"),Nav);const int32 Paths=Nav.Paths.Num(),Moves=Nav.Moves.Num();const FString Reason=Geometry?Nav.Reason:A->LastGeometry.Reason;
        const bool Clean=A->ClearRuntime()&&Count(W)==BeforeCount;Good&=Clean;
        Save(C->Stem()+TEXT("-summary.tsv"),FString::Printf(TEXT("Floor\tSeed\tFault\tGeometry\tRooms\tTree\tLoop\tLegal\tReady\tPaths\tMoves\tClean\tReason\n%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%s\n"),C->Floor,C->Seed,C->Fault,Geometry,Rooms,Tree,Loop,Legal,Ready,Paths,Moves,Clean,*Reason.Replace(TEXT("\n"),TEXT(" "))));
        C->Test->TestTrue(TEXT("Real C2, strict Nav/MoveTo for successful batches and safe owned cleanup"),Good);return true;
    }
};
class FRestore:public IAutomationLatentCommand
{
    TSharedPtr<FContext>C;
public:explicit FRestore(TSharedPtr<FContext> In):C(In){}
    bool Update()override{if(GEditor->PlayWorld)return false;if(auto* A=C->EditorRig.Get()){A->FloorProfile=C->BeforeProfile.Get();A->Seed=C->BeforeSeed;A->bAutoGenerate=C->BeforeAuto;A->bNavigationEnabled=C->BeforeNav;A->Settings=C->BeforeSettings;A->Planning=C->BeforePlanning;A->PreparedTemplates=C->BeforeCatalog;C->Test->TestEqual(TEXT("PIE stop left Editor map actors untouched"),Count(A->GetWorld()),C->EditorCount);}return true;}
};
}
IMPLEMENT_COMPLEX_AUTOMATION_TEST(FM1BPIE,"ProjectVerification.M1B.Runtime",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
void FM1BPIE::GetTests(TArray<FString>& Names,TArray<FString>& Commands)const
{Names={TEXT("Floor1Tree"),TEXT("Floor2Loop"),TEXT("Floor2Fallback"),TEXT("Floor3Boss"),TEXT("ControlledFailure")};Commands={TEXT("1,0"),TEXT("2,0"),TEXT("2,3"),TEXT("3,0"),TEXT("2,3,1")};}
bool FM1BPIE::RunTest(const FString& Params)
{
    using namespace M1BPIE;TArray<FString> Fields;Params.ParseIntoArray(Fields,TEXT(","));if(Fields.Num()<2)return false;
    auto C=MakeShared<FContext>();C->Test=this;C->Floor=FCString::Atoi(*Fields[0]);C->Seed=FCString::Atoi(*Fields[1]);C->Fault=Fields.Num()==3;if(!Prepare(C))return false;
    auto& Framework=FAutomationTestFramework::Get();Framework.EnqueueLatentCommand(MakeShared<FStartPIECommand>(false));Framework.EnqueueLatentCommand(MakeShared<FRun>(C));Framework.EnqueueLatentCommand(MakeShared<FEndPlayMapCommand>());Framework.EnqueueLatentCommand(MakeShared<FRestore>(C));return true;
}
#endif
