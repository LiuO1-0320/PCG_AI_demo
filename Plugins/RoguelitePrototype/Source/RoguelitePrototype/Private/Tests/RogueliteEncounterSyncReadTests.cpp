#include "Tests/RogueliteEncounterSyncReadTestTypes.h"
#include "RogueliteRuntimeLoopAssembly.h"
#include "RogueliteRoomTemplate.h"

ECollisionEnabled::Type URogueliteEncounterSyncGetter::GetCollisionEnabled() const
{
    ++QueryCount;
    if(MutationMode==3&&Encounter.IsValid()&&ReentryCalls<2)
    {
        bOuterSlotPreserved&=IsOuterReadCurrent&&IsOuterReadCurrent();
        ++ReentryCalls;++ReentryDepth;
        MaximumReentryDepth=FMath::Max(MaximumReentryDepth,ReentryDepth);
        Encounter->TickComponent(.02f,LEVELTICK_All,&Encounter->PrimaryComponentTick);
        --ReentryDepth;bChanged=true;
    }
    if(MutationMode&&!bChanged&&Source.IsValid())
    {
        bChanged=true;
        if(MutationMode==1&&!Source->Batch.Rooms.IsEmpty())
            Source->Batch.Rooms[0].Room->RoomLength+=10;
        if(MutationMode==2)Source->ClearRuntime();
    }
    return Super::GetCollisionEnabled();
}

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Gameplay/RogueliteEncounterSynchronousRead.h"
#include "Gameplay/RogueliteCombatSliceActors.h"
#include "Gameplay/RogueliteCombatSlicePlayer.h"
#include "RogueliteNavigationReadinessComponent.h"
#include "Tests/RogueliteCombatSliceRig.h"
#include "Components/BoxComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Editor.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "EngineUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "HAL/FileManager.h"
#include "Tests/AutomationEditorCommon.h"
#include "UObject/StrongObjectPtr.h"
#include <type_traits>

static_assert(!std::is_copy_constructible_v<FRogueliteEncounterSynchronousRead>);
static_assert(!std::is_move_constructible_v<FRogueliteEncounterSynchronousRead>);
static_assert(!std::is_copy_assignable_v<FRogueliteEncounterSynchronousRead>);
static_assert(!std::is_move_assignable_v<FRogueliteEncounterSynchronousRead>);

namespace EncounterSyncReadTests
{
const TCHAR* Map=TEXT("/Game/Roguelite/Tests/P04G2/L_Roguelite_TwoCombatRooms_Dev");

// Exact scalar bits make the comparison independent of display rounding.
template<typename T>
void Value(FString& Text,const TCHAR* Name,const T& Item)
{
    static_assert(std::is_arithmetic_v<T>||std::is_enum_v<T>);
    Text+=FString(Name)+TEXT("=")+BytesToHex(reinterpret_cast<const uint8*>(&Item),sizeof(T))+TEXT("\n");
}
void Value(FString& Text,const TCHAR* Name,const FString& Item)
{Text+=FString(Name)+TEXT("=")+FString::FromInt(Item.Len())+TEXT(":")+Item+TEXT("\n");}
void Value(FString& Text,const TCHAR* Name,const FGuid& Item)
{Value(Text,Name,Item.ToString(EGuidFormats::Digits));}
void Value(FString& Text,const TCHAR* Name,const FVector& Item)
{
    Text+=FString(Name)+TEXT("\n");
    Value(Text,TEXT("X"),Item.X);Value(Text,TEXT("Y"),Item.Y);Value(Text,TEXT("Z"),Item.Z);
}
void Pose(FString& Text,const FTransform& Transform)
{
    Value(Text,TEXT("Location"),Transform.GetLocation());Value(Text,TEXT("Scale"),Transform.GetScale3D());
    const FQuat Rotation=Transform.GetRotation();
    Value(Text,TEXT("QuatX"),Rotation.X);Value(Text,TEXT("QuatY"),Rotation.Y);
    Value(Text,TEXT("QuatZ"),Rotation.Z);Value(Text,TEXT("QuatW"),Rotation.W);
}
void Ids(FString& Text,const TCHAR* Name,const TArray<FGuid>& Items)
{
    Text+=FString(Name)+TEXT("\n");Value(Text,TEXT("Count"),Items.Num());
    for(const FGuid& Id:Items)Value(Text,TEXT("Id"),Id);
}
void Ids(FString& Text,const TCHAR* Name,const TSet<FGuid>& Items)
{
    TArray<FString> Sorted;for(const FGuid& Id:Items)Sorted.Add(Id.ToString(EGuidFormats::Digits));
    Sorted.Sort();Text+=FString(Name)+TEXT("\n");Value(Text,TEXT("Count"),Sorted.Num());
    for(const FString& Id:Sorted)Value(Text,TEXT("Id"),Id);
}
bool EqualSnapshot(const FRogueliteLoopTraversalSnapshot& A,const FRogueliteLoopTraversalSnapshot& B)
{return FRogueliteLoopTraversalSnapshot::StaticStruct()->CompareScriptStruct(&A,&B,0);}
FString SnapshotText(const FRogueliteLoopTraversalSnapshot& Snapshot)
{
    FString Text;
    FRogueliteLoopTraversalSnapshot::StaticStruct()->ExportText(Text,&Snapshot,nullptr,nullptr,PPF_None,nullptr);
    return Text;
}
FRogueliteLoopTraversalSnapshot LegacySnapshot(ARogueliteRuntimeLoopAssembly& Source)
{
    auto Snapshot=FRogueliteLoopCommitQueries::Capture(&Source,Source.Batch,Source.Settings,Source.CommittedLoops);
    Snapshot.bNavigationReady=Snapshot.bCurrent&&Source.IsDungeonReady();
    Snapshot.NavigationState=Snapshot.bNavigationReady?TEXT("NavigationReady"):TEXT("NavigationPending");
    return Snapshot;
}

// These observers never call HasLiveBinding, IsActive, Describe or passage queries.
FString ObserveWorld(ARogueliteCombatSliceRig& Source)
{
    FString Text;Value(Text,TEXT("Leases"),FRogueliteDynamicGameplayBridge::ActiveComponentCount(&Source));
    Value(Text,TEXT("Batch"),Source.Batch.BatchId);
    Value(Text,TEXT("OwnedCount"),Source.Batch.OwnedActors.Num());
    Value(Text,TEXT("Rooms"),Source.Batch.Rooms.Num());Value(Text,TEXT("Edges"),Source.Batch.Edges.Num());
    TArray<AActor*> Actors;for(TActorIterator<AActor> It(Source.GetWorld());It;++It)Actors.Add(*It);
    Actors.Sort([](const AActor& A,const AActor& B){return A.GetPathName()<B.GetPathName();});
    for(const AActor* Actor:Actors)
    {
        Value(Text,TEXT("Actor"),Actor->GetPathName());Value(Text,TEXT("Class"),Actor->GetClass()->GetPathName());
        Value(Text,TEXT("Owner"),GetPathNameSafe(Actor->GetOwner()));Pose(Text,Actor->GetActorTransform());
        Value(Text,TEXT("ActorCollision"),Actor->GetActorEnableCollision());Value(Text,TEXT("ActorTick"),Actor->IsActorTickEnabled());
        TInlineComponentArray<UPrimitiveComponent*> Parts;Actor->GetComponents(Parts);
        Parts.Sort([](const UPrimitiveComponent& A,const UPrimitiveComponent& B){return A.GetPathName()<B.GetPathName();});
        for(const UPrimitiveComponent* Part:Parts)
        {
            Value(Text,TEXT("Primitive"),Part->GetPathName());Pose(Text,Part->GetComponentTransform());
            Value(Text,TEXT("Registered"),Part->IsRegistered());Value(Text,TEXT("Mobility"),uint8(Part->Mobility));
            Value(Text,TEXT("Collision"),uint8(Part->GetCollisionEnabled()));
            Value(Text,TEXT("PawnResponse"),uint8(Part->GetCollisionResponseToChannel(ECC_Pawn)));
            Value(Text,TEXT("VisibilityResponse"),uint8(Part->GetCollisionResponseToChannel(ECC_Visibility)));
            Value(Text,TEXT("ObjectType"),uint8(Part->GetCollisionObjectType()));
        }
        if(const auto* Player=Cast<ARogueliteSlicePlayer>(Actor))
        {
            Value(Text,TEXT("HP"),Player->HealthComponent->GetHealth());Value(Text,TEXT("DamageEvents"),Player->HealthComponent->DamageEvents);
            Value(Text,TEXT("Ammo"),Player->Ammo);Value(Text,TEXT("Accepted"),Player->ShotAcceptedCount);
            Value(Text,TEXT("Rejected"),Player->ShotRejectedCount);Value(Text,TEXT("Hits"),Player->HitConfirmedCount);
            Value(Text,TEXT("Reloading"),Player->IsReloading());Value(Text,TEXT("ReloadStarts"),Player->ReloadStartedCount);
            Value(Text,TEXT("ReloadCompleted"),Player->ReloadCompletedCount);Value(Text,TEXT("ReloadCancelled"),Player->ReloadCancelledCount);
            Value(Text,TEXT("ReloadRemaining"),Player->GetReloadRemainingSeconds());
            Value(Text,TEXT("GameplaySummary"),Player->GameplaySummary);Value(Text,TEXT("Velocity"),Player->GetVelocity());
        }
        if(const auto* Enemy=Cast<ARogueliteSliceEnemy>(Actor))
        {
            Value(Text,TEXT("HP"),Enemy->Health->GetHealth());Value(Text,TEXT("DamageEvents"),Enemy->Health->DamageEvents);
            Value(Text,TEXT("Resolution"),Enemy->Health->GetResolution());Value(Text,TEXT("Dormant"),Enemy->IsDormant());
            Value(Text,TEXT("Attacks"),Enemy->AttackCount);Value(Text,TEXT("Member"),Enemy->GetMemberId());
            Value(Text,TEXT("Node"),Enemy->GetNodeId());Value(Text,TEXT("EnemyBatch"),Enemy->GetBoundBatch());
        }
        if(const auto* Gate=Cast<ARogueliteSliceGate>(Actor))
        {
            Value(Text,TEXT("GateClosed"),Gate->IsClosed());Value(Text,TEXT("GateId"),Gate->GetConnectionId());
            Value(Text,TEXT("GateNode"),Gate->GetRoomNodeId());Value(Text,TEXT("GateBatch"),Gate->GetBoundBatch());
        }
    }
    return Text;
}

struct FPrefixResult
{
    bool bIdentity=false,bRoster=false,bProduced=false,bConsumed=false,bInvalidated=false;
    FString Reason,Encounter,World;
    TOptional<FRogueliteLoopTraversalSnapshot> Payload;
};
}

