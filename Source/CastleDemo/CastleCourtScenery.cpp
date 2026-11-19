#include "CastleCourtScenery.h"
#include "CastleDemo.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInterface.h"

namespace {
const FVector Up(0,0,1);
const FVector Axes[] = {FVector(1,0,0),FVector(0,1,0),FVector(-1,0,0),FVector(0,-1,0)};
template<class T> T* Asset(const FString& Dir, const TCHAR* Name)
{
    return LoadObject<T>(nullptr,*FString::Printf(TEXT("%s/%s.%s"),*Dir,Name,Name));
}
}

FCastleCourtScenery::FCastleCourtScenery(UWorld* World)
{
    const FString Kit=TEXT("/Game/Demo/CourtArt/Masonry"), Palette=TEXT("/Game/Demo/MemoryRoomArt");
    Cube=Asset<UStaticMesh>(TEXT("/Engine/BasicShapes"),TEXT("Cube"));
    Sphere=Asset<UStaticMesh>(TEXT("/Engine/BasicShapes"),TEXT("Sphere"));
    Wall=Asset<UStaticMesh>(Kit,TEXT("SM_Wall_00")); Tiles=Asset<UStaticMesh>(Kit,TEXT("SM_Ground"));
    Pillar=Asset<UStaticMesh>(Kit,TEXT("SM_Pillar"));
    Candle=Asset<UStaticMesh>(TEXT("/Game/Dungeon/Candles/Meshes"),TEXT("SM_Candle_Melted_01"));
    Standard=Asset<UStaticMesh>(TEXT("/Game/Demo/CourtArt"),TEXT("SM_TornStandard"));
    ShatteredPortrait=Asset<UStaticMesh>(TEXT("/Game/Demo/CourtArt/Portraits"),TEXT("SM_ShatteredCourtPortrait"));
    DroppedFrameRail=Asset<UStaticMesh>(TEXT("/Game/Demo/CourtArt/Portraits"),TEXT("SM_DroppedFrameRail"));
    Stone=Asset<UMaterialInterface>(Palette,TEXT("M_MemoryStone")); Paving=Asset<UMaterialInterface>(Palette,TEXT("M_MemoryTiles"));
    Limestone=Asset<UMaterialInterface>(TEXT("/Game/Demo/CourtArt"),TEXT("M_CourtLimestone"));
    if(!Limestone)Limestone=Asset<UMaterialInterface>(Palette,TEXT("M_Limestone"));
    Dark=Asset<UMaterialInterface>(Palette,TEXT("M_Charcoal"));
    Wine=Asset<UMaterialInterface>(Palette,TEXT("M_Velvet")); Gold=Asset<UMaterialInterface>(Palette,TEXT("M_AntiqueGold"));
    Flame=Asset<UMaterialInterface>(Palette,TEXT("M_CandleFlame"));
    if(!Cube || !Wall || !Pillar || !Tiles || !Stone || !Paving || !Dark || !Limestone) return;
    Owner=World->SpawnActor<AActor>();
    if(!Owner)return;
    Owner->Tags.Add(TEXT("CastleCourtScenery"));
    auto* Root=NewObject<USceneComponent>(Owner); Owner->AddInstanceComponent(Root); Owner->SetRootComponent(Root);Root->RegisterComponent();
    Shell=NewObject<UInstancedStaticMeshComponent>(Owner);Owner->AddInstanceComponent(Shell);Shell->SetupAttachment(Root);
    Shell->SetStaticMesh(Cube);Shell->SetCollisionProfileName(TEXT("BlockAll"));Shell->SetCanEverAffectNavigation(true);
    Shell->SetVisibility(false);Shell->SetHiddenInGame(true);Shell->SetCastShadow(false);Shell->RegisterComponent();
    // The upper masonry blocks shots but cannot add a walkable surface above the vault to the navmesh.
    UpperShell=NewObject<UInstancedStaticMeshComponent>(Owner);
    Owner->AddInstanceComponent(UpperShell);
    UpperShell->SetupAttachment(Root);
    UpperShell->SetStaticMesh(Cube);
    UpperShell->ComponentTags.Add(TEXT("CastleVaultMasonry"));
    UpperShell->SetCollisionProfileName(TEXT("BlockAll"));
    UpperShell->SetCanEverAffectNavigation(false);
    UpperShell->SetVisibility(false);
    UpperShell->SetHiddenInGame(true);
    UpperShell->SetCastShadow(false);
    UpperShell->RegisterComponent();
}

