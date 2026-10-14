#include "RogueliteRoomTemplate.h"
#include "RoguelitePrototype.h"
#include "Components/StaticMeshComponent.h"
#include "Components/ChildActorComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Materials/MaterialInterface.h"
#if WITH_EDITOR
#include "Logging/MessageLog.h"
#include "Misc/UObjectToken.h"
#endif

ARogueliteRoomTemplate::ARogueliteRoomTemplate()
{
    TemplateContentRoot = CreateDefaultSubobject<USceneComponent>(TEXT("TemplateContentRoot"));
    TemplateContentRoot->SetupAttachment(GetRootComponent());
    TemplateContentRoot->SetMobility(EComponentMobility::Static);
}
TArray<USceneComponent*> ARogueliteRoomTemplate::GetTemplateOwnedComponents() const
{
    TArray<USceneComponent*> Result;
    if (!TemplateContentRoot || TemplateContentRoot->GetOwner() != this) return Result;
    for (UActorComponent* Component : GetComponents())
        if (auto* Scene = Cast<USceneComponent>(Component); Scene && !Scene->IsEditorOnly() && Scene != TemplateContentRoot && Scene->GetOwner() == this
            && Scene->IsAttachedTo(TemplateContentRoot)) Result.Add(Scene);
    Result.Sort([](const USceneComponent& A, const USceneComponent& B) { return A.GetName() < B.GetName(); });
    return Result;
}
TArray<FRogueliteTemplateMemberState> ARogueliteRoomTemplate::GetTemplateMemberStates() const
{
    TArray<FRogueliteTemplateMemberState> Result;
    for (USceneComponent* Component : GetTemplateOwnedComponents())
    {
        auto& State = Result.AddDefaulted_GetRef();
        State.Component = Component; State.Name = Component->GetFName();
        State.ParentName = Component->GetAttachParent() ? Component->GetAttachParent()->GetFName() : NAME_None;
        State.ParentLocalTransform = Component->GetRelativeTransform();
        State.WorldTransform = Component->GetComponentTransform();
        State.RoomLocalTransform = State.WorldTransform.GetRelativeTransform(GetActorTransform());
        State.bOwnedByInstance = Component->GetOwner() == this;
        State.Mobility = Component->Mobility;
        if (const auto* Primitive = Cast<UPrimitiveComponent>(Component))
        {
            State.CollisionProfile = Primitive->GetCollisionProfileName();
            State.CollisionEnabled = static_cast<int32>(Primitive->GetCollisionEnabled());
        }
        if (const auto* Mesh = Cast<UStaticMeshComponent>(Component))
        {
            State.StaticMeshPath = GetPathNameSafe(Mesh->GetStaticMesh());
            for (int32 Index = 0; Index < Mesh->GetNumMaterials(); ++Index) State.MaterialPaths.Add(GetPathNameSafe(Mesh->GetMaterial(Index)));
        }
    }
    return Result;
}
FRogueliteTemplateDescriptor ARogueliteRoomTemplate::GetTemplateDescriptor() const
{
    FRogueliteTemplateDescriptor Result;
    Result.TemplateId = TemplateId; Result.Purpose = TemplatePurpose; Result.ClassPath = GetClass()->GetPathName();
    Result.Dimensions = FVector(RoomLength, RoomWidth, RoomHeight);
    Result.WallThickness = WallThickness; Result.FloorThickness = FloorThickness; Result.DoorSockets = GetDoorSockets();
    return Result;
}
FRogueliteTemplateValidation ARogueliteRoomTemplate::EvaluateTemplate() const
{
    FRogueliteTemplateValidation Result;
    const auto Issue = [&](const FString& Code, const FString& Detail)
    { Result.Issues.Add(Code + TEXT(" Room=") + GetPathName() + TEXT(": ") + Detail); };
    if (TemplateId.IsNone()) Issue(TEXT("P03D_NO_TEMPLATE_ID"), TEXT("Set a stable TemplateId on the template class."));
    if (!TemplateContentRoot || TemplateContentRoot->GetOwner() != this || !TemplateContentRoot->IsAttachedTo(GetRootComponent()))
        Issue(TEXT("P03D_INVALID_OWNER_ROOT"), TEXT("TemplateContentRoot must belong to this instance and remain below RoomRoot."));
    const FTransform ActorTransform = GetActorTransform(); const FRotator Rotation = ActorTransform.Rotator();
    if (ActorTransform.ContainsNaN() || !ActorTransform.GetScale3D().Equals(FVector::OneVector, .0001)
        || FMath::Abs(Rotation.Pitch) > .01 || FMath::Abs(Rotation.Roll) > .01
        || FMath::Abs(FMath::FindDeltaAngleDegrees(Rotation.Yaw, FMath::RoundToFloat(Rotation.Yaw / 90) * 90)) > .01)
        Issue(TEXT("P03D_INVALID_TRANSFORM"), TEXT("Root Scale=1, Pitch/Roll=0 and quarter-turn Yaw required; member authored scales are retained."));
    if (!FMath::IsFinite(DoorApproachDepth) || DoorApproachDepth < 1)
        Issue(TEXT("P03D_INVALID_APPROACH"), TEXT("DoorApproachDepth must be a positive finite centimeter value."));
    // Deferred spawn may calculate socket transforms before Blueprint SCS runs.
    // This is only authoring preflight, never evidence of registered geometry.
    if (GetWorld() && GetWorld()->IsGameWorld() && !IsActorInitialized())
    { Result.bDeferredContent = true; Result.bValid = Result.Issues.IsEmpty(); return Result; }
    const FBox Interior(FVector(-RoomLength*.5, -RoomWidth*.5, 0), FVector(RoomLength*.5, RoomWidth*.5, RoomHeight));
    const auto Members = GetTemplateOwnedComponents();
    for (USceneComponent* Component : Members)
    {
        if (Component->GetComponentTransform().ContainsNaN())
            Issue(TEXT("P03D_INVALID_MEMBER_TRANSFORM"), Component->GetName());
        if (Component->IsA<UChildActorComponent>())
            Issue(TEXT("P03D_CHILD_ACTOR_UNSUPPORTED"), Component->GetName() + TEXT(" requires separately verified lifecycle/ownership support."));
        const auto* Primitive = Cast<UPrimitiveComponent>(Component);
        if (!Primitive) continue; // Light attenuation bounds are not occupied geometry.
        ++Result.OwnedGeometryCount;
        const FTransform InRoom = Component->GetComponentTransform().GetRelativeTransform(ActorTransform);
        const FBox Bounds = Primitive->CalcBounds(InRoom).GetBox();
        if (!Bounds.IsValid || !Interior.ExpandBy(.1).IsInsideOrOn(Bounds.Min) || !Interior.ExpandBy(.1).IsInsideOrOn(Bounds.Max))
            Issue(TEXT("P03D_OUT_OF_BOUNDS"), FString::Printf(TEXT("Component=%s LocalMin=%s LocalMax=%s"), *Component->GetName(), *Bounds.Min.ToString(), *Bounds.Max.ToString()));
        if (!Primitive->IsQueryCollisionEnabled() || Primitive->GetCollisionResponseToChannel(ECC_Pawn) != ECR_Block) continue;
        for (const FRogueliteDoorSocket& Door : GetDoorSockets())
        {
            if (!Door.bValid) continue;
            const FVector N = Door.LocalOutwardDirection, T(-N.Y, N.X, 0);
            FVector Center = Door.LocalPosition - N * (Door.WallThickness + DoorApproachDepth*.5f);
            Center.Z = Door.DoorHeight*.5f;
            const FVector Extent = N.GetAbs()*(DoorApproachDepth*.5f + Door.WallThickness)
                + T.GetAbs()*(Door.DoorWidth*.5f) + FVector(0,0,FMath::Max(.1f, Door.DoorHeight*.5f-.1f));
            if (Bounds.Intersect(FBox(Center-Extent, Center+Extent)))
                Issue(TEXT("P03D_DOOR_INTERFERENCE"), Component->GetName() + TEXT(" overlaps enabled socket ") + UEnum::GetValueAsString(Door.Side));
        }
    }
    if (Result.OwnedGeometryCount == 0) Issue(TEXT("P03D_EMPTY_TEMPLATE"), TEXT("No owned geometry under TemplateContentRoot; independent actor selection does not define a template."));
    Result.bValid = Result.Issues.IsEmpty();
    return Result;
}
FRogueliteTemplateValidation ARogueliteRoomTemplate::ValidateTemplate()
{
    TemplateValidation = EvaluateTemplate(); TemplateMemberStates = GetTemplateMemberStates();
    for (const FString& Issue : TemplateValidation.Issues) UE_LOG(LogRoguelitePrototype, Warning, TEXT("%s"), *Issue);
    return TemplateValidation;
}
void ARogueliteRoomTemplate::RebuildTemplate() { RebuildRoom(); ValidateTemplate(); }
void ARogueliteRoomTemplate::OnConstruction(const FTransform& Transform) { Super::OnConstruction(Transform); ValidateTemplate(); }
void ARogueliteRoomTemplate::PostRegisterAllComponents() { Super::PostRegisterAllComponents(); ValidateTemplate(); }
void ARogueliteRoomTemplate::PostInitializeComponents() { Super::PostInitializeComponents(); ValidateTemplate(); }
ARogueliteRoomTemplate* ARogueliteRoomTemplate::CreateTemplateInstance(UObject* Context, TSubclassOf<ARogueliteRoomTemplate> Class,
    const FTransform& Transform, FString& FailureReason)
{
    FailureReason.Reset();
    UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(Context, EGetWorldErrorMode::ReturnNull) : nullptr;
    const FRotator Rotation = Transform.Rotator();
    if (!World || !Class || Class->HasAnyClassFlags(CLASS_Abstract) || Transform.ContainsNaN()
        || !Transform.GetScale3D().Equals(FVector::OneVector, .0001) || FMath::Abs(Rotation.Pitch)>.01 || FMath::Abs(Rotation.Roll)>.01
        || FMath::Abs(FMath::FindDeltaAngleDegrees(Rotation.Yaw, FMath::RoundToFloat(Rotation.Yaw/90)*90))>.01)
    { FailureReason=TEXT("P03D_SPAWN_INVALID_INPUT: valid world/class and rigid quarter-turn Transform required."); return nullptr; }
    auto* Instance = World->SpawnActorDeferred<ARogueliteRoomTemplate>(Class, Transform, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
    if (!Instance) { FailureReason=TEXT("P03D_DEFERRED_SPAWN_FAILED"); return nullptr; }
    Instance->FinishSpawning(Transform);
    const auto Validation = Instance->ValidateTemplate();
    if (!Validation.bValid)
    {
        FailureReason = FString::Join(Validation.Issues, TEXT(" | "));
        Instance->Destroy(); // Only this newly created failed instance, never existing scene actors.
        return nullptr;
    }
    return Instance;
}
#if WITH_EDITOR
void ARogueliteRoomTemplate::PostEditChangeProperty(FPropertyChangedEvent& Event) { Super::PostEditChangeProperty(Event); ValidateTemplate(); }
void ARogueliteRoomTemplate::PostEditChangeChainProperty(FPropertyChangedChainEvent& Event) { Super::PostEditChangeChainProperty(Event); ValidateTemplate(); }
void ARogueliteRoomTemplate::PostEditMove(bool bFinished) { Super::PostEditMove(bFinished); ValidateTemplate(); }
void ARogueliteRoomTemplate::CheckForErrors()
{
    Super::CheckForErrors();
    for (const FString& Issue : EvaluateTemplate().Issues)
        FMessageLog(TEXT("MapCheck")).Warning()->AddToken(FUObjectToken::Create(this))->AddToken(FTextToken::Create(FText::FromString(Issue)));
}
#endif
