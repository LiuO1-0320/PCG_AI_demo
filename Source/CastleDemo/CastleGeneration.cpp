#include "CastleDemo.h"
#include "CastleLanguage.h"
#include "PCGComponent.h"
#include "PCGContext.h"
#include "PCGElement.h"
#include "PCGGraph.h"
#include "PCGNode.h"
#include "Data/PCGPointData.h"
#include "Metadata/PCGMetadata.h"
#include "Components/BoxComponent.h"
#include "Components/Button.h"
#include "Components/EditableTextBox.h"
#include "Components/TextBlock.h"
#include "Misc/ConfigCacheIni.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Components/PointLightComponent.h"
#include "Engine/PointLight.h"
#include "Engine/TextRenderActor.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "NavigationSystem.h"
#include "Materials/MaterialInterface.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "HAL/PlatformMisc.h"
#include "Engine/GameViewportClient.h"
#include "Misc/Paths.h"

void CastleGenerateFloorNodes(int32 RunSeed,int32 Floor,TArray<FCastleRoom>& Rooms,TArray<int32>& Route);

static int32 CastleStreamSeed(int32 RunSeed, int32 Floor, uint32 Purpose)
{
    uint32 X=uint32(RunSeed) ^ (uint32(Floor)*0x9e3779b9u) ^ Purpose;
    X^=X>>16; X*=0x7feb352du; X^=X>>15; X*=0x846ca68bu; X^=X>>16;
    return int32(X);
}

class FCastleLayoutElement : public IPCGElement
{
public:
    virtual bool CanExecuteOnlyOnMainThread(FPCGContext*) const override { return true; }
    virtual bool IsCacheable(const UPCGSettings*) const override { return false; }
protected:
    virtual bool ExecuteInternal(FPCGContext* Context) const override
    {
        auto* Component = Cast<UPCGComponent>(Context->ExecutionSource.Get());
        auto* D = Component ? Cast<ACastleDirector>(Component->GetOwner()) : nullptr;
        if (!D) return true;
        const auto* Settings=Context->GetInputSettings<UCastleLayoutSettings>();
        const ECastlePCGRegion Area=Settings->GeneratorRegion;
        if(Area==ECastlePCGRegion::Exploration) D->GenerateLayout(Settings);
        else if(Area==ECastlePCGRegion::TreasureCave) D->GenerateCave(Settings);
        else D->GenerateBossArena(Settings);
        auto* Points = NewObject<UPCGPointData>();
        auto* RoomType = Points->Metadata->CreateAttribute<int32>(TEXT("RoomType"), 0, false, false);
        auto* Region = Points->Metadata->CreateAttribute<int32>(TEXT("Region"), 0, false, false);
        auto* DoorMask = Points->Metadata->CreateAttribute<int32>(TEXT("DoorMask"), 0, false, false);
        auto* RouteOrder = Points->Metadata->CreateAttribute<int32>(TEXT("RouteOrder"), -1, false, false);
        auto AddPoint = [&](FVector Pos, int32 Type, int32 Area, int32 Doors, int32 Order)
        {
            FPCGPoint P; P.Transform = FTransform(Pos); P.Seed = D->RunSeed;
            P.BoundsMin = FVector(-900,-900,0); P.BoundsMax = FVector(900,900,480);
            P.MetadataEntry = Points->Metadata->AddEntry();
            RoomType->SetValue(P.MetadataEntry, Type); Region->SetValue(P.MetadataEntry, Area); DoorMask->SetValue(P.MetadataEntry, Doors); RouteOrder->SetValue(P.MetadataEntry, Order);
            Points->GetMutablePoints().Add(P);
        };
        int32 ActiveRooms=0;
        if(Area==ECastlePCGRegion::Exploration || Area==ECastlePCGRegion::TreasureCave)
        {
            const auto& Nodes=Area==ECastlePCGRegion::Exploration?D->Rooms:D->CaveRooms;
            for(const auto& R:Nodes) if(R.bActive)
            {
                AddPoint(R.Center,static_cast<int32>(R.Type),int32(Area),R.Doors,R.RouteOrder); ++ActiveRooms;
            }
        }
        else
        {
            AddPoint(D->BossCenter,-1,int32(Area),0,-1); ++ActiveRooms;
            for(const FVector& Pos:D->BossPlatforms) AddPoint(Pos,-2,int32(Area),0,-1);
            for(const FVector& Pos:D->BossCoverPoints) AddPoint(Pos,-3,int32(Area),0,-1);
        }
        FPCGTaggedData& Output=Context->OutputData.TaggedData.Emplace_GetRef();
        Output.Pin=TEXT("Out"); Output.Data=Points;
        Output.Tags.Add(Area==ECastlePCGRegion::Exploration?TEXT("CastleExploration"):Area==ECastlePCGRegion::TreasureCave?TEXT("CastleTreasureCave"):TEXT("CastleBossArena"));
        UE_LOG(LogTemp,Display,TEXT("CASTLE_PCG_REGION_READY Region=%d Floor=%d Seed=%d Rooms=%d Points=%d"),int32(Area),D->Floor,D->RunSeed,ActiveRooms,Points->GetPoints().Num());
        return true;
    }
};