void FCastleCourtScenery::Put(UStaticMesh* Mesh,UMaterialInterface* Mat,FVector Center,FVector LocalSize,FRotator Rot)
{
    if(!Mesh)return;
    const TPair<UStaticMesh*,UMaterialInterface*> Key(Mesh,Mat);
    auto*& Batch=Batches.FindOrAdd(Key);
    if(!Batch)
    {
        Batch=NewObject<UInstancedStaticMeshComponent>(Owner);Owner->AddInstanceComponent(Batch);Batch->SetupAttachment(Owner->GetRootComponent());
        Batch->SetStaticMesh(Mesh);Batch->SetCollisionEnabled(ECollisionEnabled::NoCollision);Batch->SetCanEverAffectNavigation(false);
        if(Mat)for(int32 I=0;I<Mesh->GetStaticMaterials().Num();++I)Batch->SetMaterial(I,Mat);
        Batch->RegisterComponent();
    }
    const FBox Bounds=Mesh->GetBoundingBox();const FVector Size=Bounds.GetSize().ComponentMax(FVector(.01));
    const FVector Scale=LocalSize/Size;
    Batch->AddInstance(FTransform(Rot,Center-Rot.RotateVector(Bounds.GetCenter()*Scale),Scale),true);
}

void FCastleCourtScenery::WallPanel(FVector Center,double Length,double Depth,double Height,double Yaw)
{
    const bool ThinX=Wall->GetBoundingBox().GetSize().X<Wall->GetBoundingBox().GetSize().Y;
    Put(Wall,Stone,Center,ThinX?FVector(Depth,Length,Height):FVector(Length,Depth,Height),FRotator(0,Yaw+(ThinX?0:90),0));
}

void FCastleCourtScenery::SolidStone(FVector Center, FVector LocalSize, FRotator Rot, UMaterialInterface* Finish)
{
    Put(Cube,Finish,Center,LocalSize,Rot);
    UpperShell->AddInstance(FTransform(Rot,Center,LocalSize/100),true);
}

void FCastleCourtScenery::Vault(FVector Center, double Yaw, double Length, double HalfSpan, double Spring, double Rise, bool LargeHall)
{
    const FRotator Turn(0,Yaw,0);
    const FVector Along = Turn.Vector();
    const FVector Across(-Along.Y,Along.X,0);
    constexpr int32 Sections = 16;
    for (int32 Section = 0; Section < Sections; ++Section)
    {
        const auto Surface = [&](int32 Edge) {
            const double X = 2.0 * Edge / Sections - 1.0;
            return Center + Across*(X*HalfSpan) + Up*(Spring + Rise*(1.0-X*X));
        };
        const FVector A = Surface(Section), B = Surface(Section+1);
        const FVector Delta = B-A;
        const FRotator Rotation = FRotationMatrix::MakeFromXY(Along,Delta.GetSafeNormal()).Rotator();
        const FVector ArchCenter = (A+B)*.5;
        // Overlapping masonry courses seal the seams, including the angle change at the crown.
        SolidStone(ArchCenter+Up*22,FVector(Length,Delta.Size()+21,44),Rotation,Limestone);
        if (LargeHall)
        {
            for(double Position : {-665.,0.,665.})
            {
                Put(Cube,Limestone,ArchCenter+Along*Position-Up*9,FVector(43,Delta.Size()+7,36),Rotation);
                Put(Cube,Stone,ArchCenter+Along*Position-Up*25,FVector(26,Delta.Size()+4,19),Rotation);
            }
        }
        else
        {
            for(double Position : {-162.,162.})
                Put(Cube,Limestone,ArchCenter+Along*Position-Up*13,FVector(23,Delta.Size()+6,21),Rotation);
        }
    }
}

