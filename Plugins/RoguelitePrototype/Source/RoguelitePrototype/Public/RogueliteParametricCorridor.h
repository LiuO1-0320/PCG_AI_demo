#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RogueliteParametricRoom.h"
#include "RogueliteParametricCorridor.generated.h"

class USceneComponent;
class UStaticMeshComponent;
class UArrowComponent;

UENUM(BlueprintType)
enum class ERogueliteCorridorEndpoint : uint8
{
    Start,
    End
};

/** P02 threshold contract; connection fields derive from RoomAssembly records. */
USTRUCT(BlueprintType)
struct ROGUELITEPROTOTYPE_API FRogueliteCorridorSocket
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Socket")
    ERogueliteCorridorEndpoint Endpoint = ERogueliteCorridorEndpoint::Start;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Socket")
    FRogueliteDoorSocket DoorSocket;

    /** Derived query result, not a writable reservation authority. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Connection")
    bool bConnected = false;

    /** Shared assembly connection ID; derived cache is rebuilt on align/release/load. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Connection")
    FGuid ConnectionId;
};

/** Centered XY, finished floor Z=0, straight along X; open thresholds at +/- Length/2. */
UCLASS(Blueprintable)
class ROGUELITEPROTOTYPE_API ARogueliteParametricCorridor : public AActor
{
    GENERATED_BODY()

public:
    ARogueliteParametricCorridor();
    virtual void OnConstruction(const FTransform& Transform) override;
    virtual void PostRegisterAllComponents() override;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Roguelite|Corridor Dimensions", meta=(Units="cm", UIMin="100", ToolTip="走廊长度沿局部 X，厘米；保留非法输入并警告。1000 是测试默认值，不绑定任何穿行时长。"))
    float CorridorLength = 1000.0f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Roguelite|Corridor Dimensions", meta=(Units="cm", UIMin="100", ToolTip="走廊内部净宽，独立于端部开口宽度。"))
    float CorridorWidth = 400.0f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Roguelite|Corridor Dimensions", meta=(Units="cm", UIMin="100", ToolTip="Z=0 到天花板底面的内部净高。"))
    float CorridorHeight = 300.0f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Roguelite|Corridor Dimensions", meta=(Units="cm", UIMin="1", ToolTip="侧墙向外扩展的厚度；天花板和端框使用同一厚度。两端框须有非重叠长度。"))
    float WallThickness = 20.0f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Roguelite|Corridor Dimensions", meta=(Units="cm", UIMin="1", ToolTip="地板在完成面 Z=0 下方延伸。"))
    float FloorThickness = 20.0f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Roguelite|End Openings", meta=(Units="cm", UIMin="1", ToolTip="两端统一净开口宽；可小于走廊净宽，差额由端框封住。200 cm 为测试默认值。"))
    float DoorWidth = 200.0f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Roguelite|End Openings", meta=(Units="cm", UIMin="1", ToolTip="两端从 Z=0 起的统一净开口高；可小于走廊净高。250 cm 为测试默认值。"))
    float DoorHeight = 250.0f;

    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Roguelite|Sockets")
    FRogueliteCorridorSocket StartSocket;

    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Roguelite|Sockets")
    FRogueliteCorridorSocket EndSocket;

    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Roguelite|Validation")
    TArray<FString> ValidationWarnings;

    UFUNCTION(BlueprintPure, Category="Roguelite|Sockets")
    TArray<FRogueliteCorridorSocket> GetCorridorSockets() const;

    UFUNCTION(BlueprintPure, Category="Roguelite|Sockets")
    bool TryGetCorridorSocket(ERogueliteCorridorEndpoint Endpoint, FRogueliteCorridorSocket& OutSocket) const;

    /** Specification check only: does not align, connect, reserve or move any Actor. */
    UFUNCTION(BlueprintPure, Category="Roguelite|Sockets")
    bool IsRoomSocketCompatible(ERogueliteCorridorEndpoint Endpoint, const FRogueliteDoorSocket& RoomSocket, FString& OutReason) const;

    /** Parameter occupancy, independent of arrows and visual Actor Bounds. Invalid input returns invalid box. */
    UFUNCTION(BlueprintPure, Category="Roguelite|Bounds")
    FBox GetLocalOccupancyBounds() const;

    UFUNCTION(BlueprintPure, Category="Roguelite|Bounds")
    FBox GetWorldOccupancyBounds() const;

    UFUNCTION(CallInEditor, BlueprintCallable, Category="Roguelite|Validation", meta=(DisplayName="Rebuild Corridor / 更新走廊"))
    void RebuildCorridor();

    // Explicit native collision diagnostic; never runs continuously or ignores this shell.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Roguelite|Passage Check", meta=(Units="cm", UIMin="1"))
    float TestCapsuleRadius = 34.0f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Roguelite|Passage Check", meta=(Units="cm", UIMin="1"))
    float TestCapsuleHalfHeight = 88.0f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Roguelite|Passage Check", meta=(Units="cm", UIMin="0.1"))
    float CapsuleClearance = 2.0f;

    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Roguelite|Passage Check")
    bool bHasPassageTestResult = false;

    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Roguelite|Passage Check")
    bool bPassageTestPassed = false;

    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Roguelite|Passage Check")
    TArray<FString> PassageTestEvidence;

    UFUNCTION(CallInEditor, BlueprintCallable, Category="Roguelite|Validation", meta=(DisplayName="Check Passage / 检查胶囊通行", ToolTip="显式 Pawn 胶囊 Sweep/起终点 Overlap。结果不是 NavMesh 或真人角色测试。"))
    void CheckPassage();

#if WITH_EDITOR
    virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
    virtual void PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedEvent) override;
    virtual void PostEditMove(bool bFinished) override;
    virtual void CheckForErrors() override;
#endif

private:
    UPROPERTY(VisibleAnywhere, Category="Roguelite|Shell")
    TObjectPtr<USceneComponent> CorridorRoot;

    // Four shell meshes, then Left/Right/Lintel at each end: ten permanent default subobjects.
    UPROPERTY(VisibleAnywhere, Category="Roguelite|Shell")
    TArray<TObjectPtr<UStaticMeshComponent>> ShellMeshes;

#if WITH_EDITORONLY_DATA
    UPROPERTY()
    TArray<TObjectPtr<UArrowComponent>> SocketMarkers;
#endif

    bool ValidateShell(FString& OutReason) const;
    bool ValidateOpening(FString& OutReason) const;
    FRogueliteCorridorSocket CalculateSocket(ERogueliteCorridorEndpoint Endpoint) const;
    void UpdateEndFrame(int32 Index, bool bOpeningValid);
};