/** This access is confined to development tests and adds no reflected mutation API. */
struct FRogueliteEncounterSyncReadTestAccess
{
    struct FTracking
    {
        FString LastReason;
        int32 CurrentRoom,LastFullRoom;
        FGuid Pending;
        FVector Previous;
        float Speed;
        bool bHavePrevious,bCrossed;
        double NextAttempt;
        explicit FTracking(const URogueliteSliceEncounter& E)
            :LastReason(E.LastReason),CurrentRoom(E.CurrentRoomNodeId),LastFullRoom(E.LastFullRoomNodeId),
             Pending(E.PendingEntryConnection),Previous(E.PreviousPlayerPosition),Speed(E.PreviousPlayerSpeed2D),
             bHavePrevious(E.bHavePreviousPosition),bCrossed(E.bCrossedEntryPlane),NextAttempt(E.NextPreparationAttemptAt){}
        void Restore(URogueliteSliceEncounter& E) const
        {
            E.LastReason=LastReason;E.CurrentRoomNodeId=CurrentRoom;E.LastFullRoomNodeId=LastFullRoom;
            E.PendingEntryConnection=Pending;E.PreviousPlayerPosition=Previous;E.PreviousPlayerSpeed2D=Speed;
            E.bHavePreviousPosition=bHavePrevious;E.bCrossedEntryPlane=bCrossed;E.NextPreparationAttemptAt=NextAttempt;
        }
    };
    static FString Capture(const URogueliteSliceEncounter& E)
    {
        using namespace EncounterSyncReadTests;
        FString Text;
#define PERF21_VALUE(Field) Value(Text,TEXT(#Field),E.Field)
        PERF21_VALUE(State);PERF21_VALUE(LastReason);PERF21_VALUE(TargetNodeId);PERF21_VALUE(CurrentRoomNodeId);
        PERF21_VALUE(ActivationCount);PERF21_VALUE(ClearCount);PERF21_VALUE(SpawnedEnemyCount);
        Value(Text,TEXT("Source"),GetPathNameSafe(E.BoundSource.Get()));Value(Text,TEXT("Player"),GetPathNameSafe(E.BoundPlayer.Get()));
        Value(Text,TEXT("World"),GetPathNameSafe(E.BoundWorld.Get()));
        PERF21_VALUE(BoundBatch);PERF21_VALUE(EntryConnectionId);PERF21_VALUE(ExitConnectionId);PERF21_VALUE(PendingEntryConnection);
        PERF21_VALUE(SourceSeed);PERF21_VALUE(GraphSeed);PERF21_VALUE(GraphRevision);PERF21_VALUE(GeometryRevision);
        PERF21_VALUE(StartNodeId);PERF21_VALUE(LastFullRoomNodeId);PERF21_VALUE(TraversalDigest);PERF21_VALUE(LastRecoveryDiagnostic);
        Ids(Text,TEXT("Incident"),E.IncidentConnectionIds);Ids(Text,TEXT("Members"),E.MemberIds);
        for(const auto& Enemy:E.Enemies)Value(Text,TEXT("Enemy"),GetPathNameSafe(Enemy.Get()));
        for(const auto& Actor:E.OwnedActors)Value(Text,TEXT("Owned"),GetPathNameSafe(Actor.Get()));
        TArray<FString> Gates;
        for(const auto& Pair:E.Gates)Gates.Add(Pair.Key.ToString(EGuidFormats::Digits)+TEXT("=")+GetPathNameSafe(Pair.Value.Get()));
        Gates.Sort();for(const FString& Gate:Gates)Value(Text,TEXT("Gate"),Gate);
        Ids(Text,TEXT("Killed"),E.KilledMembers);Ids(Text,TEXT("SeenShots"),E.SeenShots);
        PERF21_VALUE(PreviousPlayerPosition);PERF21_VALUE(ParkFeet);PERF21_VALUE(PreviousPlayerSpeed2D);
        Value(Text,TEXT("Probe"),GetPathNameSafe(E.RecoveryProbe.Get()));
        PERF21_VALUE(bCoordinated);PERF21_VALUE(bHavePreviousPosition);PERF21_VALUE(bCrossedEntryPlane);
        PERF21_VALUE(bCleaning);PERF21_VALUE(bFailSecondSpawn);PERF21_VALUE(bRecoveryRequested);PERF21_VALUE(bParkRequested);
        PERF21_VALUE(bParkFinished);PERF21_VALUE(bRecoveryTimedOut);PERF21_VALUE(RecoveryAttempts);
        PERF21_VALUE(RecoveryStartedAt);PERF21_VALUE(NextRecoveryAttemptAt);PERF21_VALUE(NextPreparationAttemptAt);
#undef PERF21_VALUE
        return Text;
    }
    static bool Admitted(const URogueliteSliceEncounter& E){return E.CanStartSynchronousRead();}
    static EncounterSyncReadTests::FPrefixResult Prefix(URogueliteSliceEncounter& E,ARogueliteCombatSliceRig& Source,bool Pilot)
    {
        using namespace EncounterSyncReadTests;
        FPrefixResult Result;
        FRogueliteEncounterSynchronousRead Read(&E);
        {
            TGuardValue<FRogueliteEncounterSynchronousRead*> Scope(E.ActiveSynchronousRead,Pilot?&Read:nullptr);
            Result.bIdentity=E.CurrentIdentity(Result.Reason,Pilot?&Read:nullptr);
            Result.bProduced=Read.IsProduced();
            if(Read.StoredTraversal.IsSet())Result.Payload=Read.StoredTraversal.GetValue();
            if(Result.bIdentity&&E.BoundPlayer->HealthComponent->IsAlive())
            {
                Result.bRoster=E.CompleteRoster(Result.Reason);
                if(Result.bRoster)E.TrackEntry(.02f,Pilot?&Read:nullptr);
            }
            Result.bConsumed=Read.IsConsumed();Result.bInvalidated=Read.IsInvalidated();
            Read.Close();
        }
        Result.Encounter=Capture(E);Result.World=ObserveWorld(Source);
        return Result;
    }
    static bool Identity(URogueliteSliceEncounter& E,FString& Reason,bool Pilot)
    {
        FRogueliteEncounterSynchronousRead Read(&E);
        TGuardValue<FRogueliteEncounterSynchronousRead*> Scope(E.ActiveSynchronousRead,Pilot?&Read:nullptr);
        const bool Result=E.CurrentIdentity(Reason,Pilot?&Read:nullptr);Read.Close();return Result;
    }
    static bool ShutdownBoundary(URogueliteSliceEncounter& E,ARogueliteCombatSliceRig& Source,FString& Rows)
    {
        FRogueliteEncounterSynchronousRead Read(&E);
        TGuardValue<FRogueliteEncounterSynchronousRead*> Scope(E.ActiveSynchronousRead,&Read);
        FString Reason;
        if(!E.CurrentIdentity(Reason,&Read)||!Read.IsProduced())
        {Rows+=TEXT("Shutdown boundary could not produce a fresh native read: ")+Reason+TEXT("\n");return false;}
        E.bHavePreviousPosition=true;
        E.PreviousPlayerPosition=Source.GetSlicePlayer()->GetActorLocation()+FVector(1000,0,0);
        E.TrackEntry(.02f,&Read);
        Rows+=FString::Printf(TEXT("ShutdownBoundary State=%d Closed=%d PayloadRemaining=%d Reason=%s\n"),
            int32(E.State),Read.IsClosed(),Read.StoredTraversal.IsSet(),*E.LastReason);
        return E.State==ERogueliteSliceEncounterState::Cancelled&&E.ClearCount==0
            &&Read.IsClosed()&&!Read.StoredTraversal.IsSet();
    }
    static bool DirectCleanupBoundary(URogueliteSliceEncounter& E,ARogueliteCombatSliceRig& Source,bool DestroyCallback,FString& Rows)
    {
        FRogueliteEncounterSynchronousRead Read(&E);
        TGuardValue<FRogueliteEncounterSynchronousRead*> Scope(E.ActiveSynchronousRead,&Read);
        FString Reason;
        if(!E.CurrentIdentity(Reason,&Read)||!Read.IsProduced())
        {Rows+=TEXT("Direct boundary could not produce native read: ")+Reason+TEXT("\n");return false;}
        auto* Trigger=E.Enemies[0].Get();
        if(!Trigger)return false;
        const FObjectKey TriggerKey(Trigger);
        TSet<FObjectKey> Owned;
        for(const auto& Weak:E.OwnedActors)if(auto* Actor=Weak.Get())Owned.Add(FObjectKey(Actor));
        bool ClosedBeforeCleanup=true;int32 CleanupNotifications=0;
        const FDelegateHandle Observer=Source.GetWorld()->AddOnActorDestroyedHandler(FOnActorDestroyed::FDelegate::CreateLambda(
            [&](AActor* Actor)
            {
                const FObjectKey Key(Actor);
                if(!Owned.Contains(Key)||(DestroyCallback&&Key==TriggerKey))return;
                ++CleanupNotifications;
                const bool Closed=Read.IsClosed()&&!Read.StoredTraversal.IsSet();ClosedBeforeCleanup&=Closed;
                Rows+=FString::Printf(TEXT("CleanupNotification Actor=%s ClosedBeforeNotification=%d PayloadRemaining=%d\n"),
                    *Actor->GetPathName(),Closed,Read.StoredTraversal.IsSet());
            }));
        bool TriggerSucceeded=true;
        if(DestroyCallback)TriggerSucceeded=Trigger->Destroy();
        else E.Shutdown(TEXT("PERF21A direct Shutdown boundary fixture"));
        Source.GetWorld()->RemoveOnActorDestroyedHandler(Observer);
        FRogueliteLoopTraversalSnapshot Out;
        const bool Accepted=Read.Consume(&Source,Source.GetSlicePlayer(),Out);
        Rows+=FString::Printf(TEXT("DirectBoundary Callback=%d TriggerSucceeded=%d Notifications=%d ClosedBeforeAll=%d Closed=%d AcceptedAfter=%d State=%d ActorsRemaining=%d\n"),
            DestroyCallback,TriggerSucceeded,CleanupNotifications,ClosedBeforeCleanup,Read.IsClosed(),Accepted,int32(E.State),E.GetGameplayActorCount());
        return TriggerSucceeded&&CleanupNotifications>0&&ClosedBeforeCleanup&&Read.IsClosed()&&!Accepted
            &&!Read.StoredTraversal.IsSet()&&E.State==ERogueliteSliceEncounterState::Cancelled
            &&E.ClearCount==0&&E.GetGameplayActorCount()==0;
    }
    static bool MultiLevelReentry(URogueliteSliceEncounter& E,ARogueliteCombatSliceRig& Source,FString& Rows)
    {
        FRogueliteEncounterSynchronousRead Read(&E);
        TGuardValue<FRogueliteEncounterSynchronousRead*> Scope(E.ActiveSynchronousRead,&Read);
        FString Reason;
        if(!E.CurrentIdentity(Reason,&Read)||!Read.IsProduced())
        {Rows+=TEXT("Multi-level reentry could not produce native read: ")+Reason+TEXT("\n");return false;}
        FActorSpawnParameters Params;Params.ObjectFlags|=RF_Transient;
        auto* Actor=Source.GetWorld()->SpawnActor<AActor>(Params);
        if(!Actor)return false;
        auto* Getter=NewObject<URogueliteEncounterSyncGetter>(Actor);Actor->SetRootComponent(Getter);
        Getter->SetMobility(EComponentMobility::Movable);Getter->SetCanEverAffectNavigation(false);
        Getter->SetCollisionEnabled(ECollisionEnabled::NoCollision);Getter->RegisterComponent();
        Getter->Source=&Source;Getter->Encounter=&E;
        Getter->IsOuterReadCurrent=[&](){return E.ActiveSynchronousRead==&Read;};Getter->MutationMode=3;
        Source.GetTraversalSnapshot();
        const bool Retained=E.ActiveSynchronousRead==&Read;
        FRogueliteLoopTraversalSnapshot Out;Out.Reason=TEXT("multilevel rejected-output sentinel");const auto Before=Out;
        const bool Accepted=Read.Consume(&Source,Source.GetSlicePlayer(),Out);
        const bool Good=Getter->ReentryCalls==2&&Getter->MaximumReentryDepth==2&&Retained&&Getter->bOuterSlotPreserved
            &&Read.IsInvalidated()&&!Accepted&&EncounterSyncReadTests::EqualSnapshot(Before,Out)
            &&E.State==ERogueliteSliceEncounterState::AwaitingEntry&&E.ClearCount==0;
        Rows+=FString::Printf(TEXT("MultiLevelReentry Calls=%d MaximumDepth=%d OuterSlotRetained=%d OuterSlotPreservedAtEveryDepth=%d Invalidated=%d AcceptedAfter=%d State=%d Reason=%s\n"),
            Getter->ReentryCalls,Getter->MaximumReentryDepth,Retained,Getter->bOuterSlotPreserved,Read.IsInvalidated(),Accepted,int32(E.State),*E.LastReason);
        Getter->MutationMode=0;Getter->IsOuterReadCurrent=nullptr;Actor->Destroy();return Good;
    }
    template<typename Mutation>
    static bool Reject(URogueliteSliceEncounter& E,ARogueliteCombatSliceRig& Source,Mutation Change,FString& Rows)
    {
        using namespace EncounterSyncReadTests;
        auto* Player=Source.GetSlicePlayer();FRogueliteEncounterSynchronousRead Read(&E);
        TGuardValue<FRogueliteEncounterSynchronousRead*> Scope(E.ActiveSynchronousRead,&Read);
        const bool Produced=Read.Produce(&Source,Player,Source.GetTraversalSnapshot());
        if(!Produced){Rows+=TEXT("Protocol fixture failed actual fresh production\n");return false;}
        Change(Read);
        FRogueliteLoopTraversalSnapshot Out;Out.Reason=TEXT("unchanged rejected-output sentinel");const auto Before=Out;
        const bool Accepted=Read.Consume(&Source,Player,Out);
        const bool Preserved=EqualSnapshot(Before,Out);
        const bool Replay=Read.Consume(&Source,Player,Out);
        Rows+=FString::Printf(TEXT("Protocol Accepted=%d OutputPreserved=%d ReplayAccepted=%d\n"),Accepted,Preserved,Replay);
        return !Accepted&&Preserved&&!Replay;
    }
    static bool Protocol(URogueliteSliceEncounter& E,ARogueliteCombatSliceRig& Source,FAutomationTestBase& Test,FString& Rows)
    {
        bool Good=true;
        const auto Check=[&](const TCHAR* Label,bool Result)
        {Good&=Result;Test.TestTrue(Label,Result);Rows+=FString::Printf(TEXT("Check=%s Pass=%d\n"),Label,Result);};
        Check(TEXT("Owner identity"),Reject(E,Source,[](auto& R){R.OwnerKey=FObjectKey();},Rows));
        Check(TEXT("Source identity"),Reject(E,Source,[](auto& R){R.SourceKey=FObjectKey();},Rows));
        Check(TEXT("Player identity"),Reject(E,Source,[](auto& R){R.PlayerKey=FObjectKey();},Rows));
        Check(TEXT("World identity"),Reject(E,Source,[](auto& R){R.WorldKey=FObjectKey(GEditor->GetEditorWorldContext().World());},Rows));
        Check(TEXT("Registry identity"),Reject(E,Source,[](auto& R){R.RegistryKey=FObjectKey();},Rows));
        Check(TEXT("Readiness identity"),Reject(E,Source,[](auto& R){R.ReadinessKey=FObjectKey();},Rows));
        Check(TEXT("Health identity"),Reject(E,Source,[](auto& R){R.HealthKey=FObjectKey();},Rows));
        Check(TEXT("Capsule identity"),Reject(E,Source,[](auto& R){R.CapsuleKey=FObjectKey();},Rows));
        Check(TEXT("Movement identity"),Reject(E,Source,[](auto& R){R.MovementKey=FObjectKey();},Rows));
        Check(TEXT("Navigation identity"),Reject(E,Source,[](auto& R){R.NavigationKey=FObjectKey();},Rows));
        Check(TEXT("Navigation data identity"),Reject(E,Source,[](auto& R){R.NavigationDataKeys.Empty();},Rows));
        Check(TEXT("Batch storage identity"),Reject(E,Source,[](auto& R){R.Batch=nullptr;},Rows));
        Check(TEXT("Settings storage identity"),Reject(E,Source,[](auto& R){R.Settings=nullptr;},Rows));
        Check(TEXT("Loop storage identity"),Reject(E,Source,[](auto& R){R.Loops=nullptr;},Rows));
        Check(TEXT("Batch id"),Reject(E,Source,[](auto& R){R.BatchId=FGuid::NewGuid();},Rows));
        Check(TEXT("Source seed"),Reject(E,Source,[](auto& R){++R.SourceSeed;},Rows));
        Check(TEXT("Batch seed"),Reject(E,Source,[](auto& R){++R.BatchSeed;},Rows));
        Check(TEXT("Graph revision"),Reject(E,Source,[](auto& R){++R.GraphRevision;},Rows));
        Check(TEXT("Geometry revision"),Reject(E,Source,[](auto& R){++R.GeometryRevision;},Rows));
        Check(TEXT("Configuration digest"),Reject(E,Source,[](auto& R){R.ConfigurationDigest+=TEXT("invalid");},Rows));
        Check(TEXT("Graph digest"),Reject(E,Source,[](auto& R){R.GraphDigest+=TEXT("invalid");},Rows));
        Check(TEXT("Layout digest"),Reject(E,Source,[](auto& R){R.LayoutDigest+=TEXT("invalid");},Rows));
        Check(TEXT("Geometry digest"),Reject(E,Source,[](auto& R){R.GeometryDigest+=TEXT("invalid");},Rows));
        Check(TEXT("Source transform"),Reject(E,Source,[](auto& R){R.SourceTransform.AddToTranslation(FVector(1,0,0));},Rows));
        Check(TEXT("Empty epoch"),Reject(E,Source,[](auto& R){R.NavigationEpoch.Reset();},Rows));
        Check(TEXT("Invalid epoch"),Reject(E,Source,[](auto& R){R.NavigationEpoch=TEXT("InvalidRuntimeGeometry");},Rows));
        Check(TEXT("Navigation world"),Reject(E,Source,[](auto& R){R.NavigationWorld+=TEXT("invalid");},Rows));
        Check(TEXT("Navigation data path"),Reject(E,Source,[](auto& R){R.NavigationDataPath+=TEXT("invalid");},Rows));
        Check(TEXT("Navigation layout"),Reject(E,Source,[](auto& R){R.NavigationLayoutDigest+=TEXT("invalid");},Rows));
        Check(TEXT("Payload graph revision"),Reject(E,Source,[](auto& R){++R.StoredTraversal.GetValue().GraphRevision;},Rows));
        Check(TEXT("Payload stable digest"),Reject(E,Source,[](auto& R){R.StoredTraversal.GetValue().StableDigest+=TEXT("invalid");},Rows));
        Check(TEXT("Closed request"),Reject(E,Source,[](auto& R){R.Close();},Rows));
        Check(TEXT("Invalidated request"),Reject(E,Source,[](auto& R){R.Invalidate();},Rows));
        {
            FRogueliteEncounterSynchronousRead Read(&E);
            TGuardValue<FRogueliteEncounterSynchronousRead*> Scope(E.ActiveSynchronousRead,&Read);
            auto Expected=Source.GetTraversalSnapshot();auto Payload=Expected;
            Check(TEXT("Fresh request produced once"),Read.Produce(&Source,Source.GetSlicePlayer(),MoveTemp(Payload)));
            FRogueliteLoopTraversalSnapshot Out;
            Check(TEXT("One complete payload consumed"),Read.Consume(&Source,Source.GetSlicePlayer(),Out)
                &&EncounterSyncReadTests::EqualSnapshot(Expected,Out));
            Check(TEXT("Same-scope replay refused"),!Read.Consume(&Source,Source.GetSlicePlayer(),Out));
            Read.Close();Check(TEXT("Closed-scope replay refused"),!Read.Consume(&Source,Source.GetSlicePlayer(),Out));
        }
        Check(TEXT("Actual nested Encounter Tick invalidates outer request"),Reject(E,Source,[&](auto& Read)
        {
            E.TickComponent(.02f,LEVELTICK_All,&E.PrimaryComponentTick);
            Check(TEXT("Nested Tick marks sticky invalidation"),Read.IsInvalidated());
        },Rows));
        return Good;
    }
};

