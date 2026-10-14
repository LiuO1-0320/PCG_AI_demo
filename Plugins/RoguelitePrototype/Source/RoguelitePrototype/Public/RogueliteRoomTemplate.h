#pragma once
#include "CoreMinimal.h"
#include "RogueliteParametricRoom.h"
#include "Components/SceneComponent.h"
#include "RogueliteRoomTemplate.generated.h"

USTRUCT(BlueprintType)
struct ROGUELITEPROTOTYPE_API FRogueliteTemplateMemberState
{
    GENERATED_BODY()
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<USceneComponent> Component;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) FName Name;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) FName ParentName;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) FTransform ParentLocalTransform;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) FTransform RoomLocalTransform;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) FTransform WorldTransform;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) bool bOwnedByInstance = false;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) FString StaticMeshPath;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TArray<FString> MaterialPaths;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) FName CollisionProfile;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) int32 CollisionEnabled = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TEnumAsByte<EComponentMobility::Type> Mobility;
};

USTRUCT(BlueprintType)
struct ROGUELITEPROTOTYPE_API FRogueliteTemplateDescriptor
{
    GENERATED_BODY()
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) FName TemplateId;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) FName Purpose;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) FString ClassPath;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) FVector Dimensions = FVector::ZeroVector;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) float WallThickness = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) float FloorThickness = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TArray<FRogueliteDoorSocket> DoorSockets;
};

USTRUCT(BlueprintType)
struct ROGUELITEPROTOTYPE_API FRogueliteTemplateValidation
{
    GENERATED_BODY()
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) bool bValid = false;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) bool bDeferredContent = false;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) int32 OwnedGeometryCount = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TArray<FString> Issues;
};

/** One shell and explicitly owned component subtree. Independent POI actors stay independent. */
UCLASS(Blueprintable)
class ROGUELITEPROTOTYPE_API ARogueliteRoomTemplate : public ARogueliteParametricRoom
{
    GENERATED_BODY()
public:
    ARogueliteRoomTemplate();
    virtual void OnConstruction(const FTransform& Transform) override;
    virtual void PostRegisterAllComponents() override;
    virtual void PostInitializeComponents() override;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Roguelite|Template") FName TemplateId;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Roguelite|Template") FName TemplatePurpose = TEXT("Combat");
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Roguelite|Template") TObjectPtr<USceneComponent> TemplateContentRoot;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Roguelite|Template", meta=(Units="cm", ClampMin="1")) float DoorApproachDepth = 120;
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Roguelite|Template Validation") FRogueliteTemplateValidation TemplateValidation;
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Roguelite|Template Validation") TArray<FRogueliteTemplateMemberState> TemplateMemberStates;
    UFUNCTION(BlueprintPure, Category="Roguelite|Template") TArray<USceneComponent*> GetTemplateOwnedComponents() const;
    UFUNCTION(BlueprintPure, Category="Roguelite|Template") TArray<FRogueliteTemplateMemberState> GetTemplateMemberStates() const;
    UFUNCTION(BlueprintPure, Category="Roguelite|Template") FRogueliteTemplateDescriptor GetTemplateDescriptor() const;
    FRogueliteTemplateValidation EvaluateTemplate() const;
    UFUNCTION(BlueprintCallable, Category="Roguelite|Template Validation") FRogueliteTemplateValidation ValidateTemplate();
    UFUNCTION(CallInEditor, Category="Roguelite|Template Validation", meta=(DisplayName="Validate Template / 检查模板")) void CheckTemplateInEditor() { ValidateTemplate(); }
    UFUNCTION(CallInEditor, BlueprintCallable, Category="Roguelite|Template Validation", meta=(DisplayName="Rebuild Template / 更新外壳与检查模板")) void RebuildTemplate();
    UFUNCTION(BlueprintCallable, Category="Roguelite|Template", meta=(WorldContext="WorldContextObject"))
    static ARogueliteRoomTemplate* CreateTemplateInstance(UObject* WorldContextObject, TSubclassOf<ARogueliteRoomTemplate> TemplateClass,
        const FTransform& WorldTransform, FString& FailureReason);
#if WITH_EDITOR
    virtual void PostEditChangeProperty(FPropertyChangedEvent& Event) override;
    virtual void PostEditChangeChainProperty(FPropertyChangedChainEvent& Event) override;
    virtual void PostEditMove(bool bFinished) override;
    virtual void CheckForErrors() override;
#endif
};
