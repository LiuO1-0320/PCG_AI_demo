using UnrealBuildTool;
public class testfordemoTarget : TargetRules
{
    public testfordemoTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Game;
        DefaultBuildSettings = BuildSettingsVersion.V7;
        IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
        ExtraModuleNames.Add("CastleDemo");
    }
}
