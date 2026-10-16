#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/HUD.h"
#include "GameFramework/PlayerController.h"
#include "Blueprint/UserWidget.h"
#include "AIController.h"
#include "PCGSettings.h"
#include "Commandlets/Commandlet.h"
#include "CastleDemo.generated.h"

class UCameraComponent;
class UStaticMeshComponent;
class UInstancedStaticMeshComponent;
class UPCGComponent;
class UMaterialInterface;
class UTextRenderComponent;
class ACastleDirector;
class UCastleStartMenu;
class UEditableTextBox;
class UButton;
class UTextBlock;

UENUM(BlueprintType)
enum class ECastleRoom : uint8 { Entrance, Shop, Combat, Chest, Event, Cave, Rest, Elite, Trap, Exit };
UENUM(BlueprintType)
enum class ECastleInteract : uint8 { Chest, Shop, Event, CaveEnter, CaveReturn, BossEnter, NextFloor, Ammo, Weapon, Rest };
UENUM(BlueprintType)
enum class ECastlePCGRegion : uint8 { Exploration, BossArena, TreasureCave };

USTRUCT(BlueprintType, meta=(ScriptName="CastleRoomData"))
struct FCastleRoom
{
    GENERATED_BODY()
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) FVector Center = FVector::ZeroVector;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) ECastleRoom Type = ECastleRoom::Entrance;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) int32 Doors = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) int32 RouteOrder = -1;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) bool bActive = true;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) bool bRevealed = false;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) bool bVisited = false;
};

UCLASS()
class CASTLEDEMO_API ACastlePlayerController : public APlayerController
{
    GENERATED_BODY()
public:
    ACastlePlayerController();
    virtual void BeginPlay() override;
};

UCLASS()
class CASTLEDEMO_API ACastlePlayer : public ACharacter
{
    GENERATED_BODY()
public:
    ACastlePlayer();
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    virtual void SetupPlayerInputComponent(UInputComponent* Input) override;
    virtual float TakeDamage(float Damage, FDamageEvent const&, AController*, AActor*) override;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<UCameraComponent> Camera;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) float Health = 100;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) float MaxHealth = 100;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) float Shield = 60;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) float MaxShield = 60;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) float BulletDamage = 22;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) int32 Ammo = 24;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) int32 MagazineSize = 24;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) int32 Reserve = 144;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) int32 Coins = 35;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) int32 Relics = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) int32 WeaponLevel = 1;
    bool bReloading = false, bDead = false, bHelp = true;
    float HitMarkerUntil = 0, HurtUntil = 0, ReloadEnd = 0;
    FString Notice;
    float NoticeUntil = 0;
    ACastleDirector* Director() const;
    class ACastleInteractable* Focused() const;
    void Tell(const FString& Message);
    void GrantRelic(int32 Kind);
    void ResetRun();
private:
    friend class ACastleDirector;
    UPROPERTY() TObjectPtr<UStaticMeshComponent> Gun;
    UPROPERTY() TObjectPtr<UStaticMeshComponent> Muzzle;
    bool bFiring = false;
    float NextShot = 0, LastDamage = -100;
    void MoveForward(float Value);
    void MoveRight(float Value);
    void Turn(float Value);
    void Look(float Value);
    void FirePressed();
    void FireReleased();
    void Fire();
    void Reload();
    void Interact();
    void Restart();
    void RestartSame();
    void Pause();
    void Help();
    void ToggleLanguage();
};

UCLASS()
class CASTLEDEMO_API ACastleInteractable : public AActor
{
    GENERATED_BODY()
public:
    ACastleInteractable();
    virtual void Tick(float DeltaSeconds) override;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Mesh;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UTextRenderComponent> Label;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) ECastleInteract Kind = ECastleInteract::Chest;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FString Title;
    UPROPERTY() TObjectPtr<ACastleDirector> Director;
    bool bUsed = false;
    int32 RewardKind = 0;
    void Configure(ECastleInteract InKind, const FString& InTitle, ACastleDirector* InDirector, int32 InReward = 0);
    FString Prompt(const ACastlePlayer* Player) const;
    void Use(ACastlePlayer* Player);
};

UCLASS()
class CASTLEDEMO_API ACastleAIController : public AAIController
{
    GENERATED_BODY()
public:
    ACastleAIController();
    virtual void OnPossess(APawn* InPawn) override;
    virtual void OnUnPossess() override;
    bool StartCastleBehavior();
    void UpdateCastleBlackboard();
    UPROPERTY(EditDefaultsOnly, Category="Castle|AI") TSoftObjectPtr<class UBehaviorTree> GuardBehavior;
    UPROPERTY(EditDefaultsOnly, Category="Castle|AI") TSoftObjectPtr<class UBehaviorTree> BossBehavior;
};

