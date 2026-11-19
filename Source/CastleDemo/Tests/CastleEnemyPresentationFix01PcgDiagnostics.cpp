#include "../CastleDemo.h"

#if !UE_BUILD_SHIPPING

#include "CollisionQueryParams.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/HitResult.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerInput.h"
#include "InputCoreTypes.h"
#include "KeyState.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/DelayedAutoRegister.h"
#include "Misc/PackageName.h"
#include "Misc/Parse.h"
#include "NavigationSystem.h"
#include "Templates/Function.h"
#include "UObject/Class.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/WeakObjectPtrTemplates.h"

namespace
{
    struct FCastleEnemyFix01PcgWorldState
    {
        bool bObservedLastMovementKey = false;
        bool bReported = false;
    };

    FString CastleEnemyFix01FormatTags(const TArray<FName>& Tags)
    {
        return FString::JoinBy(Tags, TEXT(","), [](FName Tag) { return Tag.ToString(); });
    }

    void CastleEnemyFix01ReportMasonry(UWorld* World, const FName VaultTag)
    {
        const FName CourtTag(TEXT("CastleCourtScenery"));
        int32 CourtOwners = 0;
        int32 TaggedComponents = 0;
        int32 RegisteredComponents = 0;
        int32 TaggedInstances = 0;

        for (TActorIterator<AActor> ActorIt(World); ActorIt; ++ActorIt)
        {
            AActor* Actor = *ActorIt;
            if (Actor->ActorHasTag(CourtTag))
            {
                ++CourtOwners;
                UE_LOG(LogTemp, Display, TEXT("CASTLE_ENEMY_FIX01_PCG_COURT_OWNER Owner=%s"), *Actor->GetPathName());
            }

            TInlineComponentArray<UPrimitiveComponent*> Components(Actor);
            for (const UPrimitiveComponent* Component : Components)
            {
                if (!Component->ComponentHasTag(VaultTag))
                {
                    continue;
                }

                const auto* Instances = Cast<UInstancedStaticMeshComponent>(Component);
                const int32 InstanceCount = Instances ? Instances->GetInstanceCount() : 0;
                ++TaggedComponents;
                RegisteredComponents += Component->IsRegistered() ? 1 : 0;
                TaggedInstances += InstanceCount;
                UE_LOG(LogTemp, Display,
                    TEXT("CASTLE_ENEMY_FIX01_PCG_TAGGED_COMPONENT Owner=%s Component=%s Registered=%d Instances=%d Profile=%s CollisionEnabled=%d ObjectType=%d VisibilityResponse=%d"),
                    *Actor->GetPathName(), *Component->GetPathName(), Component->IsRegistered(), InstanceCount,
                    *Component->GetCollisionProfileName().ToString(), static_cast<int32>(Component->GetCollisionEnabled()),
                    static_cast<int32>(Component->GetCollisionObjectType()), static_cast<int32>(Component->GetCollisionResponseToChannel(ECC_Visibility)));
            }
        }

        UE_LOG(LogTemp, Display,
            TEXT("CASTLE_ENEMY_FIX01_PCG_MASONRY World=%s CourtOwners=%d TaggedComponents=%d RegisteredComponents=%d TaggedInstances=%d"),
            *World->GetPathName(), CourtOwners, TaggedComponents, RegisteredComponents, TaggedInstances);

        for (const TCHAR* MeshName : { TEXT("SM_Wall_00"), TEXT("SM_Ground"), TEXT("SM_Pillar") })
        {
            const FString PackageName = FString::Printf(TEXT("/Game/Demo/CourtArt/Masonry/%s"), MeshName);
            const FString ObjectPath = FString::Printf(TEXT("%s.%s"), *PackageName, MeshName);
            const UStaticMesh* LoadedMesh = FindObject<UStaticMesh>(nullptr, *ObjectPath);
            UE_LOG(LogTemp, Display, TEXT("CASTLE_ENEMY_FIX01_PCG_ASSET Path=%s PackageExists=%d LoadedStaticMeshValid=%d"),
                *ObjectPath, FPackageName::DoesPackageExist(PackageName), IsValid(LoadedMesh));
        }
    }

