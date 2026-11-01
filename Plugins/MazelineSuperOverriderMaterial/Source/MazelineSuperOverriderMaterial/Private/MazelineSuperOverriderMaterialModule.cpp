#include "Modules/ModuleManager.h"
#include "HAL/IConsoleManager.h"
#include "ISettingsModule.h"
#include "MazelineShaderPatchManager.h"
#include "SMazelineMaterialSettings.h"
#include "ToolMenus.h"

DEFINE_LOG_CATEGORY_STATIC(LogMazelineSuperOverriderMaterial, Log, All);

class FMazelineSuperOverriderMaterialModule : public IModuleInterface
{
public:
    virtual void StartupModule() override
    {
        Commands.Add(IConsoleManager::Get().RegisterConsoleCommand(TEXT("Mazeline.ShaderOverride.Status"),
            TEXT("Verify native shader patch status and backup hashes."),
            FConsoleCommandDelegate::CreateLambda([]
            {
                FMazelinePatchStatus Status;
                FString Error;
                const bool bOk = FMazelineShaderPatchManager::Current().Inspect(Status, Error);
                UE_LOG(LogMazelineSuperOverriderMaterial, Display, TEXT("MAZELINE_NATIVE_STATUS state=%s matches=%s valid=%s owner=%s %s"),
                    *Status.State, Status.bFilesMatch ? TEXT("yes") : TEXT("no"), bOk ? TEXT("yes") : TEXT("no"), *Status.Owner, *Error);
            })));
        for (const FString& Action : {FString(TEXT("Prepare")), FString(TEXT("Apply")), FString(TEXT("Restore")), FString(TEXT("Recover"))})
        {
            Commands.Add(IConsoleManager::Get().RegisterConsoleCommand(*(TEXT("Mazeline.ShaderOverride.") + Action),
                TEXT("Native C++ shader patch operation; changes require an editor restart."),
                FConsoleCommandDelegate::CreateLambda([Action]
                {
                    FString Message;
                    const bool bOk = FMazelineShaderPatchManager::RunCurrent(Action.ToLower(), Message);
                    UE_LOG(LogMazelineSuperOverriderMaterial, Display, TEXT("MAZELINE_NATIVE_RESULT %s %s: %s"), *Action, bOk ? TEXT("success") : TEXT("failed"), *Message);
                })));
        }
        Commands.Add(IConsoleManager::Get().RegisterConsoleCommand(TEXT("Mazeline.ShaderOverride.Settings"),
            TEXT("Open Project Settings > Plugins > Mazeline Super Override Material."),
            FConsoleCommandDelegate::CreateLambda([]
            {
                if (!IsRunningCommandlet()) FModuleManager::LoadModuleChecked<ISettingsModule>("Settings").ShowViewer("Project", "Plugins", "MazelineSuperOverrideMaterial");
            })));
        if (!IsRunningCommandlet())
        {
            ISettingsModule& Settings = FModuleManager::LoadModuleChecked<ISettingsModule>("Settings");
            SettingsWidget = SNew(SMazelineMaterialSettings);
            const auto Section = Settings.RegisterSettings("Project", "Plugins", "MazelineSuperOverrideMaterial",
                NSLOCTEXT("Mazeline", "SettingsTitle", "Mazeline Super Override Material"),
                NSLOCTEXT("Mazeline", "SettingsDescription", "Manage the native engine shader patch, backups, and restore operations."),
                StaticCastSharedRef<SWidget>(SettingsWidget.ToSharedRef()));
            UE_LOG(LogMazelineSuperOverriderMaterial, Display, TEXT("MAZELINE_SETTINGS_REGISTERED %s"), Section.IsValid() ? TEXT("yes") : TEXT("no"));
            UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FMazelineSuperOverriderMaterialModule::RegisterMenus));
        }
        UE_LOG(LogMazelineSuperOverriderMaterial, Log, TEXT("Mazeline native C++ patch manager loaded (no Python dependency)."));
    }
    virtual void ShutdownModule() override
    {
        if (ISettingsModule* Settings = FModuleManager::GetModulePtr<ISettingsModule>("Settings"))
            Settings->UnregisterSettings("Project", "Plugins", "MazelineSuperOverrideMaterial");
        SettingsWidget.Reset();
        if (!IsRunningCommandlet())
        {
            UToolMenus::UnRegisterStartupCallback(this);
            UToolMenus::UnregisterOwner(this);
        }
        for (IConsoleObject* Command : Commands) IConsoleManager::Get().UnregisterConsoleObject(Command);
        Commands.Empty();
    }
private:
    void RegisterMenus()
    {
        FToolMenuOwnerScoped Owner(this);
        UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Tools");
        Menu->FindOrAddSection("MazelineShaderOverride").AddEntry(FToolMenuEntry::InitMenuEntry(
            "MazelineShaderSettings", NSLOCTEXT("Mazeline", "OpenSettings", "Mazeline: Shader Patch Settings"),
            NSLOCTEXT("Mazeline", "OpenSettingsTip", "Open patch status, apply, restore and recovery controls."),
            FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([]
            {
                FModuleManager::LoadModuleChecked<ISettingsModule>("Settings").ShowViewer("Project", "Plugins", "MazelineSuperOverrideMaterial");
            }))));
    }
    TArray<IConsoleObject*> Commands;
    TSharedPtr<SMazelineMaterialSettings> SettingsWidget;
};

IMPLEMENT_MODULE(FMazelineSuperOverriderMaterialModule, MazelineSuperOverriderMaterial)
