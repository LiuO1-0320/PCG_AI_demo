#include "CastleDemo.h"
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

void CastleGenerateFirstFloorNodes(int32 RunSeed,TArray<FCastleRoom>& Rooms,TArray<int32>& Route);

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
        D->GenerateLayout(Context->GetInputSettings<UCastleLayoutSettings>());
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
        for (const auto& R : D->Rooms) if(R.bActive) {AddPoint(R.Center, static_cast<int32>(R.Type), 0, R.Doors, R.RouteOrder);++ActiveRooms;}
        if (D->Floor != 1)
        {
            AddPoint(D->BossCenter, -1, 1, 0, -1); AddPoint(D->CaveCenter, -1, 2, 0, -1);
        }
        FPCGTaggedData& Output = Context->OutputData.TaggedData.Emplace_GetRef();
        Output.Pin = TEXT("Out"); Output.Data = Points; Output.Tags.Add(TEXT("CastleLayout"));
        UE_LOG(LogTemp, Display, TEXT("CASTLE_PCG_READY Floor=%d Seed=%d Rooms=%d"), D->Floor, D->RunSeed, ActiveRooms);
        return true;
    }
};

TArray<FPCGPinProperties> UCastleLayoutSettings::InputPinProperties() const { return {}; }
TArray<FPCGPinProperties> UCastleLayoutSettings::OutputPinProperties() const { return { FPCGPinProperties(TEXT("Out"), EPCGDataType::Point) }; }
FPCGElementPtr UCastleLayoutSettings::CreateElement() const { return MakeShared<FCastleLayoutElement>(); }

UPCGGraph* ACastleDirector::MakeGraph(UObject* Outer)
{
    auto* Graph = NewObject<UPCGGraph>(Outer);
    UCastleLayoutSettings* Settings = nullptr;
    auto* Node = Graph->AddNodeOfType(Settings);
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
}

void ACastleDirector::BeginPlay()
{
    Super::BeginPlay();
    bReady=false;
    auto* Graph = LoadObject<UPCGGraph>(nullptr, TEXT("/Game/Demo/PCG_Castle.PCG_Castle"));
    PCG->SetGraph(Graph ? Graph : MakeGraph(this));
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
        PCG->Seed = int32(uint32(RunSeed) + uint32(Floor)*1009u);
        PCG->Generate(true);
    }
    if(bReady && Floor==1 && Region==TEXT("EXPLORATION"))
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
        P->Tell(FString::Printf(TEXT("Generating floor %d..."), InFloor));
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
    Geometry.Empty(); Rooms.Empty(); FirstFloorRoute.Empty(); Boss = nullptr;
}

void ACastleDirector::AddBox(FVector Pos, FVector Size, int32 Material, bool Collision)
{
    // Separate visual-only objects from navigation/collision geometry.
    const int32 Slot = Material + (Collision ? 0 : 9);
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
    Geometry[Slot]->AddInstance(FTransform(FQuat::Identity, Pos, Size / 100), true);
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
    Sign->GetTextRender()->SetCollisionEnabled(ECollisionEnabled::NoCollision); GeneratedActors.Add(Sign);
}

void ACastleDirector::AddLight(FVector Pos, FLinearColor Color)
{
    auto* Light = GetWorld()->SpawnActor<APointLight>(Pos, FRotator::ZeroRotator);
    Light->SetMobility(EComponentMobility::Movable);
    auto* Component = Light->PointLightComponent.Get();
    Component->SetIntensity(9000); Component->SetAttenuationRadius(1900); Component->SetLightColor(Color); Component->SetCastShadows(false);
    GeneratedActors.Add(Light);
}

ACastleInteractable* ACastleDirector::AddInteract(FVector Pos, ECastleInteract Kind, const FString& Title, int32 Reward)
{
    FActorSpawnParameters P; P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    auto* Item = GetWorld()->SpawnActor<ACastleInteractable>(Pos, FRotator::ZeroRotator, P);
    Item->Configure(Kind, Title, this, Reward); GeneratedActors.Add(Item); return Item;
}

ACastleEnemy* ACastleDirector::AddEnemy(FVector Pos, bool IsBoss)
{
    FActorSpawnParameters P; P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    auto* Enemy = GetWorld()->SpawnActor<ACastleEnemy>(Pos, FRotator::ZeroRotator, P);
    Enemy->Configure(IsBoss, Floor, this); GeneratedActors.Add(Enemy); return Enemy;
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
    GeneratedActors.Add(Trap);
    return Trap;
}

