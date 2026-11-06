#pragma once
#include "CoreMinimal.h"
#include "RogueliteRuntimeLoopAssembly.h"
#include "GameFramework/Character.h"
#include "NavigationSystem.h"
#include "RogueliteDynamicBridgeTestRig.generated.h"

class UBoxComponent;

/** Map-local configuration of the existing navigation system, not a new navigation system. */
UCLASS()
class ROGUELITEPROTOTYPE_API URogueliteDynamicBridgeNavigationConfig : public UNavigationSystemModuleConfig
{
    GENERATED_BODY()
public:
    URogueliteDynamicBridgeNavigationConfig();
    virtual UNavigationSystemBase* CreateAndConfigureNavigationSystem(UWorld& World) const override;
};

/** Safety-test character only: no weapons, health, encounter or enemy behaviour. */
UCLASS()
class ROGUELITEPROTOTYPE_API ARogueliteDynamicBridgeTestCharacter : public ACharacter
{
    GENERATED_BODY()
public:
    ARogueliteDynamicBridgeTestCharacter();
};

UCLASS()
class ROGUELITEPROTOTYPE_API ARogueliteDynamicBridgeTestGate : public AActor
{
    GENERATED_BODY()
public:
    ARogueliteDynamicBridgeTestGate();
    UPROPERTY(VisibleAnywhere) TObjectPtr<UBoxComponent> Blocker;
    void SetClosed(bool Closed);
    void PlaceAtSocket(const FRogueliteDoorSocket& Socket);
};

/** Independent G1A sandbox. Existing runtime assembly and readiness remain authoritative. */
UCLASS()
class ROGUELITEPROTOTYPE_API ARogueliteDynamicBridgeTestRig : public ARogueliteRuntimeLoopAssembly
{
    GENERATED_BODY()
public:
    ARogueliteDynamicBridgeTestRig();
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    UPROPERTY(EditAnywhere,Category="G1A Test Only") FString Scenario=TEXT("BaselinePawn");
    UPROPERTY(EditAnywhere,Category="G1A Test Only") bool bAutoRunScenario=false;
    UPROPERTY(VisibleInstanceOnly,Transient,Category="G1A Evidence") bool bScenarioFinished=false;
    UPROPERTY(VisibleInstanceOnly,Transient,Category="G1A Evidence") bool bScenarioPassed=false;
    UPROPERTY(VisibleInstanceOnly,Transient,Category="G1A Evidence") FString ScenarioEvidence;
    UFUNCTION(BlueprintCallable,Category="G1A Test Only") bool StartScenario(const FString& Name);
    void ClearTestObjects();
private:
    void PollScenario();
    void FinishScenario(bool Passed,const FString& Why);
    FTimerHandle ScenarioTimer;
    double ScenarioStarted=0,ActionStarted=0;
    int32 ScenarioStep=0;
    FGuid ObservedBatch;
    FString ObservedDigest;
    FVector PawnStart=FVector::ZeroVector;
    TWeakObjectPtr<ARogueliteDynamicBridgeTestCharacter> TestPawn;
    TWeakObjectPtr<ARogueliteDynamicBridgeTestGate> TestGate;
};
