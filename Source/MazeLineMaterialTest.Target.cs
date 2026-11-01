using UnrealBuildTool;
using System.Collections.Generic;

public class MazeLineMaterialTestTarget : TargetRules
{
    public MazeLineMaterialTestTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Game;
        DefaultBuildSettings = BuildSettingsVersion.V7;
        IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
        if (Target.Platform == UnrealTargetPlatform.Win64)
        {
            WindowsPlatform.Compiler = WindowsCompiler.VisualStudio2022;
            WindowsPlatform.CompilerVersion = "14.44.35228";
        }
        ExtraModuleNames.Add("MazeLineMaterialTest");
    }
}
