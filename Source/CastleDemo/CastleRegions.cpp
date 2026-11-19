#include "CastleDemo.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
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
        BuildRoom(C,Half,Room.Doors,6);
        // 组员的完整石材房间：墙/地面/柱/画框/地毯/四角蜡烛/氛围灯
        BuildMedievalMemoryRoom(Room,Half);

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
