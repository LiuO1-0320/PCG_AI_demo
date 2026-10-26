#include "RogueliteOpenPassagePrototypeRig.h"
#include "RogueliteCompactCorridorContract.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"

namespace { const FName HFTag(TEXT("HF03OpenPassagePrototype")); }
ARogueliteOpenPassagePrototypeRig::ARogueliteOpenPassagePrototypeRig()
{
    PrimaryActorTick.bCanEverTick=false;
    SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("PrototypeRoot")));
    Tags.Add(HFTag);
}
FRogueliteDoorConfig& ARogueliteOpenPassagePrototypeRig::Door(ARogueliteParametricRoom* Room,ERogueliteRoomSide Side)
{
    switch(Side){case ERogueliteRoomSide::North:return Room->NorthDoor;case ERogueliteRoomSide::South:return Room->SouthDoor;case ERogueliteRoomSide::East:return Room->EastDoor;default:return Room->WestDoor;}
}
bool ARogueliteOpenPassagePrototypeRig::HasOwnedFixture() const
{
    for(AActor* A:{static_cast<AActor*>(RoomA.Get()),static_cast<AActor*>(RoomB.Get()),static_cast<AActor*>(Connector.Get()),static_cast<AActor*>(Assembly.Get())})
        if(!IsValid(A)||A->GetWorld()!=GetWorld()||A->GetOwner()!=this||!A->ActorHasTag(HFTag))return false;
    return true;
}
bool ARogueliteOpenPassagePrototypeRig::Configure(ERogueliteOpeningMode Mode)
{
    if(!GetWorld()||GetWorld()->IsGameWorld()||!HasOwnedFixture())
    {Status=TEXT("HF03 fixture changes are Editor-only and owned-actors-only. No live Static geometry changed.");return false;}
    if(Mode!=ERogueliteOpeningMode::Legacy&&Mode!=ERogueliteOpeningMode::OpenPassage)return false;
    if(Connector->CorridorLength<FRogueliteCompactCorridorContract::MinCm||Connector->CorridorLength>FRogueliteCompactCorridorContract::MaxCm)
    {Status=TEXT("HF02 requires1200..1500cm; prototype refused without altering the corridor.");return false;}
    if(Mode==ERogueliteOpeningMode::OpenPassage&&(RoomA->RoomHeight!=Connector->CorridorHeight||RoomB->RoomHeight!=Connector->CorridorHeight))
    {Status=TEXT("Unequal room/ceiling heights unsupported by Phase1. No template or dimension is changed.");return false;}
    const auto OldA=Door(RoomA,SideA),OldB=Door(RoomB,SideB);
    const float OldWidth=Connector->DoorWidth,OldHeight=Connector->DoorHeight;
    const FTransform BeforeA=RoomA->GetActorTransform(),BeforeB=RoomB->GetActorTransform(),BeforeC=Connector->GetActorTransform();
    auto& DA=Door(RoomA,SideA);auto& DB=Door(RoomB,SideB);
    DA.OpeningMode=DB.OpeningMode=Mode;
    DA.DoorWidth=DB.DoorWidth=Connector->DoorWidth=Mode==ERogueliteOpeningMode::OpenPassage?Connector->CorridorWidth:200;
    DA.DoorHeight=DB.DoorHeight=Connector->DoorHeight=Mode==ERogueliteOpeningMode::OpenPassage?Connector->CorridorHeight:250;
    RoomA->RebuildRoom();RoomB->RebuildRoom();Connector->RebuildCorridor();
    FTransform ProposedC,ProposedB;FString Why;
    bool Good=Assembly->ComputeCorridorTransforms(RoomA,SideA,Connector,ERogueliteCorridorEndpoint::Start,RoomB,SideB,ProposedC,ProposedB,Why)
        &&ProposedC.Equals(BeforeC,.0001)&&ProposedB.Equals(BeforeB,.0001);
    if(Good){LastValidation=Assembly->TryAlignCorridorConnection(RoomA,SideA,Connector,ERogueliteCorridorEndpoint::Start,RoomB,SideB);Good=LastValidation.bSucceeded;Why=LastValidation.Message;}
    if(Good&&RoomA->GetActorTransform().Equals(BeforeA)&&RoomB->GetActorTransform().Equals(BeforeB)&&Connector->GetActorTransform().Equals(BeforeC))
    {CurrentMode=Mode;Status=FString::Printf(TEXT("%s | Actual probes=%d | No Actor Transform changed | Navigation Not Tested"),Mode==ERogueliteOpeningMode::Legacy?TEXT("Legacy"):TEXT("OpenPassage"),LastValidation.Probes.Num());return true;}
    Door(RoomA,SideA)=OldA;Door(RoomB,SideB)=OldB;Connector->DoorWidth=OldWidth;Connector->DoorHeight=OldHeight;
    RoomA->RebuildRoom();RoomB->RebuildRoom();Connector->RebuildCorridor();
    const FGuid Existing=ARogueliteRoomAssembly::GetCorridorConnectionId(Connector);if(Existing.IsValid())Assembly->RevalidateConnection(Existing);
    Status=TEXT("Refused; original dimensions/configuration restored: ")+Why;return false;
}
void ARogueliteOpenPassagePrototypeRig::UseLegacy(){Configure(ERogueliteOpeningMode::Legacy);}
void ARogueliteOpenPassagePrototypeRig::UseOpenPassage(){Configure(ERogueliteOpeningMode::OpenPassage);}
void ARogueliteOpenPassagePrototypeRig::ValidatePrototype()
{
    if(!GetWorld()||GetWorld()->IsGameWorld())
    {Status=TEXT("Use Editor C2 validation or the independent PIE physics test. The legacy Revalidate API rebuilds Static corridor components and is not called in GameWorld.");return;}
    if(!HasOwnedFixture())return;
    LastValidation=Assembly->RevalidateConnection(ARogueliteRoomAssembly::GetCorridorConnectionId(Connector));
    Status=LastValidation.Message+FString::Printf(TEXT(" | Actual probes=%d | Navigation Not Tested"),LastValidation.Probes.Num());
}
#if WITH_EDITOR
namespace
{
    FRogueliteDoorSocket TestDoor(ERogueliteRoomSide Side,const FVector& Dimensions,float Offset,const FTransform& Pose)
    {
        FRogueliteDoorSocket D;D.Side=Side;D.bValid=true;D.DoorWidth=200;D.DoorHeight=250;D.WallThickness=20;D.AppliedOffset=Offset;
        const bool Y=Side==ERogueliteRoomSide::East||Side==ERogueliteRoomSide::West;
        const float Sign=Side==ERogueliteRoomSide::South||Side==ERogueliteRoomSide::West?-1:1;
        D.LocalOutwardDirection=Y?FVector(0,Sign,0):FVector(Sign,0,0);
        D.LocalPosition=D.LocalOutwardDirection*((Y?Dimensions.Y:Dimensions.X)*.5+20)+(Y?FVector(Offset,0,0):FVector(0,Offset,0));
        D.WorldPosition=Pose.TransformPosition(D.LocalPosition);D.WorldOutwardDirection=Pose.TransformVectorNoScale(D.LocalOutwardDirection);return D;
    }
    template<class T,class F> T* Make(UWorld* W,AActor* Owner,const FTransform& Pose,F Configure)
    {
        FActorSpawnParameters P;P.Owner=Owner;P.bDeferConstruction=true;P.ObjectFlags=RF_Transactional;P.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        P.CustomPreSpawnInitialization=[Configure](AActor* A){Configure(CastChecked<T>(A));};
        auto* A=W->SpawnActor<T>(T::StaticClass(),Pose,P);if(A){A->Tags.Add(HFTag);A->FinishSpawning(Pose);}return A;
    }
}
ARogueliteOpenPassagePrototypeRig* ARogueliteOpenPassagePrototypeRig::CreateEditorPrototype(UWorld* W,const FTransform& Anchor,ERogueliteRoomSide Side,float OffsetA,float OffsetB,FVector BDims,float FloorA,float FloorB,float CorridorFloor)
{
    if(!W||W->IsGameWorld())return nullptr;
    auto* Rig=W->SpawnActor<ARogueliteOpenPassagePrototypeRig>();if(!Rig)return nullptr;
    Rig->SideA=Side;Rig->SideB=static_cast<ERogueliteRoomSide>(static_cast<uint8>(Side)^1);
    auto DA=TestDoor(Side,FVector(2000,2000,400),OffsetA,Anchor),DB=TestDoor(Rig->SideB,BDims,OffsetB,FTransform::Identity);
    auto Start=DA,End=DA;Start.LocalPosition=Start.WorldPosition=FVector(-750,0,0);End.LocalPosition=End.WorldPosition=FVector(750,0,0);
    Start.LocalOutwardDirection=Start.WorldOutwardDirection=FVector(-1,0,0);End.LocalOutwardDirection=End.WorldOutwardDirection=FVector(1,0,0);
    FTransform CT,BT;FString Why;
    if(!ARogueliteRoomAssembly::ComputeCorridorSocketTransforms(DA,DB,Start,End,.1f,CT,BT,Why)){Rig->Destroy();return nullptr;}
    Rig->RoomA=Make<ARogueliteParametricRoom>(W,Rig,Anchor,[=](auto* A){A->FloorThickness=FloorA;auto& D=Door(A,Side);D.bEnabled=true;D.bAutoCenter=false;D.ManualOffset=OffsetA;});
    Rig->RoomB=Make<ARogueliteParametricRoom>(W,Rig,BT,[=](auto* B){B->RoomLength=BDims.X;B->RoomWidth=BDims.Y;B->RoomHeight=BDims.Z;B->FloorThickness=FloorB;auto& D=Door(B,Rig->SideB);D.bEnabled=true;D.bAutoCenter=false;D.ManualOffset=OffsetB;});
    Rig->Connector=Make<ARogueliteParametricCorridor>(W,Rig,CT,[=](auto* C){C->CorridorLength=1500;C->CorridorWidth=500;C->CorridorHeight=400;C->FloorThickness=CorridorFloor;});
    Rig->Assembly=Make<ARogueliteRoomAssembly>(W,Rig,FTransform::Identity,[](auto* A){A->bCheckNavigation=false;});
    if(!Rig->HasOwnedFixture()||!Rig->Configure(ERogueliteOpeningMode::Legacy)){Rig->DestroyOwnTestFixture();return nullptr;}
    Rig->SetActorLabel(TEXT("HF03_Prototype_Controls"));Rig->RoomA->SetActorLabel(TEXT("HF03_Room_A"));Rig->RoomB->SetActorLabel(TEXT("HF03_Room_B"));Rig->Connector->SetActorLabel(TEXT("HF03_Corridor_1500"));
    return Rig;
}
void ARogueliteOpenPassagePrototypeRig::DestroyOwnTestFixture()
{
    if(!GetWorld()||GetWorld()->IsGameWorld())return;
    if(IsValid(Assembly))for(const auto& C:Assembly->GetConnectionRecords())Assembly->ReleaseConnection(C.Id);
    for(AActor* A:{static_cast<AActor*>(Connector.Get()),static_cast<AActor*>(RoomB.Get()),static_cast<AActor*>(RoomA.Get()),static_cast<AActor*>(Assembly.Get())})if(IsValid(A)&&A->GetOwner()==this&&A->ActorHasTag(HFTag))A->Destroy();
    Destroy();
}
#endif
