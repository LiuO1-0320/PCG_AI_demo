#pragma once
#include "GameFramework/Character.h"
#include "AIController.h"
#include "RogueliteNavigationTestCharacter.generated.h"

/** Test-only walking character. No weapons, squad, enemy or gameplay behavior. */
UCLASS()
class ARogueliteNavigationTestCharacter : public ACharacter
{
    GENERATED_BODY()
public:
    ARogueliteNavigationTestCharacter();
    // Possess/UnPossess rewrites APawn::Owner. Creation identity is independent.
    TWeakObjectPtr<class URogueliteNavigationReadinessComponent> CreatedByVerifier;
    FGuid CreationBatch;
};
UCLASS()
class ARogueliteNavigationTestController : public AAIController
{
    GENERATED_BODY()
public:
    virtual void OnMoveCompleted(FAIRequestID RequestID,const FPathFollowingResult& Result) override;
    TWeakObjectPtr<class URogueliteNavigationReadinessComponent> Verifier;
    FGuid VerificationBatch;
};