UCLASS()
class CASTLEDEMO_API ACastleEnemy : public ACharacter
{
    GENERATED_BODY()
public:
    ACastleEnemy();
    virtual void Tick(float DeltaSeconds) override;
    virtual float TakeDamage(float Damage, FDamageEvent const&, AController*, AActor*) override;
    void Configure(bool InBoss, int32 Floor, ACastleDirector* InDirector);
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) bool bBoss = false;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) bool bElite = false;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) float Health = 75;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) float MaxHealth = 75;
    UPROPERTY() TObjectPtr<ACastleDirector> Director;
    UPROPERTY() TObjectPtr<UStaticMeshComponent> Body;
    UPROPERTY() TObjectPtr<UTextRenderComponent> Label;
    UPROPERTY() TObjectPtr<UStaticMeshComponent> Telegraph;
    bool bActive = true, bDead = false;
    FVector Home, BlastCenter;
    float AttackDamage = 10;
    // Ability execution is reused by BT tasks; decisions no longer run in Character::Tick.
    bool CanRunAI(ACastlePlayer* Player) const;
    bool IsInDecisionRange(ACastlePlayer* Player) const;
    void ResolvePendingBlast(ACastlePlayer* Player);
    void UpdatePursuit(ACastlePlayer* Player, bool bCanSee);
    void UpdateAttack(ACastlePlayer* Player, bool bCanSee);
    void UpdateGroundSlam(ACastlePlayer* Player);
    bool IsChargingBlast() const { return bCharging; }
    int32 AIPursuitTicks = 0, AIAttackTicks = 0, AISlamTicks = 0;
    int32 AIAttackEvents = 0, AIBlastEvents = 0, AILastVolley = 0;
private:
    float NextThink = 0, NextAttack = 0, NextBlast = 0, BlastTime = 0;
    bool bCharging = false;
    void ShootAt(ACastlePlayer* Player, int32 Count);
};

UCLASS()
class CASTLEDEMO_API ACastleTrap : public AActor
{
    GENERATED_BODY()
public:
    ACastleTrap();
    UPROPERTY(VisibleAnywhere) TObjectPtr<class UBoxComponent> Trigger;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) bool bTriggered = false;
    virtual void NotifyActorBeginOverlap(AActor* OtherActor) override;
};

UCLASS()
class CASTLEDEMO_API ACastleBolt : public AActor
{
    GENERATED_BODY()
public:
    ACastleBolt();
    virtual void Tick(float DeltaSeconds) override;
    UPROPERTY() TObjectPtr<UStaticMeshComponent> Mesh;
    UPROPERTY() TObjectPtr<AActor> Shooter;
    FVector Velocity;
    float Damage = 10;
};

UCLASS(BlueprintType)
class CASTLEDEMO_API UCastleLayoutSettings : public UPCGSettings
{
    GENERATED_BODY()
public:
#if WITH_EDITOR
    virtual FName GetDefaultNodeName() const override { return TEXT("CastleLayout"); }
    virtual FText GetDefaultNodeTitle() const override { return FText::FromString(GeneratorRegion==ECastlePCGRegion::Exploration?TEXT("Castle | Exploration rooms"):GeneratorRegion==ECastlePCGRegion::TreasureCave?TEXT("Castle | Four-room vault"):TEXT("Castle | Multilevel boss arena")); }
    virtual EPCGSettingsType GetType() const override { return EPCGSettingsType::Spawner; }
#endif
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Castle") ECastlePCGRegion GeneratorRegion = ECastlePCGRegion::Exploration;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Castle", meta=(ClampMin="0", ClampMax="8")) int32 ExtraConnections = 2;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Castle", meta=(ClampMin="0", ClampMax="30")) int32 ArenaCoverCount = 10;
protected:
    virtual TArray<FPCGPinProperties> InputPinProperties() const override;
    virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
    virtual FPCGElementPtr CreateElement() const override;
};

UCLASS()
class CASTLEDEMO_API ACastleDirector : public AActor
{
    GENERATED_BODY()
public:
    ACastleDirector();
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<UPCGComponent> PCG;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<UPCGComponent> CavePCG;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<UPCGComponent> BossPCG;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Generation") int32 RunSeed = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) int32 Floor = 1;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TArray<FCastleRoom> Rooms;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TArray<int32> FirstFloorRoute;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TArray<FCastleRoom> CaveRooms;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TArray<int32> CaveRoute;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TArray<FVector> BossPlatforms;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TArray<FVector> BossCoverPoints;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) FVector CaveEntrance;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) FVector CaveExit;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) int32 GridWidth = 4;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) int32 CurrentRoomIndex = 0;
    UPROPERTY() TObjectPtr<UCastleStartMenu> StartMenu;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) bool bBossDefeated = false;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) bool bWon = false;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) bool bReady = false;
    UPROPERTY() TObjectPtr<ACastleEnemy> Boss;
    UPROPERTY() TArray<TObjectPtr<AActor>> GeneratedActors;
    UPROPERTY() TArray<TObjectPtr<UInstancedStaticMeshComponent>> Geometry;
    UPROPERTY() TArray<TObjectPtr<UMaterialInterface>> Materials;
    FVector BossCenter = FVector(11200, 3600, 0);
    FVector CaveCenter = FVector(0, 10000, 0);
    FVector ReturnLocation;
    int32 Kills = 0;
    FString Region = TEXT("EXPLORATION");
    void GenerateLayout(const UCastleLayoutSettings* Settings);
    void GenerateCave(const UCastleLayoutSettings* Settings);
    void GenerateBossArena(const UCastleLayoutSettings* Settings);
    void RequestFloor(int32 InFloor);
    void StartRun(int32 Seed);
    void NewRun();
    void RetrySameSeed();
    void RevealRoom(int32 RoomIndex);
    void EnterBoss(ACastlePlayer* Player);
    void EnterCave(ACastlePlayer* Player, const FVector& From);
    void LeaveCave(ACastlePlayer* Player);
    void EnemyKilled(ACastleEnemy* Enemy);
    void Travel(ACastlePlayer* Player, FVector Destination, float Yaw = 0);
    static class UPCGGraph* MakeGraph(UObject* Outer, ECastlePCGRegion GeneratorRegion = ECastlePCGRegion::Exploration);
    FString BossName() const;
    ACastleInteractable* AddInteract(FVector Pos, ECastleInteract Kind, const FString& Title, int32 Reward = 0);
