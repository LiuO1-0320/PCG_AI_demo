#include "QueenCombatAnimation.h"
#include "Animation/AnimSingleNodeInstanceProxy.h"
#include "Animation/AnimNodeBase.h"
#include "Engine/World.h"

namespace
{
struct FQueenPoseProxy : FAnimSingleNodeInstanceProxy
{
    using FAnimSingleNodeInstanceProxy::FAnimSingleNodeInstanceProxy;
    float Weight = 0.f;
    float Side = 0.f;

    virtual void PreUpdate(UAnimInstance* Instance, float DeltaSeconds) override
    {
        FAnimSingleNodeInstanceProxy::PreUpdate(Instance, DeltaSeconds);
        const auto* QueenAnim = CastChecked<UQueenCombatAnimation>(Instance);
        const float Age = QueenAnim->GetWorld()->GetTimeSeconds() - QueenAnim->HitTime;
        // Fast impact followed by a longer settle into the underlying attack pose.
        Weight = Age < .07f ? FMath::SmoothStep(0.f,.07f,Age) : 1.f-FMath::SmoothStep(.07f,.32f,Age);
        Side = QueenAnim->HitSide;
    }

    virtual bool Evaluate(FPoseContext& Output) override
    {
        FAnimSingleNodeInstanceProxy::Evaluate(Output);
        if (Weight <= 0.f) return true;
        const auto& Bones = Output.Pose.GetBoneContainer();
        auto Bend = [&](FName Name, float Pitch, float Roll)
        {
            const int32 MeshIndex = Bones.GetPoseBoneIndexForBoneName(Name);
            if (MeshIndex == INDEX_NONE) return;
            const auto Index = Bones.MakeCompactPoseIndex(FMeshPoseBoneIndex(MeshIndex));
            if (!Index.IsValid()) return;
            FQuat ParentRotation = FQuat::Identity;
            for (auto Parent = Output.Pose.GetParentBoneIndex(Index); Parent.IsValid(); Parent = Output.Pose.GetParentBoneIndex(Parent))
                ParentRotation = Output.Pose[Parent].GetRotation() * ParentRotation;
            const FQuat Offset = FRotator(Pitch*Weight,0,Roll*Side*Weight).Quaternion();
            auto& Bone = Output.Pose[Index];
            Bone.SetRotation((ParentRotation.Inverse()*Offset*ParentRotation*Bone.GetRotation()).GetNormalized());
        };
        static const FName Spine(TEXT("spine_02")), Chest(TEXT("chest")), Neck(TEXT("neck"));
        Bend(Spine, 6.f, 3.f);
        Bend(Chest, 9.f, 5.f);
        Bend(Neck, -5.f, -2.f);
        return true;
    }
};
}

FAnimInstanceProxy* UQueenCombatAnimation::CreateAnimInstanceProxy()
{
    return new FQueenPoseProxy(this);
}

void UQueenCombatAnimation::ReactToHit(float Side)
{
    const float Now = GetWorld()->GetTimeSeconds();
    if (Now-HitTime < .20f) return;
    HitTime = Now;
    HitSide = FMath::Clamp(Side,-1.f,1.f);
}