    void CastleEnemyFix01ReportOverheadProbe(UWorld* World, const ACastleDirector* Director, const ACastlePlayer* Player)
    {
        const FName VaultTag(TEXT("CastleVaultMasonry"));
        const FCollisionQueryParams VaultQuery(NAME_None, false, Player);
        const FVector Offsets[] = { FVector::ZeroVector, FVector(650, 650, 0), FVector(-650, -650, 0) };
        int32 Probes = 0;

        for (int32 RoomIndex = 0; RoomIndex < Director->Rooms.Num(); ++RoomIndex)
        {
            const FCastleRoom& Room = Director->Rooms[RoomIndex];
            if (!Room.bActive)
            {
                continue;
            }

            for (const FVector& Offset : Offsets)
            {
                const FVector Start = Room.Center + Offset + FVector(0, 0, 612);
                const FVector End = Start + FVector(0, 0, 720);
                FHitResult Hit;
                ++Probes;
                const bool bHit = World->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, VaultQuery);
                const UPrimitiveComponent* Component = Hit.GetComponent();
                if (bHit && Component && Component->ComponentHasTag(VaultTag))
                {
                    continue;
                }

                const AActor* Actor = Hit.GetActor();
                UE_LOG(LogTemp, Display,
                    TEXT("CASTLE_ENEMY_FIX01_PCG_PROBE Result=FAIL World=%s Director=%s Seed=%d Floor=%d RoomIndex=%d RoomType=%s Center=(%s) Doors=%d Offset=(%s) Start=(%s) End=(%s) Channel=ECC_Visibility TraceComplex=0 IgnoredPlayer=%s Probes=%d"),
                    *World->GetPathName(), *Director->GetPathName(), Director->RunSeed, Director->Floor, RoomIndex,
                    *StaticEnum<ECastleRoom>()->GetNameStringByValue(static_cast<int64>(Room.Type)), *Room.Center.ToCompactString(),
                    Room.Doors, *Offset.ToCompactString(), *Start.ToCompactString(), *End.ToCompactString(), *Player->GetPathName(), Probes);
                UE_LOG(LogTemp, Display,
                    TEXT("CASTLE_ENEMY_FIX01_PCG_HIT Hit=%d Blocking=%d StartPenetrating=%d Time=%.6f Distance=%.3f Impact=(%s) Item=%d Actor=%s ActorTags=[%s] Component=%s ComponentTags=[%s] Profile=%s CollisionEnabled=%d ObjectType=%d VisibilityResponse=%d"),
                    bHit, Hit.bBlockingHit, Hit.bStartPenetrating, Hit.Time, Hit.Distance, *Hit.ImpactPoint.ToCompactString(), Hit.Item,
                    *GetPathNameSafe(Actor), Actor ? *CastleEnemyFix01FormatTags(Actor->Tags) : TEXT(""),
                    *GetPathNameSafe(Component), Component ? *CastleEnemyFix01FormatTags(Component->ComponentTags) : TEXT(""),
                    Component ? *Component->GetCollisionProfileName().ToString() : TEXT("None"),
                    Component ? static_cast<int32>(Component->GetCollisionEnabled()) : INDEX_NONE,
                    Component ? static_cast<int32>(Component->GetCollisionObjectType()) : INDEX_NONE,
                    Component ? static_cast<int32>(Component->GetCollisionResponseToChannel(ECC_Visibility)) : INDEX_NONE);
                CastleEnemyFix01ReportMasonry(World, VaultTag);
                return;
            }
        }

        UE_LOG(LogTemp, Display, TEXT("CASTLE_ENEMY_FIX01_PCG_PROBE Result=PASS World=%s Director=%s Seed=%d Floor=%d Probes=%d"),
            *World->GetPathName(), *Director->GetPathName(), Director->RunSeed, Director->Floor, Probes);
        CastleEnemyFix01ReportMasonry(World, VaultTag);
    }

    void CastleEnemyFix01ObservePcg(UWorld* World, ELevelTick, float)
    {
        if (!World->IsGameWorld())
        {
            return;
        }

        static TMap<TWeakObjectPtr<UWorld>, FCastleEnemyFix01PcgWorldState> WorldStates;
        FCastleEnemyFix01PcgWorldState& State = WorldStates.FindOrAdd(TWeakObjectPtr<UWorld>(World));
        if (State.bReported)
        {
            return;
        }

        TActorIterator<ACastleDirector> DirectorIt(World);
        const ACastleDirector* Director = DirectorIt ? *DirectorIt : nullptr;
        if (!Director || !Director->bReady || Director->RunSeed != 1337 || Director->Floor != 1)
        {
            return;
        }

        const auto* Player = Cast<ACastlePlayer>(UGameplayStatics::GetPlayerPawn(World, 0));
        const auto* Controller = Player ? Cast<APlayerController>(Player->GetController()) : nullptr;
        const UPlayerInput* PlayerInput = Controller ? Controller->PlayerInput.Get() : nullptr;
        auto* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
        if (!PlayerInput || !Nav || Nav->IsNavigationBuildInProgress())
        {
            return;
        }

        const FKeyState* LastMovementKey = PlayerInput->GetKeyState(EKeys::A);
        if (!LastMovementKey)
        {
            return;
        }

        State.bObservedLastMovementKey |= LastMovementKey->bDown;
        // The release can still be pending in the input accumulator at the next pre-actor tick.
        if (State.bObservedLastMovementKey && (!LastMovementKey->bDown || !LastMovementKey->EventAccumulator[IE_Released].IsEmpty()))
        {
            State.bReported = true;
            CastleEnemyFix01ReportOverheadProbe(World, Director, Player);
        }
    }

    const FDelayedAutoRegisterHelper CastleEnemyFix01PcgDiagnosticRegistration(EDelayedRegisterRunPhase::EndOfEngineInit, []
    {
        const TCHAR* CommandLine = FCommandLine::Get();
        if (FParse::Param(CommandLine, TEXT("CastleEnemyFix01PcgDiagnostics")) && FParse::Param(CommandLine, TEXT("CastleSmokeTest")))
        {
            FWorldDelegates::OnWorldPreActorTick.AddStatic(&CastleEnemyFix01ObservePcg);
        }
    });
}

#endif
