#include "CastleDemo.h"

void ACastleDirector::BuildMemoryRoom(const FCastleRoom& Room, float Half, bool bIntact)
{
    const FVector C=Room.Center;
    // Independent decorative stream: never consumes layout or reward randomness.
    FRandomStream Decor(int32(uint32(RunSeed)^uint32(Floor)*0x9e3779b9u^
        GetTypeHash(FIntPoint(FMath::RoundToInt(C.X),FMath::RoundToInt(C.Y)))^0x6d656d6fu));
    const FVector Normals[]={FVector(1,0,0),FVector(0,1,0),FVector(-1,0,0),FVector(0,-1,0)};
    for(int32 Side=0;Side<4;++Side)
    {
        const FVector N=Normals[Side],T(-N.Y,N.X,0);
        const bool AlongX=Side%2==0;
        auto Put=[&](float Depth,float Across,float Height,FVector Size,int32 Material,float Lean=0.f)
        {
            const FVector WorldSize=AlongX?FVector(Size.X,Size.Y,Size.Z):FVector(Size.Y,Size.X,Size.Z);
            const FRotator Rotation=AlongX?FRotator(0,0,Lean):FRotator(Lean,0,0);
            AddBox(C+N*Depth+T*Across+FVector(0,0,Height),WorldSize,Material,false,Rotation);
        };
        // Thin wall pilasters and pointed arch trim stay outside the 480 cm doorway.
        for(int32 Sign:{-1,1})
        {
            Put(Half-32,Sign*(Half-140),200,FVector(70,72,400),1);
            Put(Half-63,Sign*115,430,FVector(30,26,230),3,Sign*48.f);
        }
        if(Room.Doors&(1<<Side))continue;
        for(int32 Sign:{-1,1})
        {
            const float Across=Sign*(Half*.53f),Z=245,Width=220,Height=270;
            // Charcoal panel, gilded frame and an anonymous painted court figure.
            Put(Half-55,Across,Z,FVector(12,Width,Height),8);
            Put(Half-72,Across-Width*.5f,Z,FVector(18,16,Height+26),3);
            Put(Half-72,Across+Width*.5f,Z,FVector(18,16,Height+26),3);
            Put(Half-72,Across,Z-Height*.5f,FVector(18,Width+26,16),3);
            if(bIntact || Decor.RandRange(0,1)==0)
                Put(Half-72,Across,Z+Height*.5f,FVector(18,Width+26,16),3);
            else
                Put(Half-78,Across+42,Z+Height*.5f-22,FVector(18,Width*.65f,16),3,18);
            Put(Half-67,Across,Z-55,FVector(8,100,110),bIntact?5:1);
            Put(Half-68,Across,Z+35,FVector(8,52,66),bIntact?3:8);
            if(!bIntact)
                Put(Half-81,Across+12,Z,FVector(12,14,200),8,Decor.FRandRange(-22,22));
            Put(Half-100,Across,Z-Height*.5f-35,FVector(70,105,12),1);
        }
    }
    // Inlaid runner and a low visual plinth leave collision and navigation unchanged.
    AddBox(C+FVector(0,0,3),FVector(340,Half*1.5f,4),bIntact?3:5,false);
    if(!bIntact || Room.RouteOrder>0)
        AddBox(C+(bIntact?FVector(420,400,10):FVector(230,0,10)),FVector(200,180,18),1,false);
    // A freestanding keepsake canvas also identifies rooms with four open doors.
    const FVector Easel=C+FVector(-Half+220,-Half+220,0);
    AddBox(Easel+FVector(0,0,200),FVector(16,130,170),8,false);
    for(int32 Sign:{-1,1})
    {
        AddBox(Easel+FVector(0,Sign*72,200),FVector(22,14,190),3,false);
        AddBox(Easel+FVector(0,Sign*60,90),FVector(20,16,180),1,false,FRotator(0,0,Sign*12));
    }
    AddBox(Easel+FVector(0,0,290),FVector(22,158,14),3,false);
    AddBox(Easel+FVector(-12,0,205),FVector(12,42,58),bIntact?3:5,false);
    for(int32 Sign:{-1,1})
        AddBox(C+FVector(Sign*195,0,4),FVector(8,Half*1.5f,5),3,false);
    AddLight(C+FVector(0,0,340),bIntact?FLinearColor(.72f,.57f,.36f):FLinearColor(.48f,.35f,.8f));
    AddSign(C+FVector(680,-400,310),bIntact?TEXT("THE COURT REMEMBERED"):TEXT("THE PAINTER'S LOST MEMORIES"),FColor(185,164,220));
    UE_LOG(LogTemp,Display,TEXT("CASTLE_MEMORY_ROOM Floor=%d Intact=%d Center=%s Doors=%d"),Floor,bIntact,*C.ToString(),Room.Doors);
}
