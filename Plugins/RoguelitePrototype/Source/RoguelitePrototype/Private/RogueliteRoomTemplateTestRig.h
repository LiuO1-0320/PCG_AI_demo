#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RogueliteRoomTemplate.h"
#include "RogueliteRoomAssembly.h"
#include "RogueliteRoomTemplateTestRig.generated.h"

/** Explicit diagnostics in the new P03-D map only, not a dungeon generator. */
UCLASS()
class ARogueliteRoomTemplateTestRig : public AActor
{
    GENERATED_BODY()
public:
    ARogueliteRoomTemplateTestRig();
    virtual void BeginPlay() override;
    UPROPERTY(EditInstanceOnly, Category="P03D Test Only") TObjectPtr<ARogueliteRoomAssembly> Assembly;
    UPROPERTY(EditInstanceOnly, Category="P03D Test Only") TSubclassOf<ARogueliteRoomTemplate> TemplateClass;
    UPROPERTY(EditInstanceOnly, Category="P03D Test Only") bool bRunRuntimeProbe=false;
    UPROPERTY(VisibleInstanceOnly, Transient, Category="P03D Test Only") bool bRuntimeProbeRan=false;
    UPROPERTY(VisibleInstanceOnly, Transient, Category="P03D Test Only") bool bRuntimeProbePassed=false;
    UPROPERTY(VisibleInstanceOnly, Transient, Category="P03D Test Only") TArray<FString> RuntimeEvidence;
    UPROPERTY(VisibleInstanceOnly, Transient, Category="P03D Test Only") TObjectPtr<ARogueliteRoomTemplate> RuntimeFactoryInstance;
    UPROPERTY(VisibleInstanceOnly, Transient, Category="P03D Test Only") TObjectPtr<ARogueliteRoomTemplate> RuntimeConnectedInstance;
    UPROPERTY(VisibleInstanceOnly, Transient, Category="P03D Test Only") TObjectPtr<ARogueliteRoomAssembly> RuntimeAssembly;
    UPROPERTY(VisibleInstanceOnly, Transient, Category="P03D Test Only") FRogueliteAssemblyResult RuntimeResult;
    UPROPERTY(VisibleInstanceOnly, Transient, Category="P03D Test Only") FRogueliteAssemblyResult RuntimeNavigationResult;
    UPROPERTY(VisibleInstanceOnly, Transient, Category="P03D Test Only") TArray<FString> NavigationBuildEvidence;
    UFUNCTION(CallInEditor, Category="P03D Test Only", meta=(DisplayName="Build Local Navigation / 构建本图导航")) void BuildLocalNavigation();
private:
    bool IsDWorld() const;
    void CheckRuntimeNavigation();
    int32 RuntimeNavAttempts=0;
    FTimerHandle RuntimeNavTimer;
};
