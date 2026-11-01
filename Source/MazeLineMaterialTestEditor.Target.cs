using UnrealBuildTool;
using System.Collections.Generic;

public class MazeLineMaterialTestEditorTarget : TargetRules
{
    public MazeLineMaterialTestEditorTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Editor;
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
