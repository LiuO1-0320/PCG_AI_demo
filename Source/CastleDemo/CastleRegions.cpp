#include "CastleDemo.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "EngineUtils.h"

namespace
{
    FName RegionTag(ECastlePCGRegion Region)
    {
        return Region==ECastlePCGRegion::Exploration?FName(TEXT("CastleExploration")):
            Region==ECastlePCGRegion::TreasureCave?FName(TEXT("CastleTreasureCave")):FName(TEXT("CastleBossArena"));
    }
    int32 RegionSeed(int32 Seed,int32 Floor,uint32 Purpose)
    {
        uint32 X=uint32(Seed)^(uint32(Floor)*0x9e3779b9u)^Purpose;
        X^=X>>16; X*=0x7feb352du; X^=X>>15; X*=0x846ca68bu; X^=X>>16;
        return int32(X);
    }
}

void ACastleDirector::TrackGenerated(AActor* Actor)
{
    ECastlePCGRegion Area=GeneratingRegion;
    if(bReady)
        Area=Actor->GetActorLocation().X>BossCenter.X-2500?ECastlePCGRegion::BossArena:
            Actor->GetActorLocation().Y>CaveCenter.Y-900?ECastlePCGRegion::TreasureCave:ECastlePCGRegion::Exploration;
    Actor->Tags.AddUnique(RegionTag(Area));
    GeneratedActors.Add(Actor);
}

void ACastleDirector::ClearGeneratedRegion(ECastlePCGRegion Area)
{
    const FName Tag=RegionTag(Area);
    for(int32 I=GeneratedActors.Num()-1;I>=0;--I)
    {
        auto* Actor=GeneratedActors[I].Get();
        if(!IsValid(Actor)) {GeneratedActors.RemoveAt(I);continue;}
        if(!Actor->ActorHasTag(Tag))continue;
        if(auto* Enemy=Cast<ACastleEnemy>(Actor)) if(auto* AI=Enemy->GetController()) {AI->UnPossess();AI->Destroy();}
        Actor->Destroy(); GeneratedActors.RemoveAt(I);
    }
    for(int32 I=int32(Area)*18;I<FMath::Min(Geometry.Num(),(int32(Area)+1)*18);++I)
        if(Geometry[I]) {Geometry[I]->DestroyComponent();Geometry[I]=nullptr;}
}

void ACastleDirector::GenerateCave(const UCastleLayoutSettings*)
{
    ClearGeneratedRegion(ECastlePCGRegion::TreasureCave);
    GeneratingRegion=ECastlePCGRegion::TreasureCave; InitializeMaterials();
    FRandomStream R(RegionSeed(RunSeed,Floor,0xcabed123u));
    const int32 DX[]={1,0,-1,0},DY[]={0,1,0,-1};
    auto Neighbor=[&](int32 I,int32 Side)
    {
        const int32 X=I%2+DX[Side],Y=I/2+DY[Side];
        return X>=0 && X<2 && Y>=0 && Y<3?Y*2+X:-1;
    };
    // Enumerate simple four-room walks. Every reward is on the entrance-to-exit
    // path, so completing the vault never requires walking back down a branch.
    TArray<TArray<int32>> Paths;
    TArray<int32> Path;
    TFunction<void(int32)> Visit=[&](int32 I)
    {
        Path.Add(I);
        if(Path.Num()==4) Paths.Add(Path);
        else for(int32 Side=0;Side<4;++Side)
        {
            const int32 N=Neighbor(I,Side);
            if(N>=0 && !Path.Contains(N))Visit(N);
        }
        Path.Pop();
    };
    for(int32 I=0;I<6;++I)Visit(I);
    check(!Paths.IsEmpty()); CaveRoute=Paths[R.RandRange(0,Paths.Num()-1)];
    CaveRooms.SetNum(6);
    for(int32 I=0;I<6;++I)
    {
        CaveRooms[I]=FCastleRoom(); CaveRooms[I].bActive=false;
        CaveRooms[I].Center=CaveCenter+FVector((I%2)*2100,(I/2)*2100,0);
    }
    for(int32 Order=0;Order<4;++Order)
    {
        const int32 I=CaveRoute[Order]; auto& Room=CaveRooms[I];
        Room.bActive=Room.bRevealed=true;Room.RouteOrder=Order;
        Room.Type=Order==0?ECastleRoom::Entrance:Order==3?ECastleRoom::Exit:ECastleRoom::Chest;
        if(Order>0)
            for(int32 Side=0;Side<4;++Side) if(Neighbor(CaveRoute[Order-1],Side)==I)
            {
                CaveRooms[CaveRoute[Order-1]].Doors|=1<<Side;
                Room.Doors|=1<<((Side+2)%4);break;
            }
    }
    CaveEntrance=CaveRooms[CaveRoute[0]].Center;
    CaveExit=CaveRooms[CaveRoute[3]].Center;
    for(int32 Order=0;Order<4;++Order)
    {
        const auto& Room=CaveRooms[CaveRoute[Order]]; const FVector C=Room.Center;
        BuildRoom(C,800,Room.Doors,6); AddLight(C+FVector(0,0,390),FLinearColor(.2f,.55f,1));
        AddSign(C+FVector(680,0,330),Order==0?TEXT("VAULT ENTRANCE"):Order==3?TEXT("VAULT EXIT"):TEXT("GUARDED RELIQUARY"),FColor(120,210,255));
        if(Order>0)
            AddInteract(C+FVector(420,400,45),ECastleInteract::Chest,TEXT("VAULT RELIC"),R.RandRange(0,2));
        if(Order==1 || Order==2)
        {
            for(int32 E=0;E<Floor;++E) AddEnemy(C+FVector(-330+E*220,300,100),false);
            AddBox(C+FVector(-440,-420,120),FVector(160,260,240),6);
        }
        for(int32 Side=0;Side<2;++Side) if(Room.Doors&(1<<Side))
        {
            const FVector Mid=C+(Side==0?FVector(1050,0,0):FVector(0,1050,0));
            AddBox(Mid+FVector(0,0,-35),Side==0?FVector(600,560,70):FVector(560,600,70),0);
            for(int32 Sign:{-1,1})
                AddBox(Mid+(Side==0?FVector(0,Sign*280,240):FVector(Sign*280,0,240)),Side==0?FVector(600,65,480):FVector(65,600,480),6);
            AddBox(Mid+FVector(0,0,2),Side==0?FVector(540,26,3):FVector(26,540,3),4,false);
        }
    }
    // Only the last room contains the return portal; the entry room stays clear.
    AddInteract(CaveExit+FVector(-440,-400,135),ECastleInteract::CaveReturn,TEXT("VAULT EXIT / RETURN"));
    UE_LOG(LogTemp,Display,TEXT("CASTLE_CAVE_ROUTE Floor=%d Seed=%d Route=%d,%d,%d,%d"),Floor,RunSeed,CaveRoute[0],CaveRoute[1],CaveRoute[2],CaveRoute[3]);
    bCaveGenerated=true;
    if(bExplorationGenerated && bBossGenerated && !bReady)CompleteFloorGeneration();
}