TArray<FPCGPinProperties> UCastleLayoutSettings::InputPinProperties() const { return {}; }
TArray<FPCGPinProperties> UCastleLayoutSettings::OutputPinProperties() const { return { FPCGPinProperties(TEXT("Out"), EPCGDataType::Point) }; }
FPCGElementPtr UCastleLayoutSettings::CreateElement() const { return MakeShared<FCastleLayoutElement>(); }

UPCGGraph* ACastleDirector::MakeGraph(UObject* Outer, ECastlePCGRegion GeneratorRegion)
{
    auto* Graph = NewObject<UPCGGraph>(Outer);
    UCastleLayoutSettings* Settings = nullptr;
    auto* Node = Graph->AddNodeOfType(Settings);
    Settings->GeneratorRegion=GeneratorRegion;
    Graph->AddEdge(Node, TEXT("Out"), Graph->GetOutputNode(), TEXT("Out"));
    return Graph;
}

ACastleDirector::ACastleDirector()
{
    PrimaryActorTick.bCanEverTick = true;
    auto* Bounds = CreateDefaultSubobject<UBoxComponent>(TEXT("GenerationBounds"));
    SetRootComponent(Bounds); Bounds->SetBoxExtent(FVector(8500,10000,1000)); Bounds->SetRelativeLocation(FVector(4000,6000,0));
    Bounds->SetCollisionEnabled(ECollisionEnabled::NoCollision); Bounds->SetCanEverAffectNavigation(false);
    PCG = CreateDefaultSubobject<UPCGComponent>(TEXT("PCG_Castle"));
    PCG->bActivated = true;
    PCG->GenerationTrigger = EPCGComponentGenerationTrigger::GenerateOnDemand;
    CavePCG=CreateDefaultSubobject<UPCGComponent>(TEXT("PCG_TreasureCave"));
    BossPCG=CreateDefaultSubobject<UPCGComponent>(TEXT("PCG_BossArena"));
    for(auto* Component:{CavePCG.Get(),BossPCG.Get()})
    {
        Component->bActivated=true;
        Component->GenerationTrigger=EPCGComponentGenerationTrigger::GenerateOnDemand;
    }
}

void ACastleDirector::BeginPlay()
{
    Super::BeginPlay();
    bReady=false;
    FString LanguageArg;
    if(FParse::Value(FCommandLine::Get(),TEXT("CastleLanguage="),LanguageArg))
    {
        if(LanguageArg==TEXT("zh")) CastleSetChinese(true,false);
        else if(LanguageArg==TEXT("en")) CastleSetChinese(false,false);
    }
    const TCHAR* GraphPaths[]={TEXT("/Game/Demo/PCG_Exploration.PCG_Exploration"),TEXT("/Game/Demo/PCG_BossArena.PCG_BossArena"),TEXT("/Game/Demo/PCG_TreasureCave.PCG_TreasureCave")};
    UPCGComponent* Components[]={PCG,BossPCG,CavePCG};
    for(int32 I=0;I<3;++I)
    {
        auto* Graph=LoadObject<UPCGGraph>(nullptr,GraphPaths[I]);
        Components[I]->SetGraph(Graph?Graph:MakeGraph(this,static_cast<ECastlePCGRegion>(I)));
    }
    FString SeedArg; int32 ParsedSeed=0;
    if(FParse::Value(FCommandLine::Get(),TEXT("CastleSeed="),SeedArg) && UCastleStartMenu::ParseSeed(SeedArg,ParsedSeed))
        StartRun(ParsedSeed);
    else if(!FParse::Param(FCommandLine::Get(),TEXT("CastleMenuSmokeTest")) &&
            (FParse::Param(FCommandLine::Get(),TEXT("CastleSmokeTest")) || FParse::Param(FCommandLine::Get(),TEXT("CastleCapture"))))
        StartRun(FMath::RandRange(1,1000000000));
    else if(auto* PC=UGameplayStatics::GetPlayerController(this,0))
    {
        StartMenu=CreateWidget<UCastleStartMenu>(PC,UCastleStartMenu::StaticClass());
        StartMenu->Director=this; StartMenu->AddToViewport(100);
        UE_LOG(LogTemp,Display,TEXT("CASTLE_MENU_READY"));
    }
}

