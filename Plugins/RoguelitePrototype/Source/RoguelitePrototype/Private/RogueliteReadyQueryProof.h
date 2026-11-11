#pragma once
#include "CoreMinimal.h"
#include "UObject/ObjectKey.h"
#include "RogueliteLoopCommit.h"

class ARogueliteRuntimeLoopAssembly;
class URogueliteNavigationReadinessComponent;
class ARogueliteRoomAssembly;
class ANavigationData;
class UNavigationSystemV1;
struct FRogueliteSeedBatch;
struct FRogueliteSeedLayoutSettings;
struct FRogueliteCommittedLoopBatch;
struct FRogueliteNavigationReadinessEvidence;

enum class ERogueliteReadyProofAdmission : uint8 { Rejected, Legacy, Native };
enum class ERogueliteReadyProofFailure : uint8
{
    None, QueryLifetime, Identity, World, Batch, Registry, Revision, Digest, Configuration,
    Transform, Navigation, Epoch, Structure
};

// Not a UObject, reflected value, stored result, or transferable capability.
// Only the producing Source's current synchronous stack may own and consume it.
struct FRogueliteReadyQueryProof final
{
private:
    friend class ARogueliteRuntimeLoopAssembly;
    friend class URogueliteNavigationReadinessComponent;
#if WITH_DEV_AUTOMATION_TESTS
    friend struct FRogueliteReadyProofTestAccess;
    friend struct FRogueliteTraversalReuseTestAccess;
#endif
    FRogueliteReadyQueryProof() = default;
    ~FRogueliteReadyQueryProof() = default;
    FRogueliteReadyQueryProof(const FRogueliteReadyQueryProof&) = delete;
    FRogueliteReadyQueryProof(FRogueliteReadyQueryProof&&) = delete;
    FRogueliteReadyQueryProof& operator=(const FRogueliteReadyQueryProof&) = delete;
    FRogueliteReadyQueryProof& operator=(FRogueliteReadyQueryProof&&) = delete;

    const ARogueliteRuntimeLoopAssembly* Source=nullptr;
    const URogueliteNavigationReadinessComponent* Readiness=nullptr;
    UWorld* World=nullptr;
    ARogueliteRoomAssembly* Registry=nullptr;
    ANavigationData* NavData=nullptr;
    UNavigationSystemV1* Navigation=nullptr;
    const FRogueliteSeedBatch* Batch=nullptr;
    const FRogueliteSeedLayoutSettings* Settings=nullptr;
    const FRogueliteCommittedLoopBatch* Loops=nullptr;
    const FRogueliteNavigationReadinessEvidence* Report=nullptr;
    FObjectKey SourceKey,ReadinessKey,WorldKey,RegistryKey,NavDataKey,NavigationKey;
    TArray<FObjectKey,TInlineAllocator<2>> NavigationDataKeys;
    FGuid BatchId;
    int32 GraphRevision=0,GeometryRevision=0;
    FString ConfigurationDigest,GraphDigest,LayoutDigest,GeometryDigest,Epoch,WorldPath;
    FTransform SourceTransform=FTransform::Identity;
    bool bProduced=false,bConsumed=false;
    // Only a strictly admitted Snapshot stack may request this payload. The
    // navigation proof still has to be produced and consumed by its old API.
    TOptional<FRogueliteLoopTraversalSnapshot> Traversal;
    FRogueliteReadyQueryProof* TraversalRequest=nullptr;
    bool bTraversalAdmitted=false,bTraversalDelivered=false,bTraversalInvalidated=false;
    ERogueliteReadyProofFailure Failure=ERogueliteReadyProofFailure::None;
};