void FCastleCourtScenery::EncloseRoom(const FCastleRoom& Node, int32 Floor)
{
    const FVector C = Node.Center;
    const double Spring = CrownHeight(Floor)-VaultRise;
    Vault(C,0.,1892.,916.,Spring,VaultRise,true);
    for(int32 Side = 0; Side < 4; ++Side)
    {
        const FVector N = Axes[Side], T(-N.Y,N.X,0);
        const FRotator R(0,Side*90.,0);
        const bool Open = (Node.Doors&(1<<Side)) != 0;
        // The vault runs along X. Its end walls rise to the crown and its side walls meet the spring line.
        const double Top = Side%2 == 0 ? CrownHeight(Floor)+34. : Spring+43.;
        constexpr double Bottom = 470.;
        const FVector UpperWall = C+N*900+Up*((Bottom+Top)*.5);
        SolidStone(UpperWall,FVector(86,1840,Top-Bottom),R,Stone);
        for(int32 Bay = 0; Bay < 4; ++Bay)
            WallPanel(UpperWall-N*19+T*(-675.+Bay*450.),450,56,Top-Bottom,Side*90.);
        // Back the irregular kit stone. Any split in a lower wall shows depth, rather than the sky.
        const FVector Back = C+N*925;
        if(Open)
        {
            for(double SideOfDoor : {-1.,1.})
                Put(Cube,Stone,Back+T*(SideOfDoor*583.)+Up*243,FVector(39,686,486),R);
            Put(Cube,Stone,Back+Up*455,FVector(39,484,90),R);
        }
        else
            Put(Cube,Stone,Back+Up*243,FVector(39,1860,486),R);
        Put(Cube,Limestone,C+N*853+Up*541,FVector(36,1774,25),R);
        if(Side%2 == 1)
        {
            for(double Position : {-665.,0.,665.})
            {
                const FVector Pier = C+N*843+T*Position;
                Put(Pillar,Stone,Pier+Up*((536.+Spring)*.5),FVector(92,89,Spring-536.),R);
                Put(Cube,Limestone,Pier+Up*(Spring-7),FVector(103,102,23),R);
            }
        }
    }
}

void FCastleCourtScenery::Light(FVector Pos,FLinearColor Color,float Intensity,float Radius)
{
    auto* L=NewObject<UPointLightComponent>(Owner);Owner->AddInstanceComponent(L);L->SetupAttachment(Owner->GetRootComponent());
    L->SetLightColor(Color);L->SetIntensity(Intensity);L->SetAttenuationRadius(Radius);L->SetCastShadows(false);
    L->SetRelativeLocation(Pos);L->RegisterComponent();
}

void FCastleCourtScenery::Arch(FVector Base,FVector N,double Yaw)
{
    const FVector T(-N.Y,N.X,0); const FRotator R(0,Yaw,0);
    // Stonework starts outside the 480 cm doorway. The crown is entirely above the 410 cm lintel.
    for(int Sign:{-1,1})
    {
        const FVector Jamb=Base+T*(Sign*275.0);
        Put(Pillar,Stone,Jamb+Up*210,FVector(66,66,420),R);
        Put(Cube,Limestone,Jamb+Up*405,FVector(76,70,19),R);
        for(int I=0;I<7;++I)
        {
            auto Sample=[&](double F){return Base+T*(Sign*275.*(1.-F*F))+Up*(430.+144.*(1.28*F-.28*F*F));};
            // From the outer springer to the pointed apex; short bevelled courses read at player scale.
            const FVector A=Sample(I/7.),B=Sample((I+1)/7.);const FVector Delta=B-A;
            const FRotator BlockRot=FRotationMatrix::MakeFromXY(N,Delta.GetSafeNormal()).Rotator();
            Put(Cube,Limestone,(A+B)*.5,FVector(67,Delta.Size()-1.8,34),BlockRot);
        }
    }
    Put(Cube,Limestone,Base+Up*579,FVector(76,40,60),R);
}

