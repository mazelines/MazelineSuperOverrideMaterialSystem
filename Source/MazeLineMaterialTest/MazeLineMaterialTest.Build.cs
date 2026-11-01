using UnrealBuildTool;

public class MazeLineMaterialTest : ModuleRules
{
    public MazeLineMaterialTest(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine" });
    }
}