void ACastleDirector::Tick(float Dt)
{
    Super::Tick(Dt);
    if(FParse::Param(FCommandLine::Get(),TEXT("CastleMenuCapture")) && IsValid(StartMenu))
    {
        const float Now=GetWorld()->GetTimeSeconds();
        if(MenuCaptureAt==0)MenuCaptureAt=Now+2;
        if(!bMenuCaptureRequested && Now>=MenuCaptureAt)
        {
            const FString Path=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("Screenshots/Demo/CastleSeedMenu.png"));
            FScreenshotRequest::RequestScreenshot(Path,true,false);
            UE_LOG(LogTemp,Display,TEXT("CASTLE_MENU_CAPTURE %s"),*Path);
            bMenuCaptureRequested=true;
        }
        if(bMenuCaptureRequested && Now>=MenuCaptureAt+2)FPlatformMisc::RequestExitWithStatus(false,0);
    }
    if(FParse::Param(FCommandLine::Get(),TEXT("CastleMenuSmokeTest")) && !bMenuSmokeStarted && IsValid(StartMenu) && IsValid(StartMenu->StartButton))
    {
        const bool OriginalLanguage=CastleIsChinese();
        StartMenu->LanguageButton->OnClicked.Broadcast();
        const auto* StartCaption=Cast<UTextBlock>(StartMenu->StartButton->GetChildAt(0));
        bool SavedLanguage=OriginalLanguage;
        GConfig->GetBool(TEXT("CastleDemo.Language"),TEXT("Chinese"),SavedLanguage,GGameUserSettingsIni);
        if(CastleIsChinese()==OriginalLanguage || SavedLanguage!=CastleIsChinese() || !StartCaption || StartCaption->GetText().ToString()!=CastleText(TEXT("START GAME")))
        {UE_LOG(LogTemp,Error,TEXT("CASTLE_LANGUAGE_SMOKE_FAILED Menu switch or persistence"));FPlatformMisc::RequestExitWithStatus(true,1);return;}
        StartMenu->LanguageButton->OnClicked.Broadcast();
        UE_LOG(LogTemp,Display,TEXT("CASTLE_LANGUAGE_MENU_OK: bilingual menu toggle and saved preference"));
        StartMenu->RandomButton->OnClicked.Broadcast();
        int32 RandomSeed=0;
        if(!UCastleStartMenu::ParseSeed(StartMenu->SeedInput->GetText().ToString(),RandomSeed))
        {UE_LOG(LogTemp,Error,TEXT("CASTLE_MENU_SMOKE_FAILED Random seed is not valid input"));FPlatformMisc::RequestExitWithStatus(true,1);return;}
        StartMenu->SeedInput->SetText(FText::FromString(TEXT("0")));
        StartMenu->StartButton->OnClicked.Broadcast();
        if(PendingFloor || RunSeed) {UE_LOG(LogTemp,Error,TEXT("CASTLE_MENU_SMOKE_FAILED Invalid seed accepted"));FPlatformMisc::RequestExitWithStatus(true,1);return;}
        StartMenu->SeedInput->SetText(FText::FromString(TEXT("24680")));
        StartMenu->StartButton->OnClicked.Broadcast();
        if(PendingFloor!=1 || RunSeed!=24680) {UE_LOG(LogTemp,Error,TEXT("CASTLE_MENU_SMOKE_FAILED Valid seed rejected"));FPlatformMisc::RequestExitWithStatus(true,1);return;}
        bMenuSmokeStarted=true;
        UE_LOG(LogTemp,Display,TEXT("CASTLE_MENU_SMOKE_OK Seed=24680"));
    }
    if (PendingFloor && GetWorld()->GetTimeSeconds() >= PendingFloorAt)
    {
        Floor = PendingFloor; PendingFloor = 0; bReady = false;
        ClearGenerated();
        bBossDefeated=bWon=false; Kills=0; Region=TEXT("EXPLORATION");
        bExplorationGenerated=bCaveGenerated=bBossGenerated=false;
        bCaveRequested=bBossRequested=false;
        PCG->Seed=CastleStreamSeed(RunSeed,Floor,0x43a2f17bu);
        PCG->Generate(true);
    }
    if(!bReady && bExplorationGenerated && Floor>1)
    {
        if(!bCaveRequested)
        {
            bCaveRequested=true; CavePCG->Seed=CastleStreamSeed(RunSeed,Floor,0xcabed123u); CavePCG->Generate(true);
        }
        else if(bCaveGenerated && !bBossRequested)
        {
            bBossRequested=true; BossPCG->Seed=CastleStreamSeed(RunSeed,Floor,0xb055a12fu); BossPCG->Generate(true);
        }
    }
    if(bReady && Region==TEXT("EXPLORATION"))
    {
        if(auto* P=UGameplayStatics::GetPlayerPawn(this,0))
        {
            const FVector Pos=P->GetActorLocation();
            const int32 X=FMath::RoundToInt(Pos.X/2400.f),Y=FMath::RoundToInt(Pos.Y/2400.f);
            if(X>=0 && X<GridWidth && Y>=0 && Y<GridWidth)
            {
                const int32 Index=Y*GridWidth+X;
                if(Rooms.IsValidIndex(Index) && Rooms[Index].bActive && FVector::Dist2D(Pos,Rooms[Index].Center)<800)
                    RevealRoom(Index);
            }
        }
    }
    if (FParse::Param(FCommandLine::Get(),TEXT("CastleSmokeTest"))) TickSmokeTest();
    if (FParse::Param(FCommandLine::Get(),TEXT("CastleCapture"))) TickCapture();
}

