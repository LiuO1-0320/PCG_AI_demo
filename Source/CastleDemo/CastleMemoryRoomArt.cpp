#include "CastleDemo.h"
#include "Components/StaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"

bool ACastleDirector::BuildMedievalMemoryRoom(const FCastleRoom& Room,float Half)
{
    auto* Wall=LoadObject<UStaticMesh>(nullptr,TEXT("/Game/Demo/CourtArt/Masonry/SM_Wall_00.SM_Wall_00"));
    auto* FloorMesh=LoadObject<UStaticMesh>(nullptr,TEXT("/Game/Demo/CourtArt/Masonry/SM_Ground.SM_Ground"));
    auto* Candles=LoadObject<UStaticMesh>(nullptr,TEXT("/Game/Dungeon/Candles/Meshes/SM_Candles_Holder_00.SM_Candles_Holder_00"));
    auto* Pillar=LoadObject<UStaticMesh>(nullptr,TEXT("/Game/Demo/CourtArt/Masonry/SM_Pillar.SM_Pillar"));
    auto* Candle=LoadObject<UStaticMesh>(nullptr,TEXT("/Game/Dungeon/Candles/Meshes/SM_Candle_Melted_01.SM_Candle_Melted_01"));
    auto* Portrait=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Demo/MemoryRoomArt/M_FacelessCourtier.M_FacelessCourtier"));
    if(!Wall || !FloorMesh || !Candles || !Portrait || !Pillar)
    {
        UE_LOG(LogTemp,Warning,TEXT("CASTLE_MEMORY_ART_UNAVAILABLE: wall=%d floor=%d candles=%d portrait=%d. Original room retained."),Wall!=nullptr,FloorMesh!=nullptr,Candles!=nullptr,Portrait!=nullptr);
        return false;
    }
    auto* Art=GetWorld()->SpawnActor<AActor>();
    auto* Root=NewObject<USceneComponent>(Art);Art->SetRootComponent(Root);Root->RegisterComponent();
    Art->Tags.Add(TEXT("CastleMemoryRoomArt"));TrackGenerated(Art);
    auto* Cube=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube"));
    auto* Sphere=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    auto Material=[](const TCHAR* Name)
    {
        return LoadObject<UMaterialInterface>(nullptr,*FString::Printf(TEXT("/Game/Demo/MemoryRoomArt/%s.%s"),Name,Name));
    };
    auto* Charcoal=Material(TEXT("M_Charcoal"));auto* Velvet=Material(TEXT("M_Velvet"));
    auto* Gold=Material(TEXT("M_AntiqueGold"));auto* Flame=Material(TEXT("M_CandleFlame"));
    auto* Stone=Material(TEXT("M_MemoryStone"));auto* Tiles=Material(TEXT("M_MemoryTiles"));
    auto Fit=[&](UStaticMesh* Mesh,FVector Center,FVector Size,FRotator Rotation,UMaterialInterface* Override=nullptr)
    {
        auto* Component=NewObject<UStaticMeshComponent>(Art);Art->AddInstanceComponent(Component);
        Component->SetupAttachment(Root);Component->SetStaticMesh(Mesh);
        Component->ComponentTags.Add(TEXT("CastleMemoryVisual"));
        Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);Component->SetCanEverAffectNavigation(false);
        const FBox Bounds=Mesh->GetBoundingBox();const FVector Source=Bounds.GetSize();
        const FVector Scale=Size/FVector(FMath::Max(Source.X,.01),FMath::Max(Source.Y,.01),FMath::Max(Source.Z,.01));
        if(Override)for(int32 Slot=0;Slot<Mesh->GetStaticMaterials().Num();++Slot)Component->SetMaterial(Slot,Override);
        Component->RegisterComponent();Component->SetWorldTransform(FTransform(Rotation,Center-Rotation.RotateVector(Bounds.GetCenter()*Scale),Scale));
        return Component;
    };
    auto Lamp=[&](FVector Position,FLinearColor Color,float Intensity,float Radius)
    {
        auto* L=NewObject<UPointLightComponent>(Art);Art->AddInstanceComponent(L);L->SetupAttachment(Root);
        L->SetMobility(EComponentMobility::Movable);L->SetLightColor(Color);L->SetIntensity(Intensity);
        L->SetAttenuationRadius(Radius);L->SetCastShadows(false);L->RegisterComponent();L->SetWorldLocation(Position);
    };
    const FVector C=Room.Center;
    // The pack's ground module has irregular edges. A visual-only backing at
    // the unchanged walking plane prevents views through the mesh's gaps.
    Fit(Cube,C+FVector(0,0,-35.5),FVector(1900,1900,70),FRotator::ZeroRotator,Tiles);
    // Each floor slab aligns its top to Z=0; the original 1900 x 1900 collision remains below it.
    for(int32 X=0;X<4;++X)for(int32 Y=0;Y<4;++Y)
        Fit(FloorMesh,C+FVector(-675+X*450,-675+Y*450,-14),FVector(450,450,28),FRotator::ZeroRotator,Tiles);
    // Match the existing boxes, not asset pivots. Door opening 480 wide, lintel underside 410.
    const FVector Normals[]={FVector(1,0,0),FVector(0,1,0),FVector(-1,0,0),FVector(0,-1,0)};
    const bool ThinX=Wall->GetBoundingBox().GetSize().X<Wall->GetBoundingBox().GetSize().Y;
    auto WallSegment=[&](FVector Center,float Width,float Height,float Yaw)
    {
        Fit(Wall,Center,ThinX?FVector(85,Width,Height):FVector(Width,85,Height),FRotator(0,Yaw+(ThinX?0:90),0),Stone);
    };
    for(int32 Side=0;Side<4;++Side)
    {
        const FVector N=Normals[Side],T(-N.Y,N.X,0);const float Yaw=Side*90.f;
        const FRotator Rot(0,Yaw,0);
        if(Room.Doors&(1<<Side))
        {
            for(int32 Sign:{-1,1})
            {
                WallSegment(C+N*Half+T*(Sign*570)+FVector(0,0,240),660,480,Yaw);
                Fit(Cube,C+N*(Half-47)+T*(Sign*260)+FVector(0,0,205),FVector(20,32,410),Rot,Gold);
            }
            WallSegment(C+N*Half+FVector(0,0,445),480,70,Yaw);
        }
        else
        {
            for(int32 Tile=0;Tile<4;++Tile)WallSegment(C+N*Half+T*(-675+Tile*450)+FVector(0,0,240),450,480,Yaw);
            // Actual portrait canvas replaces the old block-shaped anonymous figure.
            for(int32 Sign:{-1,1})
            {
                const FVector Base=C+N*(Half-65)+T*(Sign*440)+FVector(0,0,250);
                Fit(Cube,Base,FVector(10,206,300),Rot,Charcoal);
                // The cube face normal faces inward. XY UV scale is independent of the collision.
                Fit(Cube,Base-N*7,FVector(3,184,278),FRotator(0,Yaw+180,0),Portrait);
                for(int32 Edge:{-1,1})
                {
                    Fit(Cube,Base-N*14+T*(Edge*105),FVector(22,18,322),Rot,Gold);
                    Fit(Cube,Base-N*14+FVector(0,0,Edge*153),FVector(22,226,18),Rot,Gold);
                    // Rounded corner ornaments, all decorative and query-free.
                    for(int32 Corner:{-1,1})Fit(Sphere,Base-N*21+T*(Edge*105)+FVector(0,0,Corner*153),FVector(32,32,32),Rot,Gold);
                }
                Lamp(Base-N*180+FVector(0,0,78),FLinearColor(1.f,.70f,.39f),9200,640);
            }
        }
    }
    for(int32 X:{-1,1})for(int32 Y:{-1,1})
        Fit(Pillar,C+FVector(X*(Half-65),Y*(Half-65),260),FVector(135,135,520),FRotator::ZeroRotator,Stone);
    // A restrained burgundy runner stops short of every doorway.
    Fit(Cube,C+FVector(0,0,1.2),FVector(390,1250,2),FRotator::ZeroRotator,Velvet);
    for(int32 Sign:{-1,1})Fit(Cube,C+FVector(Sign*198,0,2),FVector(8,1250,2),FRotator::ZeroRotator,Gold);
    for(int32 X:{-1,1})for(int32 Y:{-1,1})
    {
        const FVector Position=C+FVector(X*670,Y*620,0);
        Fit(Cube,Position+FVector(0,0,45),FVector(55,55,90),FRotator::ZeroRotator,Gold);
        Fit(Candles,Position+FVector(0,0,94),FVector(65,65,8),FRotator(0,45*X,0));
        if(Candle)Fit(Candle,Position+FVector(0,0,123),FVector(28,28,50),FRotator::ZeroRotator);
        Fit(Sphere,Position+FVector(0,0,153),FVector(7,7,15),FRotator::ZeroRotator,Flame);
        Lamp(Position+FVector(0,0,165),FLinearColor(1.f,.66f,.30f),13800,880);
    }
    Lamp(C+FVector(230,0,200),FLinearColor(.62f,.53f,.82f),10000,550);
    Lamp(C+FVector(0,0,430),FLinearColor(.78f,.72f,.63f),18500,1350);
    UE_LOG(LogTemp,Display,TEXT("CASTLE_MEMORY_ART_READY Seed=%d Floor=1 Doors=%d Wall=%s FloorMesh=%s Candles=%s"),RunSeed,Room.Doors,*Wall->GetPathName(),*FloorMesh->GetPathName(),*Candles->GetPathName());
    return true;
}
