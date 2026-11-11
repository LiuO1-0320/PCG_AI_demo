#include "CoreMinimal.h"
#include "ProfilingDebugging/CsvProfiler.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS && CSV_PROFILER
#include "Tests/RogueliteCombatSliceRig.h"
#include "Gameplay/RogueliteCombatSliceEncounter.h"
#include "Gameplay/RogueliteCombatSlicePlayer.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

namespace RogueliteEncounterPerfPhases
{
TAutoConsoleVariable<int32> EnabledCVar(TEXT("roguelite.PERF21A.ObservePhases"),0,
    TEXT("Read-only G2 phase telemetry. Default off. Does not grant gameplay validity."),ECVF_Default);

bool Enabled()
{
    static const bool CommandLineEnabled=FParse::Param(FCommandLine::Get(),TEXT("PERF21AObservePhases"));
    return CommandLineEnabled||EnabledCVar.GetValueOnGameThread()!=0;
}

const TCHAR* StateText(ERogueliteSliceEncounterState State)
{
    switch(State)
    {
    case ERogueliteSliceEncounterState::AwaitingEntry:return TEXT("AwaitingEntry");
    case ERogueliteSliceEncounterState::Preparation:return TEXT("Preparation");
    case ERogueliteSliceEncounterState::CombatActive:return TEXT("CombatActive");
    case ERogueliteSliceEncounterState::Cleared:return TEXT("Cleared");
    case ERogueliteSliceEncounterState::Cancelled:return TEXT("Cancelled");
    default:return TEXT("Uninitialized");
    }
}

struct FPhase
{
    FString Name=TEXT("NotReady"),World;
    FGuid Batch;
    ERogueliteSliceEncounterState Primary=ERogueliteSliceEncounterState::Uninitialized;
    ERogueliteSliceEncounterState Secondary=ERogueliteSliceEncounterState::Uninitialized;
    int32 Node=INDEX_NONE;
    bool PrimaryRecovery=false,SecondaryRecovery=false,PrimaryParked=false,SecondaryParked=false;
    double Speed2D=0;

