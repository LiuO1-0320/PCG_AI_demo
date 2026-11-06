#include "Gameplay/RogueliteCombatSlicePlayer.h"
#include "Gameplay/RogueliteCombatSliceActors.h"
#include "RogueliteDynamicGameplayBridge.h"
#include "RogueliteRuntimeLoopAssembly.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "EnhancedPlayerInput.h"
#include "Engine/LocalPlayer.h"
#include "Engine/Canvas.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "InputCoreTypes.h"
#include "DrawDebugHelpers.h"

namespace RogueliteSliceInput
{
void MapAxis(UInputMappingContext* Context,UInputAction* Action,FKey Key,bool Negate,bool Swizzle)
{
    auto& Mapping=Context->MapKey(Action,Key);
    if(Negate) Mapping.Modifiers.Add(NewObject<UInputModifierNegate>(Context));
    if(Swizzle) Mapping.Modifiers.Add(NewObject<UInputModifierSwizzleAxis>(Context));
}
}

ARogueliteSlicePlayer::ARogueliteSlicePlayer()
{
    PrimaryActorTick.bCanEverTick=true;
    AutoPossessAI=EAutoPossessAI::Disabled;
    OverrideInputComponentClass=UEnhancedInputComponent::StaticClass();
    GetCapsuleComponent()->InitCapsuleSize(34.f,88.f);
    GetCapsuleComponent()->SetCollisionObjectType(ECC_Pawn);
    GetCapsuleComponent()->SetCollisionResponseToAllChannels(ECR_Block);
    GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Camera,ECR_Ignore);
    GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    GetCapsuleComponent()->SetCanEverAffectNavigation(false);
    GetMesh()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    GetMesh()->SetCanEverAffectNavigation(false);
    GetCharacterMovement()->SetMovementMode(MOVE_None);
    GetCharacterMovement()->MaxWalkSpeed=600.f;
    GetCharacterMovement()->JumpZVelocity=420.f;
    GetCharacterMovement()->bOrientRotationToMovement=false;
    bUseControllerRotationYaw=true;
    FirstPersonCamera=CreateDefaultSubobject<UCameraComponent>(TEXT("G1BFirstPersonCamera"));
    FirstPersonCamera->SetupAttachment(GetCapsuleComponent());
    FirstPersonCamera->SetRelativeLocation(FVector(0,0,64));
    FirstPersonCamera->bUsePawnControlRotation=true;
    HealthComponent=CreateDefaultSubobject<URogueliteSliceHealth>(TEXT("G1BPlayerHealth"));
}

void ARogueliteSlicePlayer::BeginPlay()
{
    Super::BeginPlay();
    HealthComponent->OnKilled.AddUObject(this,&ARogueliteSlicePlayer::PlayerKilled);
}

void ARogueliteSlicePlayer::Restart()
{
    Super::Restart();
    // Possess/client restart restores the engine default movement mode; keep the waiting pawn inert.
    if(!HasLiveBinding())UnbindFromDungeon();
}

