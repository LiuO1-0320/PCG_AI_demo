#pragma once
#include "RogueliteRuntimeSeedAssembly.h"
#include "RogueliteNavigationReadinessComponent.h"
#include "RogueliteNavigationReadinessTestRig.generated.h"

/** New-map-only validation driver, not a production dungeon or AI system. */
UCLASS()
class ROGUELITEPROTOTYPE_API ARogueliteNavigationReadinessTestRig : public ARogueliteRuntimeSeedAssembly
{
    GENERATED_BODY()
public:
    ARogueliteNavigationReadinessTestRig();
    virtual void BeginPlay() override;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Roguelite|B3B Test Only") bool bRunNavigationSuite=false;
    UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Transient,Category="Roguelite|B3B Test Only") bool bSuiteFinished=false;
    UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Transient,Category="Roguelite|B3B Test Only") bool bSuitePassed=false;
    UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Transient,Category="Roguelite|B3B Test Only") FString CurrentCase;
    UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Transient,Category="Roguelite|B3B Test Only") TArray<FString> SuiteEvidence;
    UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Transient,Category="Roguelite|B3B Test Only") TArray<FRogueliteNavigationReadinessEvidence> Cases;
    UFUNCTION(CallInEditor,Category="Roguelite|B3B Test Only",meta=(DisplayName="Run B2 Editor Regression / 验证B2编辑器回归")) void RunB2EditorRegression();
private:
    void StartSuite();
    void StartCase();
    void CheckCase();
    void AddCheck(const FString& Name,bool Passed);
    void RemoveFixture();
    FTimerHandle SuiteTimer;
    FRogueliteSeedLayoutSettings SmallSettings;
    FTransform SmallTransform;
    UPROPERTY() TObjectPtr<AActor> Blocker;
    FTransform BlockerTransform;
    FGuid CancelledBatch;
    int32 CaseIndex=0;
    bool bAllPassed=true;
};