namespace EncounterSyncReadTests
{
struct FRun final:IAutomationLatentCommand
{
    FAutomationTestBase* Test;
    FString Case,Rows;
    double Started=FPlatformTime::Seconds();
    bool bGood=true;
    FRun(FAutomationTestBase* InTest,FString InCase):Test(InTest),Case(MoveTemp(InCase)){}
    void Check(const TCHAR* Label,bool Result)
    {
        bGood&=Result;Test->TestTrue(Label,Result);
        Rows+=FString::Printf(TEXT("Check=%s Pass=%d\n"),Label,Result);
    }
    void Difference(const TCHAR* Label,const FString& Expected,const FString& Actual)
    {
        Check(Label,Expected==Actual);
        if(Expected!=Actual)Rows+=FString::Printf(TEXT("%s Expected\n%s\nActual\n%s\n"),Label,*Expected,*Actual);
    }
    void Snapshot(ARogueliteCombatSliceRig& Source,const TCHAR* Label)
    {
        const auto Expected=LegacySnapshot(Source),Actual=Source.GetTraversalSnapshot();
        Check(Label,EqualSnapshot(Expected,Actual));
        Rows+=FString::Printf(TEXT("Snapshot %s\nExpected=%s\nActual=%s\n"),Label,*SnapshotText(Expected),*SnapshotText(Actual));
    }
    void Identity(ARogueliteCombatSliceRig& Source,URogueliteSliceEncounter& E,const TCHAR* Label)
    {
        FString ExpectedReason,ActualReason;
        const bool Expected=FRogueliteEncounterSyncReadTestAccess::Identity(E,ExpectedReason,false);
        const bool Actual=FRogueliteEncounterSyncReadTestAccess::Identity(E,ActualReason,true);
        Check(Label,Expected==Actual&&ExpectedReason==ActualReason);
        Rows+=FString::Printf(TEXT("Identity %s Expected=%d Actual=%d ExpectedReason=%s ActualReason=%s\n"),Label,Expected,Actual,*ExpectedReason,*ActualReason);
        Snapshot(Source,Label);
    }
    bool Finish(ARogueliteCombatSliceRig* Source,const TCHAR* Reason)
    {
        if(Source&&Source->Batch.bSucceeded)Check(TEXT("Test owned batch cleanup"),Source->ClearRuntime());
        Rows+=FString::Printf(TEXT("Passed=%d\nReason=%s\n"),bGood,Reason);
        FString Directory;
        if(FParse::Value(FCommandLine::Get(),TEXT("PERF21AEvidenceDir="),Directory))
        {
            IFileManager::Get().MakeDirectory(*Directory,true);
            const FString File=Directory/(Case+TEXT("-")+FGuid::NewGuid().ToString(EGuidFormats::Digits)+TEXT(".txt"));
            Check(TEXT("Independent detailed evidence saved"),FFileHelper::SaveStringToFile(Rows,*File));
        }
        return true;
    }
    bool Update() override;
};
}