private:
    void ClearGenerated();
    void ClearGeneratedRegion(ECastlePCGRegion InRegion);
    void CompleteFloorGeneration();
    void InitializeMaterials();
    void TrackGenerated(AActor* Actor);
    ECastlePCGRegion GeneratingRegion = ECastlePCGRegion::Exploration;
    bool bExplorationGenerated = false, bCaveGenerated = false, bBossGenerated = false;
    bool bCaveRequested = false, bBossRequested = false;
    void AddBox(FVector Pos, FVector Size, int32 Material, bool Collision = true, FRotator Rotation = FRotator::ZeroRotator);
    void BuildRoom(FVector Center, float HalfSize, int32 Doors, int32 Material);
    void AddSign(FVector Pos, const FString& Text, FColor Color);
    void AddLight(FVector Pos, FLinearColor Color);
    ACastleEnemy* AddEnemy(FVector Pos, bool IsBoss);
    ACastleTrap* AddTrap(FVector Pos);
    float PendingFloorAt = 0;
    int32 PendingFloor = 0;
    void TickSmokeTest();
    void TickAISmokeTest();
    void TickAICapture();
    int32 SmokeFloor = 0;
    float SmokeWaitStart = 0;
    float SmokeDamageBeforeTravel = 0;
    void TickCapture();
    int32 CaptureStage = 0;
    float NextCaptureAt = 0;
    FString PendingCaptureName;
    int32 SmokeMovementStep = 0;
    float SmokeMovementAt = 0;
    FVector SmokeMovementStart;
    bool bMenuSmokeStarted = false;
    bool bSmokeRetried = false;
    float MenuCaptureAt = 0;
    bool bMenuCaptureRequested = false;
};

UCLASS()
class CASTLEDEMO_API UCastleStartMenu : public UUserWidget
{
    GENERATED_BODY()
public:
    UPROPERTY() TObjectPtr<ACastleDirector> Director;
    UPROPERTY() TObjectPtr<UEditableTextBox> SeedInput;
    UPROPERTY() TObjectPtr<UButton> StartButton;
    UPROPERTY() TObjectPtr<UButton> RandomButton;
    UPROPERTY() TObjectPtr<UTextBlock> Message;
    UPROPERTY() TObjectPtr<UButton> LanguageButton;
    UPROPERTY() TArray<TObjectPtr<UTextBlock>> MenuLabels;
    TArray<FString> MenuLabelSources;
    FString MessageSource;
    void RefreshLanguage();
    virtual void NativeOnInitialized() override;
    virtual void NativeConstruct() override;
    static bool ParseSeed(const FString& Text, int32& OutSeed);
private:
    UFUNCTION() void ChooseRandom();
    UFUNCTION() void StartGame();
    UFUNCTION() void ToggleLanguage();
};

UCLASS()
class CASTLEDEMO_API ACastleHUD : public AHUD
{
    GENERATED_BODY()
public:
    virtual void DrawHUD() override;
private:
    UPROPERTY() TObjectPtr<class UFont> LanguageFont;
    void Text(const FString& Value, float X, float Y, FLinearColor Color, float Scale = 1);
    void Bar(float X, float Y, float W, float H, float Value, float Max, FLinearColor Color);
};

UCLASS()
class CASTLEDEMO_API ACastleGameMode : public AGameModeBase
{
    GENERATED_BODY()
public:
    ACastleGameMode();
    virtual void BeginPlay() override;
};

// Rebuilds the editable map/materials/PCG graph without clicking through the editor.
UCLASS()
class CASTLEDEMO_API UCastleSetupCommandlet : public UCommandlet
{
    GENERATED_BODY()
public:
    UCastleSetupCommandlet();
    virtual int32 Main(const FString& Params) override;
};