bool ARogueliteSlicePlayer::BindToDungeon(ARogueliteRuntimeLoopAssembly* Source,const FTransform& Spawn,FString& Reason)
{
    if(!HealthComponent||!HealthComponent->IsAlive())
    {
        Reason=TEXT("A dead player cannot bind or respawn within this prototype run.");BindingStatus=Reason;return false;
    }
    const auto* PC=Cast<APlayerController>(GetController());
    if(!IsInGameThread()||!IsValid(Source)||!GetWorld()||!GetWorld()->IsGameWorld()
        ||Source->GetWorld()!=GetWorld()||!PC||!PC->IsLocalController()||!PC->GetLocalPlayer()
        ||PC->GetPawn()!=this||GetOwner()!=PC)
    {
        Reason=TEXT("G1B player binding requires its real possessed local PlayerController, same GameWorld and actual Pawn owner.");
        BindingStatus=Reason;return false;
    }
    if(!Source->IsDungeonReady()||!Source->GetTraversalSnapshot().bCurrent||!Source->Batch.BatchId.IsValid())
    {
        Reason=TEXT("G1B player waits for the source's original strict navigation Ready and current traversal.");
        BindingStatus=Reason;return false;
    }
    if(!GetActorScale3D().Equals(FVector::OneVector,.0001)||!Spawn.GetScale3D().Equals(FVector::OneVector,.0001)
        ||Spawn.ContainsNaN())
    {
        Reason=TEXT("G1B player spawn requires a finite, unit-scale transform; Actor scaling is unsupported.");
        BindingStatus=Reason;return false;
    }
    if(BoundSource.Get()==Source&&BoundBatch==Source->Batch.BatchId&&HasLiveBinding())
    {
        Reason=TEXT("G1B player is already bound to this current batch.");return true;
    }
    FCollisionQueryParams Query(SCENE_QUERY_STAT(G1B_PlayerSpawn),false,this);
    if(GetWorld()->OverlapBlockingTestByChannel(Spawn.GetLocation(),FQuat::Identity,ECC_Pawn,
        FCollisionShape::MakeCapsule(34.f,88.f),Query))
    {
        Reason=TEXT("G1B player spawn capsule overlaps existing blocking geometry.");
        BindingStatus=Reason;return false;
    }
    UnbindFromDungeon();
    if(!FRogueliteDynamicGameplayBridge::Enable(Source,Source->Batch.BatchId,Reason))
    {BindingStatus=Reason;return false;}
    SetActorLocationAndRotation(Spawn.GetLocation(),Spawn.GetRotation(),false,nullptr,ETeleportType::TeleportPhysics);
    if(!FRogueliteDynamicGameplayBridge::RegisterComponent(Source,Source->Batch.BatchId,this,GetCapsuleComponent(),
        ERogueliteDynamicParticipant::Pawn,Reason))
    {BindingStatus=Reason;return false;}
    BoundSource=Source;BoundBatch=Source->Batch.BatchId;
    AcceptedShots.Empty();LastShot=FRogueliteSliceShot();NextShotTime=0;
    GetCharacterMovement()->StopMovementImmediately();
    GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    GetCharacterMovement()->SetMovementMode(MOVE_Walking);
    GetController()->SetControlRotation(Spawn.Rotator());
    BindingStatus=TEXT("Ready - WASD / mouse / Space");
    Reason=TEXT("Actual local PlayerController ownership verified; exact root capsule registered before Pawn collision and walking.");
    return true;
}

void ARogueliteSlicePlayer::UnbindFromDungeon()
{
    GetCharacterMovement()->StopMovementImmediately();
    GetCharacterMovement()->SetMovementMode(MOVE_None);
    StopJumping();
    GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    if(auto* Source=BoundSource.Get()) FRogueliteDynamicGameplayBridge::UnregisterComponent(Source,GetCapsuleComponent());
    BoundSource.Reset();BoundBatch.Invalidate();BindingStatus=TEXT("Waiting for dungeon");
    AcceptedShots.Empty();LastShot=FRogueliteSliceShot();
}

ARogueliteRuntimeLoopAssembly* ARogueliteSlicePlayer::GetDungeonSource() const
{
    return BoundSource.Get();
}

bool ARogueliteSlicePlayer::FindAcceptedShot(FGuid Id,FRogueliteSliceShot& OutShot) const
{
    if(!Id.IsValid()||!HasLiveBinding())return false;
    if(const auto* Recorded=AcceptedShots.Find(Id);Recorded&&Recorded->Shooter.Get()==this&&Recorded->BatchId==BoundBatch)
    {OutShot=*Recorded;return true;}
    return false;
}

