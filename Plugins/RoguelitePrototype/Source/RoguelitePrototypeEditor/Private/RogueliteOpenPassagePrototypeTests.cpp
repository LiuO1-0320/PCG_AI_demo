#include "RogueliteOpenPassagePrototypeRig.h"
#include "RogueliteDevGameMode.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "Engine/DirectionalLight.h"
#include "Engine/SkyLight.h"
#include "GameFramework/PlayerStart.h"
#include "Editor.h"
#include "LevelEditorViewport.h"
#include "FileHelpers.h"
#include "EngineUtils.h"
#include "GameFramework/WorldSettings.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/PackageName.h"
#include "HAL/FileManager.h"
#if WITH_DEV_AUTOMATION_TESTS
namespace
{
const TCHAR* Map=TEXT("/Game/Roguelite/Maps/L_Roguelite_OpenPassage_Dev");
FString Evidence(){static FString P=FPaths::ProjectPluginsDir()/TEXT("RoguelitePrototype/Development/P03E2HF03/Geometry-")+FDateTime::UtcNow().ToString(TEXT("%Y%m%d-%H%M%S"));IFileManager::Get().MakeDirectory(*P,true);return P;}
void Save(const FString& Name,const FString& Data){FFileHelper::SaveStringToFile(Data,*(Evidence()/Name),FFileHelper::EEncodingOptions::ForceUTF8);}
bool OwnWorld(){return GEditor&&!GEditor->PlayWorld&&GEditor->GetEditorWorldContext().World()->GetOutermost()->GetName()==Map;}
ARogueliteOpenPassagePrototypeRig* MainRig(){if(!OwnWorld())return nullptr;for(TActorIterator<ARogueliteOpenPassagePrototypeRig> I(GEditor->GetEditorWorldContext().World());I;++I)if(!I->IsActorBeingDestroyed()&&I->GetActorLabel()==TEXT("HF03_Prototype_Controls"))return *I;return nullptr;}
TMap<FString,FTransform> Scene(UWorld* W){TMap<FString,FTransform> R;for(TActorIterator<AActor> I(W);I;++I)if(!I->IsActorBeingDestroyed())R.Add(I->GetPathName(),I->GetActorTransform());return R;}
bool Same(UWorld* W,const TMap<FString,FTransform>& Before){auto Now=Scene(W);if(Now.Num()!=Before.Num())return false;for(const auto& P:Before){auto* T=Now.Find(P.Key);if(!T||!T->Equals(P.Value,.0001))return false;}return true;}
bool ShapeAudit(FAutomationTestBase* Test,ARogueliteOpenPassagePrototypeRig* Rig,bool Open,FString& Data)
{
    bool Good=true;TArray<UStaticMeshComponent*> C;Rig->Connector->GetComponents(C);Good&=C.Num()==10;
    for(auto* M:C)
    {
        const bool Inactive=Open&&M->GetName().Contains(TEXT("Frame"));
        Good&=M->IsVisible()!=Inactive;
        Good&=M->GetCollisionEnabled()==(Inactive?ECollisionEnabled::NoCollision:ECollisionEnabled::QueryAndPhysics);
        const FBox Box=M->GetStaticMesh()->GetBoundingBox().TransformBy(M->GetComponentTransform());
        if(M->GetFName()==TEXT("Floor"))Good&=FMath::Abs(Box.Max.Z-Rig->Connector->GetActorLocation().Z)<.001;
        if(M->GetFName()==TEXT("Ceiling"))Good&=FMath::Abs(Box.Min.Z-(Rig->Connector->GetActorLocation().Z+400))<.001;
        Data+=FString::Printf(TEXT("%s Visible=%d Collision=%d WorldMin=%s WorldMax=%s\n"),*M->GetName(),M->IsVisible(),int32(M->GetCollisionEnabled()),*Box.Min.ToString(),*Box.Max.ToString());
    }
    const TCHAR* SideNames[]={TEXT("North"),TEXT("South"),TEXT("East"),TEXT("West")};
    ARogueliteParametricRoom* Rooms[]={Rig->RoomA,Rig->RoomB};const ERogueliteRoomSide Sides[]={Rig->SideA,Rig->SideB};
    for(int32 I=0;I<2;++I)
    {
        TArray<UStaticMeshComponent*> Meshes;Rooms[I]->GetComponents(Meshes);Good&=Meshes.Num()==13;
        const FString Header=FString(SideNames[uint8(Sides[I])])+TEXT("WallLintel");
        for(auto* M:Meshes)
        {
            if(M->GetName()==Header)Good&=M->IsVisible()!=Open&&M->GetCollisionEnabled()==(Open?ECollisionEnabled::NoCollision:ECollisionEnabled::QueryAndPhysics);
            if(M->GetFName()==TEXT("Floor")){const auto B=M->GetStaticMesh()->GetBoundingBox().TransformBy(M->GetComponentTransform());Good&=FMath::Abs(B.Max.Z-Rooms[I]->GetActorLocation().Z)<.001;Data+=FString::Printf(TEXT("Room%d FloorTop=%.6f FloorBottom=%.6f WallTop=%.6f\n"),I,B.Max.Z,B.Min.Z,Rooms[I]->GetActorLocation().Z+Rooms[I]->RoomHeight);}
        }
        FRogueliteDoorSocket Door;Good&=Rooms[I]->TryGetDoorSocket(Sides[I],Door)&&FMath::Abs(Door.WorldPosition.Z-Rooms[I]->GetActorLocation().Z)<.001;
        Good&=Door.DoorWidth==(Open?500:200)&&Door.DoorHeight==(Open?400:250);
    }
    Test->TestTrue(TEXT("Actual geometry, active collision and Z definitions match mode"),Good);return Good;
}
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHF03Initialize,"ProjectAuthoring.HF03.InitializePrototype",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FHF03Initialize::RunTest(const FString&)
{
    if(!GEditor||GEditor->PlayWorld)return false;
    if(FPaths::FileExists(FPackageName::LongPackageNameToFilename(Map,FPackageName::GetMapPackageExtension())))
    {AddWarning(TEXT("Existing HF03 map preserved; authoring refused to overwrite."));return true;}
    auto* W=UEditorLoadingAndSavingUtils::NewBlankMap(false);if(!W)return false;W->GetWorldSettings()->DefaultGameMode=ARogueliteDevGameMode::StaticClass();
    W->SpawnActor<APlayerStart>(FVector(0,0,150),FRotator::ZeroRotator);W->SpawnActor<ADirectionalLight>(FVector(0,0,1200),FRotator(-45,-30,0));W->SpawnActor<ASkyLight>();
    return TestTrue(TEXT("Save only new HF03 map"),UEditorLoadingAndSavingUtils::SaveMap(W,Map));
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHF03CreateFixture,"ProjectAuthoring.HF03.CreateComparisonFixture",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FHF03CreateFixture::RunTest(const FString&)
{
    if(!OwnWorld())return false;
    if(MainRig()){AddInfo(TEXT("Existing HF03 comparison fixture preserved."));return true;}
    auto* W=GEditor->GetEditorWorldContext().World();for(TActorIterator<ARogueliteParametricRoom> I(W);I;++I)if(!I->IsActorBeingDestroyed()){AddError(TEXT("Foreign room present; refusing fixture creation"));return false;}
    auto* Rig=ARogueliteOpenPassagePrototypeRig::CreateEditorPrototype(W,FTransform::Identity);TestNotNull(TEXT("Independent comparison fixture"),Rig);if(!Rig)return false;
    TestEqual(TEXT("Initial original Legacy59"),Rig->LastValidation.Probes.Num(),59);return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHF03Geometry,"ProjectVerification.HF03.IndependentGeometry",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FHF03Geometry::RunTest(const FString&)
{
    if(!OwnWorld()){AddError(TEXT("HF03-only Editor world required"));return false;}auto* W=GEditor->GetEditorWorldContext().World();const auto Before=Scene(W);
    FString Rows=TEXT("Mode\tSide\tOffsetA\tOffsetB\tFloorA\tFloorB\tCorridorFloor\tProbes\tGeometry\tCapsule\tSameTransforms\tSameConnection\tShapeAudit\n");
    TestTrue(TEXT("Old config default remains Legacy"),FRogueliteDoorConfig().OpeningMode==ERogueliteOpeningMode::Legacy);
    for(int32 Side=0;Side<4;++Side)for(int32 Shift=0;Shift<2;++Shift)
    {
        const FTransform Anchor(FRotator(0,Shift?Side*90:0,0),FVector(30000,30000,Shift?37.5:0));
        auto* Rig=ARogueliteOpenPassagePrototypeRig::CreateEditorPrototype(W,Anchor,ERogueliteRoomSide(Side),Shift?150:0,Shift?-175:0,FVector(3000,2500,400),Shift?10:20,Shift?30:20,Shift?15:20);
        TestNotNull(TEXT("Independent matrix fixture"),Rig);if(!Rig)return false;
        const FTransform A=Rig->RoomA->GetActorTransform(),B=Rig->RoomB->GetActorTransform(),C=Rig->Connector->GetActorTransform();const FGuid Id=Rig->LastValidation.ConnectionId;
        for(int32 Mode=0;Mode<2;++Mode)
        {
            TestTrue(TEXT("Explicit Editor-only mode switch: ")+Rig->Status,Rig->Configure(ERogueliteOpeningMode(Mode)));
            const auto R=Rig->LastValidation;TestEqual(TEXT("Truthful mode-specific probe count"),R.Probes.Num(),Mode==0?59:75);
            TestTrue(TEXT("All real geometry and capsule checks"),R.bSucceeded&&R.bGeometryPassed&&R.bCapsulePassed);
            bool Pose=Rig->RoomA->GetActorTransform().Equals(A)&&Rig->RoomB->GetActorTransform().Equals(B)&&Rig->Connector->GetActorTransform().Equals(C);
            bool Lease=Id==R.ConnectionId&&Rig->Assembly->Connections.Num()==1&&ARogueliteRoomAssembly::GetRoomSocketConnectionId(Rig->RoomA,Rig->SideA)==Id;
            TestTrue(TEXT("Mode does not move Actors or replace the reservation"),Pose&&Lease);
            FString Shapes;const bool Shape=ShapeAudit(this,Rig,Mode==1,Shapes);Save(FString::Printf(TEXT("mode%d-side%d-offset%d-shapes.txt"),Mode,Side,Shift),Shapes);
            FString Evidence;FRogueliteAssemblyResult::StaticStruct()->ExportText(Evidence,&R,nullptr,nullptr,PPF_None,nullptr);Save(FString::Printf(TEXT("mode%d-side%d-offset%d-c2.txt"),Mode,Side,Shift),Evidence);
            Rows+=FString::Printf(TEXT("%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\n"),Mode,Side,Shift?150:0,Shift?-175:0,Shift?10:20,Shift?30:20,Shift?15:20,R.Probes.Num(),R.bGeometryPassed,R.bCapsulePassed,Pose,Lease,Shape);
        }
        if(Side==0&&Shift==0)
        {
            for(int32 I=0;I<10;++I){TestTrue(TEXT("Repeated switch has no stale collision"),Rig->Configure(I%2?ERogueliteOpeningMode::Legacy:ERogueliteOpeningMode::OpenPassage));FString S;ShapeAudit(this,Rig,I%2==0,S);}
            TestTrue(TEXT("Restore Open for negative checks"),Rig->Configure(ERogueliteOpeningMode::OpenPassage));
            FRogueliteDoorSocket AtSeam;Rig->RoomA->TryGetDoorSocket(Rig->SideA,AtSeam);auto* Block=W->SpawnActor<AStaticMeshActor>(AtSeam.WorldPosition+AtSeam.WorldOutwardDirection*30+FVector(0,0,330),FRotator::ZeroRotator);Block->GetStaticMeshComponent()->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));Block->SetActorScale3D(FVector(2,5,.2));
            auto Bad=Rig->Assembly->RevalidateConnection(Id);bool UpperFailed=false;for(const auto& P:Bad.Probes)UpperFailed|=P.Name.Contains(TEXT("upper capsule"))&&!P.bPassed;
            TestTrue(TEXT("Visible injected upper obstruction cannot pass upper capsule checks"),!Bad.bSucceeded&&UpperFailed);FString Failure;FRogueliteAssemblyResult::StaticStruct()->ExportText(Failure,&Bad,nullptr,nullptr,PPF_None,nullptr);Save(TEXT("negative-upper-blocker.txt"),Failure);Block->Destroy();
            TestTrue(TEXT("After removing own test obstacle current geometry revalidates"),Rig->Assembly->RevalidateConnection(Id).bSucceeded);
            auto& D=ARogueliteOpenPassagePrototypeRig::Door(Rig->RoomA,Rig->SideA);const auto Authored=D;D.DoorHeight=399;Rig->RoomA->RebuildRoom();FRogueliteDoorSocket Invalid;
            TestFalse(TEXT("Open height399 does not pretend to be wall-top400"),Rig->RoomA->TryGetDoorSocket(Rig->SideA,Invalid));TestEqual(TEXT("Illegal height data retained"),D.DoorHeight,399.f);D=Authored;
            D.ManualOffset=701;Rig->RoomA->RebuildRoom();TestFalse(TEXT("Open still enforces corner clearance"),Rig->RoomA->TryGetDoorSocket(Rig->SideA,Invalid));D=Authored;
            D.bEnabled=false;Rig->RoomA->RebuildRoom();auto Closed=Rig->Assembly->RevalidateConnection(Id);TestFalse(TEXT("Disabled exit refuses connection"),Closed.bSucceeded);D=Authored;Rig->RoomA->RebuildRoom();TestTrue(TEXT("Restored side still revalidates"),Rig->Assembly->RevalidateConnection(Id).bSucceeded);
        }
        Rig->DestroyOwnTestFixture();
    }
    auto* Height=ARogueliteOpenPassagePrototypeRig::CreateEditorPrototype(W,FTransform(FVector(30000,30000,0)),ERogueliteRoomSide::North,0,0,FVector(3000,2500,500));
    TestNotNull(TEXT("Legacy unequal-height fixture"),Height);if(Height){const FTransform B=Height->RoomB->GetActorTransform();TestTrue(TEXT("Legacy400/500 remains valid59"),Height->LastValidation.bSucceeded&&Height->LastValidation.Probes.Num()==59);TestFalse(TEXT("Open unequal-height adaptation explicitly unavailable"),Height->Configure(ERogueliteOpeningMode::OpenPassage));TestTrue(TEXT("Height mismatch does not change old geometry or Transform"),Height->CurrentMode==ERogueliteOpeningMode::Legacy&&Height->RoomB->RoomHeight==500&&Height->RoomB->GetActorTransform().Equals(B));Save(TEXT("height-compatibility.txt"),Height->Status);Height->DestroyOwnTestFixture();}
    Save(TEXT("cases.tsv"),Rows);TestTrue(TEXT("Only new ephemeral fixtures were cleaned; saved comparison Actors unchanged"),Same(W,Before));return true;
}
namespace
{
bool View(FAutomationTestBase* Test,ERogueliteOpeningMode Mode)
{
    auto* Rig=MainRig();if(!Rig){Test->AddError(TEXT("Main HF03 prototype required"));return false;}
    if(!Rig->Configure(Mode)){Test->AddError(Rig->Status);return false;}
    FRogueliteDoorSocket D;Rig->RoomA->TryGetDoorSocket(Rig->SideA,D);const FVector Camera=D.WorldPosition-D.WorldOutwardDirection*300+FVector(0,0,150);const FRotator Rotation=D.WorldOutwardDirection.Rotation();
    for(auto* V:GEditor->GetLevelViewportClients())if(V&&V->Viewport){V->SetGameView(true);V->SetViewLocation(Camera);V->SetViewRotation(Rotation);V->Invalidate();}
    GEditor->SelectNone(false,true,false);FString S;ShapeAudit(Test,Rig,Mode==ERogueliteOpeningMode::OpenPassage,S);Save(Mode==ERogueliteOpeningMode::Legacy?TEXT("legacy-view-shapes.txt"):TEXT("open-view-shapes.txt"),S);
    Save(TEXT("camera.txt"),Camera.ToString()+TEXT(" | ")+Rotation.ToString());Test->AddInfo(Rig->Status);return true;
}
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHF03LegacyView,"ProjectAuthoring.HF03.LegacyView",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FHF03LegacyView::RunTest(const FString&){return View(this,ERogueliteOpeningMode::Legacy);}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHF03OpenView,"ProjectAuthoring.HF03.OpenView",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FHF03OpenView::RunTest(const FString&){return View(this,ERogueliteOpeningMode::OpenPassage);}
#endif