void ACastleDirector::RequestFloor(int32 InFloor)
{
    if (PendingFloor) return;
    bReady = false; PendingFloor = FMath::Clamp(InFloor, 1, 3);
    PendingFloorAt = GetWorld()->GetTimeSeconds() + .15f;
    if (auto* P = Cast<ACastlePlayer>(UGameplayStatics::GetPlayerPawn(this, 0)))
    {
        P->GetCharacterMovement()->StopMovementImmediately();
        P->GetCharacterMovement()->DisableMovement();
        P->SetActorEnableCollision(false);
        P->Tell((CastleIsChinese()?FString::Printf(TEXT("正在生成第 %d 层……"), InFloor):FString::Printf(TEXT("Generating floor %d..."), InFloor)));
    }
}

void ACastleDirector::ClearGenerated()
{
    for (TActorIterator<ACastleBolt> It(GetWorld()); It; ++It) It->Destroy();
    for (AActor* Actor : GeneratedActors)
    {
        if (!IsValid(Actor)) continue;
        if (auto* Enemy = Cast<ACastleEnemy>(Actor)) if (auto* AI = Enemy->GetController()) { AI->UnPossess(); AI->Destroy(); }
        Actor->Destroy();
    }
    GeneratedActors.Empty();
    for (UInstancedStaticMeshComponent* Component : Geometry) if (Component) Component->DestroyComponent();
    Geometry.Empty(); Rooms.Empty(); FirstFloorRoute.Empty(); CaveRooms.Empty(); CaveRoute.Empty();
    BossPlatforms.Empty(); BossCoverPoints.Empty(); Boss=nullptr;
}

void ACastleDirector::AddBox(FVector Pos, FVector Size, int32 Material, bool Collision, FRotator Rotation)
{
    // Separate visual-only objects from navigation/collision geometry.
    const int32 Slot = int32(GeneratingRegion)*18 + Material + (Collision ? 0 : 9);
    while (Geometry.Num() <= Slot) Geometry.Add(nullptr);
    if (!Geometry[Slot])
    {
        auto* ISM = NewObject<UInstancedStaticMeshComponent>(this);
        AddInstanceComponent(ISM); ISM->SetupAttachment(GetRootComponent());
        ISM->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
        ISM->SetMaterial(0, Materials[Material]);
        ISM->SetCollisionProfileName(Collision ? TEXT("BlockAll") : TEXT("NoCollision"));
        ISM->SetCanEverAffectNavigation(Collision); ISM->RegisterComponent(); Geometry[Slot] = ISM;
    }
    Geometry[Slot]->AddInstance(FTransform(Rotation, Pos, Size / 100), true);
}

