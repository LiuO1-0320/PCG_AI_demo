#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RogueliteParametricRoom.h"
#include "RogueliteParametricCorridor.h"
#include "RogueliteRoomAssembly.generated.h"

/** Direct and StraightCorridor implemented; other modes remain reserved. */
UENUM(BlueprintType)
enum class ERogueliteConnectionMode : uint8
{
    Direct,
    StraightCorridor,
    CornerCorridor,
    CustomConnector
};

UENUM(BlueprintType)
enum class ERogueliteNavigationCheck : uint8
{
    NotChecked,
    Unavailable,
    Passed,
    Failed
};

UENUM(BlueprintType)
enum class ERogueliteConnectionValidation : uint8
{
    NotChecked,
    Passed,
    Failed
};

USTRUCT(BlueprintType)
struct ROGUELITEPROTOTYPE_API FRogueliteAssemblyProbe
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Probe")
    FString Name;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Probe")
    FVector Start = FVector::ZeroVector;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Probe")
    FVector End = FVector::ZeroVector;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Probe")
    bool bExpectedBlocking = false;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Probe")
    bool bBlockingHit = false;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Probe")
    bool bPassed = false;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Probe")
    float HitDistance = 0.0f;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Probe")
    FString HitObject;
};

USTRUCT(BlueprintType)
struct ROGUELITEPROTOTYPE_API FRogueliteAssemblyResult
{
    GENERATED_BODY()

    /** Success means geometry + Pawn capsule, and navigation if available. Read Navigation separately. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Result")
    bool bSucceeded = false;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Result")
    bool bGeometryPassed = false;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Result")
    bool bCapsulePassed = false;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Result")
    ERogueliteNavigationCheck Navigation = ERogueliteNavigationCheck::NotChecked;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Result")
    FString Message;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Result")
    FString NavigationMessage;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Result")
    TArray<FVector> NavigationPathPoints;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Result")
    bool bNavigationPartial = false;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Result")
    FTransform ProposedTargetTransform = FTransform::Identity;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Result")
    FTransform ProposedCorridorTransform = FTransform::Identity;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Result")
    FGuid ConnectionId;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Result")
    bool bCorridorRestoredOnFailure = true;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Result")
    bool bReservationsUnchangedOnFailure = true;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Result")
    float SecondSocketDistance = 0.0f;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Result")
    float SecondNormalDot = 0.0f;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Result")
    bool bTargetRestoredOnFailure = true;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Result")
    float SocketDistance = 0.0f;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Result")
    float NormalDot = 0.0f;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Result")
    float CapsuleRadius = 0.0f;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Result")
    float CapsuleHalfHeight = 0.0f;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Result")
    FBox OccupancyA = FBox(ForceInit);
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Result")
    FBox OccupancyB = FBox(ForceInit);
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Result")
    FBox OccupancyCorridor = FBox(ForceInit);
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Result")
    TArray<FRogueliteAssemblyProbe> Probes;
};

/** Logical endpoints/reservation are independent of the physical connection mode. */
USTRUCT(BlueprintType)
struct ROGUELITEPROTOTYPE_API FRogueliteRoomConnection
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Connection")
    FGuid Id;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Connection")
    TObjectPtr<ARogueliteParametricRoom> RoomA = nullptr;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Connection")
    ERogueliteRoomSide SocketA = ERogueliteRoomSide::North;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Connection")
    TObjectPtr<ARogueliteParametricRoom> RoomB = nullptr;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Connection")
    ERogueliteRoomSide SocketB = ERogueliteRoomSide::South;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Connection")
    ERogueliteConnectionMode Mode = ERogueliteConnectionMode::Direct;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Connection")
    TObjectPtr<ARogueliteParametricCorridor> Corridor = nullptr;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Connection")
    ERogueliteCorridorEndpoint EndpointA = ERogueliteCorridorEndpoint::Start;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Connection")
    ERogueliteCorridorEndpoint EndpointB = ERogueliteCorridorEndpoint::End;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Connection")
    ERogueliteConnectionValidation ValidationStatus = ERogueliteConnectionValidation::NotChecked;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Connection")
    int32 ValidationRevision = 0;
    /** Active reservations remain occupied even if revalidation fails; release explicitly. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Connection")
    bool bActive = true;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Connection", meta=(ToolTip="上次检查的几何与胶囊结果；改参数后点击 Revalidate Connections。重载后需重新检查。"))
    bool bGeometryValid = false;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Connection")
    ERogueliteNavigationCheck Navigation = ERogueliteNavigationCheck::NotChecked;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Connection")
    FString ValidationMessage;
};

/** Explicit authoring tool/connection registry. Never auto-aligns in Tick or OnConstruction. */
UCLASS(Blueprintable)
class ROGUELITEPROTOTYPE_API ARogueliteRoomAssembly : public AActor
{
    GENERATED_BODY()

public:
    ARogueliteRoomAssembly();
    virtual void PostLoad() override;

    UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category="Roguelite|Assembly Setup")
    TObjectPtr<ARogueliteParametricRoom> RoomA;
    UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category="Roguelite|Assembly Setup")
    ERogueliteRoomSide SocketA = ERogueliteRoomSide::North;
    UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category="Roguelite|Assembly Setup")
    TObjectPtr<ARogueliteParametricRoom> RoomB;
    UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category="Roguelite|Assembly Setup")
    ERogueliteRoomSide SocketB = ERogueliteRoomSide::South;
    UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category="Roguelite|Assembly Setup", meta=(ToolTip="Direct 或 StraightCorridor；转角/自定义模式未实现。"))
    ERogueliteConnectionMode ConnectionMode = ERogueliteConnectionMode::Direct;
    UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category="Roguelite|Assembly Setup")
    TObjectPtr<ARogueliteParametricCorridor> Corridor;
    UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category="Roguelite|Assembly Setup")
    ERogueliteCorridorEndpoint CorridorEndpointA = ERogueliteCorridorEndpoint::Start;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Roguelite|Assembly Validation", meta=(Units="cm", ClampMin="0.001", ClampMax="1"))
    float PositionTolerance = 0.1f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Roguelite|Assembly Validation", meta=(ClampMin="0.001", ClampMax="1"))
    float AngleToleranceDegrees = 0.01f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Roguelite|Assembly Validation", meta=(Units="cm", ClampMin="0", ClampMax="1"))
    float DoorSizeTolerance = 0.1f;
    // Matches the actual UE 5.8.3 ACharacter capsule defaults, not a gameplay decision.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Roguelite|Assembly Validation", meta=(Units="cm", ClampMin="1"))
    float TestCapsuleRadius = 34.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Roguelite|Assembly Validation", meta=(Units="cm", ClampMin="1"))
    float TestCapsuleHalfHeight = 88.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Roguelite|Assembly Validation", meta=(Units="cm", ClampMin="0.1"))
    float CapsuleClearance = 2.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Roguelite|Assembly Validation")
    bool bCheckNavigation = true;

    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Roguelite|Connections")
    TArray<FRogueliteRoomConnection> Connections;
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Roguelite|Assembly Result")
    FRogueliteAssemblyResult LastResult;

    UFUNCTION(CallInEditor, Category="Roguelite|Assembly Actions", meta=(DisplayName="Align Selected Rooms / 拼接所选房间"))
    void AlignSelectedRooms();
    UFUNCTION(CallInEditor, BlueprintCallable, Category="Roguelite|Assembly Actions", meta=(DisplayName="Align Corridor Connection / 拼接走廊连接"))
    void AlignCorridorConnection();
    UFUNCTION(CallInEditor, BlueprintCallable, Category="Roguelite|Assembly Actions", meta=(DisplayName="Disconnect Corridor Connection / 释放走廊连接"))
    void DisconnectCorridorConnection();
    UFUNCTION(CallInEditor, BlueprintCallable, Category="Roguelite|Assembly Actions", meta=(DisplayName="Revalidate Connections / 重新检查连接"))
    void RevalidateConnections();
    UFUNCTION(CallInEditor, Category="Roguelite|Assembly Actions", meta=(DisplayName="Disconnect Selected Rooms / 释放所选连接"))
    void DisconnectSelectedRooms();

    UFUNCTION(BlueprintCallable, Category="Roguelite|Assembly")
    FRogueliteAssemblyResult TryAlignRoomToSocket(ARogueliteParametricRoom* FixedRoom, ERogueliteRoomSide FixedSocket,
        ARogueliteParametricRoom* TargetRoom, ERogueliteRoomSide TargetSocket, ERogueliteConnectionMode Mode);
    UFUNCTION(BlueprintCallable, Category="Roguelite|Assembly")
    bool ReleaseConnection(FGuid ConnectionId);

    UFUNCTION(BlueprintCallable, Category="Roguelite|Assembly")
    FRogueliteAssemblyResult TryAlignCorridorConnection(ARogueliteParametricRoom* FixedRoom, ERogueliteRoomSide FixedSocket,
        ARogueliteParametricCorridor* Connector, ERogueliteCorridorEndpoint EndpointFacingA,
        ARogueliteParametricRoom* TargetRoom, ERogueliteRoomSide TargetSocket);

    /** Calculation only, useful before FinishSpawning static shells; no movement/reservation/collision claims. */
    UFUNCTION(BlueprintCallable, Category="Roguelite|Assembly")
    bool ComputeCorridorTransforms(ARogueliteParametricRoom* FixedRoom, ERogueliteRoomSide FixedSocket,
        ARogueliteParametricCorridor* Connector, ERogueliteCorridorEndpoint EndpointFacingA,
        ARogueliteParametricRoom* TargetRoom, ERogueliteRoomSide TargetSocket,
        FTransform& OutCorridor, FTransform& OutRoomB, FString& OutReason) const;

    UFUNCTION(BlueprintCallable, Category="Roguelite|Assembly")
    FRogueliteAssemblyResult RevalidateConnection(FGuid ConnectionId);
    UFUNCTION(BlueprintPure, Category="Roguelite|Connections")
    TArray<FRogueliteRoomConnection> GetConnectionRecords() const { return Connections; }
    UFUNCTION(BlueprintPure, Category="Roguelite|Connections")
    static FGuid GetRoomSocketConnectionId(ARogueliteParametricRoom* Room, ERogueliteRoomSide Side);
    UFUNCTION(BlueprintPure, Category="Roguelite|Connections")
    static FGuid GetCorridorConnectionId(ARogueliteParametricCorridor* Connector);

