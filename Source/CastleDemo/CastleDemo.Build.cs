using UnrealBuildTool;
public class CastleDemo : ModuleRules
{
    public CastleDemo(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "InputCore", "AIModule", "NavigationSystem", "PCG" });
        if (Target.bBuildEditor)
            PrivateDependencyModuleNames.AddRange(new[] { "UnrealEd", "AssetRegistry", "BSPUtils" });
    }
}