void ACastleDirector::BuildRoom(FVector C, float Half, int32 Doors, int32 Material)
{
    AddBox(C + FVector(0,0,-35), FVector(Half*2+100,Half*2+100,70), 0);
    // bit order: east, north, west, south; open passages are 480 cm wide.
    for (int32 Side = 0; Side < 4; ++Side)
    {
        const bool AlongX = Side == 0 || Side == 2;
        const float Sign = Side < 2 ? 1 : -1;
        const FVector Normal = AlongX ? FVector(Sign,0,0) : FVector(0,Sign,0);
        const FVector Tangent = AlongX ? FVector(0,1,0) : FVector(1,0,0);
        if (Doors & (1 << Side))
        {
            for (int32 S : {-1,1})
            {
                const float Length = Half - 240;
                AddBox(C + Normal*Half + Tangent*(S*(240+Length*.5f)) + FVector(0,0,240),
                    AlongX ? FVector(85,Length,480) : FVector(Length,85,480), Material);
            }
            AddBox(C + Normal*Half + FVector(0,0,445), AlongX ? FVector(90,480,70) : FVector(480,90,70), 1);
        }
        else AddBox(C + Normal*Half + FVector(0,0,240), AlongX ? FVector(85,Half*2,480) : FVector(Half*2,85,480), Material);
        AddBox(C + Normal*(Half-20) + FVector(0,0,390), AlongX ? FVector(50,Half*2,26) : FVector(Half*2,50,26), 1, false);
    }
    for (int32 X : {-1,1}) for (int32 Y : {-1,1})
    {
        AddBox(C+FVector(X*(Half-65),Y*(Half-65),260), FVector(135,135,520), 1);
        AddBox(C+FVector(X*(Half-65),Y*(Half-65),520), FVector(190,190,35), 3, false);
    }
    // Inlaid tiles give navigation cues without blocking the room.
    for (int32 I = -3; I <= 3; ++I)
    {
        AddBox(C+FVector(I*240,0,1), FVector(3,Half*1.8f,2), 1, false);
        AddBox(C+FVector(0,I*240,1), FVector(Half*1.8f,3,2), 1, false);
    }
    AddLight(C+FVector(0,0,420), FLinearColor(.95f,.68f,.38f));
}

void ACastleDirector::AddSign(FVector Pos, const FString& Value, FColor Color)
{
    auto* Sign = GetWorld()->SpawnActor<ATextRenderActor>(Pos, FRotator(0,180,0));
    Sign->GetTextRender()->SetText(FText::FromString(Value));
    Sign->GetTextRender()->SetHorizontalAlignment(EHTA_Center);
    Sign->GetTextRender()->SetWorldSize(36); Sign->GetTextRender()->SetTextRenderColor(Color);
    Sign->GetTextRender()->SetCollisionEnabled(ECollisionEnabled::NoCollision); TrackGenerated(Sign);
}

void ACastleDirector::AddLight(FVector Pos, FLinearColor Color)
{
    auto* Light = GetWorld()->SpawnActor<APointLight>(Pos, FRotator::ZeroRotator);
    Light->SetMobility(EComponentMobility::Movable);
    auto* Component = Light->PointLightComponent.Get();
    Component->SetIntensity(9000); Component->SetAttenuationRadius(1900); Component->SetLightColor(Color); Component->SetCastShadows(false);
    TrackGenerated(Light);
}

ACastleInteractable* ACastleDirector::AddInteract(FVector Pos, ECastleInteract Kind, const FString& Title, int32 Reward)
{
    FActorSpawnParameters P; P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    auto* Item = GetWorld()->SpawnActor<ACastleInteractable>(Pos, FRotator::ZeroRotator, P);
    Item->Configure(Kind, Title, this, Reward); TrackGenerated(Item); return Item;
}

ACastleEnemy* ACastleDirector::AddEnemy(FVector Pos, bool IsBoss)
{
    FActorSpawnParameters P; P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    auto* Enemy = GetWorld()->SpawnActor<ACastleEnemy>(Pos, FRotator::ZeroRotator, P);
    Enemy->Configure(IsBoss, Floor, this); TrackGenerated(Enemy); return Enemy;
}

void ACastleDirector::StartRun(int32 Seed)
{
    RunSeed=FMath::Max(1,Seed); bWon=false; Floor=1; CurrentRoomIndex=0;
    if(auto* P=Cast<ACastlePlayer>(UGameplayStatics::GetPlayerPawn(this,0))) P->ResetRun();
    UE_LOG(LogTemp,Display,TEXT("CASTLE_RUN_START Seed=%d"),RunSeed);
    RequestFloor(1);
}

