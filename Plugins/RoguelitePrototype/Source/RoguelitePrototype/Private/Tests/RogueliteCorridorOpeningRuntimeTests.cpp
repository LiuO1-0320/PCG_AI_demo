#include "Misc/AutomationTest.h"
#include "RogueliteFloorProfile.h"
#include "RogueliteCompactCorridorContract.h"
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "RogueliteRuntimeNavigationSystem.h"
#include "RogueliteNavigationTestCharacter.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "FileHelpers.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Tests/AutomationEditorCommon.h"
namespace {
FRogueliteResolvedFloor LoopConfig;FRoguelitePreparedTemplateCatalog RuntimeCatalog;TMap<FString,FTransform> BeforePIE;
FString EvidenceDir(){static FString D=FPaths::ProjectPluginsDir()/TEXT("RoguelitePrototype/Development/P03E2HF02/Runtime-")+FDateTime::UtcNow().ToString(TEXT("%Y%m%d-%H%M%S"));IFileManager::Get().MakeDirectory(*D,true);return D;}
void Save(const FString& Name,const FString& Text){FFileHelper::SaveStringToFile(Text,*(EvidenceDir()/Name),FFileHelper::EEncodingOptions::ForceUTF8);}
TMap<FString,FTransform> Scene(UWorld* W){TMap<FString,FTransform> R;for(TActorIterator<AActor> I(W);I;++I)if(!I->IsActorBeingDestroyed())R.Add(I->GetPathName(),I->GetActorTransform());return R;}
bool Same(UWorld* W,const TMap<FString,FTransform>& B){auto A=Scene(W);if(A.Num()!=B.Num())return false;for(const auto& P:B){const auto* T=A.Find(P.Key);if(!T||!T->Equals(P.Value,.0001))return false;}return true;}
}
class FHF02Game : public IAutomationLatentCommand
{
    FAutomationTestBase* T;double Started=FPlatformTime::Seconds(),LegStart=0,NavStart=0;int32 Phase=0,Leg=0,Samples=0;bool WalkingOnly=true;
    TWeakObjectPtr<ARogueliteRuntimeLoopAssembly> Owner;TWeakObjectPtr<ARogueliteNavigationTestCharacter> Walker;
    TArray<TWeakObjectPtr<ARogueliteParametricCorridor>> Corridors;FVector Begin,Direction;double Distance=0;FString WalkRows=TEXT("Kind\tC1cm\tSpeedCmPerSec\tActualSeconds\tTravelCm\tSamples\tWalkingOnly\tPassed\n");
    bool Finish(){if(Walker.IsValid())Walker->Destroy();if(Owner.IsValid()){T->TestTrue(TEXT("Runtime own compact batch cleanup"),Owner->ClearRuntime());Owner->Destroy();}return true;}
public:
    FHF02Game(FAutomationTestBase* In):T(In){}
    virtual bool Update() override
    {
        auto* W=GEditor?GEditor->PlayWorld.Get():nullptr;
        if(!W||!W->HasBegunPlay()){if(FPlatformTime::Seconds()-Started>30){T->AddError(TEXT("PIE did not start"));return true;}return false;}
        if(Phase==0)
        {
            if(W->GetTimeSeconds()<1.2)return false;ARogueliteRuntimeLoopAssembly* A=nullptr;for(TActorIterator<ARogueliteRuntimeLoopAssembly> I(W);I;++I){A=*I;break;}if(!A){T->AddError(TEXT("Original dedicated Runtime navigation source absent"));return true;}
            A->ClearRuntime();A->bAutoGenerate=false;A->bNavigationEnabled=false;Owner=A;A->Settings=LoopConfig.Source;A->Planning=LoopConfig.Planning;A->Seed=LoopConfig.UsedSeed;
            const bool Good=FRogueliteFloorProfileResolver::GenerateRuntime(A,LoopConfig,ERogueliteFloorValidationPurpose::DeveloperValidation,RuntimeCatalog,A->Batch,A->CommittedLoops,A->LastGeometry);
            T->TestTrue(TEXT("Shared compact runtime actual assembly: ")+A->LastGeometry.Reason,Good);if(!Good)return Finish();A->bGeometryValidated=true;A->Stage=ERogueliteDungeonBuildStage::NavigationPending;
            TArray<FRogueliteCompactCorridorMeasurement> M;FString Why;T->TestTrue(TEXT("Runtime actual all lengths"),FRogueliteCompactCorridorContract::Actual(A->Batch,A->CommittedLoops,M,Why));
            T->TestEqual(TEXT("Actual committed compact loop"),A->CommittedLoops.Edges.Num(),1);if(A->CommittedLoops.Edges.IsEmpty())return Finish();
            auto* Longest=A->Batch.Edges[0].Corridor.Get();for(const auto& E:A->Batch.Edges)if(E.Corridor->CorridorLength>Longest->CorridorLength)Longest=E.Corridor;Corridors={Longest,A->CommittedLoops.Edges[0].Corridor.Get()};Phase=1;
        }
        if(Phase==1)
        {
            if(Leg>=Corridors.Num()){NavStart=FPlatformTime::Seconds();T->TestTrue(TEXT("Original strict B4-B3 navigation started"),Owner->NavigationReadiness->Start(Owner->LastGeometry.GeometrySeconds));Phase=3;return false;}
            auto* C=Corridors[Leg].Get();if(!C){T->AddError(TEXT("Representative corridor absent"));return Finish();}FRogueliteCorridorSocket A,B;C->TryGetCorridorSocket(ERogueliteCorridorEndpoint::Start,A);C->TryGetCorridorSocket(ERogueliteCorridorEndpoint::End,B);
            Begin=A.DoorSocket.WorldPosition;Distance=FVector::Distance(Begin,B.DoorSocket.WorldPosition);Direction=(B.DoorSocket.WorldPosition-Begin).GetSafeNormal();
            FActorSpawnParameters P;P.ObjectFlags=RF_Transient;P.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
            auto* Character=W->SpawnActor<ARogueliteNavigationTestCharacter>(Begin+FVector(0,0,88.1),Direction.Rotation(),P);if(!Character){T->AddError(TEXT("Walking fixture absent"));return Finish();}
            Character->GetCharacterMovement()->bRunPhysicsWithNoController=true;Character->GetCharacterMovement()->SetMovementMode(MOVE_Walking);Walker=Character;LegStart=W->GetTimeSeconds();Samples=0;WalkingOnly=true;Phase=2;
        }
        if(Phase==2)
        {
            auto* C=Walker.Get();if(!C){T->AddError(TEXT("Walking character lost"));return Finish();}++Samples;WalkingOnly&=C->GetCharacterMovement()->MovementMode==MOVE_Walking&&!C->bPressedJump;
            const double Progress=FVector::DotProduct(C->GetActorLocation()-Begin,Direction),Seconds=W->GetTimeSeconds()-LegStart;
            if(Progress>=Distance-.1||Seconds>10)
            {
                const bool Pass=Progress>=Distance-.1&&Seconds<=10&&WalkingOnly;T->TestTrue(TEXT("Real continuous no-sprint/no-jump corridor traversal <=10s"),Pass);
                const FString Line=FString::Printf(TEXT("%s\t%.6f\t%.3f\t%.6f\t%.6f\t%d\t%d\t%d\n"),Leg==0?TEXT("Tree"):TEXT("Loop"),Distance,C->GetCharacterMovement()->MaxWalkSpeed,Seconds,Progress,Samples,WalkingOnly,Pass);WalkRows+=Line;Save(TEXT("continuous-walking.tsv"),WalkRows);UE_LOG(LogTemp,Display,TEXT("HF02_CONTINUOUS_WALK %s"),*Line);
                C->Destroy();Walker.Reset();++Leg;Phase=1;return false;
            }
            C->AddMovementInput(Direction,1,true);return false;
        }
        if(Phase==3)
        {
            const auto& R=Owner->NavigationReadiness->Report;
            if(R.bReady||R.State!=TEXT("NavigationPending")||FPlatformTime::Seconds()-NavStart>240)
            {
                FString Text;FRogueliteNavigationReadinessEvidence::StaticStruct()->ExportText(Text,&R,nullptr,nullptr,PPF_None,nullptr);Save(TEXT("compact-navigation.txt"),Text);
                T->TestTrue(TEXT("Compact profile retains original strict navigation Ready: ")+R.Reason,R.bReady&&Owner->IsDungeonReady());int32 Paths=0,Moves=0;for(const auto& P:R.Paths)Paths+=P.bSucceeded&&!P.bPartial;for(const auto& M:R.Moves)Moves+=M.bSucceeded;
                UE_LOG(LogTemp,Display,TEXT("HF02_RUNTIME_NAV Seed=%d Ready=%d Paths=%d/%d Moves=%d/%d Build=%.3f Move=%.3f"),LoopConfig.UsedSeed,R.bReady,Paths,R.Paths.Num(),Moves,R.Moves.Num(),R.BuildSeconds,R.MoveSeconds);return Finish();
            }
        }
        return false;
    }
};
class FHF02Stopped : public IAutomationLatentCommand
{
    FAutomationTestBase* T;
public:FHF02Stopped(FAutomationTestBase* In):T(In){}virtual bool Update() override{if(GEditor->PlayWorld)return false;T->TestTrue(TEXT("Runtime leaves Editor scene unchanged"),Same(GEditor->GetEditorWorldContext().World(),BeforePIE));return true;}
};
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHF02Runtime,"ProjectVerification.HF02.RuntimeWalkingAndNavigation",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FHF02Runtime::RunTest(const FString&)
{
    if(!GEditor||GEditor->PlayWorld)return false;
    const FString Root=FPaths::ProjectPluginsDir()/TEXT("RoguelitePrototype/Development/P03E2HF02");TArray<FString> Dirs;IFileManager::Get().FindFiles(Dirs,*(Root/TEXT("Native-*")),false,true);Dirs.Sort();FString Fixture;
    for(int32 I=Dirs.Num()-1;I>=0;--I)if(FFileHelper::LoadFileToString(Fixture,*(Root/Dirs[I]/TEXT("runtime-config.txt"))))break;
    if(Fixture.IsEmpty()||!FRogueliteResolvedFloor::StaticStruct()->ImportText(*Fixture,&LoopConfig,nullptr,PPF_None,GWarn,TEXT("CompactFixture"))){AddError(TEXT("No real compact Loop fixture from preceding matrix; no synthetic fallback."));return false;}
    auto* W=GEditor->GetEditorWorldContext().World();auto* Probe=W->SpawnActor<AActor>();FString Why;const bool Prepared=FRoguelitePreparedTemplateCatalog::CaptureEditor(Probe,LoopConfig.Source,RuntimeCatalog,Why);Probe->Destroy();if(!Prepared){AddError(Why);return false;}
    // Existing map-local navigation class explicitly allows this original test map. Read only;
    // only the PIE copy changes config. No core whitelist/global navigation change required.
    if(!UEditorLoadingAndSavingUtils::LoadMap(TEXT("/Game/Roguelite/Maps/L_Roguelite_RuntimeLoopNavigation_Dev"))){AddError(TEXT("Dedicated legacy navigation map unavailable"));return false;}
    BeforePIE=Scene(GEditor->GetEditorWorldContext().World());FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FStartPIECommand>(false));FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FHF02Game>(this));FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FEndPlayMapCommand>());FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FHF02Stopped>(this));return true;
}
#endif
