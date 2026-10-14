using UnrealBuildTool;
public class RoguelitePrototypeEditor : ModuleRules
{
    public RoguelitePrototypeEditor(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage=PCHUsageMode.UseExplicitOrSharedPCHs;
        PrivateDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "RoguelitePrototype", "UnrealEd", "Slate", "SlateCore", "ToolMenus", "InputCore" });
    }
}