void ACastleDirector::RevealRoom(int32 RoomIndex)
{
    if(!Rooms.IsValidIndex(RoomIndex) || !Rooms[RoomIndex].bActive)return;
    CurrentRoomIndex=RoomIndex;
    Rooms[RoomIndex].bVisited=true; Rooms[RoomIndex].bRevealed=true;
    const int32 DX[]={1,0,-1,0},DY[]={0,1,0,-1};
    for(int32 Side=0;Side<4;++Side) if(Rooms[RoomIndex].Doors&(1<<Side))
    {
        const int32 X=RoomIndex%GridWidth+DX[Side],Y=RoomIndex/GridWidth+DY[Side];
        if(X>=0 && X<GridWidth && Y>=0 && Y<GridWidth)
        {
            const int32 N=Y*GridWidth+X;
            if(Rooms.IsValidIndex(N) && Rooms[N].bActive) Rooms[N].bRevealed=true;
        }
    }
}

ACastleTrap* ACastleDirector::AddTrap(FVector Pos)
{
    FActorSpawnParameters Params; Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    auto* Trap = GetWorld()->SpawnActor<ACastleTrap>(Pos, FRotator::ZeroRotator, Params);
    TrackGenerated(Trap);
    return Trap;
}

void ACastleDirector::GenerateLayout(const UCastleLayoutSettings* Settings)
{
    ClearGeneratedRegion(ECastlePCGRegion::Exploration);
    Rooms.Empty(); FirstFloorRoute.Empty();
    GeneratingRegion=ECastlePCGRegion::Exploration;
    InitializeMaterials();
    FRandomStream R(CastleStreamSeed(RunSeed,Floor,0x43a2f17bu));
    GridWidth=4;
    Rooms.SetNum(GridWidth*GridWidth);
    for (int32 I=0;I<Rooms.Num();++I)
    {
        Rooms[I].Center=FVector((I%GridWidth)*2400,(I/GridWidth)*2400,0);
        Rooms[I].bActive=false;
        Rooms[I].bRevealed=Rooms[I].bVisited=false;
    }
    CastleGenerateFloorNodes(RunSeed,Floor,Rooms,FirstFloorRoute);
    RevealRoom(0);
    if(Floor>1)
        for(const auto& Room:Rooms) if(Room.bActive && Room.Type==ECastleRoom::Chest)
        {
            AddInteract(Room.Center+FVector(-520,-450,135),ECastleInteract::CaveEnter,TEXT("TREASURE CAVE"));
            break;
        }
    const TCHAR* Names[] = { TEXT("ENTRANCE"),TEXT("ARMORY / SHOP"),TEXT("SENTINEL HALL"),TEXT("RELIQUARY"),TEXT("OFFERING ALTAR"),TEXT("TREASURE PORTAL"),TEXT("REST ROOM"),TEXT("ELITE GUARD"),TEXT("TRAP ROOM"),TEXT("FLOOR EXIT") };
    for (int32 I=0; I<Rooms.Num(); ++I)
    {
        const auto& Room = Rooms[I]; const FVector C = Room.Center;
        if(!Room.bActive)continue;
        BuildRoom(C,900,Room.Doors,2);
        AddSign(C+FVector(790,0,330),Names[static_cast<int32>(Room.Type)],FColor(220,210,180));
        switch (Room.Type)
        {
        case ECastleRoom::Entrance:
            AddSign(C+FVector(700,-470,240),TEXT("THE HOLLOW CITADEL\nExplore / Upgrade / Ascend"),FColor(100,220,220));
            AddInteract(C+FVector(220,-400,45),ECastleInteract::Ammo,TEXT("STARTER SUPPLIES")); break;
        case ECastleRoom::Shop: AddInteract(C+FVector(250,0,45),ECastleInteract::Shop,TEXT("ARMORY")); break;
        case ECastleRoom::Combat:
            for(int32 E=0;E<2+Floor;++E) AddEnemy(C+FVector(R.FRandRange(-450,450),R.FRandRange(-450,450),100),false);
            AddBox(C+FVector(-430,-350,90),FVector(160,260,180),8); AddBox(C+FVector(430,350,90),FVector(160,260,180),8); break;
        case ECastleRoom::Chest:
            AddInteract(C+FVector(230,0,45),ECastleInteract::Chest,TEXT("RELIC CHEST"),R.RandRange(0,2));
            AddInteract(C+FVector(-250,250,45),ECastleInteract::Weapon,TEXT("REINFORCED CARBINE")); break;
        case ECastleRoom::Event: AddInteract(C+FVector(230,0,45),ECastleInteract::Event,TEXT("BLOOD OFFERING"),R.RandRange(0,2)); break;
        case ECastleRoom::Cave: AddInteract(C+FVector(400,0,135),ECastleInteract::CaveEnter,TEXT("TREASURE CAVE")); break;
        case ECastleRoom::Rest: AddInteract(C+FVector(230,0,45),ECastleInteract::Rest,TEXT("FIELD CAMP")); break;
        case ECastleRoom::Elite:
        {
            auto* Elite = AddEnemy(C+FVector(0,0,100),false);
            Elite->bElite = true; Elite->MaxHealth *= 1.8f; Elite->Health = Elite->MaxHealth; Elite->AttackDamage *= 1.25f;
            Elite->Label->SetText(FText::FromString(TEXT("ELITE SENTINEL")));
            AddBox(C+FVector(-350,300,100),FVector(180,220,200),8);
            AddBox(C+FVector(350,-300,100),FVector(180,220,200),8);
            break;
        }
        case ECastleRoom::Trap:
            for (int32 X : {-230,230})
            {
                AddBox(C+FVector(X,0,3),FVector(140,1000,6),5,false);
                AddTrap(C+FVector(X,0,110));
            }
            AddSign(C+FVector(-600,500,190),TEXT("RED STRIPES HURT - GO AROUND"),FColor(255,100,140));
            AddInteract(C+FVector(580,450,45),ECastleInteract::Chest,TEXT("TRAP REWARD"),R.RandRange(0,2));
            break;
        case ECastleRoom::Exit:
        {
            // Keep the room center open for navigation, including single-door corner exits.
            const FVector AwayFromDoor = (Room.Doors & 1) ? FVector(-520,0,0)
                : (Room.Doors & 2) ? FVector(0,-520,0)
                : (Room.Doors & 4) ? FVector(520,0,0) : FVector(0,520,0);
            AddInteract(C+AwayFromDoor+FVector(0,0,135),Floor==1?ECastleInteract::NextFloor:ECastleInteract::BossEnter,Floor==1?TEXT("ASCEND TO FLOOR 2"):TEXT("BOSS GATE"));
            break;
        }
        }
        // Each corridor is built exactly once and aligns with the matching openings.
        for(int32 S=0; S<2; ++S) if(Room.Doors&(1<<S))
        {
            const FVector Mid=C+(S==0?FVector(1200,0,0):FVector(0,1200,0));
            AddBox(Mid+FVector(0,0,-35),S==0?FVector(700,560,70):FVector(560,700,70),0);
            for(int32 Sign : {-1,1}) AddBox(Mid+(S==0?FVector(0,Sign*280,240):FVector(Sign*280,0,240)),S==0?FVector(700,65,480):FVector(65,700,480),2);
            AddBox(Mid+FVector(0,0,2),S==0?FVector(600,26,3):FVector(26,600,3),4,false);
        }
    }
    if(Floor==1)
    {
        uint32 ContentHash=2166136261u;
        for(AActor* Actor:GeneratedActors) if(auto* Item=Cast<ACastleInteractable>(Actor))
        {
            ContentHash=(ContentHash^uint32(Item->Kind))*16777619u;
            ContentHash=(ContentHash^uint32(Item->RewardKind))*16777619u;
            ContentHash=(ContentHash^uint32(FMath::RoundToInt(Item->GetActorLocation().X)))*16777619u;
            ContentHash=(ContentHash^uint32(FMath::RoundToInt(Item->GetActorLocation().Y)))*16777619u;
        }
        UE_LOG(LogTemp,Display,TEXT("CASTLE_FIRST_FLOOR_CONTENT Hash=%u"),ContentHash);
    }
    bExplorationGenerated=true;
    if(Floor==1 && !bReady) CompleteFloorGeneration();
}

