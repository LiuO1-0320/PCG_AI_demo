#include "RogueliteWorkbenchPreview.h"
#include "Engine/World.h"
ARogueliteWorkbenchPreview::ARogueliteWorkbenchPreview()
{Tags.Remove(TEXT("RogueliteLoopAwareTestOnly"));Tags.AddUnique(TEXT("RogueliteDesignerWorkbench"));}
void ARogueliteWorkbenchPreview::ApplyProfile(URogueliteFloorProfile* In,const FRogueliteResolvedFloor& C)
{
    Profile=In;AppliedRules=In->Rules;AppliedProfilePath=In->GetPathName();AppliedProfileDigest=C.ProfileDigest;AppliedConfigurationDigest=C.ConfigurationDigest;UsedPreviewSeed=C.UsedSeed;Settings=C.Source;Planning=C.Planning;AppliedSettingsDigest=FRogueliteFloorProfileResolver::SettingsDigest(Settings,Planning);
}
bool ARogueliteWorkbenchPreview::IsProfileCurrent(FString& Reason) const
{
    if(!GetWorld()||GetWorld()->WorldType!=EWorldType::Editor){Reason=TEXT("Editor工作台预览不能冒充PIE运行时生成批次。");return false;}
    if(!WorkbenchId.IsValid()||!ActorHasTag(TEXT("RogueliteDesignerWorkbench"))||!IsValid(Profile)||Profile->GetPathName()!=AppliedProfilePath||FRogueliteFloorProfileResolver::Digest(Profile->Rules)!=AppliedProfileDigest||FRogueliteFloorProfileResolver::SettingsDigest(Settings,Planning)!=AppliedSettingsDigest)
    {Reason=TEXT("ProfileChanged：楼层规则或批次配置已改变，先Clear再Generate；不沿用旧成功状态。");return false;}
    Reason=TEXT("当前Profile、Seed及应用配置一致。");return true;
}