    bool SameBoundary(const FPhase& Other) const
    {
        return Name==Other.Name&&Batch==Other.Batch&&Primary==Other.Primary&&Secondary==Other.Secondary
            &&Node==Other.Node&&PrimaryRecovery==Other.PrimaryRecovery&&SecondaryRecovery==Other.SecondaryRecovery
            &&PrimaryParked==Other.PrimaryParked&&SecondaryParked==Other.SecondaryParked;
    }
    void Emit(const TCHAR* Sample=TEXT("WorldTickStart")) const
    {
        CSV_EVENT_GLOBAL(TEXT("PERF21A_PHASE Phase=%s Primary=%s Secondary=%s Batch=%s World=%s Node=%d RecoveryBusy=%d/%d Parked=%d/%d Speed2D=%.3f Sample=%s"),
            *Name,StateText(Primary),StateText(Secondary),*Batch.ToString(EGuidFormats::Digits),*World,Node,
            PrimaryRecovery,SecondaryRecovery,PrimaryParked,SecondaryParked,Speed2D,Sample);
    }
};

// This duplicates only the existing room-coordinate projection. It never calls
// GetCurrentPlayerRoomNode, binding checks, traversal or navigation queries.
int32 ObserveNode(const ARogueliteCombatSliceRig& Rig,const FVector& Position)
{
    int32 Found=INDEX_NONE;
    for(const auto& Room:Rig.Batch.Rooms)
    {
        const FVector Local=Room.PlacedTransform.InverseTransformPosition(Position);
        if(FMath::Abs(Local.X)<=Room.Dimensions.X*.5&&FMath::Abs(Local.Y)<=Room.Dimensions.Y*.5
            &&Local.Z>=0&&Local.Z<=Room.Dimensions.Z)
        {
            if(Found!=INDEX_NONE)return INDEX_NONE;
            Found=Room.NodeId;
        }
    }
    return Found;
}

FPhase Observe(const ARogueliteCombatSliceRig& Rig)
{
    FPhase Phase;Phase.World=Rig.GetWorld()->GetPathName();Phase.Batch=Rig.Batch.BatchId;
    const auto* Primary=Rig.Encounter.Get();const auto* Secondary=Rig.SecondEncounter.Get();
    if(IsValid(Primary))
    {
        Phase.Primary=Primary->GetState();Phase.PrimaryRecovery=Primary->IsRecoveryBusy();Phase.PrimaryParked=Primary->IsRecoveryParked();
    }
    if(IsValid(Secondary))
    {
        Phase.Secondary=Secondary->GetState();Phase.SecondaryRecovery=Secondary->IsRecoveryBusy();Phase.SecondaryParked=Secondary->IsRecoveryParked();
    }
    const auto* Player=Rig.GetSlicePlayer();
    if(!Rig.bPlayerBridgeReady||!Phase.Batch.IsValid()||!IsValid(Player)
        ||Player->GetClass()!=ARogueliteSlicePlayer::StaticClass()||!IsValid(Primary)||!IsValid(Secondary)
        ||Phase.Primary==ERogueliteSliceEncounterState::Uninitialized||Phase.Secondary==ERogueliteSliceEncounterState::Uninitialized
        ||Phase.Primary==ERogueliteSliceEncounterState::Cancelled||Phase.Secondary==ERogueliteSliceEncounterState::Cancelled)return Phase;
    Phase.Node=ObserveNode(Rig,Player->GetActorLocation());Phase.Speed2D=Player->GetVelocity().Size2D();
    if(Phase.Primary==ERogueliteSliceEncounterState::CombatActive||Phase.Secondary==ERogueliteSliceEncounterState::CombatActive)
        Phase.Name=TEXT("Combat");
    else if(Phase.Primary==ERogueliteSliceEncounterState::Preparation||Phase.Secondary==ERogueliteSliceEncounterState::Preparation)
        Phase.Name=TEXT("Preparation");
    else if(Phase.PrimaryRecovery||Phase.SecondaryRecovery)Phase.Name=TEXT("Recovery");
    else if((Phase.Node==Primary->GetTargetNodeId()&&Phase.Primary==ERogueliteSliceEncounterState::Cleared)
        ||(Phase.Node==Secondary->GetTargetNodeId()&&Phase.Secondary==ERogueliteSliceEncounterState::Cleared))Phase.Name=TEXT("Cleared");
    else Phase.Name=Phase.Speed2D>1?TEXT("Move"):TEXT("ReadyIdle");
    return Phase;
}

struct FWorldObservation
{
    TWeakObjectPtr<ARogueliteCombatSliceRig> Rig;
    FPhase Last;
    bool HasPhase=false,WasCapturing=false;
    int32 CsvFrame=INDEX_NONE;
};

class FObserver
{
public:
    FObserver()
    {
        TickHandle=FWorldDelegates::OnWorldTickStart.AddRaw(this,&FObserver::Tick);
        CleanupHandle=FWorldDelegates::OnWorldCleanup.AddRaw(this,&FObserver::Cleanup);
    }
    ~FObserver()
    {
        FWorldDelegates::OnWorldTickStart.Remove(TickHandle);
        FWorldDelegates::OnWorldCleanup.Remove(CleanupHandle);
        Worlds.Reset();
    }
private:
    void Tick(UWorld* World,ELevelTick,float)
    {
        if(!IsInGameThread())return;
        if(!Enabled()){Worlds.Reset();return;}
        TRACE_CPUPROFILER_EVENT_SCOPE(PERF21A_PhaseObserver);
        if(!World||!World->IsGameWorld()||World->bIsTearingDown)return;
        const FString Package=World->GetOutermost()->GetName();
        if(!Package.StartsWith(TEXT("/Game/Roguelite/Tests/P04G2/"))
            ||!Package.EndsWith(TEXT("L_Roguelite_TwoCombatRooms_Dev")))return;
        auto& Observation=Worlds.FindOrAdd(TWeakObjectPtr<UWorld>(World));
        auto* Rig=Observation.Rig.Get();
        if(!IsValid(Rig))
        {
            for(TActorIterator<ARogueliteCombatSliceRig> It(World);It;++It)
            {
                if(It->GetClass()!=ARogueliteCombatSliceRig::StaticClass())continue;
                if(Rig)return; // An ambiguous fixture never receives a guessed phase.
                Rig=*It;
            }
            Observation.Rig=Rig;
        }
        if(!Rig||!Rig->bEnableTwoCombatRooms)return;
        // TickStart observes the last completed game tick. Every label is a
        // sampling boundary, with up to one World tick of detection latency.
        FPhase Phase=Observe(*Rig);
        const bool Capturing=FCsvProfiler::IsCapturing();
        const int32 CsvFrame=Capturing?FCsvProfiler::Get()->GetCaptureFrameNumber():INDEX_NONE;
        const bool NewCapture=Capturing&&(!Observation.WasCapturing||CsvFrame<Observation.CsvFrame);
        if(!Observation.HasPhase||NewCapture||!Phase.SameBoundary(Observation.Last))Phase.Emit();
        Observation.Last=MoveTemp(Phase);Observation.HasPhase=true;
        Observation.WasCapturing=Capturing;Observation.CsvFrame=CsvFrame;
    }
    void Cleanup(UWorld* World,bool,bool)
    {
        if(!IsInGameThread())return;
        const TWeakObjectPtr<UWorld> Key(World);
        if(auto* Observation=Worlds.Find(Key);Observation&&Observation->HasPhase&&Enabled())
        {
            TRACE_CPUPROFILER_EVENT_SCOPE(PERF21A_PhaseObserver);
            Observation->Last.Name=TEXT("Stopped");Observation->Last.Emit(TEXT("WorldCleanup"));
        }
        Worlds.Remove(Key);
    }
    TMap<TWeakObjectPtr<UWorld>,FWorldObservation> Worlds;
    FDelegateHandle TickHandle,CleanupHandle;
};

FObserver Observer;
}
#endif
