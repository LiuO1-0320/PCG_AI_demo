#pragma once
#include "RogueliteRoomTemplate.h"
#include "RogueliteProductionCandidateRoom.generated.h"
/** Candidate-greybox fixture only: real owned geometry exists before Blueprint SCS registration. */
UCLASS(Blueprintable)
class ROGUELITEPROTOTYPE_API ARogueliteProductionCandidateRoom : public ARogueliteRoomTemplate
{
    GENERATED_BODY()
public:
    ARogueliteProductionCandidateRoom();
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="M1A Candidate") TObjectPtr<UStaticMeshComponent> M1BaselinePOI;
};