void FCastleCourtScenery::Banner(FVector Top,double Yaw,double Width,double Height)
{
    const FRotator R(0,Yaw,0);const FVector N=R.Vector();
    Put(Cube,Gold,Top+Up*6,FVector(14,Width+30,9),R);
    if(Standard)Put(Standard,nullptr,Top-N*7-Up*(Height*.5),FVector(14,Width,Height),R);
    else {
        Put(Cube,Wine,Top-Up*(Height*.46),FVector(7,Width,Height*.92),R);
        Put(Cube,Gold,Top-N*5-Up*(Height*.20),FVector(3,Width*.72,5),R);
    }
}

void FCastleCourtScenery::Frame(FVector Center,FVector N,double Yaw,bool Broken)
{
    const FVector T(-N.Y,N.X,0);const FRotator R(0,Yaw,0);
    if(ShatteredPortrait)
    {
        Put(ShatteredPortrait,nullptr,Center-N*11,FVector(20,229,304),FRotator(0,Yaw,Broken?4.5:-1.5));
        Put(Cube,Limestone,Center-N*5-Up*180,FVector(8,53,13),R);
        if(DroppedFrameRail && Broken)
        {
            const FVector FloorPiece(Center.X,Center.Y,8);
            Put(DroppedFrameRail,nullptr,FloorPiece-N*105+T*151,FVector(16,109,20),FRotator(0,Yaw+21,12));
        }
        return;
    }
    // An absent painting leaves a bare masonry patch; the missing rail tells a different story from the chest portraits.
    if(!Broken)Put(Cube,Dark,Center,FVector(10,176,213),R);
    else {
        // Expose real stone through the frame. One remnant of its stretcher survives in a corner.
        Put(Cube,Dark,Center+T*45+Up*57,FVector(9,75,12),R);
        Put(Cube,Dark,Center+T*75+Up*29,FVector(9,13,69),R);
    }
    for(int S:{-1,1}) {
        Put(Cube,Gold,Center-N*10+T*(S*94.)+Up*((S==1 && Broken)?26:0),FVector(18,13,(S==1 && Broken)?175:232),R);
        if(!Broken || S==1)Put(Cube,Gold,Center-N*10+Up*(S*111.),FVector(18,200,13),R);
    }
    if(Broken)Put(Cube,Gold,Center-N*12-T*51-Up*109,FVector(18,92,13),FRotator(0,Yaw,7));
    Put(Cube,Limestone,Center-N*5-Up*142,FVector(7,50,12),R);
    if(Broken) {
        // The loose gilded rail, fasteners and chipped masonry are heaped against the wall, outside the walkable lanes.
        const FVector Foot=Center-N*55+T*168-Up*(Center.Z-9.);
        Put(Cube,Gold,Foot,FVector(14,136,10),FRotator(0,Yaw+12,3));
        for(int I=0;I<6;++I) {
            const FVector At=Foot+T*(I*20.-46.)-N*((I*17)%49)+Up*(I%2?3:0);
            Put(Wall,Stone,At,FVector(14.+(I*7)%15,22.+(I*11)%21,12.+(I*3)%8),FRotator(4.*(I%3),Yaw+I*29.,I%2?9.:-4.));
        }
    }
}

void FCastleCourtScenery::Sconce(FVector Base,FVector N,double Yaw)
{
    const FVector T(-N.Y,N.X,0);const FRotator R(0,Yaw,0);
    Put(Cube,Dark,Base,FVector(12,36,65),R);
    Put(Cube,Gold,Base-N*24+Up*3,FVector(54,39,9),R);
    for(int S:{-1,0,1}) {
        const double H=(S==0)?35:22; const FVector Wax=Base-N*43+T*(S*12.);
        Put(Candle?Candle:Cube,Limestone,Wax+Up*(7+H*.5),FVector(9,9,H),R);
        Put(Sphere,Flame,Wax+Up*(11+H),FVector(6,6,13),R);
    }
}

