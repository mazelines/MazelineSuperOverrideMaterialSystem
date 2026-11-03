using UnrealBuildTool;

public class MazelineSuperOverriderMaterial : ModuleRules
{
    public MazelineSuperOverriderMaterial(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PrivateDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "Projects", "Slate", "SlateCore", "ToolMenus", "Settings", "UnrealEd", "AssetRegistry", "Json", "RenderCore", "RHI", "MazelineMaterialRuntime" });
        if (Target.Platform == UnrealTargetPlatform.Win64)
        {
            PublicSystemLibraries.Add("bcrypt.lib");
        }
    }
}
