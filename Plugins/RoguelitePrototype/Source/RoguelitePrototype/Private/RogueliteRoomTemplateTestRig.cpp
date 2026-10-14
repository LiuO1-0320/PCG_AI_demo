#include "RogueliteRoomTemplateTestRig.h"
#include "RoguelitePrototype.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "NavigationSystem.h"
#include "NavigationData.h"
#include "TimerManager.h"
#include "CollisionShape.h"

ARogueliteRoomTemplateTestRig::ARogueliteRoomTemplateTestRig()
{ PrimaryActorTick.bCanEverTick=false; SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("TestRoot"))); }
bool ARogueliteRoomTemplateTestRig::IsDWorld() const
{ return GetWorld() && GetWorld()->GetOutermost()->GetName().EndsWith(TEXT("L_Roguelite_RoomTemplate_Dev")); }
void ARogueliteRoomTemplateTestRig::BuildLocalNavigation()
{
    NavigationBuildEvidence.Reset();
#if WITH_EDITOR
    if (!IsDWorld() || GetWorld()->IsGameWorld()) { NavigationBuildEvidence.Add(TEXT("Refused: Editor P03-D map only.")); return; }
    auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
    if (!Nav || Nav->IsNavigationBuildingLocked(static_cast<uint8>(~ENavigationBuildLock::NoUpdateInEditor)))
    { NavigationBuildEvidence.Add(TEXT("Navigation waiting/unavailable: world missing or locked; no forced unlock.")); return; }
    Nav->Build();
    auto* Data=Nav->GetDefaultNavDataInstance(FNavigationSystem::DontCreate);
    const FString Evidence=FString::Printf(TEXT("P03D_NAV_BUILD Data=%s Agent=%.1fx%.1f Registered=%d Busy=%d; Revalidate Connection must query the actual path."),
        *GetPathNameSafe(Data),Data?Data->GetConfig().AgentRadius:0,Data?Data->GetConfig().AgentHeight:0,Data?Data->IsRegistered():0,
        UNavigationSystemV1::IsNavigationBeingBuiltOrLocked(GetWorld()));
    NavigationBuildEvidence.Add(Evidence); UE_LOG(LogRoguelitePrototype, Display, TEXT("%s"),*Evidence);
#endif
}
void ARogueliteRoomTemplateTestRig::BeginPlay()
{
    Super::BeginPlay(); if (!bRunRuntimeProbe || !IsDWorld()) return;
    bRuntimeProbeRan=true; FString Reason;
    RuntimeFactoryInstance=ARogueliteRoomTemplate::CreateTemplateInstance(this,TemplateClass,FTransform(FRotator(0,90,0),FVector(8500,6000,0)),Reason);
    if (!RuntimeFactoryInstance) { RuntimeEvidence.Add(TEXT("Factory failed: ")+Reason); return; }
    auto* World=GetWorld(); RuntimeAssembly=World->SpawnActor<ARogueliteRoomAssembly>();
    auto* A=World->SpawnActorDeferred<ARogueliteParametricRoom>(ARogueliteParametricRoom::StaticClass(),FTransform(FVector(0,5000,0)),this,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
    auto* C=World->SpawnActorDeferred<ARogueliteParametricCorridor>(ARogueliteParametricCorridor::StaticClass(),FTransform(FVector(12000,5000,0)),this,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
    RuntimeConnectedInstance=World->SpawnActorDeferred<ARogueliteRoomTemplate>(TemplateClass,FTransform(FVector(12000,8000,0)),this,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
    if (!RuntimeAssembly || !A || !C || !RuntimeConnectedInstance) { RuntimeEvidence.Add(TEXT("Deferred spawn failed.")); return; }
    A->NorthDoor.bEnabled=true; A->NorthDoor.bAutoCenter=false; A->NorthDoor.ManualOffset=250;
    FTransform CT,BT;
    const bool Computed=RuntimeAssembly->ComputeCorridorTransforms(A,ERogueliteRoomSide::North,C,ERogueliteCorridorEndpoint::Start,RuntimeConnectedInstance,ERogueliteRoomSide::South,CT,BT,Reason);
    A->FinishSpawning(A->GetActorTransform()); C->FinishSpawning(Computed?CT:C->GetActorTransform());
    RuntimeConnectedInstance->FinishSpawning(Computed?BT:RuntimeConnectedInstance->GetActorTransform());
    RuntimeAssembly->bCheckNavigation=false;
    RuntimeResult=RuntimeAssembly->TryAlignCorridorConnection(A,ERogueliteRoomSide::North,C,ERogueliteCorridorEndpoint::Start,RuntimeConnectedInstance,ERogueliteRoomSide::South);
    const auto FactoryMembers=RuntimeFactoryInstance->GetTemplateMemberStates(),ConnectedMembers=RuntimeConnectedInstance->GetTemplateMemberStates();
    bool MembersMatch=FactoryMembers.Num()==ConnectedMembers.Num() && FactoryMembers.Num()>=6;
    bool Owned=true,StaticMeshes=true;
    for (int32 Index=0;Index<FMath::Min(FactoryMembers.Num(),ConnectedMembers.Num());++Index)
    {
        const auto& F=FactoryMembers[Index]; const auto& B=ConnectedMembers[Index];
        MembersMatch &= F.Name==B.Name && F.ParentName==B.ParentName && F.ParentLocalTransform.Equals(B.ParentLocalTransform,.001)
            && F.RoomLocalTransform.Equals(B.RoomLocalTransform,.001) && F.StaticMeshPath==B.StaticMeshPath && F.MaterialPaths==B.MaterialPaths
            && F.CollisionProfile==B.CollisionProfile && F.CollisionEnabled==B.CollisionEnabled && F.Mobility==B.Mobility;
        Owned &= F.bOwnedByInstance && B.bOwnedByInstance && F.Component!=B.Component;
        if (B.Component->IsA<UStaticMeshComponent>()) StaticMeshes &= B.Mobility==EComponentMobility::Static;
    }
    // Trace an actual owned mesh's collision along its local Z axis; engine Cube is authored in the representative template.
    int32 OwnedCollisionTests=0,OwnedCollisionPassed=0;
    for (const auto& Member:ConnectedMembers)
    {
        const auto* Mesh=Cast<UStaticMeshComponent>(Member.Component.Get());
        if (!Mesh || !Mesh->IsQueryCollisionEnabled()) continue;
        FHitResult Hit; FCollisionQueryParams Params(SCENE_QUERY_STAT(P03DOwnedGeometry),true);
        const auto X=Mesh->GetComponentTransform();
        ++OwnedCollisionTests;
        OwnedCollisionPassed += World->LineTraceSingleByChannel(Hit,X.TransformPosition(FVector(0,0,100)),X.TransformPosition(FVector(0,0,-100)),ECC_Visibility,Params)
            && Hit.GetComponent()==Mesh;
    }
    const bool OwnedCollision=OwnedCollisionTests>=5 && OwnedCollisionPassed==OwnedCollisionTests;
    const auto FactoryValidation=RuntimeFactoryInstance->ValidateTemplate(),ConnectedValidation=RuntimeConnectedInstance->ValidateTemplate();
    const bool FinalContentValidated=FactoryValidation.bValid && !FactoryValidation.bDeferredContent
        && ConnectedValidation.bValid && !ConnectedValidation.bDeferredContent;
    const FTransform Before=RuntimeConnectedInstance->GetActorTransform();
    C->CorridorLength+=300; C->RebuildCorridor();
    const auto UnsafeMove=RuntimeAssembly->TryAlignCorridorConnection(A,ERogueliteRoomSide::North,C,ERogueliteCorridorEndpoint::Start,RuntimeConnectedInstance,ERogueliteRoomSide::South);
    const bool StaticMoveRefused=!UnsafeMove.bSucceeded && Before.Equals(RuntimeConnectedInstance->GetActorTransform(),.001);
    C->CorridorLength-=300; C->RebuildCorridor();
    bRuntimeProbePassed=Computed && RuntimeResult.bSucceeded && RuntimeResult.bCapsulePassed && MembersMatch && Owned && StaticMeshes && OwnedCollision && StaticMoveRefused && FinalContentValidated;
    RuntimeEvidence.Add(FString::Printf(TEXT("Factory=1 DeferredComputed=%d RegisteredMembers=%d LocalTransformsResourcesCollisionMatch=%d InstanceOwned=%d StaticMeshes=%d OwnedCollision=%d OwnedMeshProbes=%d/%d FinalContentValidated=%d StaticMoveRefused=%d Probes=%d"),
        Computed,ConnectedMembers.Num(),MembersMatch,Owned,StaticMeshes,OwnedCollision,OwnedCollisionPassed,OwnedCollisionTests,FinalContentValidated,StaticMoveRefused,RuntimeResult.Probes.Num()));
    RuntimeEvidence.Add(RuntimeResult.Message);
    UE_LOG(LogRoguelitePrototype, Display, TEXT("P03D_RUNTIME_RESULT Passed=%d %s"),bRuntimeProbePassed,*RuntimeEvidence[0]);
    World->GetTimerManager().SetTimer(RuntimeNavTimer,this,&ARogueliteRoomTemplateTestRig::CheckRuntimeNavigation,.5,false);
}
void ARogueliteRoomTemplateTestRig::CheckRuntimeNavigation()
{
    if (!RuntimeAssembly || !RuntimeResult.ConnectionId.IsValid()) return;
    auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
    if (Nav && UNavigationSystemV1::IsNavigationBeingBuiltOrLocked(GetWorld()) && ++RuntimeNavAttempts<20)
    { GetWorld()->GetTimerManager().SetTimer(RuntimeNavTimer,this,&ARogueliteRoomTemplateTestRig::CheckRuntimeNavigation,.5,false); return; }
    RuntimeAssembly->bCheckNavigation=true;
    RuntimeNavigationResult=RuntimeAssembly->RevalidateConnection(RuntimeResult.ConnectionId);
    UE_LOG(LogRoguelitePrototype, Display, TEXT("P03D_RUNTIME_NAV %s"),*RuntimeNavigationResult.Message);
}
