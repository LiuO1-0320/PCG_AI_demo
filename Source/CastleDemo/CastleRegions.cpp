#include "CastleDemo.h"
#include "CastleCourtProps.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/PointLight.h"
#include "EngineUtils.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/GameViewportClient.h"
#include "HAL/PlatformMisc.h"

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

    // ===== PCG2-藏宝洞：石材分级 =====
    // 洞体的可见墙面来自 BuildRoom 的材质槽 6（M_MemoryStone），房间美术里的
    // 石材组件大多被洞体挡住。两边都换成带参数的 M_CaveStone / M_CaveTile 动态
    // 实例，用同一个“记忆深度”驱动色温、粗糙度与斑驳度：
    //   深度 0（入口）= 冷、潮、粗、脏   深度 1（出口）= 暖、润、净、亮
    // 新材质缺失时整段跳过，房间保持原来的 M_MemoryStone，不会退化成灰盒。
    UMaterialInterface* CaveBase(bool bTile)
    {
        return LoadObject<UMaterialInterface>(nullptr,bTile
            ?TEXT("/Game/Demo/CaveArt/M_CaveTile.M_CaveTile")
            :TEXT("/Game/Demo/CaveArt/M_CaveStone.M_CaveStone"));
    }

    // 墙面专用：同一张贴图，重复次数降到 1.1x1.8，砖纹放大成石块。
    UMaterialInterface* CaveSlabBase()
    {
        return LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Demo/CaveArt/M_CaveSlab.M_CaveSlab"));
    }

    void ApplyCaveGrade(UMaterialInstanceDynamic* Mid,float MemDepth,bool bTile)
    {
        if(!Mid) return;
        const float D=FMath::Clamp(MemDepth,0.f,1.f);
        // 整条曲线都往暖金推：入口只是"暗一点、旧一点"，不是"变冷"。
        // 冷暖对比交给灯光和斑驳度，墙面本身保持金色基调。
        const FLinearColor Cold=bTile?FLinearColor(1.05f,1.02f,1.02f):FLinearColor(1.18f,1.14f,1.10f);
        const FLinearColor Warm=bTile?FLinearColor(1.52f,1.36f,1.06f):FLinearColor(1.62f,1.46f,1.16f);
        const float RoughCold=bTile?1.10f:1.20f,RoughWarm=bTile?.72f:.84f;
        const float GrimeCold=bTile?.48f:.34f,GrimeWarm=bTile?.15f:.12f;
        Mid->SetVectorParameterValue(TEXT("Tint"),FLinearColor(
            FMath::Lerp(Cold.R,Warm.R,D),FMath::Lerp(Cold.G,Warm.G,D),FMath::Lerp(Cold.B,Warm.B,D),1.f));
        Mid->SetScalarParameterValue(TEXT("RoughnessScale"),FMath::Lerp(RoughCold,RoughWarm,D));
        Mid->SetScalarParameterValue(TEXT("MacroGrime"),FMath::Lerp(GrimeCold,GrimeWarm,D));
    }

    // 房间美术本身：石材槽和地砖槽各一份 MID，外加那盏大范围补光的色温。
    void GradeCaveRoomArt(AActor* Art,float MemDepth)
    {
        if(!IsValid(Art)) return;
        auto* OldStone=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Demo/MemoryRoomArt/M_MemoryStone.M_MemoryStone"));
        auto* OldTile=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Demo/MemoryRoomArt/M_MemoryTiles.M_MemoryTiles"));
        auto* NewStone=CaveSlabBase();
        if(!NewStone) NewStone=CaveBase(false);
        auto* NewTile=CaveBase(true);
        if(!OldStone||!OldTile||!NewStone||!NewTile) return;

        auto* StoneMid=UMaterialInstanceDynamic::Create(NewStone,Art);
        auto* TileMid=UMaterialInstanceDynamic::Create(NewTile,Art);
        ApplyCaveGrade(StoneMid,MemDepth,false);
        ApplyCaveGrade(TileMid,MemDepth,true);

        int32 Walls=0,Floors=0;
        TArray<UStaticMeshComponent*> Meshes;
        Art->GetComponents<UStaticMeshComponent>(Meshes);
        for(auto* Comp:Meshes)
        {
            if(!Comp) continue;
            bool bStone=false,bTile=false;
            for(int32 Slot=0;Slot<Comp->GetNumMaterials();++Slot)
            {
                auto* Current=Comp->GetMaterial(Slot);
                if(Current==OldStone) bStone=true;
                else if(Current==OldTile) bTile=true;
            }
            if(!bStone&&!bTile) continue;
            auto* Use=bStone?StoneMid:TileMid;
            for(int32 Slot=0;Slot<Comp->GetNumMaterials();++Slot) Comp->SetMaterial(Slot,Use);
            bStone?++Walls:++Floors;
        }
        // 组员那 7 盏灯全是无阴影的大半径点光，叠起来把每面墙照得一样亮 —— 这就是
        // 房间"平"的根因。这里把大范围补光压到 22%、半径砍到 760，让位给吊灯；
        // 四角烛台收窄成光池，墙脚自然暗下去，明暗交界才立得住。
        int32 LitLamps=0;
        TArray<UPointLightComponent*> Lamps;
        Art->GetComponents<UPointLightComponent>(Lamps);
        for(auto* Lamp:Lamps)
        {
            if(!Lamp) continue;
            const float Radius=Lamp->AttenuationRadius;
            if(Radius>=1200.f)
            {
                Lamp->SetIntensity(Lamp->Intensity*.55f);
                Lamp->SetAttenuationRadius(900.f);
                Lamp->SetLightColor(FLinearColor(.74f,.74f,.76f));
                ++LitLamps;
            }
            else if(Radius>800.f)
            {
                // 四角烛台原本贴着地面、把地板照得跟天花一样亮，这正是"平"的来源。
                Lamp->SetIntensity(Lamp->Intensity*.45f);
                Lamp->SetAttenuationRadius(Radius*.75f);
                ++LitLamps;
            }
        }
        UE_LOG(LogTemp,Display,TEXT("CAVE_MATERIAL_GRADE Depth=%.2f Walls=%d Floors=%d Lamps=%d"),
            FMath::Clamp(MemDepth,0.f,1.f),Walls,Floors,LitLamps);
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

void ACastleDirector::AddDecorMesh(const TCHAR* AssetPath, FVector Pos, FVector Scale, FRotator Rotation, int32 Material)
{
    UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, AssetPath);
    if (!Mesh) return;
    FActorSpawnParameters SpawnParams;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    auto* Actor = GetWorld()->SpawnActor<AActor>(AActor::StaticClass(), Pos, Rotation, SpawnParams);
    if (!Actor) return;
    auto* Comp = NewObject<UStaticMeshComponent>(Actor);
    if (!Comp) { Actor->Destroy(); return; }
    Comp->SetStaticMesh(Mesh);
    Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Comp->SetCanEverAffectNavigation(false);
    Comp->SetMobility(EComponentMobility::Movable);
    // 必须先设为 Root 再注册，否则组件会以世界原点注册，模型全部堆在 (0,0,0)。
    Actor->SetRootComponent(Comp);
    Comp->RegisterComponent();
    Actor->SetActorLocation(Pos);
    Actor->SetActorRotation(Rotation);
    Actor->SetActorScale3D(Scale);
    // 材质必须在注册之后设置，否则不生效（资产本身已由导入脚本写入 M_Stone）。
    if (Materials.IsValidIndex(Material) && Materials[Material])
    {
        const int32 Slots = FMath::Max(1, Comp->GetNumMaterials());
        for (int32 Slot = 0; Slot < Slots; ++Slot)
            Comp->SetMaterial(Slot, Materials[Material]);
    }
    TrackGenerated(Actor);
}

