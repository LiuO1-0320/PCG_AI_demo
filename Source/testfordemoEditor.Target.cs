using UnrealBuildTool;
public class testfordemoEditorTarget : TargetRules
{
    public testfordemoEditorTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Editor;
        DefaultBuildSettings = BuildSettingsVersion.V7;
        IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
        ExtraModuleNames.Add("CastleDemo");
    }
}