private:
    bool ValidateEndpoints(ARogueliteParametricRoom* A, ERogueliteRoomSide SideA,
        ARogueliteParametricRoom* B, ERogueliteRoomSide SideB, bool bWillMoveB,
        FRogueliteDoorSocket& DoorA, FRogueliteDoorSocket& DoorB, FRogueliteAssemblyResult& Result) const;
    bool ValidateSettings(FRogueliteAssemblyResult& Result) const;
    bool CheckOccupancy(ARogueliteParametricRoom* A, ARogueliteParametricRoom* B,
        const FTransform& TargetTransform, FRogueliteAssemblyResult& Result, ARogueliteParametricCorridor* IgnoreCorridor = nullptr) const;
    bool ValidateDirect(ARogueliteParametricRoom* A, ERogueliteRoomSide SideA,
        ARogueliteParametricRoom* B, ERogueliteRoomSide SideB, FRogueliteAssemblyResult& Result) const;
    bool IsOccupied(ARogueliteParametricRoom* Room, ERogueliteRoomSide Side, const FGuid& ExceptId) const;
    bool HasOtherConnection(AActor* Actor, const FGuid& ExceptId) const;
    bool IsCorridorOccupied(ARogueliteParametricCorridor* Connector, const FGuid& ExceptId) const;
    bool CheckCorridorOccupancy(ARogueliteParametricRoom* A, ARogueliteParametricCorridor* C, ARogueliteParametricRoom* B,
        const FTransform& CorridorTransform, const FTransform& TargetTransform, FRogueliteAssemblyResult& Result) const;
    bool ValidateCorridorConnection(const FRogueliteRoomConnection& Connection, FRogueliteAssemblyResult& Result) const;
    bool CheckCorridorPassage(const FRogueliteRoomConnection& Connection, const FRogueliteDoorSocket& DoorA,
        const FRogueliteDoorSocket& DoorB, FRogueliteAssemblyResult& Result) const;
    void RecordValidation(FRogueliteRoomConnection& Connection, const FRogueliteAssemblyResult& Result);
    void PublishResult(const FRogueliteAssemblyResult& Result);
};
