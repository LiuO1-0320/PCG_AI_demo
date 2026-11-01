#pragma once
#include "CoreMinimal.h"
#include "RogueliteRuntimeLoopAssembly.h"
#include "RogueliteRoomRoleTestRig.generated.h"
// Independent G1 acceptance fixture; production generation and navigation are inherited.
UCLASS()
class ROGUELITEPROTOTYPE_API ARogueliteRoomRoleTestRig : public ARogueliteRuntimeLoopAssembly
{
    GENERATED_BODY()
public:
    ARogueliteRoomRoleTestRig();
    UFUNCTION(CallInEditor,Category="G1 Test Only",meta=(DisplayName="G1 Generate Editor Preview")) void G1GeneratePreview();
    UFUNCTION(CallInEditor,Category="G1 Test Only",meta=(DisplayName="G1 Clear Editor Preview")) void G1ClearPreview();
    UFUNCTION(CallInEditor,Category="G1 Test Only",meta=(DisplayName="G1 Prepare Templates For PIE")) void G1PrepareTemplates();
    UPROPERTY(VisibleInstanceOnly,Transient,Category="G1 Test Only") FRogueliteLoopAwareEvidence G1EditorEvidence;
};