void ACastleDirector::GenerateCave(const UCastleLayoutSettings*)
{
    ClearGeneratedRegion(ECastlePCGRegion::TreasureCave);
    GeneratingRegion=ECastlePCGRegion::TreasureCave; InitializeMaterials();
    // 藏宝洞专属石材：索引 6 只在藏宝洞使用（探索区/Boss 区用索引 2），
    // 换成组员的 M_MemoryStone 后，BuildRoom 的墙与所有 AddBox(...,6) 直接变石材。
    if (Materials.IsValidIndex(6))
        Materials[6] = LoadObject<UMaterialInterface>(nullptr,
            TEXT("/Game/Demo/MemoryRoomArt/M_MemoryStone.M_MemoryStone"));
    // ===== 金厅构件 =====
    // 材质槽只有 0-8 可用（非碰撞对象会 +9 落到第二槽区，Material=9 会串到别的区域），
    // 所以吊灯/金线这类新材质不能塞进 Materials 数组，直接给组件指定材质指针。
    auto* GoldMat=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Demo/CaveArt/M_CaveGold.M_CaveGold"));
    auto* FlameMat=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Demo/MemoryRoomArt/M_CandleFlame.M_CandleFlame"));
    auto* VelvetMat=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Demo/MemoryRoomArt/M_Velvet.M_Velvet"));
    auto* GlowMat=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Demo/MemoryRoomArt/M_MemoryGlow.M_MemoryGlow"));
    auto CavePart=[&](const TCHAR* Path,FVector Pos,FVector Scale,FRotator Rot,UMaterialInterface* Mat)
    {
        auto* Mesh=LoadObject<UStaticMesh>(nullptr,Path);
        if(!Mesh) { UE_LOG(LogTemp,Warning,TEXT("CAVE_PART_MISSING %s"),Path); return; }
        FActorSpawnParameters SpawnParams;
        SpawnParams.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        auto* Actor=GetWorld()->SpawnActor<AActor>(AActor::StaticClass(),Pos,Rot,SpawnParams);
        if(!Actor) return;
        auto* Comp=NewObject<UStaticMeshComponent>(Actor);
        Comp->SetStaticMesh(Mesh);
        Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Comp->SetCanEverAffectNavigation(false);
        Comp->SetMobility(EComponentMobility::Movable);
        Comp->SetCastShadow(false);
        Actor->SetRootComponent(Comp);
        Comp->RegisterComponent();
        Actor->SetActorLocation(Pos); Actor->SetActorRotation(Rot); Actor->SetActorScale3D(Scale);
        if(Mat) for(int32 Slot=0;Slot<FMath::Max(1,Comp->GetNumMaterials());++Slot) Comp->SetMaterial(Slot,Mat);
        TrackGenerated(Actor);
    };
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
    int32 ArtBuilt=0;
    for(int32 Order=0;Order<4;++Order)
    {
        const auto& Room=CaveRooms[CaveRoute[Order]]; const FVector C=Room.Center;
        // ===== PCG2-藏宝洞：程序化记忆殿堂（仅藏宝洞区域生效）=====
        // 房间尺度、层高、拱顶样式、陈设主题、柱阵、水晶全部由种子决定。
        // 独立随机流：不消耗布局/奖励种子 R，不影响探索区、Boss 区与 AI。
        // 所有装饰均无碰撞（AddBox 第 4 参数 false），不挡门洞、不改导航。
        FRandomStream G(int32(uint32(RunSeed)^uint32(Floor)*0x5bd1e995u^
            GetTypeHash(FIntPoint(FMath::RoundToInt(C.X),FMath::RoundToInt(C.Y)))^0x6772616eu));

        // ===== 【核心】记忆深度：0=入口(记忆残存) .. 1=出口(记忆完整) =====
        // 空间本身在讲故事：越深入，房间越高、越大、陈设越密、光越暖。
        const float MemDepth=Order/3.f;
        // 房间尺寸固定 900：复用组员的 BuildMedievalMemoryRoom（它按 1800x1800 模块化铺设）
        const float Half=900.f;
        const float CeilH=980.f+MemDepth*380.f+G.FRandRange(0.f,120.f);   // 980 -> 1460
        const float Density=FMath::Lerp(.45f,1.f,MemDepth);               // 陈设密度 0.45 -> 1
        // 洞体可见墙面走材质槽 6（只有藏宝洞用这个索引）：每房一份 MID，按深度分级。
        UMaterialInstanceDynamic* RoomStone=nullptr;
        if(auto* ShellBase=CaveSlabBase())
            if(Materials.IsValidIndex(6))
            {
                RoomStone=UMaterialInstanceDynamic::Create(ShellBase,this);
                ApplyCaveGrade(RoomStone,MemDepth,false);
                Materials[6]=RoomStone;
            }
        BuildRoom(C,Half,Room.Doors,6);
        // 组员的完整石材房间：墙/地面/柱/画框/地毯/四角蜡烛/氛围灯
        // 生成前后做一次 tagged actor 差集，拿到本次新建的美术 Actor，再按深度改材质。
        TSet<AActor*> ArtBefore;
        for(TActorIterator<AActor> It(GetWorld());It;++It)
            if(It->ActorHasTag(TEXT("CastleMemoryRoomArt"))) ArtBefore.Add(*It);
        if(BuildMedievalMemoryRoom(Room,Half)) ++ArtBuilt;
        for(TActorIterator<AActor> It(GetWorld());It;++It)
            if(It->ActorHasTag(TEXT("CastleMemoryRoomArt")) && !ArtBefore.Contains(*It))
                GradeCaveRoomArt(*It,MemDepth);

        // ===== 金厅吊灯 =====
        // 整间房只有这一盏开阴影：灯下亮成一片、墙脚自然留暗，房间才有纵深。
        // 深度越深，灯越亮（记忆越完整，厅越"金碧辉煌"）。
        {
            const float HangZ=CeilH-250.f;
            const FVector Hub=C+FVector(0,0,HangZ);
            CavePart(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"),C+FVector(0,0,(CeilH+HangZ)*.5f),
                FVector(.10f,.10f,(CeilH-HangZ)/100.f),FRotator::ZeroRotator,GoldMat);
            CavePart(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"),Hub,FVector(3.4f,3.4f,.12f),FRotator::ZeroRotator,GoldMat);
            CavePart(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"),Hub+FVector(0,0,30),FVector(1.6f,1.6f,.22f),FRotator::ZeroRotator,GoldMat);
            for(int32 A=0;A<10;++A)
            {
                const float Ang=A*(2.f*PI/10.f);
                const FVector Arm(FMath::Cos(Ang)*150.f,FMath::Sin(Ang)*150.f,0);
                CavePart(TEXT("/Engine/BasicShapes/Cube.Cube"),Hub+Arm*.5f,
                    FVector(1.5f,.08f,.08f),FRotator(0,Ang*57.2958f,0),GoldMat);
                CavePart(TEXT("/Engine/BasicShapes/Sphere.Sphere"),Hub+Arm+FVector(0,0,26),
                    FVector(.14f,.14f,.30f),FRotator::ZeroRotator,FlameMat);
            }
            auto* HeroLight=GetWorld()->SpawnActor<APointLight>(Hub+FVector(0,0,-30),FRotator::ZeroRotator);
            if(HeroLight)
            {
                HeroLight->SetMobility(EComponentMobility::Movable);
                auto* Bulb=HeroLight->PointLightComponent.Get();
                if(Bulb)
                {
                    Bulb->SetMobility(EComponentMobility::Movable);
                    // 点光源是平方反比衰减：灯挂在 7 米高，要到地面上跟"地面 1.6 米的
                    // 蜡烛灯"同量级，强度得差 (7/1.6)^2 ≈ 20 倍，所以是 26 万起。
                    Bulb->SetIntensity(FMath::Lerp(380000.f,560000.f,MemDepth));
                    Bulb->SetAttenuationRadius(1900.f);
                    Bulb->SetLightColor(FLinearColor(1.f,.80f,.50f));
                    Bulb->SetCastShadows(true);
                    Bulb->MarkRenderStateDirty();
                }
                TrackGenerated(HeroLight);
                UE_LOG(LogTemp,Display,TEXT("CAVE_CHANDELIER Order=%d Hub=%s Bulb=%d Intensity=%.0f Mobility=%d"),
                    Order,*Hub.ToCompactString(),Bulb!=nullptr,Bulb?Bulb->Intensity:0.f,
                    Bulb?int32(Bulb->Mobility):-1);
            }
            else UE_LOG(LogTemp,Warning,TEXT("CAVE_CHANDELIER_FAILED Order=%d"),Order);
            // 金色腰线：沿着四面墙顶一圈，压在门楣上，让"金碧辉煌"从墙面立起来
            for(int32 Side=0;Side<4;++Side)
            {
                const bool bAlongX=(Side==1||Side==3);
                const FVector N(Side==0?1.f:Side==2?-1.f:0.f,Side==1?1.f:Side==3?-1.f:0.f,0.f);
                const FVector T(-N.Y,N.X,0);
                const FVector Size=bAlongX?FVector(18.2f,.14f,.30f):FVector(.14f,18.2f,.30f);
                CavePart(TEXT("/Engine/BasicShapes/Cube.Cube"),C+N*(Half-52.f)+FVector(0,0,472.f),
                    Size,FRotator::ZeroRotator,GoldMat);
                // 镀金壁柱：只放在没有门的那几面墙中间（画框在 ±440，这里不抢位置），
                // 房间从"一整面砖墙"变成有节奏的厅。
                if(Room.Doors&(1<<Side)) continue;
                CavePart(TEXT("/Game/Models/Kenney/wall-pillar.wall-pillar"),
                    C+N*(Half-58.f),FVector(1.5f,1.5f,4.6f),FRotator(0,Side*90.f,0),GoldMat);
            }
            // 地面金线：围着地毯一圈，"宝库"从脚下就有交代
            for(int32 Sign:{-1,1})
            {
                CavePart(TEXT("/Engine/BasicShapes/Cube.Cube"),C+FVector(Sign*380,0,2.4f),
                    FVector(.06f,8.2f,.012f),FRotator::ZeroRotator,GoldMat);
                CavePart(TEXT("/Engine/BasicShapes/Cube.Cube"),C+FVector(0,Sign*380,2.4f),
                    FVector(8.2f,.06f,.012f),FRotator::ZeroRotator,GoldMat);
            }
            // ===== 补齐上半截 + 封顶 =====
            // 房间美术的墙只有 480 高，再往上是空的，抬头直接看见天光（画面里天花板发蓝
            // 就是这个）。用同一份石材 MID 把 480 一直砌到拱顶上方，再压一块顶板：
            // 房间才真正"关起来"，吊灯的阴影也才有地方落。
            if(RoomStone)
            {
                const float RoofZ=CeilH+300.f;
                for(int32 Side=0;Side<4;++Side)
                {
                    const bool bAlongX=(Side==1||Side==3);
                    const FVector N(Side==0?1.f:Side==2?-1.f:0.f,Side==1?1.f:Side==3?-1.f:0.f,0.f);
                    CavePart(TEXT("/Engine/BasicShapes/Cube.Cube"),C+N*Half+FVector(0,0,(480.f+RoofZ)*.5f),
                        bAlongX?FVector(18.6f,.85f,(RoofZ-480.f)/100.f):FVector(.85f,18.6f,(RoofZ-480.f)/100.f),
                        FRotator::ZeroRotator,RoomStone);
                }
                CavePart(TEXT("/Engine/BasicShapes/Cube.Cube"),C+FVector(0,0,RoofZ),
                    FVector(18.6f,18.6f,.20f),FRotator::ZeroRotator,RoomStone);
                // 顶上的金格：抬头看到的不是一块水泥板，而是格子金顶
                for(int32 Bar=-3;Bar<=3;++Bar)
                {
                    CavePart(TEXT("/Engine/BasicShapes/Cube.Cube"),C+FVector(Bar*265.f,0,RoofZ-14.f),
                        FVector(.22f,18.2f,.22f),FRotator::ZeroRotator,GoldMat);
                    CavePart(TEXT("/Engine/BasicShapes/Cube.Cube"),C+FVector(0,Bar*265.f,RoofZ-14.f),
                        FVector(18.2f,.22f,.22f),FRotator::ZeroRotator,GoldMat);
                }
            }
            // ===== 富丽陈设 =====
            // 壁画之间挂天鹅绒长幅，四角摆金瓶嵌记忆宝石 —— 空厅变成宝库。
            for(int32 Side=0;Side<4;++Side)
            {
                if(Room.Doors&(1<<Side)) continue;
                const FVector N(Side==0?1.f:Side==2?-1.f:0.f,Side==1?1.f:Side==3?-1.f:0.f,0.f);
                const FVector T(-N.Y,N.X,0);
                for(int32 Sign:{-1,1})
                    CavePart(TEXT("/Game/Models/Kenney/flag-banner-long.flag-banner-long"),
                        C+N*(Half-80.f)+T*(Sign*690.f)+FVector(0,0,272.f),
                        FVector(1.6f,1.6f,1.6f),FRotator(0,Side*90.f+180.f,0),VelvetMat);
            }
            for(int32 X:{-1,1}) for(int32 Y:{-1,1})
            {
                const FVector P=C+FVector(X*460.f,Y*520.f,0);
                CavePart(TEXT("/Engine/BasicShapes/Cube.Cube"),P+FVector(0,0,22),
                    FVector(.64f,.64f,.44f),FRotator::ZeroRotator,GoldMat);
                CavePart(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"),P+FVector(0,0,66),
                    FVector(.38f,.38f,.72f),FRotator::ZeroRotator,GoldMat);
                CavePart(TEXT("/Engine/BasicShapes/Sphere.Sphere"),P+FVector(0,0,116),
                    FVector(.20f,.20f,.30f),FRotator::ZeroRotator,GlowMat);
            }
        }

        // 光照基调三套：暖金 / 记忆紫 / 冷金；深处固定暖金（记忆最完整）
        const FLinearColor Moods[3]={FLinearColor(1.f,.80f,.52f),FLinearColor(.88f,.62f,.98f),FLinearColor(.98f,.91f,.68f)};
        const FLinearColor Mood = MemDepth>=.66f?Moods[0]:Moods[G.RandRange(0,2)];
        // 拱顶：形式服从空间 —— 宽厅配肋拱/连拱，高塔配阶梯穹顶
        int32 VaultStyle;
        if(Half>855.f) VaultStyle=G.RandRange(0,1);
        else if(CeilH>1310.f) VaultStyle=2;
        else VaultStyle=1;
        // 主题：按记忆深度递进（人 -> 物 -> 时间 -> 身份），中间两房方向由种子决定
        int32 Theme;
        if(Order==0) Theme=0;
        else if(Order==3) Theme=3;
        else { const bool bFlip=(RunSeed&1)!=0; Theme = Order==1 ? (bFlip?2:1) : (bFlip?1:2); }
        // 中央柱阵 0-4 根（深层更多），柱头三式
        const int32 Pillars=G.RandRange(0,int32(1.f+MemDepth*3.f));
        const int32 CapStyle=G.RandRange(0,2);
        // 每面墙画框 1-3 幅，随密度增加
        const int32 FramesPerWall=1+int32(Density*2.f*G.FRand());
        // 挂毯/烛台/壁龛/隔断：随密度递增
        const bool bTapestry=G.FRand()<FMath::Lerp(.50f,.95f,MemDepth);
        const bool bCandles=G.FRand()<FMath::Lerp(.40f,.95f,MemDepth);
        const bool bNiche=G.FRand()<MemDepth*.85f;
        const int32 Screens=G.RandRange(0,MemDepth>.5f?2:1);
        const int32 GemCount=G.RandRange(1,3);

        const FVector NV[4]={FVector(1,0,0),FVector(0,1,0),FVector(-1,0,0),FVector(0,-1,0)};
        auto Beam=[&](FVector A,FVector B,float Th,int32 Mat)
        {
            const FVector D=B-A;
            AddBox((A+B)*0.5f,FVector(Th,D.Size(),Th),Mat,false,D.Rotation());
        };
        for(int32 Side=0;Side<4;++Side)
        {
            const FVector N=NV[Side],T(-N.Y,N.X,0);
            const bool AlongX=Side%2==0;
            auto Put=[&](float Depth,float Across,float Height,FVector Size,int32 Mat,float Lean=0.f)
            {
                const FVector WS=AlongX?Size:FVector(Size.Y,Size.X,Size.Z);
                AddBox(C+N*Depth+T*Across+FVector(0,0,Height),WS,Mat,false,
                    AlongX?FRotator(0,0,Lean):FRotator(Lean,0,0));
            };
        }
        // 拱顶：三种样式由种子决定
        if(VaultStyle==0) // 交叉肋拱 + 金拱顶石
        {
            const FVector Top=C+FVector(0,0,CeilH+230.f);
            for(int32 X:{-1,1}) for(int32 Y:{-1,1})
                Beam(C+FVector(X*(Half-65),Y*(Half-65),CeilH-15.f),Top,34,1);
            AddBox(Top+FVector(0,0,12),FVector(160,160,62),3,false);
        }
        else if(VaultStyle==1) // 沿 X 轴三连尖拱券
        {
            for(int32 K=-1;K<=1;++K)
            {
                const FVector Arc=C+FVector(K*Half*.55f,0,CeilH-10.f);
                Beam(Arc+FVector(0,-Half*.62f,0),Arc+FVector(0,0,190.f),30,1);
                Beam(Arc+FVector(0, Half*.62f,0),Arc+FVector(0,0,190.f),30,1);
                AddBox(Arc+FVector(0,0,200.f),FVector(60,60,50),3,false);
            }
        }
        else // 阶梯穹顶（四层收分，顶层压金）
        {
            for(int32 K=0;K<4;++K)
            {
                const float S=(Half*1.45f)*(1.f-K*.2f);
                AddBox(C+FVector(0,0,CeilH+40.f+K*52.f),FVector(S,S,46),K==3?3:1,false);
            }
        }
        // 回字形檐口：四周出檐、中间开天窗，让天光进入并遮住墙顶接缝
        {
            const float Span=Half*2+500.f, Rim=Span*.22f;
            AddBox(C+FVector(0, Span*.5f-Rim*.5f, CeilH+300.f),FVector(Span,Rim,90),1,false);
            AddBox(C+FVector(0,-(Span*.5f-Rim*.5f),CeilH+300.f),FVector(Span,Rim,90),1,false);
            AddBox(C+FVector( Span*.5f-Rim*.5f,0,CeilH+300.f),FVector(Rim,Span,90),1,false);
            AddBox(C+FVector(-(Span*.5f-Rim*.5f),0,CeilH+300.f),FVector(Rim,Span,90),1,false);
        }
        // 藏宝洞自主照明：中央 + 四角 + 地面补光，色调由记忆基调决定
        AddLight(C+FVector(0,0,CeilH*.55f),Mood);
        for(int32 X:{-1,1}) for(int32 Y:{-1,1})
            AddLight(C+FVector(X*Half*.45f,Y*Half*.45f,320.f),Mood);
        AddLight(C+FVector(0,0,180.f),Mood);
        // 记忆石碑：用组员的 SM_Pillar 缩小当石碑（石材，融入场景），
        // 保留"记忆标记"的程序化位置，替掉原先突兀的纯色紫方块。
        for(int32 Gi=0;Gi<GemCount;++Gi)
        {
            const float Ang=(45.f+Gi*90.f+G.FRandRange(-12.f,12.f))*3.14159265f/180.f;
            const FVector Gem=C+FVector(FMath::Cos(Ang)*Half*.86f,FMath::Sin(Ang)*Half*.86f,0);
            const float Tilt=G.FRandRange(-8.f,8.f);
            AddDecorMesh(TEXT("/Game/Demo/CourtArt/Masonry/SM_Pillar.SM_Pillar"),
                Gem,FVector(0.34f,0.34f,0.34f),FRotator(0,Tilt,0),-1);
        }
        // 叙事铭文：与房间主题绑定（肖像说人、乐器说歌、钟表说时间、礼服说身份）
        {
            static const TCHAR* Lines[4][3]={
                {TEXT("THE COURT REMEMBERED"),TEXT("SHE PAINTED EVERY FACE HERE"),TEXT("NO ONE LEFT TO WEAR THE CROWN")},
                {TEXT("THE HARP STILL HOLDS ITS NOTE"),TEXT("A FEAST NO ONE LEFT"),TEXT("THE LAST DANCE NEVER ENDED")},
                {TEXT("THE HOURS STOPPED AT DUSK"),TEXT("A CLOCK THAT NEVER STRIKES"),TEXT("TIME RAN OUT FOR ALL OF US")},
                {TEXT("A GOWN FOR A QUEEN LONG GONE"),TEXT("THE VELVET OUTLIVED ITS WEARER"),TEXT("SHE NEVER CAME BACK FOR IT")}};
            AddSign(C+FVector(0,-Half*.60f,430),Lines[Theme][G.RandRange(0,2)],FColor(205,190,240));
        }
        // 程序化参数标牌（只在入口房，直接展示这次生成了什么）
        if(Order==0)
        {
            static const TCHAR* ThemeNames[]={TEXT("PORTRAIT"),TEXT("MUSIC"),TEXT("CLOCK"),TEXT("GOWN")};
            static const TCHAR* VaultNames[]={TEXT("RIB VAULT"),TEXT("BARREL VAULT"),TEXT("STEPPED DOME")};
            const FString Info=FString::Printf(TEXT("MEMORY %.2f  |  %s  |  %s  |  %.1f x %.1f m"),
                MemDepth,ThemeNames[Theme],VaultNames[VaultStyle],Half*2.f/100.f,CeilH/100.f);
            AddSign(C+FVector(-Half*.72f,0,720),Info,FColor(140,220,255));
        }
        // 生成统计：可在日志中核对每个房间的实际参数
        UE_LOG(LogTemp,Display,TEXT("CAVE_ROOM Order=%d Depth=%.2f Theme=%d Vault=%d Half=%.0f Ceil=%.0f"),
            Order,MemDepth,Theme,VaultStyle,Half,CeilH);
        // 终点房：记忆圣坛（规则驱动，每局必有）
        if(Order==3)
        {
            const FVector Alt=C+FVector(-Half*.55f,Half*.55f,0);
            AddBox(Alt+FVector(0,0,45),FVector(380,240,90),2,false);
            AddBox(Alt+FVector(0,0,105),FVector(300,190,110),1,false);
            AddBox(Alt+FVector(0,0,175),FVector(230,150,60),3,false);
            AddBox(Alt+FVector(0,0,250),FVector(84,84,90),6,false);
            AddBox(Alt+FVector(0,0,330),FVector(48,48,70),6,false,FRotator(0,45,0));
            // Kenney 石阶：通向圣坛的两级台阶
            for(int32 K=0;K<2;++K)
                AddDecorMesh(TEXT("/Game/Models/Kenney/stairs-stone.stairs-stone"),
                    Alt+FVector(-150.f-K*60.f,0,K*67.f),FVector(2.4f,2.4f,1.f),FRotator(0,0,0),-1);
            // Kenney 栏杆：圣坛两侧围护
            for(int32 S:{-1,1})
                AddDecorMesh(TEXT("/Game/Models/Kenney/wall-narrow-stairs-rail.wall-narrow-stairs-rail"),
                    Alt+FVector(0,S*150.f,0),FVector(1.6f,1.6f,1.6f),FRotator(0,90.f,0),-1);
        }
        // ===== 以下功能性物件保持原逻辑，不参与装饰随机 =====
        AddSign(C+FVector(680,0,330),Order==0?TEXT("VAULT ENTRANCE"):Order==3?TEXT("VAULT EXIT"):TEXT("GUARDED RELIQUARY"),FColor(120,210,255));
        if(Order>0)
        {
            // 宝箱奖励分级：深层必出最高级藏品（索引 2）
            const int32 RelicKind = MemDepth>=.66f ? 2 : R.RandRange(0,2);
            AddInteract(C+FVector(420,400,45),ECastleInteract::Chest,TEXT("VAULT RELIC"),RelicKind);
        }
        if(Order==1 || Order==2)
        {
            // 守卫数量随记忆深度递增：Floor .. Floor+2
            const int32 Guards = Floor + int32(MemDepth*2.f);
            for(int32 E=0;E<Guards;++E) AddOrdinaryEnemy(C+FVector(-330+E*220,300,100));
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
    if (Floor == 2)
    {
        const auto* Player = Cast<ACastlePlayer>(UGameplayStatics::GetPlayerPawn(this,0));
        if (!Player || !Player->HasWeapon(ECastleWeapon::Gatling))
            CastleDressCourtProp(AddInteract(CaveExit+FVector(280,-250,45),ECastleInteract::Gatling,TEXT("GATLING")));
    }
    UE_LOG(LogTemp,Display,TEXT("CASTLE_CAVE_ROUTE Floor=%d Seed=%d Route=%d,%d,%d,%d"),Floor,RunSeed,CaveRoute[0],CaveRoute[1],CaveRoute[2],CaveRoute[3]);
    // 约束校验：统计本区域几何实例数（0 = 生成失败；过大 = 性能风险）
    {
        int32 TotalInstances=0;
        const int32 Base=int32(ECastlePCGRegion::TreasureCave)*18;
        for(int32 I=Base;I<Base+18;++I)
            if(Geometry.IsValidIndex(I) && Geometry[I]) TotalInstances+=Geometry[I]->GetInstanceCount();
        UE_LOG(LogTemp,Display,TEXT("CAVE_GEOMETRY Instances=%d"),TotalInstances);
        if(TotalInstances==0) UE_LOG(LogTemp,Warning,TEXT("CAVE_GEOMETRY_EMPTY: vault generated no mesh instances"));
    }
    // 洞体的墙面方块和美术模块完全重合，尺寸也一致（都是 85 厚、贴着 Half）。重合面
    // 谁先画谁留下，所以看到的一直是那个 1800 长的大方块 —— 一张贴图铺 1800 单位，
    // 细节全糊。地面同理：洞体是一整块 1900 的板，美术是 4x4 的 450 模块。
    // 四个房间都成功铺了美术模块时，把方块那两层藏掉（只藏可见性，碰撞照旧）。
    // 少一间没铺上就整层保留，避免屋里出现通透的洞。
    if(ArtBuilt>=4)
    {
        const int32 Base=int32(ECastlePCGRegion::TreasureCave)*18;
        for(int32 Material:{0,6})
        {
            const int32 Slot=Base+Material;
            if(Geometry.IsValidIndex(Slot) && Geometry[Slot]) Geometry[Slot]->SetVisibility(false);
        }
    }
    UE_LOG(LogTemp,Display,TEXT("CASTLE_CAVE_SHELL ArtRooms=%d Hidden=%d"),ArtBuilt,ArtBuilt>=4?1:0);
    bCaveGenerated=true;
    if(bExplorationGenerated && bBossGenerated && !bReady)CompleteFloorGeneration();
}

void ACastleDirector::GenerateBossArena(const TArray<FTransform>& Decks, const TArray<FTransform>& Columns)
{
    ClearGeneratedRegion(ECastlePCGRegion::BossArena);
    GeneratingRegion=ECastlePCGRegion::BossArena; InitializeMaterials();
    BossPlatforms.Reset(); BossCoverPoints.Reset();
    if (!BuildBossPalaceShell()) BuildRoom(BossCenter,2250,0,2);
    else AddBox(BossCenter-FVector(0,0,20),FVector(5000,4000,40),2);
    for (const FTransform& Deck : Decks)
    {
        constexpr float Height = 240.f;
        const FVector Top = BossCenter + Deck.GetLocation();
        const FRotator DeckRotation = Deck.Rotator();
        const float Side = FMath::Sign(Deck.GetLocation().X);
        AddBox(Top-FVector(0,0,Height*.5f),FVector(700,600,Height),2,true,DeckRotation);
        AddPalaceMesh(TEXT("/Game/BossPalace/Ruins/Meshes/SM_Ruin_Plinth.SM_Ruin_Plinth"),Top-FVector(0,0,Height*.5f),FVector(710,610,Height),DeckRotation);
        BossPlatforms.Add(Top);
        const FVector Start=Top+DeckRotation.RotateVector(FVector(-Side*1300,0,-Height));
        const FVector End=Top+DeckRotation.RotateVector(FVector(-Side*350,0,0));
        const FRotator Slope=(End-Start).Rotation();
        const FVector Normal=Slope.RotateVector(FVector::UpVector);
        // Extend into both adjoining surfaces to avoid tiny collision/nav seams.
        AddBox((Start+End)*.5f-Normal*20,FVector((End-Start).Size()+80,500,40),1,true,Slope);
        AddPalaceMesh(TEXT("/Game/DarkFantasyCastle/Meshes/SM_BrickStairs.SM_BrickStairs"), (Start+End)*.5f,FVector(500,950,Height),FRotator(0,DeckRotation.Yaw+(Side>0?90:-90),0));
        AddLight(Top+FVector(0,0,300),FLinearColor(.6f,.25f,1));
    }
    // Keep the horizontal spawn-to-boss lane and all ramp approaches open.
    // Cover columns use a fixed size and a seeded pose.
    for (const FTransform& Column : Columns)
    {
        const FVector Pos = BossCenter + Column.GetLocation();
        constexpr float Height = 320.f;
        const FRotator ColumnRotation = Column.Rotator();
        AddBox(Pos+FVector(0,0,Height*.5f),FVector(130,130,Height),8,true,ColumnRotation);
        AddPalaceMesh(TEXT("/Game/BossPalace/Ruins/Meshes/SM_Ruin_BrokenShaft.SM_Ruin_BrokenShaft"),Pos+FVector(0,0,Height*.5f),FVector(142,142,Height+12),ColumnRotation);
        BossCoverPoints.Add(Pos+FVector(0,0,Height));
    }
    // Smooth collision supports navigation; the authored stone meshes supply the visible surfaces.
    for (int32 Material : {1, 2, 8})
    {
        const int32 Slot=int32(ECastlePCGRegion::BossArena)*18+Material;
        if (Geometry.IsValidIndex(Slot) && Geometry[Slot]) Geometry[Slot]->SetVisibility(false);
    }
    Boss=AddEnemy(BossCenter+FVector(400,0,180),true);
    AddInteract(BossCenter+FVector(-1700,-300,45),ECastleInteract::Ammo,TEXT("ARENA SUPPLIES"));
    UE_LOG(LogTemp,Display,TEXT("CASTLE_BOSS_SPACE Floor=%d Seed=%d Platforms=%d Cover=%d Palace=1"),Floor,RunSeed,BossPlatforms.Num(),BossCoverPoints.Num());
    bBossGenerated=true;
    if(bExplorationGenerated && bCaveGenerated && !bReady)CompleteFloorGeneration();
}
