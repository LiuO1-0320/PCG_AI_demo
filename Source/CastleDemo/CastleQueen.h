#pragma once
#include "CastleDemo.h"
#include "CastleQueen.generated.h"

UCLASS()
class CASTLEDEMO_API ACastleQueen : public ACastleEnemy
{
    GENERATED_BODY()
public:
    ACastleQueen();
    virtual void Configure(bool InBoss,int32 Floor,ACastleDirector* InDirector) override;
    virtual void Tick(float Dt) override;
    virtual float TakeDamage(float Damage,const FDamageEvent& Event,AController* DamageInstigator,AActor* Causer) override;
    virtual void ResolvePendingBlast(ACastlePlayer* Player) override;
    virtual void UpdateAttack(ACastlePlayer* Player,bool CanSee) override;
    virtual void UpdateGroundSlam(ACastlePlayer* Player) override;
    virtual void UpdatePursuit(ACastlePlayer* Player,bool CanSee) override;
    virtual bool IsChargingBlast() const override {return Action==2;}
    bool AcceptBoltHit();
    UPROPERTY() TObjectPtr<class USkeletalMeshComponent> QueenMesh;
    UPROPERTY() TObjectPtr<class UAnimSequence> ShootAnimation;
    UPROPERTY() TObjectPtr<class UAnimSequence> ChargeAnimation;
    UPROPERTY() TObjectPtr<class UMaterialInstanceDynamic> FrameGlow;
    UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Cores;
    UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Halos;
    UPROPERTY() TArray<TObjectPtr<class UPointLightComponent>> Lights;
    int32 Action=0; // idle, volley charge, ground charge, phase transition
    bool bPhaseTwo=false;
private:
    bool UpdateCover(ACastlePlayer* Player);
    void LeaveCover();
    FVector CoverGoal = FVector::ZeroVector;
    bool bSeekingCover = false;
    bool bAtCover = false;
    int32 CoverIndex = INDEX_NONE, LastCoverIndex = INDEX_NONE;
    float CoverDeadline = 0, NextCover = 0;
    float Due=0, NextVolley=0, NextGround=0, Recovery=0, FlashUntil=0, LastHit=-100;
    void BeginAction(int32 NewAction,float Delay);
    void ClearQueenBolts();
    FVector Muzzle(int32 Side) const;
};

UCLASS()
class CASTLEDEMO_API ACastleQueenBolt : public ACastleBolt
{
    GENERATED_BODY()
public:
    ACastleQueenBolt();
    virtual void Tick(float Dt) override;
};