void ACastleDirector::InitializeMaterials()
{
    if (Materials.IsEmpty())
    {
        for (const TCHAR* Name : {TEXT("M_Floor"),TEXT("M_Trim"),TEXT("M_Stone"),TEXT("M_Gold"),TEXT("M_Teal"),TEXT("M_Purple"),TEXT("M_Cave"),TEXT("M_Portal"),TEXT("M_Metal")})
            Materials.Add(LoadObject<UMaterialInterface>(nullptr, *FString::Printf(TEXT("/Game/Demo/Materials/%s.%s"),Name,Name)));
    }
}

void ACastleDirector::CompleteFloorGeneration()
{
    bReady=true;
    if(auto* P=Cast<ACastlePlayer>(UGameplayStatics::GetPlayerPawn(this,0)))
    {
        P->SetActorEnableCollision(true); Travel(P,FVector(-400,0,110));
        P->Shield=P->MaxShield; P->Health=FMath::Min(P->MaxHealth,P->Health+35); P->Reserve+=48;
        P->Tell(Floor==1 ? TEXT("Floor 1 | Fight, explore side rooms, then find the ASCEND portal.")
                         : (CastleIsChinese()?FString::Printf(TEXT("第 %d 层 | 探索房间、收集藏品，然后寻找首领入口。"),Floor):FString::Printf(TEXT("Floor %d | Explore rooms, collect relics, then find the BOSS GATE."),Floor)));
    }
    if(auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld())) Nav->Build();
}

