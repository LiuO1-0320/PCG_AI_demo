#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTService.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BehaviorTree/Decorators/BTDecorator_Blackboard.h"
#include "Commandlets/Commandlet.h"
#include "CastleAI.generated.h"

// The root service runs even when combat is out of range, so an already armed slam resolves.
UCLASS()
class CASTLEDEMO_API UBTService_CastlePerception : public UBTService
{
    GENERATED_BODY()
public:
    UBTService_CastlePerception();
protected:
    virtual void TickNode(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) override;
    virtual void OnSearchStart(FBehaviorTreeSearchData& SearchData) override;
};

UENUM()
enum class ECastleAIAction : uint8 { Pursuit, Attack, GroundSlam, Hold };

// Latent tasks run concurrently under native Simple Parallel composites.
// Runtime timers/state belong to the possessed enemy, never to shared BT template objects.
UCLASS()
class CASTLEDEMO_API UBTTask_CastleAction : public UBTTaskNode
{
    GENERATED_BODY()
public:
    UBTTask_CastleAction();
    UPROPERTY(EditAnywhere, Category="Castle") ECastleAIAction Action = ECastleAIAction::Hold;
    virtual FString GetStaticDescription() const override;
protected:
    virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
    virtual void TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) override;
};

UCLASS()
class CASTLEDEMO_API UBTDecorator_CastleBool : public UBTDecorator_Blackboard
{
    GENERATED_BODY()
public:
    void ConfigureKey(FName Key);
};

// Creates only AI assets, including editable editor graphs, without rebuilding the map.
UCLASS()
class CASTLEDEMO_API UCastleAISetupCommandlet : public UCommandlet
{
    GENERATED_BODY()
public:
    UCastleAISetupCommandlet();
    virtual int32 Main(const FString& Params) override;
};