bool ARogueliteSlicePlayer::TryFire()
{
    auto Reject=[this](const FString& Why)
    {
        ++ShotRejectedCount;LastRejectedReason=Why;LastFireOutcome=TEXT("Rejected: ")+Why;LastAppliedDamage=0;
        UE_LOG(LogTemp,Display,TEXT("G1B_SHOT_REJECT Player=%s Reason=%s"),*GetName(),*Why);return false;
    };
    if(!GetWorld()||GetWorld()->bIsTearingDown||!HealthComponent||!HealthComponent->IsAlive())return Reject(TEXT("Player is not alive in an active world"));
    if(!HasLiveBinding())return Reject(TEXT("No current dungeon binding"));
    if(bResolvingShot)return Reject(TEXT("Another shot is still resolving"));
    if(Ammo<=0)return Reject(TEXT("Empty ammunition"));
    if(!FMath::IsFinite(FireIntervalSeconds)||FireIntervalSeconds<=0||!FMath::IsFinite(ShotDamage)||ShotDamage<=0
        ||!FMath::IsFinite(WeaponRangeCm)||WeaponRangeCm<=0)return Reject(TEXT("Invalid weapon configuration"));
    const double Now=GetWorld()->GetTimeSeconds();
    if(Now<NextShotTime)return Reject(TEXT("Shot interval has not elapsed"));
    if(!IsValid(FirstPersonCamera))return Reject(TEXT("First-person camera is unavailable"));
    TGuardValue<bool> ResolvingGuard(bResolvingShot,true);
    FRogueliteSliceShot Shot;
    Shot.Id=FGuid::NewGuid();Shot.BatchId=BoundBatch;Shot.Shooter=this;Shot.PawnLocation=GetActorLocation();
    Shot.Origin=FirstPersonCamera->GetComponentLocation();Shot.Direction=FirstPersonCamera->GetForwardVector().GetSafeNormal();Shot.Damage=ShotDamage;
    --Ammo;NextShotTime=Now+FireIntervalSeconds;++ShotAcceptedCount;LastAppliedDamage=0;LastRejectedReason.Empty();
    LastFireOutcome=TEXT("Shot accepted");LastShot=Shot;AcceptedShots.Add(Shot.Id,Shot);
    // A legitimate encounter can activate here before first-shot damage; the player never activates enemies itself.
    OnShotAccepted.Broadcast(Shot);
    if(!HasLiveBinding()||BoundBatch!=Shot.BatchId||!HealthComponent->IsAlive())
    {LastFireOutcome=TEXT("Shot accepted; binding invalidated before hit resolution");return true;}
    const FVector End=Shot.Origin+Shot.Direction*WeaponRangeCm;
    FCollisionQueryParams Query(SCENE_QUERY_STAT(G1B_PlayerShot),false,this);
    Shot.bHit=GetWorld()->LineTraceSingleByChannel(Shot.Hit,Shot.Origin,End,ECC_Visibility,Query);
    LastShot=Shot;AcceptedShots.Add(Shot.Id,Shot);
    if(auto* Enemy=Cast<ARogueliteSliceEnemy>(Shot.Hit.GetActor());Shot.bHit&&Enemy)
    {
        LastAppliedDamage=Enemy->ReceiveShot(this,Shot.Id);
        if(LastAppliedDamage>0){++HitConfirmedCount;TotalDamageDealt+=LastAppliedDamage;}
    }
    LastFireOutcome=LastAppliedDamage>0?FString::Printf(TEXT("Hit confirmed: %.0f damage"),LastAppliedDamage)
        :(Shot.bHit?TEXT("Shot accepted: world or non-damageable hit"):TEXT("Shot accepted: miss"));
    DrawDebugLine(GetWorld(),Shot.Origin,Shot.bHit?Shot.Hit.ImpactPoint:End,LastAppliedDamage>0?FColor::Green:FColor::Yellow,false,.25f,0,1.f);
    UE_LOG(LogTemp,Display,TEXT("G1B_SHOT Id=%s Batch=%s Hit=%d Target=%s Damage=%.1f Ammo=%d"),
        *Shot.Id.ToString(),*Shot.BatchId.ToString(),Shot.bHit,*GetNameSafe(Shot.Hit.GetActor()),LastAppliedDamage,Ammo);
    return true;
}

float ARogueliteSlicePlayer::ReceiveTestDamage(float Damage,FGuid EventId,AActor* Causer)
{
    const auto* Enemy=Cast<ARogueliteSliceEnemy>(Causer);
    if(!HasLiveBinding()||!HealthComponent||!HealthComponent->IsAlive()||!Enemy||!Enemy->IsActive()
        ||Enemy->GetWorld()!=GetWorld()||Enemy->GetDungeonSource()!=BoundSource.Get()||Enemy->GetBoundBatch()!=BoundBatch)return 0;
    return HealthComponent->ApplyDamageOnce(Damage,EventId,Causer);
}

void ARogueliteSlicePlayer::PlayerKilled(URogueliteSliceHealth* DeadHealth)
{
    if(DeadHealth!=HealthComponent)return;
    ++PlayerDeathCount;UnbindFromDungeon();BindingStatus=TEXT("Dead - restart PIE for a new run");
}