void FCastleCourtScenery::Room(const FCastleRoom& Node,int32 DressingSeed)
{
    ++RoomCount;
    const FVector C=Node.Center;FRandomStream Patina(DressingSeed);
    const bool Entrance=Node.Type==ECastleRoom::Entrance, Exit=Node.Type==ECastleRoom::Exit;
    const bool Safe=Node.Type==ECastleRoom::Rest || Node.Type==ECastleRoom::Shop;
    const bool Formal=Entrance || Exit || Node.Type==ECastleRoom::Elite || Node.Type==ECastleRoom::Event;
    int Focus=0;while(Focus<4 && (Node.Doors&(1<<Focus)))++Focus;
    for(int X=0;X<4;++X)for(int Y=0;Y<4;++Y) {
        const FVector P=C+FVector(-675+450*X,-675+450*Y,0);
        const FRotator Turn(0,90.*Patina.RandRange(0,3),0);
        // The kit's flags are deeply uneven; use a *textured*, per-bay substrate under their low spots and seams.
        Put(Cube,Paving,P-Up*38,FVector(450,450,70),Turn);
        Put(Tiles,Paving,P-Up*12,FVector(450,450,24),Turn);
    }
    if(Entrance || Exit) {
        // A compass rose in the flagstones echoes the painter's diamond seal. No route is painted on a closed wall.
        for(int I=0;I<8;++I) {
            const double A=I*PI/4.;const FVector A0(FMath::Cos(A),FMath::Sin(A),0);
            const double B=(I+1)*PI/4.;const FVector A1(FMath::Cos(B),FMath::Sin(B),0);
            auto Inlay=[&](FVector P,FVector Q,double W,UMaterialInterface* M,double Z){
                Put(Cube,M,C+(P+Q)*.5+Up*Z,FVector((Q-P).Size(),W,1.2),(Q-P).Rotation());
            };
            Inlay(A0*190,A1*190,5,Gold,1.4);
            Inlay(A0*162,(A0+A1)*43,7,Limestone,1.6);
            Inlay((A0+A1)*43,A1*162,7,Limestone,1.6);
        }
    }
    for(int Side=0;Side<4;++Side) {
        const FVector N=Axes[Side],T(-N.Y,N.X,0); const double Yaw=Side*90.;const FRotator R(0,Yaw,0);
        const bool Open=(Node.Doors&(1<<Side))!=0;
        auto Bay=[&](double Offset,double Length){
            const FVector B=C+N*900+T*Offset;
            WallPanel(B+Up*260,Length,85,520,Yaw);
            Put(Cube,Stone,B-N*28+Up*57,FVector(58,Length,114),R);
            Put(Cube,Limestone,B-N*45+Up*117,FVector(28,Length,9),R);
            Put(Cube,Stone,B+Up*506,FVector(122,Length,33),R);
        };
        if(Open) {
            for(int S:{-1,1})for(int K=0;K<2;++K)Bay(S*(405+330*K),330);
            // Close the tympanum behind the arch so the visual opening agrees with its solid lintel.
            WallPanel(C+N*900+Up*495,480,85,170,Yaw);
            Arch(C+N*846,N,Yaw);
        } else {
            for(int K=0;K<4;++K)Bay(-675+K*450,450);
            if(Side==Focus || (Entrance && Side==0)) {
                Frame(C+N*840+Up*269,N,Yaw,!Safe);
                if(Formal || Safe)for(int S:{-1,1})Banner(C+N*835+T*(S*350.)+Up*514,Yaw,Formal?125:84,Formal?282:185);
                // High blind arches keep the Gothic silhouette in the room without opening onto the sky.
                for(int W=-1;W<=1;++W) {
                    if(!Formal && W==1)continue;
                    const FVector Sill=C+N*842+T*(W*153.)+Up*553;
                    const double Rise=W==0?104.:69.;
                    Put(Cube,Stone,Sill,FVector(80,164,25),R);
                    for(int S:{-1,1}) {
                        Put(Pillar,Stone,Sill+T*(S*67.)+Up*(Rise*.5),FVector(37,25,Rise),R);
                        for(int J=0;J<5;++J) {
                            auto Point=[&](double F){return Sill+T*(S*67.*(1.-F*F))+Up*(Rise+63.*F);};
                            const FVector A=Point(J/5.),B=Point((J+1)/5.);
                            Put(Cube,Limestone,(A+B)*.5,FVector(37,(B-A).Size()+1.,16),FRotationMatrix::MakeFromXY(N,(B-A).GetSafeNormal()).Rotator());
                        }
                    }
                }
            }
        }
        if(Open && (Entrance || Exit || (Formal && Side%2==0)))for(int S:{-1,1})Banner(C+N*833+T*(S*460.)+Up*493,Yaw,112,246);
        if((Open && (Entrance || Safe || Exit)) || Side==Focus) {
            for(int S:{-1,1})Sconce(C+N*827+T*(S*(Open?350.:242.))+Up*(Safe?162.:195.),N,Yaw);
            for(int S:{-1,1})
                Light(C+N*672+T*(S*(Open?350.:211.))+Up*(Safe?202.:246.),FLinearColor(1.f,.64f,.36f),Safe?17500:11800,850);
        }
    }
    for(int X:{-1,1})for(int Y:{-1,1}) {
        const FVector P=C+FVector(X*835,Y*835,0);
        Put(Pillar,Stone,P+Up*281,FVector(137,137,562));
        Put(Cube,Limestone,P+Up*98,FVector(148,148,12));
        Put(Cube,Stone,P+Up*560,FVector(174,174,34));
    }
    // Low ambient light keeps movement legible while the wall sconces do most of the work.
    Light(C+FVector(-80,-90,415),Safe?FLinearColor(1.f,.79f,.55f):FLinearColor(.68f,.78f,1.f),Safe?27400:21700,1590);
    if(Entrance)
        Light(C+Up*610,FLinearColor(.59f,.67f,.87f),16000,1100);
}