void ACastleDirector::GenerateLayout(const UCastleLayoutSettings* Settings)
{
    ClearGenerated(); bBossDefeated = bWon = false; Kills = 0; Region = TEXT("EXPLORATION");
    if (Materials.IsEmpty())
    {
        for (const TCHAR* Name : {TEXT("M_Floor"),TEXT("M_Trim"),TEXT("M_Stone"),TEXT("M_Gold"),TEXT("M_Teal"),TEXT("M_Purple"),TEXT("M_Cave"),TEXT("M_Portal"),TEXT("M_Metal")})
            Materials.Add(LoadObject<UMaterialInterface>(nullptr, *FString::Printf(TEXT("/Game/Demo/Materials/%s.%s"),Name,Name)));
    }
    FRandomStream R(Floor==1 ? CastleStreamSeed(RunSeed,Floor,0x43a2f17bu) : int32(uint32(RunSeed)+uint32(Floor)*1009u));
    GridWidth=Floor==1?4:3;
    Rooms.SetNum(GridWidth*GridWidth);
    for (int32 I=0;I<Rooms.Num();++I)
    {
        Rooms[I].Center=FVector((I%GridWidth)*2400,(I/GridWidth)*2400,0);
        Rooms[I].bActive=Floor!=1;
        Rooms[I].bRevealed=Rooms[I].bVisited=false;
    }
    const int32 DX[] = {1,0,-1,0}, DY[] = {0,1,0,-1};
    auto Neighbor = [&](int32 I, int32 Side)
    {
        const int32 X = I%GridWidth+DX[Side], Y = I/GridWidth+DY[Side];
        return X>=0 && X<GridWidth && Y>=0 && Y<GridWidth ? Y*GridWidth+X : -1;
    };
    auto Connect = [&](int32 A, int32 Side, int32 B) { Rooms[A].Doors |= 1<<Side; Rooms[B].Doors |= 1<<((Side+2)%4); };
    if (Floor == 1)
    {
        CastleGenerateFirstFloorNodes(RunSeed,Rooms,FirstFloorRoute);
        RevealRoom(0);
    }
    else
    {
        // Keep the existing exploration layout for later floors while floor one is developed.
        TArray<int32> Stack{0}; TSet<int32> Visited{0};
        while (!Stack.IsEmpty())
        {
            const int32 Current = Stack.Last(); TArray<int32> Options;
            for (int32 S=0; S<4; ++S) { int32 N=Neighbor(Current,S); if(N>=0 && !Visited.Contains(N)) Options.Add(S); }
            if (Options.IsEmpty()) { Stack.Pop(); continue; }
            const int32 S = Options[R.RandRange(0,Options.Num()-1)], N = Neighbor(Current,S);
            Connect(Current,S,N); Visited.Add(N); Stack.Add(N);
        }
        TArray<FIntPoint> Missing;
        for (int32 I=0; I<9; ++I) for(int32 S=0;S<2;++S) if(Neighbor(I,S)>=0 && !(Rooms[I].Doors&(1<<S))) Missing.Add(FIntPoint(I,S));
        const int32 Extra = Settings ? Settings->ExtraConnections : 2;
        for(int32 I=0; I<Extra && !Missing.IsEmpty(); ++I) { int32 Pick=R.RandRange(0,Missing.Num()-1); auto E=Missing[Pick]; Connect(E.X,E.Y,Neighbor(E.X,E.Y)); Missing.RemoveAt(Pick); }
        TArray<ECastleRoom> Types{ECastleRoom::Shop, ECastleRoom::Combat, ECastleRoom::Combat, ECastleRoom::Combat, ECastleRoom::Chest, ECastleRoom::Chest, ECastleRoom::Event, ECastleRoom::Cave};
        for(int32 I=Types.Num()-1;I>0;--I) Types.Swap(I,R.RandRange(0,I));
        for(int32 I=1;I<9;++I) Rooms[I].Type=Types[I-1];
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
            AddInteract(C+AwayFromDoor+FVector(0,0,135),ECastleInteract::NextFloor,TEXT("ASCEND TO FLOOR 2"));
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
    if (Floor != 1)
    {
        // Existing later-floor boss and cave flow stays separate from the first-floor route.
        int32 Dist[9]; for(int32& V:Dist)V=-1; Dist[0]=0; TArray<int32> Queue{0}; int32 Farthest=0;
        for(int32 Head=0;Head<Queue.Num();++Head) for(int32 S=0;S<4;++S) if(Rooms[Queue[Head]].Doors&(1<<S))
        { int32 N=Neighbor(Queue[Head],S); if(N>=0 && Dist[N]<0) { Dist[N]=Dist[Queue[Head]]+1; Queue.Add(N); if(Dist[N]>Dist[Farthest])Farthest=N; } }
        AddInteract(Rooms[Farthest].Center+FVector(-450,-450,135),ECastleInteract::BossEnter,TEXT("BOSS GATE"));

        BuildRoom(BossCenter,R.FRandRange(2000,2300),0,2);
        AddSign(BossCenter+FVector(1950,0,380),BossName(),FColor(220,170,255));
        const int32 CoverCount = Settings ? Settings->ArenaCoverCount : 10;
        for(int32 I=0;I<CoverCount;++I)
        {
            const float Angle=2*PI*I/FMath::Max(1,CoverCount)+R.FRandRange(-.1f,.1f);
            const float Radius=R.FRandRange(850,1450);
            const FVector Pos=BossCenter+FVector(FMath::Cos(Angle)*Radius,FMath::Sin(Angle)*Radius,110);
            AddBox(Pos,FVector(R.FRandRange(130,240),R.FRandRange(200,330),220),8);
            AddBox(Pos+FVector(0,0,115),FVector(180,260,10),5,false);
        }
        Boss = AddEnemy(BossCenter+FVector(400,0,180),true);
        AddInteract(BossCenter+FVector(-1500,300,45),ECastleInteract::Ammo,TEXT("ARENA SUPPLIES"));

        BuildRoom(CaveCenter,R.FRandRange(1600,1900),0,6);
        AddLight(CaveCenter+FVector(0,0,400),FLinearColor(.25f,.5f,1));
        AddSign(CaveCenter+FVector(1600,0,350),TEXT("THE SUNKEN VAULT"),FColor(120,210,255));
        for(int32 I=0;I<12;++I)
        {
            const float Angle=2*PI*I/12, Radius=R.FRandRange(1050,1400);
            AddBox(CaveCenter+FVector(FMath::Cos(Angle)*Radius,FMath::Sin(Angle)*Radius,160),FVector(R.FRandRange(150,270),R.FRandRange(140,250),R.FRandRange(280,520)),6);
        }
        for(int32 I=0;I<3;++I) AddInteract(CaveCenter+FVector(100+I*300+R.FRandRange(-60,60),(I-1)*350+R.FRandRange(-70,70),45),ECastleInteract::Chest,TEXT("VAULT RELIC"),R.RandRange(0,2));
        for(int32 I=0;I<2+Floor;++I) AddEnemy(CaveCenter+FVector(R.FRandRange(450,800),R.FRandRange(-800,800),100),false);
        AddInteract(CaveCenter+FVector(-1150,-400,135),ECastleInteract::CaveReturn,TEXT("RETURN PORTAL"));
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
    bReady=true;
    if(auto* P=Cast<ACastlePlayer>(UGameplayStatics::GetPlayerPawn(this,0)))
    {
        P->SetActorEnableCollision(true); Travel(P,FVector(-400,0,110));
        P->Shield=P->MaxShield; P->Health=FMath::Min(P->MaxHealth,P->Health+35); P->Reserve+=48;
        P->Tell(Floor==1 ? TEXT("Floor 1 | Fight, explore side rooms, then find the ASCEND portal.")
                         : FString::Printf(TEXT("Floor %d | Explore rooms, collect relics, then find the BOSS GATE."),Floor));
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
    if(!Boss || bBossDefeated) return;
    Region=TEXT("BOSS ARENA"); Travel(P,BossCenter+FVector(-1700,0,110)); Boss->bActive=true;
    P->Tell(TEXT("Boss arena sealed. Dodge bolts, use cover, leave red telegraphs."));
}
void ACastleDirector::EnterCave(ACastlePlayer* P,const FVector& From)
{
    ReturnLocation=From+FVector(-240,0,0); ReturnLocation.Z=110;
    Region=TEXT("TREASURE CAVE"); Travel(P,CaveCenter+FVector(-1000,0,110)); P->Tell(TEXT("Optional vault: rare relics guarded by sentinels. Return through the blue portal."));
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
