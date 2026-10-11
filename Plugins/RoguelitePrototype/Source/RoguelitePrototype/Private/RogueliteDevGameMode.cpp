#include "RogueliteDevGameMode.h"
#include "RoguelitePrototype.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/HUD.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpectatorPawn.h"
#include "Kismet/GameplayStatics.h"

ARogueliteDevGameMode::ARogueliteDevGameMode()
{
    DefaultPawnClass = ASpectatorPawn::StaticClass();
    PlayerControllerClass = APlayerController::StaticClass();
    HUDClass = AHUD::StaticClass();
}

void ARogueliteDevGameMode::StartPlay()
{
    Super::StartPlay();

    int32 LegacyActorCount = 0;
    for (TActorIterator<AActor> It(GetWorld()); It; ++It)
    {
        // Inspect the class path without including or linking the old module.
        LegacyActorCount += It->GetClass()->GetPathName().StartsWith(TEXT("/Script/CastleDemo.")) ? 1 : 0;
    }

    const APawn* Pawn = UGameplayStatics::GetPlayerPawn(this, 0);
    UE_LOG(LogRoguelitePrototype, Display,
        TEXT("ROGUELITE_DEV_START Map=%s GameMode=%s PawnClass=%s LegacyActorCount=%d"),
        *GetWorld()->GetMapName(), *GetClass()->GetPathName(),
        Pawn ? *Pawn->GetClass()->GetPathName() : TEXT("None"), LegacyActorCount);
}