void ACastleDirector::GenerateBossArena(const UCastleLayoutSettings* Settings)
{
    ClearGeneratedRegion(ECastlePCGRegion::BossArena);
    GeneratingRegion=ECastlePCGRegion::BossArena; InitializeMaterials();
    BossPlatforms.Reset(); BossCoverPoints.Reset();
    FRandomStream R(RegionSeed(RunSeed,Floor,0xb055a12fu));
    BuildRoom(BossCenter,2250,0,2);
    AddSign(BossCenter+FVector(2130,0,430),BossName(),FColor(220,170,255));
    TArray<FIntPoint> Corners{FIntPoint(-1,-1),FIntPoint(1,-1),FIntPoint(-1,1),FIntPoint(1,1)};
    for(int32 I=Corners.Num()-1;I>0;--I)Corners.Swap(I,R.RandRange(0,I));
    const int32 PlatformCount=R.RandRange(2,4);
    for(int32 I=0;I<PlatformCount;++I)
    {
        const auto Corner=Corners[I];
        const float Height=160+80*R.RandRange(0,2);
        const FVector Top=BossCenter+FVector(Corner.X*1450,Corner.Y*1250,Height);
        AddBox(Top-FVector(0,0,Height*.5f),FVector(700,850,Height),2);
        BossPlatforms.Add(Top);
        const FVector Start=BossCenter+FVector(Corner.X*150,Corner.Y*1250,0);
        const FVector End=BossCenter+FVector(Corner.X*1100,Corner.Y*1250,Height);
        const FRotator Slope=(End-Start).Rotation();
        const FVector Normal=Slope.RotateVector(FVector::UpVector);
        // Extend into both adjoining surfaces to avoid tiny collision/nav seams.
        AddBox((Start+End)*.5f-Normal*20,FVector((End-Start).Size()+80,600,40),1,true,Slope);
        AddBox(Top+FVector(Corner.X*210,0,90),FVector(160,340,180),8);
        AddLight(Top+FVector(0,0,300),FLinearColor(.6f,.25f,1));
    }
    // Keep the horizontal spawn-to-boss lane and all ramp approaches open.
    // Cover alternates between short barricades, tall columns and L-shaped walls.
    const int32 CoverCount=FMath::Clamp(Settings?Settings->ArenaCoverCount:10,4,18);
    for(int32 I=0;I<CoverCount;++I)
    {
        const int32 Row=I/6,Column=I%6;
        const float YSign=(I%2)?1.f:-1.f;
        const FVector Pos=BossCenter+FVector(-1500+Column*600+R.FRandRange(-55,55),YSign*(570+Row*130),0);
        const int32 Kind=R.RandRange(0,2);
        const float Height=Kind==0?100:Kind==1?R.FRandRange(300,440):220;
        const float Width=Kind==1?130:R.FRandRange(180,260);
        AddBox(Pos+FVector(0,0,Height*.5f),FVector(Width,Kind==1?130:180,Height),8);
        if(Kind==2)AddBox(Pos+FVector(Width*.5f+55,90,Height*.5f),FVector(110,260,Height),8);
        BossCoverPoints.Add(Pos+FVector(0,0,Height));
    }
    Boss=AddEnemy(BossCenter+FVector(400,0,180),true);
    AddInteract(BossCenter+FVector(-1700,-300,45),ECastleInteract::Ammo,TEXT("ARENA SUPPLIES"));
    UE_LOG(LogTemp,Display,TEXT("CASTLE_BOSS_SPACE Floor=%d Seed=%d Platforms=%d Cover=%d"),Floor,RunSeed,BossPlatforms.Num(),BossCoverPoints.Num());
    bBossGenerated=true;
    if(bExplorationGenerated && bCaveGenerated && !bReady)CompleteFloorGeneration();
}
