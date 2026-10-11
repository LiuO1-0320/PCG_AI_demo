#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RogueliteParametricRoom.generated.h"

class USceneComponent;
class UStaticMeshComponent;
class UArrowComponent;

UENUM(BlueprintType)
enum class ERogueliteRoomSide : uint8
{
    North,
    South,
    East,
    West
};

/** Authoring values are retained even when an opening is invalid. */
USTRUCT(BlueprintType)
struct ROGUELITEPROTOTYPE_API FRogueliteDoorConfig
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Door", meta=(ToolTip="启用此侧唯一门洞；非法配置会警告并恢复完整墙体。"))
    bool bEnabled = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Door", meta=(ToolTip="自动居中；关闭后使用 ManualOffset，保留先前的手工偏移值。"))
    bool bAutoCenter = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Door", meta=(Units="cm", EditCondition="!bAutoCenter", ToolTip="相对墙中心的厘米偏移。北/南正值沿 +Y，东/西正值沿 +X。超限保留原值，不自动挪门。"))
    float ManualOffset = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Door", meta=(Units="cm", UIMin="1", ToolTip="门洞净宽。200 cm 是可调测试默认值；非法输入保留并警告。"))
    float DoorWidth = 200.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Door", meta=(Units="cm", UIMin="1", ToolTip="从地面 Z=0 起的门洞净高。250 cm 是可调测试默认值。"))
    float DoorHeight = 250.0f;
};

/** Door/sealing modules can use the same side ID, outer threshold and clear dimensions. */
USTRUCT(BlueprintType)
struct ROGUELITEPROTOTYPE_API FRogueliteDoorSocket
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Socket")
    ERogueliteRoomSide Side = ERogueliteRoomSide::North;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Socket")
    bool bValid = false;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Socket", meta=(ToolTip="墙体外表面的门槛中心，局部 Z=0。"))
    FVector LocalPosition = FVector::ZeroVector;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Socket")
    FVector WorldPosition = FVector::ZeroVector;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Socket")
    FVector LocalOutwardDirection = FVector::ForwardVector;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Socket")
    FVector WorldOutwardDirection = FVector::ForwardVector;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Socket", meta=(Units="cm"))
    float DoorWidth = 0.0f;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Socket", meta=(Units="cm"))
    float DoorHeight = 0.0f;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Socket", meta=(Units="cm"))
    float WallThickness = 0.0f;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Socket", meta=(Units="cm", ToolTip="本次计算偏移；自动居中为 0，不会覆盖配置中的 ManualOffset。"))
    float AppliedOffset = 0.0f;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Socket")
    FString ValidationMessage;
};

/** Rectangular shell. X = length / north, Y = width / east, finished floor Z = 0. */
UCLASS()
class ROGUELITEPROTOTYPE_API ARogueliteParametricRoom : public AActor
{
    GENERATED_BODY()

public:
    ARogueliteParametricRoom();
    virtual void OnConstruction(const FTransform& Transform) override;
    virtual void PostRegisterAllComponents() override;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Roguelite|Room Dimensions", meta=(Units="cm", ClampMin="100", UIMin="100", ToolTip="室内净长度，沿局部 X 轴；请使用尺寸属性，不要缩放整个 Actor。"))
    float RoomLength = 2000.0f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Roguelite|Room Dimensions", meta=(Units="cm", ClampMin="100", UIMin="100", ToolTip="室内净宽度，沿局部 Y 轴。"))
    float RoomWidth = 2000.0f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Roguelite|Room Dimensions", meta=(Units="cm", ClampMin="100", UIMin="100", ToolTip="地面完成面 Z=0 到墙顶的净高度；首版没有屋顶。"))
    float RoomHeight = 400.0f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Roguelite|Room Dimensions", meta=(Units="cm", ClampMin="1", UIMin="1", ToolTip="墙厚向室内边界之外扩展，不占用净尺寸。"))
    float WallThickness = 20.0f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Roguelite|Room Dimensions", meta=(Units="cm", ClampMin="1", UIMin="1", ToolTip="地板在 Z=0 下方延伸的厚度。"))
    float FloorThickness = 20.0f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Roguelite|Door Sockets", meta=(DisplayName="North Door / 北门"))
    FRogueliteDoorConfig NorthDoor;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Roguelite|Door Sockets", meta=(DisplayName="South Door / 南门"))
    FRogueliteDoorConfig SouthDoor;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Roguelite|Door Sockets", meta=(DisplayName="East Door / 东门"))
    FRogueliteDoorConfig EastDoor;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Roguelite|Door Sockets", meta=(DisplayName="West Door / 西门"))
    FRogueliteDoorConfig WestDoor;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Roguelite|Door Safety", meta=(Units="cm", UIMin="1", ToolTip="门洞到两端室内转角的最小实体墙余量；测试默认 50 cm。非法值保留并警告。"))
    float MinDoorSideWall = 50.0f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Roguelite|Door Safety", meta=(Units="cm", UIMin="1", ToolTip="门洞以上的最小横梁高度；测试默认 20 cm。"))
    float MinDoorLintelHeight = 20.0f;

    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Roguelite|Door Sockets", meta=(ToolTip="启用侧的计算结果，含非法项；仅 bValid=true 的项具有真实开口。运行时查询使用 GetDoorSockets。"))
    TArray<FRogueliteDoorSocket> DoorSockets;

