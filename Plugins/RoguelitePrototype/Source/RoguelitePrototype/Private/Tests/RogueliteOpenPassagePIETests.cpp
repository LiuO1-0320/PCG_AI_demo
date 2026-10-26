#include "Misc/AutomationTest.h"
#include "RogueliteOpenPassagePrototypeRig.h"
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "RogueliteNavigationTestCharacter.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/SceneComponent.h"
#include "Editor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Components/PrimitiveComponent.h"
#include "Tests/AutomationEditorCommon.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
namespace
{
TMap<FString,FTransform> Before;
FString Dir(){static FString D=FPaths::ProjectPluginsDir()/TEXT("RoguelitePrototype/Development/P03E2HF03/PIE-")+FDateTime::UtcNow().ToString(TEXT("%Y%m%d-%H%M%S"));IFileManager::Get().MakeDirectory(*D,true);return D;}
void Save(const FString& Name,const FString& Data){FFileHelper::SaveStringToFile(Data,*(Dir()/Name),FFileHelper::EEncodingOptions::ForceUTF8);}
// Replays the complete Editor C2 evidence in the real PIE physics scene. This
// test-only reader does not publish connection validity or call the legacy
// RevalidateConnection API, which unconditionally rebuilds registered Static C1.
bool ReplayPhysics(UWorld* W,ARogueliteOpenPassagePrototypeRig* Rig,FString& Evidence)
{
    const auto& Reference=Rig->LastValidation;
    if(!Reference.bSucceeded||Reference.Probes.Num()!=(Rig->CurrentMode==ERogueliteOpeningMode::Legacy?59:75))return false;
    FCollisionQueryParams Rays(SCENE_QUERY_STAT(HF03PIERays),true),Capsules(SCENE_QUERY_STAT(HF03PIECapsules),false);
    Rays.AddIgnoredActor(Rig->Assembly);Capsules.AddIgnoredActor(Rig->Assembly);
    const FCollisionShape Shape=FCollisionShape::MakeCapsule(Rig->Assembly->TestCapsuleRadius,Rig->Assembly->TestCapsuleHalfHeight);
    bool All=true;
    for(const auto& P:Reference.Probes)
    {
        FHitResult Hit;bool Blocked=false,StartOverlap=false,EndOverlap=false;
        const bool Capsule=P.Name.Contains(TEXT("capsule")),Ground=P.Name.Contains(TEXT("ground"));
        ARogueliteParametricRoom* Room=P.Name.StartsWith(TEXT("C-B"))?Rig->RoomB.Get():Rig->RoomA.Get();
        if(Capsule)
        {
            StartOverlap=W->OverlapBlockingTestByChannel(P.Start,FQuat::Identity,ECC_Pawn,Shape,Capsules);
            EndOverlap=!P.bExpectedBlocking&&W->OverlapBlockingTestByChannel(P.End,FQuat::Identity,ECC_Pawn,Shape,Capsules);
            Blocked=W->SweepSingleByChannel(Hit,P.Start,P.End,FQuat::Identity,ECC_Pawn,Shape,Capsules);
        }
        else
        {
            FCollisionQueryParams Params=Rays;
            // C2 isolates each of the two touching solids for its wall probes.
            if(!P.Name.Contains(TEXT("floor")))
            {
                if(P.Name.Contains(TEXT("entity0"))||P.Name.Contains(TEXT("upper wall boundary")))Params.AddIgnoredActor(Rig->Connector);
                else if(P.Name.Contains(TEXT("entity1")))Params.AddIgnoredActor(Room);
            }
            Blocked=W->LineTraceSingleByChannel(Hit,P.Start,P.End,ECC_Visibility,Params);
        }
        const bool SameSolid=Hit.GetActor()&&Hit.GetComponent()&&P.HitObject.EndsWith(Hit.GetActor()->GetName()+TEXT(".")+Hit.GetComponent()->GetName());
        bool Passed=P.bExpectedBlocking?Blocked&&SameSolid:!Blocked&&!StartOverlap&&!EndOverlap;
        if(Capsule&&P.bExpectedBlocking)
        {
            if(Ground)Passed=Blocked&&!StartOverlap&&!Hit.bStartPenetrating&&(Hit.GetActor()==Room||Hit.GetActor()==Rig->Connector)&&Hit.GetComponent()&&Hit.GetComponent()->GetFName()==TEXT("Floor")&&Hit.ImpactNormal.Z>.99f;
            else Passed&=!StartOverlap&&!Hit.bStartPenetrating&&FMath::Abs(Hit.ImpactNormal.Z)<.01f;
        }
        All&=Passed;
        Evidence+=FString::Printf(TEXT("%s | ExpectedBlocking=%d ActualBlocking=%d StartOverlap=%d EndOverlap=%d StartPenetrating=%d SameSolid=%d Hit=%s Passed=%d\n"),*P.Name,P.bExpectedBlocking,Blocked,StartOverlap,EndOverlap,Hit.bStartPenetrating,SameSolid,*GetPathNameSafe(Hit.GetComponent()),Passed);
    }
    return All;
}
class FHF03Physics : public IAutomationLatentCommand
{
    FAutomationTestBase* Test;ERogueliteOpeningMode Mode;double Began=FPlatformTime::Seconds(),LegTime=0;int32 Phase=0,Leg=0,Samples=0;bool OnlyWalking=true;
    TWeakObjectPtr<ARogueliteOpenPassagePrototypeRig> Rig;TWeakObjectPtr<ARogueliteNavigationTestCharacter> Walker;
    FTransform A,B,C;FVector Start,Direction;double Length=0;
    FString Rows=TEXT("Mode\tDirection\tLengthCm\tSpeedCmS\tSeconds\tSamples\tWalkingOnly\tPassed\n");
public:
    FHF03Physics(FAutomationTestBase* T,ERogueliteOpeningMode M):Test(T),Mode(M){}
    virtual bool Update() override
    {
        auto* W=GEditor?GEditor->PlayWorld.Get():nullptr;if(!W||!W->HasBegunPlay()){if(FPlatformTime::Seconds()-Began>30){Test->AddError(TEXT("PIE world not ready"));return true;}return false;}
        if(Phase==0)
        {
            for(TActorIterator<ARogueliteOpenPassagePrototypeRig> I(W);I;++I){Rig=*I;break;}if(!Rig.IsValid()){Test->AddError(TEXT("Copied independent prototype missing"));return true;}
            Test->TestTrue(TEXT("Actual GameWorld mode from saved Editor geometry"),Rig->CurrentMode==Mode&&W->IsGameWorld());
            A=Rig->RoomA->GetActorTransform();B=Rig->RoomB->GetActorTransform();C=Rig->Connector->GetActorTransform();
            const FGuid Id=ARogueliteRoomAssembly::GetCorridorConnectionId(Rig->Connector);
            Test->TestTrue(TEXT("Copied C2 sole connection identity remains consistent"),Id.IsValid()&&Id==Rig->LastValidation.ConnectionId&&Rig->Assembly->Connections.Num()==1);
            FString Evidence;const bool Physics=ReplayPhysics(W,Rig.Get(),Evidence);
            Test->TestTrue(TEXT("Independent PIE replay of every Editor C2 ray/capsule, including floors and closed perimeter"),Physics);
            Save(Mode==ERogueliteOpeningMode::Legacy?TEXT("legacy-physics-replay.txt"):TEXT("open-physics-replay.txt"),Evidence);
            Test->TestTrue(TEXT("Room and corridor roots remain Static"),Rig->RoomA->GetRootComponent()->Mobility==EComponentMobility::Static&&Rig->RoomB->GetRootComponent()->Mobility==EComponentMobility::Static&&Rig->Connector->GetRootComponent()->Mobility==EComponentMobility::Static);
            Test->TestFalse(TEXT("Mode mutation explicitly refused in GameWorld"),Rig->Configure(Mode==ERogueliteOpeningMode::Legacy?ERogueliteOpeningMode::OpenPassage:ERogueliteOpeningMode::Legacy));Phase=1;
        }
        if(Phase==1)
        {
            if(Leg==2){Test->TestTrue(TEXT("No Static Actor moved during PIE validation/walking"),Rig->RoomA->GetActorTransform().Equals(A)&&Rig->RoomB->GetActorTransform().Equals(B)&&Rig->Connector->GetActorTransform().Equals(C));return true;}
            FRogueliteCorridorSocket S,E;Rig->Connector->TryGetCorridorSocket(Leg==0?ERogueliteCorridorEndpoint::Start:ERogueliteCorridorEndpoint::End,S);Rig->Connector->TryGetCorridorSocket(Leg==0?ERogueliteCorridorEndpoint::End:ERogueliteCorridorEndpoint::Start,E);
            Start=S.DoorSocket.WorldPosition;Direction=(E.DoorSocket.WorldPosition-Start).GetSafeNormal();Length=FVector::Distance(Start,E.DoorSocket.WorldPosition);
            FActorSpawnParameters P;P.ObjectFlags=RF_Transient;P.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
            auto* Character=W->SpawnActor<ARogueliteNavigationTestCharacter>(Start+FVector(0,0,88.1),Direction.Rotation(),P);if(!Character){Test->AddError(TEXT("Test Character unavailable"));return true;}
            Walker=Character;Character->GetCharacterMovement()->bRunPhysicsWithNoController=true;Character->GetCharacterMovement()->SetMovementMode(MOVE_Walking);Samples=0;OnlyWalking=true;LegTime=W->GetTimeSeconds();Phase=2;
        }
        if(Phase==2)
        {
            auto* Character=Walker.Get();if(!Character){Test->AddError(TEXT("Walker lost"));return true;}++Samples;OnlyWalking&=Character->GetCharacterMovement()->MovementMode==MOVE_Walking&&!Character->bPressedJump;
            const double Progress=FVector::DotProduct(Character->GetActorLocation()-Start,Direction),Seconds=W->GetTimeSeconds()-LegTime;
            if(Progress>=Length-.1||Seconds>10)
            {
                const bool Good=Progress>=Length-.1&&Seconds<=10&&OnlyWalking;Test->TestTrue(TEXT("Continuous normal walking across1500cm <=10s"),Good);
                Rows+=FString::Printf(TEXT("%d\t%d\t%.3f\t%.3f\t%.6f\t%d\t%d\t%d\n"),int32(Mode),Leg,Length,Character->GetCharacterMovement()->MaxWalkSpeed,Seconds,Samples,OnlyWalking,Good);
                Save(Mode==ERogueliteOpeningMode::Legacy?TEXT("legacy-walking.tsv"):TEXT("open-walking.tsv"),Rows);Character->Destroy();Walker.Reset();++Leg;Phase=1;return false;
            }
            Character->AddMovementInput(Direction,1,true);
        }
        return false;
    }
};
class FHF03Stop : public IAutomationLatentCommand
{
    FAutomationTestBase* Test;
public:explicit FHF03Stop(FAutomationTestBase* T):Test(T){}
    virtual bool Update() override{if(GEditor->PlayWorld)return false;int32 Count=0;for(TActorIterator<AActor> I(GEditor->GetEditorWorldContext().World());I;++I)if(!I->IsActorBeingDestroyed()){++Count;auto* T=Before.Find(I->GetPathName());Test->TestTrue(TEXT("PIE leaves Editor actor pose unchanged"),T&&T->Equals(I->GetActorTransform()));}Test->TestEqual(TEXT("PIE leaves no Editor Actors"),Count,Before.Num());return true;}
};
bool Run(FAutomationTestBase* Test,ERogueliteOpeningMode Mode)
{
    if(!GEditor||GEditor->PlayWorld||GEditor->GetEditorWorldContext().World()->GetOutermost()->GetName()!=TEXT("/Game/Roguelite/Maps/L_Roguelite_OpenPassage_Dev")){Test->AddError(TEXT("Only the HF03 independent map is allowed"));return false;}
    auto* W=GEditor->GetEditorWorldContext().World();ARogueliteOpenPassagePrototypeRig* R=nullptr;Before.Reset();for(TActorIterator<AActor> I(W);I;++I)if(!I->IsActorBeingDestroyed()){Before.Add(I->GetPathName(),I->GetActorTransform());if(auto* Candidate=Cast<ARogueliteOpenPassagePrototypeRig>(*I))R=Candidate;}
    if(!R||R->CurrentMode!=Mode){Test->AddError(TEXT("Configure this exact mode in Editor before running PIE; no live geometry edits attempted"));return false;}
    FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FStartPIECommand>(false));FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FHF03Physics>(Test,Mode));FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FEndPlayMapCommand>());FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FHF03Stop>(Test));return true;
}
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHF03LegacyPIE,"ProjectVerification.HF03.LegacyPIE",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FHF03LegacyPIE::RunTest(const FString&){return Run(this,ERogueliteOpeningMode::Legacy);}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHF03OpenPIE,"ProjectVerification.HF03.OpenPIE",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FHF03OpenPIE::RunTest(const FString&){return Run(this,ERogueliteOpeningMode::OpenPassage);}
#endif
