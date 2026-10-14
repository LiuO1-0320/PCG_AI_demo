#pragma once
#include "RogueliteRuntimeSeedAssembly.h"
#include "RogueliteDungeonMap.h"
#include "RogueliteDungeonMapTestRig.generated.h"
/** E1-only fixture. Never used by the preview window; only runs on its new test map. */
UCLASS()
class ROGUELITEPROTOTYPE_API ARogueliteDungeonMapTestRig : public ARogueliteRuntimeSeedAssembly
{
    GENERATED_BODY()
public:
    ARogueliteDungeonMapTestRig();
    virtual void BeginPlay() override;
    UPROPERTY(EditAnywhere,Category="Roguelite|E1 Test Only") bool bRunMapChecks=false;
    UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Transient,Category="Roguelite|E1 Test Only") FRogueliteDungeonMapSnapshot MapSnapshot;
    UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Transient,Category="Roguelite|E1 Test Only") TArray<FString> MapEvidence;
    UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Transient,Category="Roguelite|E1 Test Only") bool bMapChecksPassed=false;
    UFUNCTION(CallInEditor,Category="Roguelite|E1 Test Only",meta=(DisplayName="Run E1 Editor Data Checks")) void RunEditorMapChecks();
    UFUNCTION(CallInEditor,Category="Roguelite|E1 Test Only",meta=(DisplayName="Read Map Snapshot")) void ReadMapSnapshot();
private:
    void RunRuntimeMapChecks();
    void Check(const FString& Name,bool Passed);
};