FString ACastleDirector::BossName() const
{
    const TCHAR* Names[]={TEXT("THE IRON WARDEN"),TEXT("THE ASH REGENT"),TEXT("THE HOLLOW KING")}; return Names[FMath::Clamp(Floor-1,0,2)];
}

void ACastleDirector::Travel(ACastlePlayer* P,FVector Pos,float Yaw)
{
    P->GetCharacterMovement()->StopMovementImmediately(); P->SetActorLocation(Pos,false,nullptr,ETeleportType::TeleportPhysics);
    P->SetActorRotation(FRotator(0,Yaw,0)); if(auto* Controller=P->GetController()) Controller->SetControlRotation(FRotator(0,Yaw,0));
    // Disabling collision during generation makes CharacterMovement enter MOVE_None.
    // Enabling collision alone does not restore its locomotion mode.
    if(bReady && !P->bDead) P->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
}
void ACastleDirector::EnterBoss(ACastlePlayer* P)
{
    if(!bReady || !Boss || bBossDefeated) return;
    Region=TEXT("BOSS ARENA"); Travel(P,BossCenter+FVector(-1700,0,110)); Boss->bActive=true;
    P->Tell(TEXT("Boss arena sealed. Dodge bolts, use cover, leave red telegraphs."));
}
void ACastleDirector::EnterCave(ACastlePlayer* P,const FVector& From)
{
    if(!bReady || CaveRoute.Num()!=4) return;
    ReturnLocation=From+FVector(-240,0,0); ReturnLocation.Z=110;
    Region=TEXT("TREASURE CAVE");
    const float EntryYaw=(CaveRooms[CaveRoute[1]].Center-CaveEntrance).Rotation().Yaw;
    Travel(P,CaveEntrance+FVector(0,0,110),EntryYaw);
    P->Tell(TEXT("Vault: collect relics along the four-room path, then leave through the exit portal."));
}
void ACastleDirector::LeaveCave(ACastlePlayer* P) { Region=TEXT("EXPLORATION"); Travel(P,ReturnLocation,180); }
void ACastleDirector::EnemyKilled(ACastleEnemy* E)
{
    ++Kills;
    if(auto* P=Cast<ACastlePlayer>(UGameplayStatics::GetPlayerPawn(this,0))) P->Coins+=E->bBoss?80:(E->bElite?30:12);
    if(E->bBoss)
    {
        bBossDefeated=true; Boss=nullptr;
        AddInteract(BossCenter+FVector(0,0,135),ECastleInteract::NextFloor,Floor==3?TEXT("CLAIM THE CITADEL"):TEXT("ASCEND"));
        if(auto* P=Cast<ACastlePlayer>(UGameplayStatics::GetPlayerPawn(this,0))) P->Tell(TEXT("BOSS DEFEATED | Use the portal at the arena center."));
    }
    else if (E->bElite)
        AddInteract(FVector(E->GetActorLocation().X,E->GetActorLocation().Y,35),ECastleInteract::Chest,TEXT("ELITE REWARD"),RunSeed%3);
    else AddInteract(FVector(E->GetActorLocation().X,E->GetActorLocation().Y,35),ECastleInteract::Ammo,TEXT("SUPPLIES"));
}
void ACastleDirector::NewRun()
{
    StartRun(FMath::RandRange(1,1000000000));
}
void ACastleDirector::RetrySameSeed() { StartRun(RunSeed); }
