#pragma once
#include "CoreMinimal.h"
#include "RogueliteRuntimeLoopAssembly.h"
#include "RogueliteProductionCandidateRig.generated.h"

/** M1A sandbox only. Thin controls over the accepted planner/registry/navigation APIs. */
UCLASS()
class ROGUELITEPROTOTYPE_API ARogueliteProductionCandidateRig : public ARogueliteRuntimeLoopAssembly
{
    GENERATED_BODY()
public:
    ARogueliteProductionCandidateRig();
    virtual bool GenerateRuntime() override;
    UFUNCTION(CallInEditor,Category="M1A Candidate",meta=(DisplayName="M1 Generate Editor Preview")) void M1GeneratePreview();
    UFUNCTION(CallInEditor,Category="M1A Candidate",meta=(DisplayName="M1 Clear Owned Preview")) void M1ClearPreview();
    UFUNCTION(CallInEditor,Category="M1A Candidate",meta=(DisplayName="M1 Prepare Current Profile For PIE")) void M1PrepareTemplates();
    UPROPERTY(VisibleInstanceOnly,Transient,Category="M1A Candidate") FRogueliteLoopAwareEvidence M1EditorEvidence;
    UPROPERTY(VisibleInstanceOnly,Transient,Category="M1A Candidate") FString M1RoleSummary;
    UFUNCTION(BlueprintPure,Category="M1A Candidate") int32 M1RoomCount() const { return Batch.Rooms.Num(); }
    UFUNCTION(BlueprintPure,Category="M1A Candidate") int32 M1LoopCount() const { return CommittedLoops.Edges.Num(); }
    UFUNCTION(BlueprintPure,Category="M1A Candidate") bool M1TreeFallback() const;
};
