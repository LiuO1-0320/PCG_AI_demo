#include "RogueliteFloorProfile.h"
#include "RogueliteGenerationGeometry.h"
#include "RogueliteSeedAssemblyInternal.h"
#include "Misc/AutomationTest.h"
#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHF03CConfiguration,"Roguelite.HF03C.StageAConfiguration",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FHF03CConfiguration::RunTest(const FString&)
{
    using F=FRogueliteFloorProfileResolver;
    FRogueliteFloorDefinition Old;FRogueliteSeedLayoutSettings OldSettings;FRogueliteSeedCorridorProfile OldCorridor;
    TestTrue(TEXT("Old serialized defaults remain Legacy and ceiling ON"),Old.OpeningMode==ERogueliteOpeningMode::Legacy&&Old.bGenerateCorridorCeiling&&OldSettings.GenerationGeometry.OpeningMode==ERogueliteOpeningMode::Legacy&&OldCorridor.bGenerateCeiling);
    const FString Legacy=RogueliteGenerationGeometry::SettingsText(OldSettings);
    TestFalse(TEXT("New extension absent from legacy canonical settings"),Legacy.Contains(TEXT("GenerationGeometry="))||Legacy.Contains(TEXT("bGenerateCeiling=")));
    FRoguelitePreparedTemplateCatalog OldCatalog;auto& Row=OldCatalog.Rows.AddDefaulted_GetRef();Row.Id=TEXT("LegacyFixture");Row.Descriptor.Dimensions=FVector(2000,2000,400);
    FString RawCatalog;FRoguelitePreparedTemplateCatalog::StaticStruct()->ExportText(RawCatalog,&OldCatalog,nullptr,nullptr,PPF_None,nullptr);
    TestEqual(TEXT("Legacy catalog canonical exporter matches original UE ExportText when extensions are absent"),OldCatalog.ComputeDigest(),FRogueliteRoomGraphGenerator::StableTextDigest(RawCatalog));
    for(int32 Floor=1;Floor<=3;++Floor)for(int32 Seed=0;Seed<20;++Seed)
    {
        auto D=F::Defaults(Floor);const auto On=F::Resolve(D,Seed);
        TestTrue(TEXT("New profile resolves Open Passage"),On.bValid&&On.Source.GenerationGeometry.OpeningMode==ERogueliteOpeningMode::OpenPassage);
        if(!On.bValid)continue;
        const auto& P=On.Source.Corridors[0];
        TestTrue(TEXT("500 cm instance/clear height and full-width socket"),P.Height==500&&P.DoorHeight==500&&P.DoorWidth==P.Width&&P.bGenerateCeiling);
        for(float L:P.LengthCandidates)TestTrue(TEXT("1200..1500 hard candidate contract unchanged"),L>=1200&&L<=1500);
        D.bGenerateCorridorCeiling=false;const auto Off=F::Resolve(D,Seed);
        TestTrue(TEXT("Ceiling OFF reaches shared corridor profile"),Off.bValid&&!Off.Source.Corridors[0].bGenerateCeiling);
        D.OpeningMode=ERogueliteOpeningMode::Legacy;const auto L=F::Resolve(D,Seed);
        TestTrue(TEXT("Legacy retains authored corridor/door dimensions"),L.bValid&&L.Source.Corridors[0].Height==350&&L.Source.Corridors[0].DoorWidth==200&&L.Source.Corridors[0].DoorHeight==250);
        const auto A=FRogueliteRoomGraphGenerator::Generate(Seed,On.Source.GraphSettings());
        const auto B=FRogueliteRoomGraphGenerator::Generate(Seed,Off.Source.GraphSettings());
        const auto C=FRogueliteRoomGraphGenerator::Generate(Seed,L.Source.GraphSettings());
        TestTrue(TEXT("Visual configuration does not alter topology"),A.bSucceeded&&A.Graph.StableDigest==B.Graph.StableDigest&&A.Graph.StableDigest==C.Graph.StableDigest);
        TestNotEqual(TEXT("Ceiling change invalidates geometry configuration"),On.ConfigurationDigest,Off.ConfigurationDigest);
        TestNotEqual(TEXT("Ceiling change invalidates prepared geometry identity"),RogueliteGenerationGeometry::GeometryDigest(On.Source),RogueliteGenerationGeometry::GeometryDigest(Off.Source));
        auto Same=On.Source;Same.Seed+=100;
        TestEqual(TEXT("Catalog geometry identity independent of Seed"),RogueliteGenerationGeometry::GeometryDigest(On.Source),RogueliteGenerationGeometry::GeometryDigest(Same));
    }
    auto Bad=F::Defaults(1);Bad.OpenPassageRoomHeight=400;TestFalse(TEXT("No silent fallback for unsupported instance height"),F::Resolve(Bad,0).bValid);
    return true;
}
#endif

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Editor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Components/StaticMeshComponent.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
namespace
{
FRogueliteDoorConfig& HF03CDoor(ARogueliteParametricRoom* R,int32 I)
{return I==0?R->NorthDoor:I==1?R->SouthDoor:I==2?R->EastDoor:R->WestDoor;}
int32 LiveActors(UWorld* W){int32 N=0;for(TActorIterator<AActor> I(W);I;++I)N+=!I->IsActorBeingDestroyed();return N;}
UStaticMeshComponent* FindShape(AActor* A,FName Name)
{TInlineComponentArray<UStaticMeshComponent*> Shapes;A->GetComponents(Shapes);for(auto* X:Shapes)if(X->GetFName()==Name)return X;return nullptr;}
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHF03CStageAInstances,"ProjectVerification.HF03C.StageAInstanceGeometry",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FHF03CStageAInstances::RunTest(const FString&)
{
    if(!GEditor||GEditor->PlayWorld){AddError(TEXT("Stage A requires exclusive Editor non-PIE world; no asset saves"));return false;}
    UWorld* W=GEditor->GetEditorWorldContext().World();if(!W||W->WorldType!=EWorldType::Editor)return false;
    const int32 Before=LiveActors(W);bool Good=true;FString Rows=TEXT("Template\tYaw\tMode\tCeiling\tSourceHeight\tEffectiveHeight\tPOI\tTemplateValid\tTopRay\tUpCapsule\tFloorSides\tAperture\tClosedWall\tCleanup\n");
    const FString Paths[]={TEXT("/Game/Roguelite/RoomTemplates/BP_Room_Junction3.BP_Room_Junction3_C"),TEXT("/Game/Roguelite/RoomTemplates/BP_Room_Warehouse.BP_Room_Warehouse_C")};
    for(int32 Template=0;Template<2&&Good;++Template)
    {
        UClass* Class=LoadClass<ARogueliteRoomTemplate>(nullptr,*Paths[Template]);if(!Class){AddError(TEXT("Original template unavailable: ")+Paths[Template]);return false;}
        const auto* CDO=Class->GetDefaultObject<ARogueliteRoomTemplate>();const float OriginalHeight=CDO->RoomHeight;
        TestEqual(TEXT("Actual source template height"),OriginalHeight,Template?500.f:400.f);
        for(int32 Yaw=0;Yaw<4&&Good;++Yaw)for(int32 Mode=0;Mode<2&&Good;++Mode)for(int32 Roof=0;Roof<2&&Good;++Roof)
        {
            auto D=FRogueliteFloorProfileResolver::Defaults(1);D.OpeningMode=Mode?ERogueliteOpeningMode::OpenPassage:ERogueliteOpeningMode::Legacy;D.bGenerateCorridorCeiling=Roof!=0;
            const auto Config=FRogueliteFloorProfileResolver::Resolve(D,0);if(!Config.bValid){AddError(Config.Message);return false;}
            const auto P=Config.Source.Corridors[0];FActorSpawnParameters Args;Args.ObjectFlags|=RF_Transient;Args.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
            const FTransform Pose(FRotator(0,Yaw*90,0),FVector(3000000,3000000,0));
            auto* Room=W->SpawnActor<ARogueliteRoomTemplate>(Class,Pose,Args);if(!Room){AddError(TEXT("Transient source instance spawn failed"));return false;}
            const FString Members=RogueliteSeedAssemblyInternal::MembersText(Room);const FTransform BeforePose=Room->GetActorTransform();
            for(int32 I=0;I<4;++I)if(HF03CDoor(Room,I).bEnabled){HF03CDoor(Room,I).bAutoCenter=false;HF03CDoor(Room,I).ManualOffset=(I%2)?-125.f:175.f;}
            Room->RebuildRoom();const auto OriginalDoors=RogueliteGenerationGeometry::Doors(Room);
            FString Why;const bool Adapted=RogueliteGenerationGeometry::ConfigureTemplate(Room,Config.Source,Why);
            const auto Valid=Room->ValidateTemplate();const auto Effective=RogueliteGenerationGeometry::Doors(Room);
            bool POI=Members==RogueliteSeedAssemblyInternal::MembersText(Room)&&BeforePose.Equals(Room->GetActorTransform(),.0001)&&Room->GetActorScale3D().Equals(FVector::OneVector,.0001);
            bool Aperture=Adapted&&Valid.bValid,Closed=false;int32 Disabled=INDEX_NONE;
            FCollisionQueryParams Q(SCENE_QUERY_STAT(HF03CStageA),false);
            for(int32 I=0;I<4;++I)
            {
                POI&=Effective[I].ManualOffset==OriginalDoors[I].ManualOffset&&Effective[I].bAutoCenter==OriginalDoors[I].bAutoCenter;
                if(!Effective[I].bEnabled)continue;
                FRogueliteDoorSocket S;const bool Socket=Room->TryGetDoorSocket(static_cast<ERogueliteRoomSide>(I),S);
                Aperture&=Socket&&S.LocalPosition.Z==0&&S.AppliedOffset==OriginalDoors[I].ManualOffset&&S.DoorWidth==(Mode?500:OriginalDoors[I].DoorWidth)&&S.DoorHeight==(Mode?500:OriginalDoors[I].DoorHeight);
                FHitResult Hit;const FVector Mid=S.WorldPosition+FVector(0,0,Mode?450:100),N=S.WorldOutwardDirection;
                Aperture&=!W->LineTraceSingleByChannel(Hit,Mid-N*(S.WallThickness+25),Mid+N*(S.WallThickness+25),ECC_Pawn,Q);
                if(Disabled==INDEX_NONE)Disabled=I;
            }
            if(Disabled!=INDEX_NONE)
            {
                FRogueliteDoorSocket S;Room->TryGetDoorSocket(static_cast<ERogueliteRoomSide>(Disabled),S);
                HF03CDoor(Room,Disabled).bEnabled=false;Room->RebuildRoom();FHitResult Hit;
                const FVector Mid=S.WorldPosition+FVector(0,0,100),N=S.WorldOutwardDirection;
                Closed=W->LineTraceSingleByChannel(Hit,Mid-N*(S.WallThickness+25),Mid+N*(S.WallThickness+25),ECC_Pawn,Q)&&Hit.GetActor()==Room;
            }
            FActorSpawnParameters CP;CP.ObjectFlags|=RF_Transient;CP.bDeferConstruction=true;CP.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
            CP.CustomPreSpawnInitialization=[P](AActor* A){auto* C=CastChecked<ARogueliteParametricCorridor>(A);C->CorridorLength=1500;C->CorridorWidth=P.Width;C->CorridorHeight=P.Height;C->WallThickness=P.WallThickness;C->FloorThickness=P.FloorThickness;C->DoorWidth=P.DoorWidth;C->DoorHeight=P.DoorHeight;C->bGenerateCeiling=P.bGenerateCeiling;};
            const FTransform CT(FRotator(0,Yaw*90,0),FVector(4000000,4000000,0));auto* C=W->SpawnActor<ARogueliteParametricCorridor>(ARogueliteParametricCorridor::StaticClass(),CT,CP);
            bool Top=false,Up=false,FloorSides=false;
            if(C)
            {
                C->FinishSpawning(CT);FHitResult H,U,F,L,R;const FVector Start=CT.TransformPosition(FVector(0,0,150)),End=CT.TransformPosition(FVector(0,0,700));
                const bool B=W->LineTraceSingleByChannel(H,Start,End,ECC_Pawn,Q);const bool BUp=W->SweepSingleByChannel(U,Start,End,FQuat::Identity,ECC_Pawn,FCollisionShape::MakeCapsule(34,88),Q);
                auto* Ceiling=FindShape(C,TEXT("Ceiling"));Top=Ceiling&&(Roof?(B&&H.GetComponent()==Ceiling):!B);Up=Roof?(BUp&&U.GetComponent()==Ceiling&&!U.bStartPenetrating):!BUp;
                FloorSides=W->LineTraceSingleByChannel(F,Start,CT.TransformPosition(FVector(0,0,-100)),ECC_Pawn,Q)&&F.GetActor()==C&&F.ImpactNormal.Z>.99
                    &&W->LineTraceSingleByChannel(L,Start,CT.TransformPosition(FVector(0,-400,150)),ECC_Pawn,Q)&&L.GetActor()==C
                    &&W->LineTraceSingleByChannel(R,Start,CT.TransformPosition(FVector(0,400,150)),ECC_Pawn,Q)&&R.GetActor()==C;
            }
            const float ActualHeight=Room->RoomHeight;const bool Height=ActualHeight==(Mode?500.f:OriginalHeight);
            if(C)W->DestroyActor(C);W->DestroyActor(Room);const bool Cleanup=LiveActors(W)==Before;
            Rows+=FString::Printf(TEXT("%d\t%d\t%d\t%d\t%.1f\t%.1f\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\n"),Template,Yaw*90,Mode,Roof,OriginalHeight,ActualHeight,POI,Valid.bValid,Top,Up,FloorSides,Aperture,Closed,Cleanup);
            const bool Passed=Adapted&&Height&&POI&&Valid.bValid&&Aperture&&Closed&&Top&&Up&&FloorSides&&Cleanup;Good&=Passed;
            TestTrue(FString::Printf(TEXT("Stage A source%d yaw%d mode%d roof%d: %s %s"),Template,Yaw*90,Mode,Roof,*Why,*FString::Join(Valid.Issues,TEXT(" | "))),Passed);
            if(!Passed)break;
        }
        TestEqual(TEXT("Source CDO not changed by instance adaptation"),CDO->RoomHeight,OriginalHeight);
    }
    const FString Dir=FPaths::ProjectPluginsDir()/TEXT("RoguelitePrototype/Development/P03E2HF03C/StageA-")+FDateTime::UtcNow().ToString(TEXT("%Y%m%d-%H%M%S"));IFileManager::Get().MakeDirectory(*Dir,true);
    FFileHelper::SaveStringToFile(Rows,*(Dir/TEXT("instances.tsv")),FFileHelper::EEncodingOptions::ForceUTF8);
    TestEqual(TEXT("Transient tests leave original Editor actors unchanged"),LiveActors(W),Before);
    AddInfo(TEXT("No map/profile/template asset saved. Evidence: ")+Dir);return Good;
}
#endif
