#pragma once
#include "CoreMinimal.h"

class AActor;
class UWorld;
class UStaticMesh;
class UMaterialInterface;
class UInstancedStaticMeshComponent;
struct FCastleRoom;

// One set of instance batches per generated exploration floor. Nothing here consumes gameplay RNG.
class FCastleCourtScenery
{
public:
    explicit FCastleCourtScenery(UWorld* World);
    static constexpr int32 FirstVaultSpring = 720;
    static constexpr int32 VaultRise = 310;
    static constexpr int32 WallRisePerFloor = 70;
    static constexpr int32 CrownHeight(int32 Floor) { return FirstVaultSpring + VaultRise + (Floor - 1) * WallRisePerFloor; }
    AActor* Owner = nullptr;
    UInstancedStaticMeshComponent* Shell = nullptr;
    void Room(const FCastleRoom& Node, int32 DressingSeed);
    void EncloseRoom(const FCastleRoom& Node, int32 Floor);
    void Passage(FVector Center, int32 Side);
    void RuinedCover(FVector Center, FVector Size);
    void Report(int32 Seed, int32 Floor) const;
private:
    UStaticMesh *Cube=nullptr, *Wall=nullptr, *Tiles=nullptr, *Pillar=nullptr, *Sphere=nullptr, *Candle=nullptr, *Standard=nullptr;
    UStaticMesh *ShatteredPortrait=nullptr, *DroppedFrameRail=nullptr;
    UMaterialInterface *Stone=nullptr, *Paving=nullptr, *Limestone=nullptr, *Dark=nullptr, *Wine=nullptr, *Gold=nullptr, *Flame=nullptr;
    UInstancedStaticMeshComponent* UpperShell = nullptr;
    TMap<TPair<UStaticMesh*,UMaterialInterface*>,UInstancedStaticMeshComponent*> Batches;
    int32 RoomCount=0, PassageCount=0;
    void Put(UStaticMesh* Mesh, UMaterialInterface* Mat, FVector Center, FVector LocalSize, FRotator Rot=FRotator::ZeroRotator);
    void WallPanel(FVector Center, double Length, double Depth, double Height, double Yaw);
    void SolidStone(FVector Center, FVector LocalSize, FRotator Rot, UMaterialInterface* Finish);
    void Vault(FVector Center, double Yaw, double Length, double HalfSpan, double Spring, double Rise, bool LargeHall);
    void Arch(FVector Base, FVector N, double Yaw);
    void Banner(FVector Top, double Yaw, double Width, double Height);
    void Frame(FVector Center, FVector N, double Yaw, bool Broken);
    void Sconce(FVector Base, FVector N, double Yaw);
    void Light(FVector Pos,FLinearColor Color,float Intensity,float Radius);
};
