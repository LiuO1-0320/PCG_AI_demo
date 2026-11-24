#include "CastleCourtProps.h"
#include "CastleDemo.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"

void CastleDressCourtProp(ACastleInteractable* Item)
{
    if(!Item || !Item->Director || Item->Director->Floor!=1)return;
    const ECastleInteract Kind=Item->Kind;
    const bool WeaponStand = Kind == ECastleInteract::Gatling || Kind == ECastleInteract::Knife;
    if(!WeaponStand && Kind!=ECastleInteract::Ammo && Kind!=ECastleInteract::Shop && Kind!=ECastleInteract::Rest)return;

    auto Shape=[](const TCHAR* Name){const FString Path=FString::Printf(TEXT("/Engine/BasicShapes/%s.%s"),Name,Name);return LoadObject<UStaticMesh>(nullptr,*Path);};
    auto Finish=[](const TCHAR* Name){const FString Path=FString::Printf(TEXT("/Game/Demo/MemoryRoomArt/%s.%s"),Name,Name);return LoadObject<UMaterialInterface>(nullptr,*Path);};
    UStaticMesh* Box=Shape(TEXT("Cube")),*Round=Shape(TEXT("Cylinder")),*Oval=Shape(TEXT("Sphere")),*Horn=Shape(TEXT("Cone"));
    UMaterialInterface* Iron=Finish(TEXT("M_Charcoal")),*Brass=Finish(TEXT("M_AntiqueGold")),*Stone=Finish(TEXT("M_MemoryStone"));
    UMaterialInterface* Cloth=Finish(TEXT("M_Velvet")),*Flame=Finish(TEXT("M_CandleFlame")),*Bone=Finish(TEXT("M_Limestone"));
    if(!Box || !Round || !Oval || !Horn || !Iron || !Brass || !Stone || !Cloth || !Flame || !Bone)return;

    // Keep the existing root for collision, proximity and use-state. Hide only its rendering.
    Item->Mesh->SetVisibility(false,false);
    Item->Label->SetVisibility(false);
    const FVector Ground=Item->GetActorLocation()-FVector(0,0,45);
    auto Piece=[&](UStaticMesh* Asset,UMaterialInterface* Material,FVector Center,FVector Size,FRotator Rotation=FRotator::ZeroRotator)
    {
        const FBoxSphereBounds Bounds=Asset->GetBounds();
        const FVector Scale=Size/(Bounds.BoxExtent*2);
        auto* Mesh=NewObject<UStaticMeshComponent>(Item);
        Item->AddInstanceComponent(Mesh);Mesh->SetupAttachment(Item->Mesh);
        Mesh->SetMobility(EComponentMobility::Movable);Mesh->SetAbsolute(false,false,true);
        Mesh->SetStaticMesh(Asset);Mesh->SetMaterial(0,Material);
        Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);Mesh->SetGenerateOverlapEvents(false);Mesh->SetCanEverAffectNavigation(false);
        Mesh->RegisterComponent();
        Mesh->SetWorldTransform(FTransform(Rotation,Ground+Center-Rotation.RotateVector(Bounds.Origin*Scale),Scale));
    };
    if (WeaponStand)
    {
        Piece(Box,Stone,{0,0,34},{92,64,68});
        Piece(Box,Brass,{0,0,69},{98,70,4});
        Piece(Box,Cloth,{0,0,72},{96,68,3});
        if (Kind == ECastleInteract::Gatling)
        {
            const FRotator BarrelAxis(90,0,0);
            Piece(Round,Iron,{-24,0,90},{33,33,35},BarrelAxis);
            for (int32 Barrel=0; Barrel<6; ++Barrel)
            {
                const double Angle=Barrel*PI/3.;
                Piece(Round,Iron,{18,8*FMath::Cos(Angle),90+8*FMath::Sin(Angle)},{6,6,61},BarrelAxis);
            }
            Piece(Round,Brass,{38,0,90},{23,23,6},BarrelAxis);
            Piece(Box,Iron,{-20,0,110},{18,8,6});
        }
        else
        {
            Piece(Round,Iron,{0,0,87},{6,6,25});
            Piece(Box,Brass,{0,0,101},{28,7,5});
            Piece(Box,Bone,{0,0,119},{9,3,32});
            Piece(Horn,Bone,{0,0,143},{9,3,18});
        }
    }
    else if(Kind==ECastleInteract::Ammo)
    {
        // Banded supply coffer; a clasp faces each approach, distinct from the violet memory chest.
        Piece(Box,Stone,{0,0,34},{86,72,62});
        Piece(Box,Iron,{0,0,67},{90,77,10});
        for(int32 Side:{-1,1})
        {
            Piece(Box,Brass,{Side*29.,0,40},{5,79,73});
            Piece(Box,Brass,{0,Side*39.,60},{13,3,17});
        }
        Piece(Box,Iron,{0,0,80},{24,6,5});
        for(int32 Side:{-1,1})Piece(Box,Iron,{Side*10.,0,76},{4,6,8});
    }
    else if(Kind==ECastleInteract::Shop)
    {
        // Stone standards and a broad slab support the iron anvil and a smith's hand hammer.
        for(int32 Side:{-1,1})Piece(Box,Stone,{Side*28.,0,25},{24,67,49});
        Piece(Box,Bone,{0,0,53},{90,81,9});
        Piece(Box,Iron,{-9,7,63},{49,31,10});
        Piece(Box,Iron,{-9,7,71},{29,21,13});
        Piece(Box,Iron,{-9,7,80},{49,27,10});
        Piece(Horn,Iron,{27,7,80},{19,14,25},FRotator(-90,0,0));
        Piece(Box,Brass,{0,-22,60},{32,5,5},FRotator(0,18,0));
        Piece(Box,Iron,{13,-18,63},{8,16,10},FRotator(0,18,0));
    }
    else
    {
        // A velvet pack and rolled blanket beside a brass travel lantern.
        Piece(Oval,Cloth,{-8,9,32},{71,53,61});
        Piece(Round,Cloth,{-8,9,65},{29,29,72},FRotator(-90,0,0));
        for(int32 Side:{-1,1})Piece(Round,Brass,{-8.+Side*20.,9,65},{30,30,5},FRotator(-90,0,0));
        Piece(Round,Iron,{26,-24,7},{28,28,12});
        Piece(Oval,Flame,{26,-24,27},{14,14,29});
        for(int32 Side:{-1,1})Piece(Round,Brass,{26.+Side*10.,-24,27},{3,3,32});
        Piece(Horn,Brass,{26,-24,47},{29,29,14});
        auto* Light=NewObject<UPointLightComponent>(Item);
        Item->AddInstanceComponent(Light);Light->SetupAttachment(Item->Mesh);Light->SetMobility(EComponentMobility::Movable);
        Light->SetLightColor(FLinearColor(1,.50f,.20f));Light->SetIntensity(75);Light->SetAttenuationRadius(260);Light->SetCastShadows(false);
        Light->RegisterComponent();Light->SetWorldLocation(Ground+FVector(26,-24,32));
    }
}