bool EncounterSyncReadTests::FRun::Update()
{
    UWorld* World=GEditor?GEditor->PlayWorld.Get():nullptr;
    if(FPlatformTime::Seconds()-Started>210)
    {Check(TEXT("Bounded real Ready PIE fixture"),false);return Finish(nullptr,TEXT("PIE or Ready timed out"));}
    if(!World)return false;
    ARogueliteCombatSliceRig* Source=nullptr;
    for(TActorIterator<ARogueliteCombatSliceRig> It(World);It;++It){Source=*It;break;}
    if(!Source)return false;
    if(!Source->SliceFailure.IsEmpty())
    {Check(TEXT("Original G2 fixture generated successfully"),false);Rows+=Source->SliceFailure;return Finish(Source,TEXT("Fixture failure"));}
    if(!Source->bPlayerBridgeReady||!Source->IsDungeonReady())return false;
    auto* Player=Source->GetSlicePlayer();auto* E=Source->Encounter.Get();
    if(!Player||!E)return false;
    Rows+=TEXT("World=")+World->GetPathName()+TEXT(" Batch=")+Source->Batch.BatchId.ToString()+TEXT("\n");
    Check(TEXT("Exact native AwaitingEntry fixture admitted"),FRogueliteEncounterSyncReadTestAccess::Admitted(*E));
    if(!bGood)return Finish(Source,TEXT("Native admission unavailable"));
    Snapshot(*Source,TEXT("Initial every reflected Snapshot field"));

    if(Case==TEXT("NormalDifferential"))
    {
        TArray<URogueliteSliceEncounter*> Encounters={E,Source->SecondEncounter.Get()};
        for(auto* Encounter:Encounters)
        {
            Check(TEXT("Both native encounters available"),Encounter!=nullptr);if(!Encounter)continue;
            const auto BeforeWorld=ObserveWorld(*Source);
            const FRogueliteEncounterSyncReadTestAccess::FTracking Tracking(*Encounter);
            const auto Legacy=FRogueliteEncounterSyncReadTestAccess::Prefix(*Encounter,*Source,false);
            Check(TEXT("Legacy prefix remains AwaitingEntry"),Encounter->GetState()==ERogueliteSliceEncounterState::AwaitingEntry);
            Difference(TEXT("Legacy prefix has no external World side effect"),BeforeWorld,Legacy.World);
            if(!bGood)return Finish(Source,TEXT("Refuse tracking-only rollback after a World mutation"));
            Tracking.Restore(*Encounter);
            const auto Pilot=FRogueliteEncounterSyncReadTestAccess::Prefix(*Encounter,*Source,true);
            Check(TEXT("Native success produces and consumes one local read"),Pilot.bIdentity&&Pilot.bRoster&&Pilot.bProduced&&Pilot.bConsumed&&!Pilot.bInvalidated);
            Check(TEXT("Legacy and pilot decisions and Reason equal"),Legacy.bIdentity==Pilot.bIdentity&&Legacy.bRoster==Pilot.bRoster&&Legacy.Reason==Pilot.Reason);
            Difference(TEXT("Every Encounter business field equal"),Legacy.Encounter,Pilot.Encounter);
            Difference(TEXT("Every observed World side effect equal"),Legacy.World,Pilot.World);
            Check(TEXT("Pilot retained complete produced Snapshot for differential test"),Pilot.Payload.IsSet());
            if(Pilot.Payload.IsSet())Check(TEXT("Every payload field equals fresh original Snapshot"),EqualSnapshot(Pilot.Payload.GetValue(),LegacySnapshot(*Source)));
        }
    }
    else if(Case==TEXT("OldFailureSemantics"))
    {
        auto* Room=Source->Batch.Rooms[0].Room.Get();const double Length=Room->RoomLength,Wall=Room->WallThickness;
        Room->RoomLength+=10;Identity(*Source,*E,TEXT("Raw RoomLength without rebuild"));
        const auto Value=Source->GetTraversalSnapshot();Check(TEXT("Preserve Current true and strict Ready false"),Value.bCurrent&&!Value.bNavigationReady);Room->RoomLength=Length;
        Room->WallThickness+=1;Identity(*Source,*E,TEXT("Raw WallThickness without rebuild"));Room->WallThickness=Wall;
        const auto Report=Source->NavigationReadiness->Report;
        for(const TCHAR* State:{TEXT("NavigationPending"),TEXT("NavigationFailed"),TEXT("NavigationTimedOut"),TEXT("NavigationUnavailable")})
        {
            Source->NavigationReadiness->Report.State=State;Source->NavigationReadiness->Report.bReady=false;
            Identity(*Source,*E,State);Source->NavigationReadiness->Report=Report;
        }
        Source->NavigationReadiness->Report.Epoch.Reset();Identity(*Source,*E,TEXT("Empty Epoch"));Source->NavigationReadiness->Report=Report;
        Source->NavigationReadiness->Report.Epoch=TEXT("InvalidRuntimeGeometry");Identity(*Source,*E,TEXT("Invalid Epoch"));Source->NavigationReadiness->Report=Report;
        ++Source->CommittedLoops.GraphRevision;Identity(*Source,*E,TEXT("GraphRevision changed"));--Source->CommittedLoops.GraphRevision;
        ++Source->CommittedLoops.GeometryRevision;Identity(*Source,*E,TEXT("GeometryRevision changed"));--Source->CommittedLoops.GeometryRevision;
        const auto Batch=Source->Batch.BatchId;Source->Batch.BatchId=FGuid::NewGuid();Identity(*Source,*E,TEXT("Actual Batch replaced"));Source->Batch.BatchId=Batch;
        const auto Digest=Source->Batch.GraphDigest;Source->Batch.GraphDigest+=TEXT("invalid");Identity(*Source,*E,TEXT("Graph digest changed"));Source->Batch.GraphDigest=Digest;
        const auto Registry=Source->Batch.Registry;Source->Batch.Registry=nullptr;Identity(*Source,*E,TEXT("Registry missing"));Source->Batch.Registry=Registry;
        const auto Owner=Room->GetOwner();Room->SetOwner(nullptr);Identity(*Source,*E,TEXT("Actual room Owner changed"));Room->SetOwner(Owner);
        TInlineComponentArray<UPrimitiveComponent*> Parts;Room->GetComponents(Parts);
        for(auto* Part:Parts)if(Part->GetFName()==TEXT("Floor")&&Part->IsRegistered())
        {
            Part->UnregisterComponent();Identity(*Source,*E,TEXT("Actual floor Unregister preserves old semantics"));
            Part->RegisterComponent();break;
        }
        Snapshot(*Source,TEXT("All old-state fixtures restored"));
    }
    else if(Case==TEXT("IndependentRequests"))
    {
        for(int32 Index=0;Index<3;++Index)
        {
            FString Reason;Check(TEXT("Fresh independent positive identity"),FRogueliteEncounterSyncReadTestAccess::Identity(*E,Reason,true));
            TInlineComponentArray<UPrimitiveComponent*> Parts;Source->Batch.Rooms[0].Room->GetComponents(Parts);
            UPrimitiveComponent* Floor=nullptr;for(auto* Part:Parts)if(Part->GetFName()==TEXT("Floor")){Floor=Part;break;}
            Check(TEXT("Actual registered Static floor exists"),Floor&&Floor->IsRegistered()&&Floor->Mobility==EComponentMobility::Static);
            if(!Floor)continue;
            auto* Room=Source->Batch.Rooms[0].Room.Get();
            const auto Collision=Floor->GetCollisionEnabled();const auto Transform=Floor->GetRelativeTransform();
            const auto ActorTransform=Room->GetActorTransform();
            TInlineComponentArray<USceneComponent*> SceneParts;Room->GetComponents(SceneParts);
            TArray<TPair<USceneComponent*,EComponentMobility::Type>> Mobility;
            for(auto* Part:SceneParts)Mobility.Emplace(Part,Part->Mobility);
            bool MobilityFaultAlsoInjected=false,Changed=false;
            if(Index==0)Floor->SetCollisionEnabled(ECollisionEnabled::NoCollision);
            if(Index==0)Changed=Floor->GetCollisionEnabled()!=Collision;
            if(Index==1)
            {
                Floor->SetRelativeLocation(Transform.GetLocation()+FVector(1,0,0));
                if(Floor->GetRelativeTransform().Equals(Transform,0))
                {
                    Rows+=TEXT("EngineBlockedInjection=Static component Transform; retry with explicitly recorded additional Mobility fault\n");
                    Floor->SetMobility(EComponentMobility::Movable);MobilityFaultAlsoInjected=true;
                    Floor->SetRelativeLocation(Transform.GetLocation()+FVector(1,0,0));
                }
                Changed=!Floor->GetRelativeTransform().Equals(Transform,0);
            }
            if(Index==2)
            {
                Room->SetActorLocation(ActorTransform.GetLocation()+FVector(1,0,0));
                if(Room->GetActorTransform().Equals(ActorTransform,0))
                {
                    Rows+=TEXT("EngineBlockedInjection=Static Actor Transform; retry with explicitly recorded additional Mobility fault\n");
                    Room->GetRootComponent()->SetMobility(EComponentMobility::Movable);MobilityFaultAlsoInjected=true;
                    Room->SetActorLocation(ActorTransform.GetLocation()+FVector(1,0,0));
                }
                Changed=!Room->GetActorTransform().Equals(ActorTransform,0);
            }
            Rows+=FString::Printf(TEXT("PhysicalInjection Case=%d ActuallyChanged=%d MobilityFaultAlsoInjected=%d ActorBefore=%s ActorAfter=%s ComponentBefore=%s ComponentAfter=%s\n"),
                Index,Changed,MobilityFaultAlsoInjected,*ActorTransform.ToString(),*Room->GetActorTransform().ToString(),*Transform.ToString(),*Floor->GetRelativeTransform().ToString());
            Check(TEXT("Requested actual physical state really changed"),Changed);
            Identity(*Source,*E,TEXT("Real mutation between two independent synchronous requests"));
            if(Changed)Check(TEXT("Second request cannot borrow earlier success"),!FRogueliteEncounterSyncReadTestAccess::Identity(*E,Reason,true));
            Room->SetActorTransform(ActorTransform);
            Floor->SetRelativeTransform(Transform);Floor->SetCollisionEnabled(Collision);
            const auto Depth=[](const USceneComponent* Part)
            {int32 Count=0;for(auto* Parent=Part->GetAttachParent();Parent;Parent=Parent->GetAttachParent())++Count;return Count;};
            Mobility.Sort([&](const auto& A,const auto& B){return Depth(A.Key)<Depth(B.Key);});
            for(const auto& Original:Mobility)Original.Key->SetMobility(Original.Value);
            bool Restored=Room->GetActorTransform().Equals(ActorTransform,0)&&Floor->GetRelativeTransform().Equals(Transform,0);
            for(const auto& Original:Mobility)Restored&=Original.Key->Mobility==Original.Value;
            Check(TEXT("Actual transforms and every affected Mobility restored"),Restored);
            Snapshot(*Source,TEXT("Actual physical mutation restored"));
        }
        FActorSpawnParameters Params;Params.ObjectFlags|=RF_Transient;
        auto* External=World->SpawnActor<AActor>(Params);
        Check(TEXT("Test-owned external actor spawned"),External!=nullptr);
        if(External)
        {
            auto* Box=NewObject<UBoxComponent>(External);External->SetRootComponent(Box);
            Box->SetMobility(EComponentMobility::Movable);Box->SetCanEverAffectNavigation(false);
            Box->SetBoxExtent(FVector(50));Box->SetCollisionProfileName(TEXT("BlockAll"));
            Box->SetWorldLocation(FVector(11000,11000,200));Box->RegisterComponent();
            Identity(*Source,*E,TEXT("External BlockAll added after independent success"));
            FString Reason;Check(TEXT("External box cannot borrow earlier success"),!FRogueliteEncounterSyncReadTestAccess::Identity(*E,Reason,true));
            External->Destroy();Snapshot(*Source,TEXT("External test fixture removed"));
        }
    }
    else if(Case==TEXT("IdentityAndLifetime"))
    {
        bGood&=FRogueliteEncounterSyncReadTestAccess::Protocol(*E,*Source,*Test,Rows);
    }
    else if(Case==TEXT("UnknownGetterFallback"))
    {
        FActorSpawnParameters Params;Params.ObjectFlags|=RF_Transient;
        auto* Blueprint=FKismetEditorUtilities::CreateBlueprint(AActor::StaticClass(),GetTransientPackage(),
            MakeUniqueObjectName(GetTransientPackage(),UBlueprint::StaticClass(),TEXT("PERF21A_Unknown")),
            BPTYPE_Normal,UBlueprint::StaticClass(),UBlueprintGeneratedClass::StaticClass());
        TStrongObjectPtr<UBlueprint> KeepBlueprint(Blueprint);
        Check(TEXT("Unknown Blueprint exists only in Transient package"),Blueprint&&Blueprint->GeneratedClass&&Blueprint->GetOutermost()==GetTransientPackage());
        if(Blueprint&&Blueprint->GeneratedClass)
        {
            auto* BlueprintActor=World->SpawnActor<AActor>(Blueprint->GeneratedClass,Params);
            Check(TEXT("Actual unknown Blueprint Actor spawned"),BlueprintActor!=nullptr);
            if(BlueprintActor)
            {
                Check(TEXT("Unknown Blueprint cannot inherit native admission"),!FRogueliteEncounterSyncReadTestAccess::Admitted(*E));
                Identity(*Source,*E,TEXT("Actual transient Blueprint retains original identity and Snapshot"));
                BlueprintActor->Destroy();
            }
        }
        auto* Actor=World->SpawnActor<AActor>(Params);Check(TEXT("Unknown getter actor spawned"),Actor!=nullptr);
        if(!Actor)return Finish(Source,TEXT("Cannot create unknown getter fixture"));
        auto* Getter=NewObject<URogueliteEncounterSyncGetter>(Actor);Actor->SetRootComponent(Getter);
        Getter->SetMobility(EComponentMobility::Movable);Getter->SetCanEverAffectNavigation(false);
        Getter->SetCollisionEnabled(ECollisionEnabled::NoCollision);Getter->RegisterComponent();Getter->Source=Source;Getter->Encounter=E;
        const int32 Before=Getter->QueryCount;
        Check(TEXT("Unknown virtual getter refused before invocation"),!FRogueliteEncounterSyncReadTestAccess::Admitted(*E)&&Getter->QueryCount==Before);
        auto* Room=Source->Batch.Rooms[0].Room.Get();const double Length=Room->RoomLength;
        Getter->MutationMode=1;Getter->QueryCount=0;Getter->bChanged=false;
        FString LegacyReason,PilotReason;
        const bool Legacy=FRogueliteEncounterSyncReadTestAccess::Identity(*E,LegacyReason,false);
        const int32 LegacyCalls=Getter->QueryCount;const bool LegacyChanged=Getter->bChanged;const double LegacyLength=Room->RoomLength;
        const auto LegacyValue=Source->GetTraversalSnapshot();
        Room->RoomLength=Length;Getter->QueryCount=0;Getter->bChanged=false;
        const bool Pilot=FRogueliteEncounterSyncReadTestAccess::Identity(*E,PilotReason,true);
        const int32 PilotCalls=Getter->QueryCount;
        Check(TEXT("Unknown getter preserves old identity and Reason"),Legacy==Pilot&&LegacyReason==PilotReason);
        Check(TEXT("Unknown getter preserves actual mutation and exact call count"),LegacyChanged&&Getter->bChanged
            &&LegacyLength==Room->RoomLength&&LegacyCalls==PilotCalls);
        const auto PilotValue=Source->GetTraversalSnapshot();
        Check(TEXT("Unknown getter preserves every final Snapshot field"),EqualSnapshot(LegacyValue,PilotValue));
        Rows+=FString::Printf(TEXT("UnknownGetter Legacy=%d Pilot=%d LegacyCalls=%d PilotCalls=%d LegacyLength=%.17g PilotLength=%.17g LegacyReason=%s PilotReason=%s\n"),
            Legacy,Pilot,LegacyCalls,PilotCalls,LegacyLength,Room->RoomLength,*LegacyReason,*PilotReason);
        Getter->MutationMode=0;Room->RoomLength=Length;Actor->Destroy();
        auto* Unknown=NewObject<URogueliteEncounterSyncUnknownEncounter>(Source);
        Check(TEXT("Unknown native Encounter class never inherits admission"),!FRogueliteEncounterSynchronousRead::Admit(Unknown,Source,Player));
        Snapshot(*Source,TEXT("Unknown getter fixture restored"));
    }
    else if(Case==TEXT("ClearDuringRead"))
    {
        Check(TEXT("Actual Source Clear rejects existing complete payload"),FRogueliteEncounterSyncReadTestAccess::Reject(*E,*Source,[&](auto&)
        {Check(TEXT("Actual Source Clear succeeded"),Source->ClearRuntime());},Rows));
        Snapshot(*Source,TEXT("Cleared source preserves original full Snapshot"));
        Check(TEXT("Cleared read is never false victory"),E->GetState()==ERogueliteSliceEncounterState::Cancelled&&E->GetClearCount()==0);
        Check(TEXT("Owned cleanup removes gameplay leases"),FRogueliteDynamicGameplayBridge::ActiveComponentCount(Source)==0);
    }
    else if(Case==TEXT("ShutdownBoundary"))
    {
        Check(TEXT("Actual Shutdown closes request before owned World cleanup"),FRogueliteEncounterSyncReadTestAccess::ShutdownBoundary(*E,*Source,Rows));
        Check(TEXT("Other unentered room remains independent"),IsValid(Source->SecondEncounter.Get())
            &&Source->SecondEncounter->GetState()==ERogueliteSliceEncounterState::AwaitingEntry
            &&Source->SecondEncounter->GetClearCount()==0);
    }
    else if(Case==TEXT("DirectShutdownBoundary")||Case==TEXT("DestroyCallbackBoundary"))
    {
        Check(TEXT("Direct action or real destruction callback closes read before owned cleanup"),
            FRogueliteEncounterSyncReadTestAccess::DirectCleanupBoundary(*E,*Source,Case==TEXT("DestroyCallbackBoundary"),Rows));
        Check(TEXT("Direct cleanup leaves other unentered encounter independent"),IsValid(Source->SecondEncounter.Get())
            &&Source->SecondEncounter->GetState()==ERogueliteSliceEncounterState::AwaitingEntry&&Source->SecondEncounter->GetClearCount()==0);
    }
    else if(Case==TEXT("MultiLevelReentry"))
    {
        Check(TEXT("Two actual nested Tick levels retain and invalidate the outer read"),
            FRogueliteEncounterSyncReadTestAccess::MultiLevelReentry(*E,*Source,Rows));
    }
    else
    {Check(TEXT("Known isolated case"),false);return Finish(Source,TEXT("Unknown test parameter"));}
    return Finish(Source,TEXT("Complete Snapshot and actual synchronous observable differential checked"));
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FRogueliteEncounterSyncReadPIETest,"ProjectVerification.PERF21A.EncounterSyncRead",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ProductFilter)
void FRogueliteEncounterSyncReadPIETest::GetTests(TArray<FString>& Names,TArray<FString>& Commands) const
{
    Names={TEXT("NormalDifferential"),TEXT("OldFailureSemantics"),TEXT("IndependentRequests"),
        TEXT("IdentityAndLifetime"),TEXT("UnknownGetterFallback"),TEXT("ClearDuringRead"),TEXT("ShutdownBoundary"),
        TEXT("DirectShutdownBoundary"),TEXT("DestroyCallbackBoundary"),TEXT("MultiLevelReentry")};Commands=Names;
}
bool FRogueliteEncounterSyncReadPIETest::RunTest(const FString& Parameters)
{
    if(!GEditor||GEditor->PlayWorld){AddError(TEXT("Refuse to replace an existing user PIE"));return false;}
    FAutomationEditorCommonUtils::LoadMap(EncounterSyncReadTests::Map);
    ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
    FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<EncounterSyncReadTests::FRun>(this,Parameters));
    ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());return true;
}
#endif
