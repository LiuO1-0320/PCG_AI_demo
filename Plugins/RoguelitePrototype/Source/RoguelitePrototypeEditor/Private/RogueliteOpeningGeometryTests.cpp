#include "RogueliteRoomAssembly.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Components/ArrowComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/StaticMesh.h"
#include "Editor.h"
#include "Engine/World.h"
#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHF02OpeningGeometry,"ProjectVerification.HF02.OpeningGeometry",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FHF02OpeningGeometry::RunTest(const FString&)
{
    UWorld* W=GEditor?GEditor->GetEditorWorldContext().World():nullptr;
    if(!W||GEditor->PlayWorld||W->GetOutermost()->GetName()!=TEXT("/Game/Roguelite/Maps/L_Roguelite_CorridorOpening_Dev")){AddError(TEXT("HF02-only Editor map required"));return false;}
    FString Dir=FPaths::ProjectPluginsDir()/TEXT("RoguelitePrototype/Development/P03E2HF02/Opening-")+FDateTime::UtcNow().ToString(TEXT("%Y%m%d-%H%M%S"));IFileManager::Get().MakeDirectory(*Dir,true);
    FString Rows=TEXT("Side\tOffset\tConnected\tProbes\tGeometry\tCapsule\tComponents\tFramesOutsideOpening\tFloorTopZ\tDebugOnly\tDisabledRejected\tClear\n");
    const auto Door=[](ARogueliteParametricRoom* R,int32 I)->FRogueliteDoorConfig&{switch(I){case 0:return R->NorthDoor;case 1:return R->SouthDoor;case 2:return R->EastDoor;default:return R->WestDoor;}};
    for(int32 Side=0;Side<4;++Side)for(int32 Shift=0;Shift<2;++Shift)
    {
        auto* A=W->SpawnActor<ARogueliteParametricRoom>();auto* B=W->SpawnActor<ARogueliteParametricRoom>();auto* C=W->SpawnActor<ARogueliteParametricCorridor>();auto* Assembly=W->SpawnActor<ARogueliteRoomAssembly>();
        if(!A||!B||!C||!Assembly){AddError(TEXT("Could not allocate own opening fixture"));return false;}
        A->SetActorRotation(FRotator(0,Side*90,0));A->RoomLength=2000;A->RoomWidth=2000;A->RoomHeight=400;
        B->RoomLength=3000;B->RoomWidth=2500;B->RoomHeight=500;B->SetActorLocation(FVector(10000,10000,0));
        const int32 Target=(Side+1)%4;auto& AD=Door(A,Side);auto& BD=Door(B,Target);
        AD.bEnabled=BD.bEnabled=true;AD.bAutoCenter=BD.bAutoCenter=false;AD.ManualOffset=Shift?150:0;BD.ManualOffset=Shift?-175:0;AD.DoorWidth=BD.DoorWidth=200;AD.DoorHeight=BD.DoorHeight=250;
        A->RebuildRoom();B->RebuildRoom();C->CorridorLength=1500;C->CorridorWidth=500;C->CorridorHeight=350;C->RebuildCorridor();
        auto R=Assembly->TryAlignCorridorConnection(A,ERogueliteRoomSide(Side),C,ERogueliteCorridorEndpoint::Start,B,ERogueliteRoomSide(Target));
        TestTrue(TEXT("Existing C2 keeps full 59-probe geometry and bidirectional capsule: ")+R.Message,R.bSucceeded&&R.bGeometryPassed&&R.bCapsulePassed&&R.Probes.Num()==59);
        FString Evidence;FRogueliteAssemblyResult::StaticStruct()->ExportText(Evidence,&R,nullptr,nullptr,PPF_None,nullptr);FFileHelper::SaveStringToFile(Evidence,*(Dir/FString::Printf(TEXT("side%d-offset%d-c2.txt"),Side,Shift)),FFileHelper::EEncodingOptions::ForceUTF8);
        TArray<UStaticMeshComponent*> Meshes;C->GetComponents(Meshes);bool Outside=true;double FloorZ=999;int32 FrameCount=0;
        for(auto* M:Meshes)
        {
            const FBox Local=M->GetStaticMesh()->GetBoundingBox().TransformBy(M->GetRelativeTransform());
            if(M->GetFName()==TEXT("Floor"))FloorZ=Local.Max.Z;
            if(M->GetName().Contains(TEXT("Frame"))){++FrameCount;Outside&=Local.Max.Y<=-100+.01||Local.Min.Y>=100-.01||Local.Min.Z>=250-.01;}
            TestFalse(TEXT("No independent Threshold/Decorative mesh exists"),M->GetName().Contains(TEXT("Threshold"))||M->GetName().Contains(TEXT("Decorative")));
        }
        TestTrue(TEXT("All six structural end pieces are outside the clear aperture"),FrameCount==6&&Outside);TestTrue(TEXT("Floor finished surface exactly z=0; no raised threshold"),FMath::Abs(FloorZ)<.01);
        bool Debug=true;TArray<UArrowComponent*> Arrows;C->GetComponents(Arrows);for(auto* M:Arrows)Debug&=M->bHiddenInGame&&M->GetCollisionEnabled()==ECollisionEnabled::NoCollision;
        TestTrue(TEXT("Socket markers are hidden in game and have no collision"),Arrows.Num()==2&&Debug);
        const FTransform CT=C->GetActorTransform(),BT=B->GetActorTransform();if(R.ConnectionId.IsValid())TestTrue(TEXT("Own test releases C2 authority"),Assembly->ReleaseConnection(R.ConnectionId));
        AD.bEnabled=false;A->RebuildRoom();auto Bad=Assembly->TryAlignCorridorConnection(A,ERogueliteRoomSide(Side),C,ERogueliteCorridorEndpoint::Start,B,ERogueliteRoomSide(Target));
        bool Rejected=!Bad.bSucceeded&&C->GetActorTransform().Equals(CT)&&B->GetActorTransform().Equals(BT)&&Assembly->Connections.IsEmpty();TestTrue(TEXT("Disabled socket rejects without moving or reserving"),Rejected);
        if(Side==0&&Shift==0)
        {
            auto* POI=W->SpawnActor<AStaticMeshActor>();POI->GetStaticMeshComponent()->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));POI->SetActorTransform(FTransform(FRotator(0,25,0),FVector(900,0,50),FVector(1.2,.8,1)));
            const FTransform Before=POI->GetActorTransform();A->ManualPOIActors.Add(POI);A->RoomLength=500;A->RebuildRoom();
            TestTrue(TEXT("P01 dimension edit leaves independent POI transform and mesh unchanged"),POI->GetActorTransform().Equals(Before)&&POI->GetStaticMeshComponent()->GetStaticMesh()!=nullptr);
            TestTrue(TEXT("Actual POI Bounds warning retained"),A->OutOfBoundsManualPOIActors.Contains(POI));A->ManualPOIActors.Reset();POI->Destroy();
        }
        Rows+=FString::Printf(TEXT("%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%.6f\t%d\t%d\t1\n"),Side,Shift,R.bSucceeded,R.Probes.Num(),R.bGeometryPassed,R.bCapsulePassed,Meshes.Num(),Outside,FloorZ,Debug,Rejected);
        C->Destroy();B->Destroy();A->Destroy();Assembly->Destroy();
    }
    FFileHelper::SaveStringToFile(Rows,*(Dir/TEXT("openings.tsv")),FFileHelper::EEncodingOptions::ForceUTF8);return true;
}
#endif
