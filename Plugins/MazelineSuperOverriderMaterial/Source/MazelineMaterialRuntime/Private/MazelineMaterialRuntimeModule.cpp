#include "Modules/ModuleManager.h"
#include "Interfaces/IPluginManager.h"
#include "ShaderCore.h"
#include "Misc/Paths.h"

class FMazelineMaterialRuntimeModule : public IModuleInterface
{
    virtual void StartupModule() override
    {
        const auto Plugin = IPluginManager::Get().FindPlugin(TEXT("MazelineSuperOverriderMaterial"));
        check(Plugin);
        AddShaderSourceDirectoryMapping(TEXT("/MazelineMaterial"), FPaths::Combine(Plugin->GetBaseDir(), TEXT("Shaders")));
    }
};
IMPLEMENT_MODULE(FMazelineMaterialRuntimeModule, MazelineMaterialRuntime)
