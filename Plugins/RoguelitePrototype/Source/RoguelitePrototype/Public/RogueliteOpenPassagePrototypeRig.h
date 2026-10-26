#pragma once
#include "CoreMinimal.h"
#include "RogueliteRoomAssembly.h"
#include "RogueliteOpenPassagePrototypeRig.generated.h"

/** HF03 independent fixture only. It never generates a dungeon or changes a live GameWorld. */
UCLASS()
class ROGUELITEPROTOTYPE_API ARogueliteOpenPassagePrototypeRig : public AActor
{
    GENERATED_BODY()
public:
    ARogueliteOpenPassagePrototypeRig();
    UPROPERTY(VisibleInstanceOnly, Category="HF03 Prototype") TObjectPtr<ARogueliteParametricRoom> RoomA;
    UPROPERTY(VisibleInstanceOnly, Category="HF03 Prototype") TObjectPtr<ARogueliteParametricRoom> RoomB;
    UPROPERTY(VisibleInstanceOnly, Category="HF03 Prototype") TObjectPtr<ARogueliteParametricCorridor> Connector;
    UPROPERTY(VisibleInstanceOnly, Category="HF03 Prototype") TObjectPtr<ARogueliteRoomAssembly> Assembly;
    UPROPERTY(VisibleInstanceOnly, Category="HF03 Prototype") ERogueliteRoomSide SideA=ERogueliteRoomSide::North;
    UPROPERTY(VisibleInstanceOnly, Category="HF03 Prototype") ERogueliteRoomSide SideB=ERogueliteRoomSide::South;
    UPROPERTY(VisibleInstanceOnly, Category="HF03 Prototype") ERogueliteOpeningMode CurrentMode=ERogueliteOpeningMode::Legacy;
    UPROPERTY(VisibleInstanceOnly, Category="HF03 Prototype") FRogueliteAssemblyResult LastValidation;
    UPROPERTY(VisibleInstanceOnly, Category="HF03 Prototype") FString Status;
    UFUNCTION(CallInEditor, Category="HF03 Prototype", meta=(DisplayName="Legacy Opening / 切换旧门洞")) void UseLegacy();
    UFUNCTION(CallInEditor, Category="HF03 Prototype", meta=(DisplayName="Open Passage / 切换通顶缺口")) void UseOpenPassage();
    UFUNCTION(CallInEditor, Category="HF03 Prototype", meta=(DisplayName="Validate Prototype / 验证独立原型")) void ValidatePrototype();
    bool Configure(ERogueliteOpeningMode Mode);
    bool HasOwnedFixture() const;
    static FRogueliteDoorConfig& Door(ARogueliteParametricRoom* Room,ERogueliteRoomSide Side);
#if WITH_EDITOR
    static ARogueliteOpenPassagePrototypeRig* CreateEditorPrototype(UWorld* World,const FTransform& Anchor,
        ERogueliteRoomSide Side=ERogueliteRoomSide::North,float OffsetA=0,float OffsetB=0,FVector BDimensions=FVector(2000,2000,400),float FloorA=20,float FloorB=20,float CorridorFloor=20);
    void DestroyOwnTestFixture();
#endif
};
