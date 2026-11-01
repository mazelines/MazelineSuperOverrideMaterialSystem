#include "MazelineShaderPatchManager.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Serialization/JsonSerializer.h"
#include "ISettingsModule.h"
#include "ISettingsContainer.h"
#include "ISettingsCategory.h"
#include "ISettingsSection.h"
#include "Widgets/SWidget.h"

namespace MazelineNativeTest
{
const TArray<FString> Files = {TEXT("ShadingCommon.ush"), TEXT("ShadingModelsMaterial.ush"), TEXT("ShadingModels.ush")};
void Write(const FString& Path, const FString& Text)
{
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
    FFileHelper::SaveArrayToFile(FMazelineShaderPatchManager::Utf8(Text), *Path);
}
TArray<uint8> Read(const FString& Path)
{
    TArray<uint8> Result;
    FFileHelper::LoadFileToArray(Result, *Path);
    return Result;
}
struct FFixture
{
    FString Root = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Automation/MazelineNative") / FGuid::NewGuid().ToString(EGuidFormats::Digits));
    FString Engine = Root / TEXT("EngineInstall");
    FString Project = Root / TEXT("Example.uproject");
    FString Registry = Root / TEXT("registry.json");
    FString Shaders = Engine / TEXT("Engine/Shaders/Private");
    FMazelineShaderPatchManager::FBundle Original;
    FFixture()
    {
        Write(Project, TEXT("{}"));
        Write(Engine / TEXT("Engine/Build/Build.version"), TEXT("{\"MajorVersion\":5,\"MinorVersion\":8,\"PatchVersion\":4}"));
        Write(Shaders / Files[0], TEXT("#define SHADINGMODELID_NUM\t14\r\n"));
        Write(Shaders / Files[1], TEXT("\tGBuffer.ShadingModelID = ShadingModel;\r\n"));
        Write(Shaders / Files[2], TEXT("FDirectLighting DefaultLitBxDF( FGBufferData GBuffer, half3 N, half3 V, FAreaLight AreaLight, FShadowTerms Shadow )\r\n{\r\n}\r\n\t\tcase SHADINGMODELID_DEFAULT_LIT:\r\n"));
        Write(Root / TEXT("function.ush"), TEXT("void MazelineEvaluateShaderOverride() {}\n"));
        Write(Registry, TEXT("{\"functions\":[{\"id\":1,\"source\":\"function.ush\"}]}"));
        for (const FString& File : Files) Original.Add(File, Read(Shaders / File));
    }
    FMazelineShaderPatchManager Manager() const { return FMazelineShaderPatchManager(Engine, Project); }
    bool IsOriginal() const
    {
        for (const FString& File : Files) if (Read(Shaders / File) != Original[File]) return false;
        return true;
    }
};
}
using namespace MazelineNativeTest;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMazelineNativeLifecycle, "Mazeline.NativePatch.Lifecycle", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMazelineNativeLifecycle::RunTest(const FString&)
{
    TestEqual(TEXT("SHA-256 compatibility"), FMazelineShaderPatchManager::Hash(FMazelineShaderPatchManager::Utf8(TEXT("abc"))),
        TEXT("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
    FFixture F;
    auto Manager = F.Manager();
    FString Message;
    bool bChanged = false;
    TestTrue(TEXT("Apply"), Manager.Execute(TEXT("apply"), F.Registry, Message, bChanged));
    TestTrue(TEXT("Apply changes files"), bChanged);
    FMazelinePatchStatus Status;
    TestTrue(TEXT("Inspect"), Manager.Inspect(Status, Message));
    TestEqual(TEXT("Applied state"), Status.State, TEXT("applied"));
    TestTrue(TEXT("Hashes match"), Status.bFilesMatch);
    TestTrue(TEXT("Idempotent apply"), Manager.Execute(TEXT("apply"), F.Registry, Message, bChanged));
    TestFalse(TEXT("No second write"), bChanged);
    Write(F.Root / TEXT("function.ush"), TEXT("void\nMazelineEvaluateShaderOverride () { /* updated */ }\n"));
    TestTrue(TEXT("Update registry body"), Manager.Execute(TEXT("apply"), F.Registry, Message, bChanged));
    TestTrue(TEXT("Update writes shader"), bChanged);
    TestTrue(TEXT("Restore"), Manager.Execute(TEXT("restore"), FString(), Message, bChanged));
    TestTrue(TEXT("Exact original bytes"), F.IsOriginal());
    TestTrue(TEXT("Restore is idempotent"), Manager.Execute(TEXT("restore"), FString(), Message, bChanged));
    TestFalse(TEXT("Restored state not rewritten"), bChanged);
    TestTrue(TEXT("Reapply"), Manager.Execute(TEXT("apply"), F.Registry, Message, bChanged));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMazelineNativeRecovery, "Mazeline.NativePatch.InterruptedTransactions", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMazelineNativeRecovery::RunTest(const FString&)
{
    for (const FString& Operation : {FString(TEXT("apply")), FString(TEXT("restore")), FString(TEXT("update"))})
    {
        FFixture F;
        auto Manager = F.Manager();
        FString Message;
        bool bChanged;
        if (Operation != TEXT("apply")) TestTrue(TEXT("Initial patch"), Manager.Execute(TEXT("apply"), F.Registry, Message, bChanged));
        TMap<FString, TArray<uint8>> Before;
        for (const FString& File : Files) Before.Add(File, Read(F.Shaders / File));
        if (Operation == TEXT("update")) Write(F.Root / TEXT("function.ush"), TEXT("void MazelineEvaluateShaderOverride() { /* new */ }\n"));
        Manager.FailBeforeShaderWrite = Operation == TEXT("update") ? 0 : 1;
        TestFalse(TEXT("Injected failure"), Manager.Execute(Operation == TEXT("restore") ? TEXT("restore") : TEXT("apply"), F.Registry, Message, bChanged));
        FMazelinePatchStatus Status;
        TestTrue(TEXT("Interrupted state is readable"), Manager.Inspect(Status, Message));
        TestEqual(TEXT("Durable pending journal"), Status.State, TEXT("interrupted"));
        auto Reopened = F.Manager();
        TestFalse(TEXT("No overwrite of pending transaction"), Reopened.Execute(TEXT("apply"), F.Registry, Message, bChanged));
        TestTrue(TEXT("Recover from new manager"), Reopened.Execute(TEXT("recover"), FString(), Message, bChanged));
        for (const FString& File : Files) TestTrue(TEXT("Recovered previous bytes ") + File, Read(F.Shaders / File) == Before[File]);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMazelineNativeConflicts, "Mazeline.NativePatch.ConflictsAndIntegrity", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMazelineNativeConflicts::RunTest(const FString&)
{
    FFixture F;
    auto Manager = F.Manager();
    FString Message;
    bool bChanged;
    TestTrue(TEXT("Initial apply"), Manager.Execute(TEXT("apply"), F.Registry, Message, bChanged));
    const FString Other = F.Root / TEXT("Other.uproject");
    Write(Other, TEXT("{}"));
    FMazelineShaderPatchManager OtherManager(F.Engine, Other);
    TestFalse(TEXT("Other project cannot restore"), OtherManager.Execute(TEXT("restore"), FString(), Message, bChanged));
    TestFalse(TEXT("Other project cannot update"), OtherManager.Execute(TEXT("apply"), F.Registry, Message, bChanged));
    const auto ModelsBefore = Read(F.Shaders / Files[2]);
    Write(F.Shaders / Files[1], TEXT("external edit"));
    TestFalse(TEXT("External edits block restore"), Manager.Execute(TEXT("restore"), FString(), Message, bChanged));
    TestTrue(TEXT("Untouched other file"), Read(F.Shaders / Files[2]) == ModelsBefore);
    TestEqual(TEXT("External edit preserved"), FMazelineShaderPatchManager::Decode(Read(F.Shaders / Files[1])), TEXT("external edit"));

    FFixture Corrupt;
    auto CorruptManager = Corrupt.Manager();
    TestTrue(TEXT("Corruption fixture apply"), CorruptManager.Execute(TEXT("apply"), Corrupt.Registry, Message, bChanged));
    FMazelinePatchStatus Status;
    TestTrue(TEXT("Inspect active"), CorruptManager.Inspect(Status, Message));
    Write(CorruptManager.GetStore() / Status.ActiveSnapshot / Files[0], TEXT("corrupt backup"));
    TestFalse(TEXT("Corrupt snapshot blocks restore"), CorruptManager.Execute(TEXT("restore"), FString(), Message, bChanged));
    TestFalse(TEXT("Corrupt snapshot never restores engine"), Corrupt.IsOriginal());

    FFixture Pending;
    auto PendingManager = Pending.Manager();
    PendingManager.FailBeforeShaderWrite = 1;
    TestFalse(TEXT("Create pending transaction"), PendingManager.Execute(TEXT("apply"), Pending.Registry, Message, bChanged));
    Write(Pending.Shaders / Files[2], TEXT("external pending edit"));
    auto Reopened = Pending.Manager();
    const auto FirstBefore = Read(Pending.Shaders / Files[0]);
    TestFalse(TEXT("Recovery refuses external edits"), Reopened.Execute(TEXT("recover"), FString(), Message, bChanged));
    TestTrue(TEXT("Recovery preflights whole bundle"), Read(Pending.Shaders / Files[0]) == FirstBefore);

    FFixture Unknown;
    Write(Unknown.Shaders / Files[0], TEXT("unknown engine anchor"));
    auto UnknownManager = Unknown.Manager();
    TestFalse(TEXT("Unknown anchors rejected"), UnknownManager.Execute(TEXT("apply"), Unknown.Registry, Message, bChanged));
    TestFalse(TEXT("No state created on transform failure"), IFileManager::Get().FileExists(*(UnknownManager.GetStore() / TEXT("state.json"))));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMazelineNativeCompatibility, "Mazeline.NativePatch.ExistingPythonBackupCompatibility", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMazelineNativeCompatibility::RunTest(const FString&)
{
    auto Manager = FMazelineShaderPatchManager::Current();
    FString Message;
    FMazelinePatchStatus Status;
    if (!TestTrue(TEXT("Read existing engine backup"), Manager.Inspect(Status, Message))) return false;
    if (!TestEqual(TEXT("Current engine applied"), Status.State, TEXT("applied"))) return false;
    TestTrue(TEXT("Existing Python SHA-256 records match"), Status.bFilesMatch);
    FString StateText;
    FFileHelper::LoadFileToString(StateText, *(Manager.GetStore() / TEXT("state.json")));
    TSharedPtr<FJsonObject> State;
    FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(StateText), State);
    if (!TestTrue(TEXT("State parsed"), State.IsValid())) return false;
    FMazelineShaderPatchManager::FBundle Original, Generated;
    for (const FString& File : Files) Original.Add(File, Read(Manager.GetStore() / State->GetStringField(TEXT("baseline")) / File));
    FString RegistryFile;
    if (!TestTrue(TEXT("Native graph scan"), FMazelineShaderPatchManager::BuildRegistry(RegistryFile, Message))) return false;
    FString RegistryText;
    FFileHelper::LoadFileToString(RegistryText, *RegistryFile);
    TSharedPtr<FJsonObject> Registry;
    FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(RegistryText), Registry);
    TMap<int32, FString> Functions;
    for (const auto& Value : Registry->GetArrayField(TEXT("functions")))
    {
        auto Row = Value->AsObject();
        FString Code;
        FFileHelper::LoadFileToString(Code, *Row->GetStringField(TEXT("source")));
        Code.ReplaceInline(TEXT("\r\n"), TEXT("\n"));
        Functions.Add(Row->GetIntegerField(TEXT("id")), Code);
    }
    TestEqual(TEXT("Three connected functions"), Functions.Num(), 3);
    if (!TestTrue(TEXT("Native generation"), FMazelineShaderPatchManager::Generate(Original, Functions, Generated, Message))) return false;
    for (const FString& File : Files)
        TestTrue(TEXT("Byte-identical to validated Python shader: ") + File,
            Generated[File] == Read(Manager.GetEngine() / TEXT("Engine/Shaders/Private") / File));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMazelineSettingsRegistration, "Mazeline.NativePatch.ProjectSettings", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMazelineSettingsRegistration::RunTest(const FString&)
{
    auto Container = FModuleManager::LoadModuleChecked<ISettingsModule>("Settings").GetContainer("Project");
    if (!TestTrue(TEXT("Project settings"), Container.IsValid())) return false;
    auto Category = Container->GetCategory("Plugins");
    if (!TestTrue(TEXT("Plugins category"), Category.IsValid())) return false;
    auto Section = Category->GetSection("MazelineSuperOverrideMaterial");
    if (!TestTrue(TEXT("Mazeline settings section"), Section.IsValid())) return false;
    TestEqual(TEXT("Requested display name"), Section->GetDisplayName().ToString(), TEXT("Mazeline Super Override Material"));
    auto Widget = Section->GetCustomWidget().Pin();
    TestTrue(TEXT("Native settings widget"), Widget.IsValid());
    if (Widget) TestEqual(TEXT("Correct widget class"), Widget->GetTypeAsString(), TEXT("SMazelineMaterialSettings"));
    return true;
}
#endif