void FCastleCourtScenery::Passage(FVector C,int32 Side)
{
    ++PassageCount;
    const FVector N=Axes[Side],T(-N.Y,N.X,0);const double Yaw=Side*90.;const FRotator R(0,Yaw,0);
    Put(Cube,Paving,C-Up*38,FVector(700,560,70),R);
    for(int S:{-1,1}) {
        Put(Tiles,Paving,C+N*(S*158.)-Up*12,FVector(316,552,24),R);
        WallPanel(C+T*(S*280.)+Up*243,700,65,486,Yaw+90.);
        Put(Cube,Stone,C+T*(S*300.)+Up*242,FVector(694,36,484),R);
        Put(Cube,Limestone,C+T*(S*249.)+Up*120,FVector(624,10,10),R);
    }
    Vault(C,Yaw,644.,275.,450.,113.,false);
    Sconce(C+T*237+Up*221,T,Yaw+90.);
    Light(C+T*153+Up*270,FLinearColor(1.f,.62f,.34f),5700,553);
}

void FCastleCourtScenery::RuinedCover(FVector Center,FVector Size)
{
    // The visible plinth only dresses the original cover box retained by the caller.
    Put(Cube,Stone,Center,Size);Put(Cube,Limestone,Center+Up*(Size.Z*.5-13),FVector(Size.X+2,Size.Y+2,9));
}

void FCastleCourtScenery::Report(int32 Seed,int32 Floor) const
{
    if(Owner)UE_LOG(LogTemp,Display,TEXT("CASTLE_COURT_READY Seed=%d Floor=%d Rooms=%d Corridors=%d Batches=%d ShellBoxes=%d VaultCrownCm=%d UpperBoxes=%d Portrait=%d"),Seed,Floor,RoomCount,PassageCount,Batches.Num(),Shell->GetInstanceCount(),CrownHeight(Floor),UpperShell->GetInstanceCount(),ShatteredPortrait!=nullptr);
}