bool ARogueliteSlicePlayer::HasLiveBinding() const
{
    const auto* Source=BoundSource.Get();
    return IsValid(Source)&&Source->GetWorld()==GetWorld()&&BoundBatch.IsValid()&&Source->Batch.BatchId==BoundBatch
        &&FRogueliteDynamicGameplayBridge::IsComponentIsolated(Source,BoundBatch,GetCapsuleComponent())
        &&Source->GetTraversalSnapshot().bCurrent;
}

void ARogueliteSlicePlayer::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if(BoundBatch.IsValid()&&!HasLiveBinding()) UnbindFromDungeon();
}

void ARogueliteSlicePlayer::UnPossessed()
{
    UnbindFromDungeon();Super::UnPossessed();
}

void ARogueliteSlicePlayer::EndPlay(const EEndPlayReason::Type Reason)
{
    UnbindFromDungeon();Super::EndPlay(Reason);
}

ARogueliteSlicePlayerController::ARogueliteSlicePlayerController()
{
    OverridePlayerInputClass=UEnhancedPlayerInput::StaticClass();
    bShowMouseCursor=false;
}

void ARogueliteSlicePlayerController::CreateInputActions()
{
    if(MappingContext)return;
    MappingContext=NewObject<UInputMappingContext>(this,NAME_None,RF_Transient);
    MoveAction=NewObject<UInputAction>(this,NAME_None,RF_Transient);
    MoveAction->ValueType=EInputActionValueType::Axis2D;
    LookAction=NewObject<UInputAction>(this,NAME_None,RF_Transient);
    LookAction->ValueType=EInputActionValueType::Axis2D;
    JumpAction=NewObject<UInputAction>(this,NAME_None,RF_Transient);
    JumpAction->ValueType=EInputActionValueType::Boolean;
    FireAction=NewObject<UInputAction>(this,NAME_None,RF_Transient);
    FireAction->ValueType=EInputActionValueType::Boolean;
    using namespace RogueliteSliceInput;
    MapAxis(MappingContext,MoveAction,EKeys::D,false,false);
    MapAxis(MappingContext,MoveAction,EKeys::A,true,false);
    MapAxis(MappingContext,MoveAction,EKeys::W,false,true);
    MapAxis(MappingContext,MoveAction,EKeys::S,true,true);
    MapAxis(MappingContext,LookAction,EKeys::MouseX,false,false);
    MapAxis(MappingContext,LookAction,EKeys::MouseY,true,true);
    MappingContext->MapKey(JumpAction,EKeys::SpaceBar);
    MappingContext->MapKey(FireAction,EKeys::LeftMouseButton);
}

void ARogueliteSlicePlayerController::SetupInputComponent()
{
    if(!InputComponent)
    {
        InputComponent=NewObject<UEnhancedInputComponent>(this,TEXT("G1BSliceControllerInput"));
        InputComponent->RegisterComponent();
    }
    Super::SetupInputComponent();CreateInputActions();
    if(auto* Enhanced=Cast<UEnhancedInputComponent>(InputComponent))
    {
        Enhanced->BindAction(MoveAction,ETriggerEvent::Triggered,this,&ARogueliteSlicePlayerController::Move);
        Enhanced->BindAction(LookAction,ETriggerEvent::Triggered,this,&ARogueliteSlicePlayerController::Look);
        Enhanced->BindAction(JumpAction,ETriggerEvent::Started,this,&ARogueliteSlicePlayerController::StartJump);
        Enhanced->BindAction(JumpAction,ETriggerEvent::Completed,this,&ARogueliteSlicePlayerController::FinishJump);
        Enhanced->BindAction(JumpAction,ETriggerEvent::Canceled,this,&ARogueliteSlicePlayerController::FinishJump);
        Enhanced->BindAction(FireAction,ETriggerEvent::Started,this,&ARogueliteSlicePlayerController::Fire);
    }
    InstallInputContext();
}

