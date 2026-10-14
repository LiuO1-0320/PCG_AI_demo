#include "RogueliteNavigationTestCharacter.h"
#include "RogueliteNavigationReadinessComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "UObject/ConstructorHelpers.h"
#include "Navigation/PathFollowingComponent.h"

ARogueliteNavigationTestCharacter::ARogueliteNavigationTestCharacter()
{
    GetCapsuleComponent()->InitCapsuleSize(34.f,88.f);
    GetCapsuleComponent()->SetCollisionProfileName(TEXT("Pawn"));
    GetCapsuleComponent()->SetCanEverAffectNavigation(false);
    AutoPossessAI=EAutoPossessAI::Disabled;
    GetCharacterMovement()->MaxWalkSpeed=600.f;
    GetCharacterMovement()->SetUpdateNavAgentWithOwnersCollisions(true);
    GetCharacterMovement()->GetNavAgentPropertiesRef().AgentRadius=34.f;
    GetCharacterMovement()->GetNavAgentPropertiesRef().AgentHeight=176.f;
    GetCharacterMovement()->GetNavAgentPropertiesRef().bCanCrouch=false;
    GetCharacterMovement()->GetNavAgentPropertiesRef().bCanJump=false;
    auto* Marker=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("TestBody"));
    Marker->SetupAttachment(GetRootComponent());
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    if(Sphere.Succeeded())Marker->SetStaticMesh(Sphere.Object);
    Marker->SetRelativeScale3D(FVector(.6,.6,1.5));
    Marker->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Marker->SetCanEverAffectNavigation(false);
}
void ARogueliteNavigationTestController::OnMoveCompleted(FAIRequestID RequestID,const FPathFollowingResult& Result)
{
    Super::OnMoveCompleted(RequestID,Result);
    if(Verifier.IsValid())Verifier->MoveCompleted(this,VerificationBatch,RequestID.GetID(),Result);
}