    /** Fresh world-space data, including invalid enabled configurations for diagnostics. */
    UFUNCTION(BlueprintPure, Category="Roguelite|Door Sockets")
    TArray<FRogueliteDoorSocket> GetDoorSockets() const;

    /** Returns true only for an enabled, legal opening. */
    UFUNCTION(BlueprintPure, Category="Roguelite|Door Sockets")
    bool TryGetDoorSocket(ERogueliteRoomSide Side, FRogueliteDoorSocket& OutSocket) const;

    UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category="Roguelite|Manual POIs", meta=(ToolTip="手工 Actor 的引用，仅检查实际 Bounds；不附加、不移动、不缩放、不删除。"))
    TArray<TObjectPtr<AActor>> ManualPOIActors;

    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Roguelite|Validation", meta=(ToolTip="尺寸、Actor Scale 与手工 POI 越界检查；Output Log 同时显示 Actor 名称和路径。"))
    TArray<FString> ValidationWarnings;

    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Roguelite|Validation")
    TArray<TObjectPtr<AActor>> OutOfBoundsManualPOIActors;

    /** Also useful after moving a manual POI without changing room dimensions. */
    UFUNCTION(CallInEditor, BlueprintCallable, Category="Roguelite|Validation", meta=(ToolTip="更新外壳并重新检查手工 POI。不会修改手工 Actor。"))
    void RebuildRoom();

#if WITH_EDITOR
    virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
    virtual void PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedEvent) override;
    virtual void PostEditMove(bool bFinished) override;
    virtual void CheckForErrors() override;
#endif

private:
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Roguelite|Shell", meta=(AllowPrivateAccess="true"))
    TObjectPtr<USceneComponent> RoomRoot;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Roguelite|Shell", meta=(AllowPrivateAccess="true"))
    TObjectPtr<UStaticMeshComponent> Floor;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Roguelite|Shell", meta=(AllowPrivateAccess="true"))
    TObjectPtr<UStaticMeshComponent> NorthWall;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Roguelite|Shell", meta=(AllowPrivateAccess="true"))
    TObjectPtr<UStaticMeshComponent> SouthWall;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Roguelite|Shell", meta=(AllowPrivateAccess="true"))
    TObjectPtr<UStaticMeshComponent> EastWall;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Roguelite|Shell", meta=(AllowPrivateAccess="true"))
    TObjectPtr<UStaticMeshComponent> WestWall;

    // Two extra default subobjects per side. Existing P01 walls become full/left segments.
    UPROPERTY(VisibleAnywhere, Category="Roguelite|Shell")
    TArray<TObjectPtr<UStaticMeshComponent>> DoorRightSegments;

    UPROPERTY(VisibleAnywhere, Category="Roguelite|Shell")
    TArray<TObjectPtr<UStaticMeshComponent>> DoorLintelSegments;

#if WITH_EDITORONLY_DATA
    UPROPERTY()
    TArray<TObjectPtr<UArrowComponent>> DoorSocketMarkers;
#endif

    FBox CalculateInteriorBounds() const;
    const FRogueliteDoorConfig& GetDoorConfig(ERogueliteRoomSide Side) const;
    FRogueliteDoorSocket CalculateDoorSocket(ERogueliteRoomSide Side) const;
    void UpdateDoorWall(ERogueliteRoomSide Side, UStaticMeshComponent* FullOrLeft);
    void ValidateManualPOIs();
};
