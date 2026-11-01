#include "Tests/RogueliteRoomRoleTestRig.h"
#include "RogueliteFloorProfile.h"
#include "Engine/World.h"
#include "UObject/Package.h"
namespace G1RigPrivate {
bool Own(const AActor* A){return A&&A->GetWorld()&&A->GetWorld()->WorldType==EWorldType::Editor&&A->GetWorld()->GetOutermost()->GetName()==TEXT("/Game/Roguelite/Tests/P03G1/L_Roguelite_RoomRoles_Dev");}
}
ARogueliteRoomRoleTestRig::ARogueliteRoomRoleTestRig(){bAutoGenerate=false;}
void ARogueliteRoomRoleTestRig::G1GeneratePreview(){
#if WITH_EDITOR
    if(!G1RigPrivate::Own(this)||!IsValid(FloorProfile)||!Batch.OwnedActors.IsEmpty())return;
    const auto C=FloorProfile->ResolvePreview(Seed);if(!C.bValid){LastFailureReason=C.ErrorCode+TEXT(": ")+C.Message;return;}
    Modify();Settings=C.Source;Planning=C.Planning;
    const auto P=FRogueliteLoopAwarePlanner::Plan(this,Settings,Planning,GetActorTransform());
    const bool Good=P.Tree.bValid&&FRogueliteLoopAwarePlanner::MaterializeEditor(this,Settings,Planning,P,Batch,CommittedLoops,G1EditorEvidence);
    if(!P.Tree.bValid)G1EditorEvidence=P.Evidence;RuntimeStatus=Good?TEXT("G1 Editor geometry validated; navigation not tested"):TEXT("G1 Editor rejected");LastFailureReason=G1EditorEvidence.Reason;
#endif
}
void ARogueliteRoomRoleTestRig::G1ClearPreview(){
#if WITH_EDITOR
    if(!G1RigPrivate::Own(this))return;Modify();FString Why;const bool Good=FRogueliteLoopAwarePlanner::Clear(this,Batch,CommittedLoops,Why);RuntimeStatus=Good?TEXT("G1 owned preview cleared"):TEXT("G1 cleanup rejected");LastFailureReason=Why;
#endif
}
void ARogueliteRoomRoleTestRig::G1PrepareTemplates(){
#if WITH_EDITOR
    if(!G1RigPrivate::Own(this)||!IsValid(FloorProfile))return;const auto C=FloorProfile->ResolvePreview(Seed);if(!C.bValid){LastFailureReason=C.ErrorCode+TEXT(": ")+C.Message;return;}
    Modify();Settings=C.Source;Planning=C.Planning;FString Why;const bool Good=FRoguelitePreparedTemplateCatalog::CaptureEditor(this,Settings,PreparedTemplates,Why);RuntimeStatus=Good?TEXT("G1 templates prepared; clear preview before PIE"):TEXT("G1 template preparation rejected");LastFailureReason=Why;
#endif
}
