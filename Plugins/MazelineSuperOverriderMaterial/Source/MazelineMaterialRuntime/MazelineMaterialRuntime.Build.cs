using UnrealBuildTool;

public class MazelineMaterialRuntime : ModuleRules
{
    public MazelineMaterialRuntime(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine" });
        PrivateDependencyModuleNames.AddRange(new[] { "Renderer", "RenderCore", "RHI", "Projects" });
        // UE 5.8.4 FViewInfo is needed to replace the cached View uniform before base pass.
        PrivateIncludePaths.Add(System.IO.Path.Combine(EngineDirectory, "Source/Runtime/Renderer/Private"));
        PrivateIncludePaths.Add(System.IO.Path.Combine(EngineDirectory, "Source/Runtime/Renderer/Internal"));
    }
}
