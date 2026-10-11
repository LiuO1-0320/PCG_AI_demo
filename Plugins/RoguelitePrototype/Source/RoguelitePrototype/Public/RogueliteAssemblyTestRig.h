#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RogueliteRoomAssembly.h"
#include "RogueliteAssemblyTestRig.generated.h"

/** Explicit opt-in diagnostics, restricted to the fresh P03-C2 test map. No Tick. */
UCLASS()
class ROGUELITEPROTOTYPE_API ARogueliteAssemblyTestRig : public AActor
{
    GENERATED_BODY()
public:
    ARogueliteAssemblyTestRig();
    virtual void BeginPlay() override;
    UPROPERTY(EditInstanceOnly, Category="P03C2 Test Only")
    bool bRunRuntimeProbe = false;
    UPROPERTY(VisibleInstanceOnly, Transient, Category="P03C2 Test Only")
    bool bRuntimeProbeRan = false;
    UPROPERTY(VisibleInstanceOnly, Transient, Category="P03C2 Test Only")
    bool bRuntimeProbePassed = false;
    UPROPERTY(VisibleInstanceOnly, Transient, Category="P03C2 Test Only")
    TArray<FString> RuntimeEvidence;
    UPROPERTY(VisibleInstanceOnly, Transient, Category="P03C2 Test Only")
    TArray<FString> NavigationBuildEvidence;
    UFUNCTION(CallInEditor, Category="P03C2 Test Only", meta=(DisplayName="Build Local Navigation / 构建本图导航"))
    void BuildLocalNavigation();
private:
    bool IsC2World() const;
};
