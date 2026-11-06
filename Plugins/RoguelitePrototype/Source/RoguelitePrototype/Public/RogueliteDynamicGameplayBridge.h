#pragma once
#include "CoreMinimal.h"

class AActor;
class APawn;
class AAIController;
class UPrimitiveComponent;
class ARogueliteRuntimeLoopAssembly;

enum class ERogueliteDynamicParticipant : uint8 { Pawn, Gate };
enum class ERogueliteGameplayPassage : uint8 { Unavailable, GameplayBlocked, Recovering, Traversable };

/** Base navigation readiness is deliberately distinct from current physical passage. */
struct ROGUELITEPROTOTYPE_API FRogueliteGameplayPassageQuery
{
    ERogueliteGameplayPassage State=ERogueliteGameplayPassage::Unavailable;
    FGuid BatchId,ConnectionId;
    bool bStructuralCurrent=false,bBaseNavigationReady=false;
    bool bForwardCapsule=false,bReverseCapsule=false,bFullPath=false,bMoveVerified=false;
    FString Reason;
};

/** Opt-in, GameWorld/batch scoped component leases. Never owns a Socket reservation.
 *  Registration is an explicit trusted native operation, not an Actor tag convention.
 *  All methods are game-thread only. Weak identities revoke access on teardown/Clear.
 */
class ROGUELITEPROTOTYPE_API FRogueliteDynamicGameplayBridge
{
public:
    static bool Enable(ARogueliteRuntimeLoopAssembly* Source,FGuid Batch,FString& Reason);
    static void Disable(ARogueliteRuntimeLoopAssembly* Source);
    static bool RegisterComponent(ARogueliteRuntimeLoopAssembly* Source,FGuid Batch,AActor* Participant,
        UPrimitiveComponent* Component,ERogueliteDynamicParticipant Kind,FString& Reason);
    static bool UnregisterComponent(ARogueliteRuntimeLoopAssembly* Source,UPrimitiveComponent* Component);
    static int32 ActiveComponentCount(const ARogueliteRuntimeLoopAssembly* Source);
    // Pure predicate used by GeometryDigest. Does not call Current/IsDungeonReady recursively.
    static bool IsComponentIsolated(const AActor* Source,FGuid Batch,const UPrimitiveComponent* Component);
    // Controlled test-gate collision transitions must invalidate any previous recovery proof.
    static void NotifyGateCollisionChanged(UPrimitiveComponent* Component);
    static FRogueliteGameplayPassageQuery QueryConnection(ARogueliteRuntimeLoopAssembly* Source,
        FGuid Connection,APawn* QueryingPawn=nullptr);
    static bool BeginRecovery(ARogueliteRuntimeLoopAssembly* Source,FGuid Connection,APawn* Pawn,
        AAIController* Controller,FString& Reason);
    static FString RecoveryEvidence(const ARogueliteRuntimeLoopAssembly* Source);
};