void ARogueliteSlicePlayerController::InstallInputContext()
{
    if(bContextInstalled||!IsLocalController())return;
    CreateInputActions();
    if(auto* Local=GetLocalPlayer())if(auto* Enhanced=Local->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
    {
        Enhanced->AddMappingContext(MappingContext,0);bContextInstalled=true;
    }
}

void ARogueliteSlicePlayerController::BeginPlay()
{
    Super::BeginPlay();InstallInputContext();
    if(IsLocalController())SetInputMode(FInputModeGameOnly());
}

void ARogueliteSlicePlayerController::OnPossess(APawn* InPawn)
{
    Super::OnPossess(InPawn);InstallInputContext();
}

void ARogueliteSlicePlayerController::EndPlay(const EEndPlayReason::Type Reason)
{
    if(bContextInstalled)if(auto* Local=GetLocalPlayer())if(auto* Enhanced=Local->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
        Enhanced->RemoveMappingContext(MappingContext);
    bContextInstalled=false;Super::EndPlay(Reason);
}

void ARogueliteSlicePlayerController::Move(const FInputActionValue& Value)
{
    LastMoveInput=Value.Get<FVector2D>();++MoveInputEvents;
    if(auto* FPS=Cast<ARogueliteSlicePlayer>(GetPawn());FPS&&FPS->HasLiveBinding())
    {
        const FRotator Yaw(0,GetControlRotation().Yaw,0);
        FPS->AddMovementInput(FRotationMatrix(Yaw).GetUnitAxis(EAxis::X),LastMoveInput.Y);
        FPS->AddMovementInput(FRotationMatrix(Yaw).GetUnitAxis(EAxis::Y),LastMoveInput.X);
    }
}

void ARogueliteSlicePlayerController::Look(const FInputActionValue& Value)
{
    LastLookInput=Value.Get<FVector2D>();++LookInputEvents;
    if(auto* FPS=Cast<ARogueliteSlicePlayer>(GetPawn());FPS&&FPS->HasLiveBinding())
    {AddYawInput(LastLookInput.X);AddPitchInput(LastLookInput.Y);}
}

void ARogueliteSlicePlayerController::StartJump(const FInputActionValue& Value)
{
    ++JumpInputEvents;
    if(auto* FPS=Cast<ARogueliteSlicePlayer>(GetPawn());FPS&&FPS->HasLiveBinding()&&Value.Get<bool>())FPS->Jump();
}

void ARogueliteSlicePlayerController::FinishJump(const FInputActionValue& Value)
{
    if(auto* FPS=Cast<ARogueliteSlicePlayer>(GetPawn()))FPS->StopJumping();
}

void ARogueliteSlicePlayerController::Fire(const FInputActionValue& Value)
{
    ++FireInputEvents;
    if(auto* FPS=Cast<ARogueliteSlicePlayer>(GetPawn());FPS&&Value.Get<bool>())
    {++FPS->FireInputPressedCount;FPS->TryFire();}
}

void ARogueliteSliceHUD::DrawHUD()
{
    Super::DrawHUD();
    if(!Canvas)return;
    const auto* FPS=PlayerOwner?Cast<ARogueliteSlicePlayer>(PlayerOwner->GetPawn()):nullptr;
    if(!FPS)return;
    const float CenterX=Canvas->SizeX*.5f,CenterY=Canvas->SizeY*.5f;
    DrawLine(CenterX-6,CenterY,CenterX+6,CenterY,FLinearColor::White,1.f);
    DrawLine(CenterX,CenterY-6,CenterX,CenterY+6,FLinearColor::White,1.f);
    const float HP=FPS->HealthComponent?FPS->HealthComponent->GetHealth():0;
    DrawText(FString::Printf(TEXT("HP %.0f    Ammo %d    Shots %d / rejected %d / hits %d"),HP,FPS->Ammo,
        FPS->ShotAcceptedCount,FPS->ShotRejectedCount,FPS->HitConfirmedCount),FLinearColor::White,20,20);
    DrawText(FPS->BindingStatus,FLinearColor::White,20,42);
    DrawText(FPS->LastFireOutcome,FLinearColor::White,20,64);
    if(!FPS->GameplaySummary.IsEmpty())DrawText(FPS->GameplaySummary,FLinearColor::White,20,86);
}

ARogueliteSliceGameMode::ARogueliteSliceGameMode()
{
    DefaultPawnClass=ARogueliteSlicePlayer::StaticClass();
    PlayerControllerClass=ARogueliteSlicePlayerController::StaticClass();
    HUDClass=ARogueliteSliceHUD::StaticClass();
}
