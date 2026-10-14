#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "RogueliteNavigationReadinessComponent.generated.h"

USTRUCT(BlueprintType)
struct ROGUELITEPROTOTYPE_API FRogueliteNavigationPathEvidence
{
    GENERATED_BODY()
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) FString Query;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) int32 FromNode=-1;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) int32 ToNode=-1;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) int32 EdgeId=-1;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) FVector From=FVector::ZeroVector;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) FVector To=FVector::ZeroVector;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) bool bSucceeded=false;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) bool bPartial=false;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) int32 Points=0;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) float Length=0;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) FString Reason;
};
USTRUCT(BlueprintType)
struct ROGUELITEPROTOTYPE_API FRogueliteNavigationMoveEvidence
{
    GENERATED_BODY()
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) FString Test;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) int32 FromNode=-1;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) int32 ToNode=-1;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) FVector Start=FVector::ZeroVector;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) FVector Goal=FVector::ZeroVector;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) FVector ActualFeet=FVector::ZeroVector;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) int32 RequestId=0;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) FString Result;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) float Distance2D=0;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) float Duration=0;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) bool bSucceeded=false;
};
USTRUCT(BlueprintType)
struct ROGUELITEPROTOTYPE_API FRogueliteNavigationReadinessEvidence
{
    GENERATED_BODY()
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) FGuid BatchId;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) int32 Seed=0;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) FString World;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) FString State;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) FString Reason;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) FString NavData;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) FString LayoutDigest;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) FVector Agent=FVector::ZeroVector;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) FVector NavAgent=FVector::ZeroVector;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) FBox Coverage=FBox(ForceInit);
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) int32 Rooms=0;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) int32 Corridors=0;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) int32 GenerationEvents=0;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) int32 BoundedChecks=0;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) bool bReady=false;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) bool bCoverageComplete=false;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) bool bCleanupComplete=false;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) float GeometrySeconds=0;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) float BuildSeconds=0;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) float PathSeconds=0;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) float MoveSeconds=0;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) float TotalSeconds=0;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) TArray<FRogueliteNavigationPathEvidence> Paths;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) TArray<FRogueliteNavigationMoveEvidence> Moves;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) TArray<FString> Diagnostics;
};

/** Runtime navigation verifier. Bounded timers and UE completion events, no component Tick. */
UCLASS(ClassGroup=(Roguelite),meta=(BlueprintSpawnableComponent))
class ROGUELITEPROTOTYPE_API URogueliteNavigationReadinessComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    URogueliteNavigationReadinessComponent();
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Roguelite|Navigation") float BuildTimeoutSeconds=30;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Roguelite|Navigation") float MoveTimeoutSeconds=120;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Roguelite|Navigation") float AcceptanceRadius=35;
    UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Transient,Category="Roguelite|Navigation") FRogueliteNavigationReadinessEvidence Report;
    UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Transient,Category="Roguelite|Navigation") FRogueliteNavigationReadinessEvidence LastFailure;
    bool Start(float GeometrySeconds);
    void Cancel();
    UFUNCTION(BlueprintPure,Category="Roguelite|Navigation") bool IsCurrentBatchReady() const;
    UFUNCTION(BlueprintCallable,Category="Roguelite|Navigation") bool RetryNavigation();
    void MoveCompleted(class ARogueliteNavigationTestController* Source,FGuid Batch,uint32 Request,const struct FPathFollowingResult& Result);
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
    UFUNCTION() void NavigationGenerated(class ANavigationData* Data);
    void CheckBuild();
    bool ValidatePaths();
    bool ChooseAnchor(int32 Node,FVector& Out);
    bool Query(const FString& Name,int32 From,int32 To,int32 Edge=-1);
    void NextMove();
    void CheckMove();
    void Fail(const FString& State,const FString& Reason);
    void ReleaseTestObjects();
    bool Current() const;
    bool ClearCapsule(const FVector& Feet) const;
    class ARogueliteRuntimeSeedAssembly* Rig() const;
    UPROPERTY() TObjectPtr<class UNavigationSystemV1> Navigation;
    UPROPERTY() TObjectPtr<class ANavigationData> Data;
    UPROPERTY() TObjectPtr<class ARogueliteRuntimeNavigationBounds> Bounds;
    UPROPERTY() TObjectPtr<class ARogueliteNavigationTestCharacter> Character;
    UPROPERTY() TObjectPtr<class ARogueliteNavigationTestController> Controller;
    TMap<int32,FVector> Anchors;
    TWeakObjectPtr<UWorld> ActiveWorld;
    FGuid ActiveBatch;
    FTimerHandle BuildTimer,MoveTimer,NextMoveTimer;
    double StartSeconds=0,MoveStart=0,AllMovesStart=0;
    uint32 ActiveRequest=0;
    int32 MoveIndex=0;
    bool bActive=false,bWaitingForMove=false,bPathsValidated=false;
};
