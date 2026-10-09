#include "CastleDemo.h"
#if WITH_EDITOR
#include "Editor.h"
#include "FileHelpers.h"
#include "Builders/CubeBuilder.h"
#include "Model.h"
#include "Engine/Polys.h"
#include "Components/BrushComponent.h"
#include "BSPOps.h"
#include "NavMesh/NavMeshBoundsVolume.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "GameFramework/PlayerStart.h"
#include "Engine/DirectionalLight.h"
#include "Engine/SkyLight.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/PostProcessVolume.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "UObject/SavePackage.h"
#include "Misc/PackageName.h"
#include "Misc/Parse.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "PCGGraph.h"
#include "PCGComponent.h"
#endif

UCastleSetupCommandlet::UCastleSetupCommandlet()
{
    IsClient=false; IsServer=false; IsEditor=true; LogToConsole=true;
}

int32 UCastleSetupCommandlet::Main(const FString& Params)
{
#if WITH_EDITOR
    auto SaveAsset=[](UObject* Asset)
    {
        UPackage* Package=Asset->GetOutermost(); Package->FullyLoad();
        FString File=FPackageName::LongPackageNameToFilename(Package->GetName(),FPackageName::GetAssetPackageExtension());
        FSavePackageArgs Args; Args.TopLevelFlags=RF_Public|RF_Standalone;
        return UPackage::SavePackage(Package,Asset,*File,Args);
    };
    UPCGGraph* RegionGraphs[3]={};
    const TCHAR* GraphNames[]={TEXT("PCG_Exploration"),TEXT("PCG_BossArena"),TEXT("PCG_TreasureCave")};
    for(int32 I=0;I<3;++I)
    {
        const FString Path=FString(TEXT("/Game/Demo/"))+GraphNames[I];
        auto* Graph=LoadObject<UPCGGraph>(nullptr,*(Path+TEXT(".")+GraphNames[I]));
        if(!Graph)
        {
            Graph=ACastleDirector::MakeGraph(CreatePackage(*Path),static_cast<ECastlePCGRegion>(I));
            Graph->Rename(GraphNames[I]); Graph->SetFlags(RF_Public|RF_Standalone);
            FAssetRegistryModule::AssetCreated(Graph);
        }
        if(!SaveAsset(Graph))return 2;
        RegionGraphs[I]=Graph;
    }
    if(FParse::Param(*Params,TEXT("GraphsOnly")))
    {
        UE_LOG(LogTemp,Display,TEXT("CASTLE_REGION_GRAPHS_OK: exploration, cave and boss PCG assets saved; existing map/materials preserved."));
        return 0;
    }
    struct FPalette { const TCHAR* Name; FLinearColor Color; float Emission; };
    const FPalette Palette[]={
        {TEXT("M_Floor"),FLinearColor(.085f,.11f,.14f),.08f},
        {TEXT("M_Trim"),FLinearColor(.19f,.23f,.27f),.12f},
        {TEXT("M_Stone"),FLinearColor(.25f,.29f,.34f),.18f},
        {TEXT("M_Gold"),FLinearColor(.95f,.57f,.12f),.4f},
        {TEXT("M_Teal"),FLinearColor(.06f,.60f,.56f),.25f},
        {TEXT("M_Purple"),FLinearColor(.44f,.12f,.65f),.2f},
        {TEXT("M_Cave"),FLinearColor(.12f,.20f,.27f),.25f},
        {TEXT("M_Portal"),FLinearColor(.08f,.47f,.92f),2.f},
        {TEXT("M_Metal"),FLinearColor(.12f,.14f,.17f),.12f},
        {TEXT("M_Enemy"),FLinearColor(.55f,.13f,.085f),0},
        {TEXT("M_Danger"),FLinearColor(1,.06f,.02f),1.8f},
        {TEXT("M_Sky"),FLinearColor(.018f,.03f,.065f),1.f}
    };
    for(const auto& P:Palette)
    {
        FString Path=FString(TEXT("/Game/Demo/Materials/"))+P.Name;
        auto* Package=CreatePackage(*Path); Package->FullyLoad();
        auto* Mat=FindObject<UMaterial>(Package,P.Name);
        if(!Mat)Mat=NewObject<UMaterial>(Package,FName(P.Name),RF_Public|RF_Standalone);
        Mat->GetExpressionCollection().Empty();
        Mat->TwoSided=true;
        auto* Color=NewObject<UMaterialExpressionConstant3Vector>(Mat); Color->Constant=P.Color;
        Mat->GetExpressionCollection().AddExpression(Color); Mat->GetEditorOnlyData()->BaseColor.Expression=Color;
        auto* Rough=NewObject<UMaterialExpressionConstant>(Mat); Rough->R= .8f;
        Mat->GetExpressionCollection().AddExpression(Rough); Mat->GetEditorOnlyData()->Roughness.Expression=Rough;
        if(P.Emission>0)
        {
            auto* Glow=NewObject<UMaterialExpressionConstant3Vector>(Mat); Glow->Constant=P.Color*P.Emission;
            Mat->GetExpressionCollection().AddExpression(Glow); Mat->GetEditorOnlyData()->EmissiveColor.Expression=Glow;
        }
        Mat->PostEditChange(); FAssetRegistryModule::AssetCreated(Mat);
        if(!SaveAsset(Mat)) return 1;
    }
    auto* Graph=RegionGraphs[0];

    UWorld* World=GEditor->NewMap();
    World->GetWorldSettings()->DefaultGameMode=ACastleGameMode::StaticClass();
    auto* Start=World->SpawnActor<APlayerStart>(FVector(-400,0,110),FRotator::ZeroRotator);
    Start->SetActorLabel(TEXT("First person start"));
    auto* Sun=World->SpawnActor<ADirectionalLight>(FVector(0,0,2000),FRotator(-60,-25,0));
    Sun->SetMobility(EComponentMobility::Movable); Sun->GetLightComponent()->SetIntensity(3.5f);
    Sun->GetLightComponent()->SetLightColor(FLinearColor(.70f,.80f,1));
    auto* Sky=World->SpawnActor<ASkyLight>(); Sky->GetLightComponent()->SetMobility(EComponentMobility::Movable);
    Sky->GetLightComponent()->SetIntensity(.7f); Sky->GetLightComponent()->SetLightColor(FLinearColor(.5f,.65f,1));
    auto* Dome=World->SpawnActor<AStaticMeshActor>(FVector(4000,6000,0),FRotator::ZeroRotator);
    Dome->GetStaticMeshComponent()->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Sphere.Sphere")));
    Dome->GetStaticMeshComponent()->SetMaterial(0,LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Demo/Materials/M_Sky.M_Sky")));
    Dome->SetActorScale3D(FVector(1000)); Dome->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("NoCollision"));
    Dome->SetActorEnableCollision(false);
    Dome->GetStaticMeshComponent()->SetCastShadow(false); Dome->GetStaticMeshComponent()->SetCanEverAffectNavigation(false);
    auto* PP=World->SpawnActor<APostProcessVolume>(); PP->bUnbound=true;
    PP->Settings.bOverride_AutoExposureMethod=true; PP->Settings.AutoExposureMethod=EAutoExposureMethod::AEM_Manual;
    PP->Settings.bOverride_AutoExposureApplyPhysicalCameraExposure=true; PP->Settings.AutoExposureApplyPhysicalCameraExposure=false;
    PP->Settings.bOverride_AutoExposureBias=true; PP->Settings.AutoExposureBias=0;
    PP->Settings.bOverride_BloomIntensity=true; PP->Settings.BloomIntensity=.25f;
    PP->Settings.bOverride_MotionBlurAmount=true; PP->Settings.MotionBlurAmount=0;
    auto* Bounds=World->SpawnActor<ANavMeshBoundsVolume>(FVector(5000,6000,400),FRotator::ZeroRotator);
    Bounds->Brush=NewObject<UModel>(Bounds,NAME_None,RF_Transactional);
    Bounds->Brush->Initialize(nullptr,true);
    Bounds->Brush->Polys=NewObject<UPolys>(Bounds->Brush,NAME_None,RF_Transactional);
    Bounds->GetBrushComponent()->Brush=Bounds->Brush;
    auto* Builder=NewObject<UCubeBuilder>(Bounds); Builder->X=18000; Builder->Y=19000; Builder->Z=2400;
    Bounds->BrushBuilder=Builder;
    if(!Builder->Build(World,Bounds))return 3;
    FBSPOps::csgPrepMovingBrush(Bounds); Bounds->PostEditChange();
    Bounds->SetActorLabel(TEXT("Dynamic navigation - all generated regions"));
    auto* Director=World->SpawnActor<ACastleDirector>(); Director->SetActorLabel(TEXT("Castle PCG Director"));
    Director->CavePCG->SetGraph(RegionGraphs[2]); Director->BossPCG->SetGraph(RegionGraphs[1]);
    Director->PCG->SetGraph(Graph); Director->RunSeed=1337;
    // Include an editor preview. BeginPlay clears it and regenerates through PCG.
    Director->GenerateLayout(GetDefault<UCastleLayoutSettings>()); Director->RunSeed=0;
    const FString MapFilename=FPackageName::LongPackageNameToFilename(TEXT("/Game/Demo/L_CastleDemo"),FPackageName::GetMapPackageExtension());
    if(!FEditorFileUtils::SaveMap(World,MapFilename))return 4;
    UE_LOG(LogTemp,Display,TEXT("CASTLE_SETUP_OK: map, PCG graph and 12 materials saved."));
    return 0;
#else
    return 1;
#endif
}
