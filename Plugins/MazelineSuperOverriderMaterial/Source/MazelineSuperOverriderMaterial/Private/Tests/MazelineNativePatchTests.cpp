#include "MazelineShaderPatchManager.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "MazelineMaterialLibrary.h"
#include "MaterialExpressionMazelineGGXOutput.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionScalarParameter.h"
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
const TArray<FString> Files = {TEXT("ShadingCommon.ush"), TEXT("ShadingModelsMaterial.ush"), TEXT("ShadingModels.ush"), TEXT("DeferredShadingCommon.ush"), TEXT("DeferredLightingCommon.ush")};
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
        Write(Shaders / Files[3], TEXT("bool HasCustomGBufferData(int ShadingModelID)\r\n{\r\n\treturn ShadingModelID == SHADINGMODELID_SUBSURFACE;\r\n}\r\n"));
        Write(Shaders / Files[4], TEXT("\t\t\tFDirectLighting Lighting;\r\n"));
        Write(Shaders / Files[2], TEXT("float3 SpecularGGX( float Roughness, float3 SpecularColor, BxDFContext Context, FAreaLight AreaLight )\r\n{\r\n\tfloat D = D_GGX( a2, Context.NoH ) * Energy;\r\n\treturn D;\r\n}\r\n")
            TEXT("FDirectLighting DefaultLitBxDF( FGBufferData GBuffer, half3 N, half3 V, FAreaLight AreaLight, FShadowTerms Shadow )\r\n{\r\n\tSpecularGGX(GBuffer.Roughness, GBuffer.SpecularColor, Context, AreaLight);\r\n\t\tLighting.Diffuse *= AreaLight.FalloffColor * (AreaLight.Falloff * AreaLight.NoL);\r\n\t\tLighting.Specular *= ComputeEnergyConservation(EnergyTerms);\r\n}\r\n\t\tcase SHADINGMODELID_DEFAULT_LIT:\r\n"));
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
    for (const FString& File : Files)
    {
        FString SnapshotId = State->GetStringField(TEXT("baseline"));
        if (!IFileManager::Get().FileExists(*(Manager.GetStore() / SnapshotId / File)))
        {
            FString ExtensionText;
            FFileHelper::LoadFileToString(ExtensionText, *(Manager.GetStore() / (File == TEXT("DeferredShadingCommon.ush") ? TEXT("legacy-extension.json") : TEXT("ramp-extension.json"))));
            TSharedPtr<FJsonObject> Extension;
            FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(ExtensionText), Extension);
            if (!TestTrue(TEXT("Legacy baseline extension exists"), Extension.IsValid())) return false;
            SnapshotId = Extension->GetStringField(TEXT("snapshot"));
        }
        Original.Add(File, Read(Manager.GetStore() / SnapshotId / File));
    }
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
    TestTrue(TEXT("At least one registered function"), !Functions.IsEmpty());
    if (!TestTrue(TEXT("Native generation"), FMazelineShaderPatchManager::Generate(Original, Functions, Generated, Message))) return false;
    for (const FString& File : Files)
        TestTrue(TEXT("Active engine matches current native generator: ") + File,
            Generated[File] == Read(Manager.GetEngine() / TEXT("Engine/Shaders/Private") / File));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMazelineOptionalSpecular, "Mazeline.NativePatch.OptionalSpecular", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMazelineOptionalSpecular::RunTest(const FString&)
{
    FFixture F;
    auto Manager = F.Manager();
    FString Message;
    bool bChanged = false;
    const FString Base = TEXT("void MazelineEvaluateShaderOverride() {}\n");
    const FString OptIn = TEXT("#define MAZELINE_HAS_SPECULAR_OVERRIDE 1\n");
    const FString Callback = TEXT("float3 MazelineEvaluateSpecularOverride() { return 0; }\n");
    Write(F.Root / TEXT("function.ush"), Base + OptIn);
    TestFalse(TEXT("Missing callback rejected before mutation"), Manager.Execute(TEXT("apply"), F.Registry, Message, bChanged));
    TestTrue(TEXT("Missing callback keeps engine original"), F.IsOriginal());
    Write(F.Root / TEXT("function.ush"), Base + Callback);
    TestFalse(TEXT("Missing opt-in rejected before mutation"), Manager.Execute(TEXT("apply"), F.Registry, Message, bChanged));
    TestTrue(TEXT("Missing opt-in keeps engine original"), F.IsOriginal());
    Write(F.Root / TEXT("function.ush"), Base + OptIn + Callback);
    TestTrue(TEXT("Optional specular applies"), Manager.Execute(TEXT("apply"), F.Registry, Message, bChanged));
    TestTrue(TEXT("Optional specular restores exactly"), Manager.Execute(TEXT("restore"), FString(), Message, bChanged) && F.IsOriginal());

    auto Invalid = F.Original;
    FString Models = FMazelineShaderPatchManager::Decode(Invalid[Files[2]]);
    Models.ReplaceInline(TEXT("\t\tLighting.Specular *= ComputeEnergyConservation(EnergyTerms);"), TEXT("changed engine anchor"));
    Invalid[Files[2]] = FMazelineShaderPatchManager::Utf8(Models);
    FMazelineShaderPatchManager::FBundle Generated;
    TMap<int32, FString> Functions;
    Functions.Add(2, Base + OptIn + Callback);
    TestFalse(TEXT("Unknown specular anchor rejected"), FMazelineShaderPatchManager::Generate(Invalid, Functions, Generated, Message));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMazelineGGXDistribution, "Mazeline.NativePatch.GGXDistribution", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMazelineGGXDistribution::RunTest(const FString&)
{
    FFixture F;
    auto Manager = F.Manager();
    FString Message;
    bool bChanged = false;
    const FString Base = TEXT("void MazelineEvaluateShaderOverride() {}\n");
    const FString OptIn = TEXT("#define MAZELINE_HAS_GGX_DISTRIBUTION_OVERRIDE 1\n");
    const FString Callback = TEXT("float MazelineEvaluateGGXDistribution(float D, float PeakD) { return D; }\n");
    Write(F.Root / TEXT("function.ush"), Base + OptIn);
    TestFalse(TEXT("GGX callback required"), Manager.Execute(TEXT("apply"), F.Registry, Message, bChanged));
    TestTrue(TEXT("Invalid GGX leaves originals intact"), F.IsOriginal());
    Write(F.Root / TEXT("function.ush"), Base + OptIn + Callback + TEXT("#define MAZELINE_HAS_SPECULAR_OVERRIDE 1\nfloat3 MazelineEvaluateSpecularOverride() { return 0; }\n"));
    TestFalse(TEXT("Competing specular callbacks rejected"), Manager.Execute(TEXT("apply"), F.Registry, Message, bChanged));
    TestTrue(TEXT("Competing modes leave originals intact"), F.IsOriginal());
    Write(F.Root / TEXT("function.ush"), Base + OptIn + Callback);
    TestTrue(TEXT("GGX generation applies"), Manager.Execute(TEXT("apply"), F.Registry, Message, bChanged));
    TestTrue(TEXT("GGX variant restore is byte-exact"), Manager.Execute(TEXT("restore"), FString(), Message, bChanged) && F.IsOriginal());
    auto Invalid = F.Original;
    FString Models = FMazelineShaderPatchManager::Decode(Invalid[Files[2]]);
    Models.ReplaceInline(TEXT("\tfloat D = D_GGX( a2, Context.NoH ) * Energy;"), TEXT("changed distribution formula"));
    Invalid[Files[2]] = FMazelineShaderPatchManager::Utf8(Models);
    FMazelineShaderPatchManager::FBundle Generated;
    TMap<int32, FString> Functions;
    Functions.Add(2, Base + OptIn + Callback);
    TestFalse(TEXT("Engine GGX drift rejected"), FMazelineShaderPatchManager::Generate(Invalid, Functions, Generated, Message));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMazelineParameterMigration, "Mazeline.NativePatch.CustomDataMigration", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMazelineParameterMigration::RunTest(const FString&)
{
    FFixture F;
    auto Manager = F.Manager();
    FString Message;
    bool bChanged = false;
    TestTrue(TEXT("Seed managed state"), Manager.Execute(TEXT("apply"), F.Registry, Message, bChanged));
    FString StateText;
    FFileHelper::LoadFileToString(StateText, *(Manager.GetStore() / TEXT("state.json")));
    TSharedPtr<FJsonObject> State;
    FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(StateText), State);
    if (!TestTrue(TEXT("Seed state parsed"), State.IsValid())) return false;
    TMap<FString, TArray<uint8>> LegacyManifests;
    for (const TCHAR* Key : {TEXT("baseline"), TEXT("active")})
    {
        const FString ManifestPath = Manager.GetStore() / State->GetStringField(Key) / TEXT("manifest.json");
        FString Text;
        FFileHelper::LoadFileToString(Text, *ManifestPath);
        TSharedPtr<FJsonObject> Manifest;
        FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Manifest);
        Manifest->GetObjectField(TEXT("files"))->RemoveField(Files[3]);
        Manifest->GetObjectField(TEXT("files"))->RemoveField(Files[4]);
        Text.Reset();
        FJsonSerializer::Serialize(Manifest.ToSharedRef(), TJsonWriterFactory<>::Create(&Text));
        Write(ManifestPath, Text);
        LegacyManifests.Add(ManifestPath, Read(ManifestPath));
    }
    FFileHelper::SaveArrayToFile(F.Original[Files[3]], *(F.Shaders / Files[3]));
    FMazelinePatchStatus Status;
    TestTrue(TEXT("Read legacy three-file state"), Manager.Inspect(Status, Message) && Status.bFilesMatch);
    Write(F.Root / TEXT("function.ush"), TEXT("void MazelineEvaluateShaderOverride() {}\n#define MAZELINE_HAS_GGX_DISTRIBUTION_OVERRIDE 1\n#define MAZELINE_GGX_MATERIAL_PARAMETERS 1\nfloat MazelineEvaluateGGXDistribution(float D, float PeakD, float Sharpness, float Threshold) { return D; }\n"));
    Manager.FailBeforeShaderWrite = 1;
    TestFalse(TEXT("Interrupt after extending legacy backup"), Manager.Execute(TEXT("apply"), F.Registry, Message, bChanged));
    auto Reopened = F.Manager();
    TestTrue(TEXT("Recover legacy-to-five-file transition"), Reopened.Execute(TEXT("recover"), FString(), Message, bChanged));
    TestTrue(TEXT("Fourth original restored during recovery"), Read(F.Shaders / Files[3]) == F.Original[Files[3]]);
    TestTrue(TEXT("Apply parameter carrier"), Reopened.Execute(TEXT("apply"), F.Registry, Message, bChanged));
    for (const auto& Pair : LegacyManifests) TestTrue(TEXT("Legacy backup remains immutable"), Read(Pair.Key) == Pair.Value);
    TestTrue(TEXT("Restore all five original files"), Reopened.Execute(TEXT("restore"), FString(), Message, bChanged) && F.IsOriginal());
    TestTrue(TEXT("Reapply after extended restore"), Reopened.Execute(TEXT("apply"), F.Registry, Message, bChanged));
    Write(F.Shaders / Files[3], TEXT("external fourth-file edit"));
    TestFalse(TEXT("External fourth-file edit blocks restore"), Reopened.Execute(TEXT("restore"), FString(), Message, bChanged));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMazelineDirectionalRamp, "Mazeline.NativePatch.DirectionalRamp", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMazelineDirectionalRamp::RunTest(const FString&)
{
    FFixture F;
    auto Manager = F.Manager();
    FString Message;
    bool bChanged = false;
    TestTrue(TEXT("Seed pre-ramp state"), Manager.Execute(TEXT("apply"), F.Registry, Message, bChanged));
    FString StateText;
    FFileHelper::LoadFileToString(StateText, *(Manager.GetStore() / TEXT("state.json")));
    TSharedPtr<FJsonObject> State;
    FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(StateText), State);
    if (!TestTrue(TEXT("Seed state parsed"), State.IsValid())) return false;
    TMap<FString, TArray<uint8>> OldManifests;
    for (const TCHAR* Key : {TEXT("baseline"), TEXT("active")})
    {
        const FString Path = Manager.GetStore() / State->GetStringField(Key) / TEXT("manifest.json");
        FString Text;
        FFileHelper::LoadFileToString(Text, *Path);
        TSharedPtr<FJsonObject> Manifest;
        FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Manifest);
        Manifest->GetObjectField(TEXT("files"))->RemoveField(Files[4]);
        Text.Reset();
        FJsonSerializer::Serialize(Manifest.ToSharedRef(), TJsonWriterFactory<>::Create(&Text));
        Write(Path, Text);
        OldManifests.Add(Path, Read(Path));
    }
    // Ramp is independent of the GGX callback; a diffuse-only function also works.
    const FString Code = TEXT("void MazelineEvaluateShaderOverride() {}\n#define MAZELINE_DIRECTIONAL_RAMP 1\n");
    Write(F.Root / TEXT("function.ush"), Code);
    TestTrue(TEXT("Extend four-file backup and apply ramp"), Manager.Execute(TEXT("apply"), F.Registry, Message, bChanged));
    for (const auto& Pair : OldManifests) TestTrue(TEXT("Old manifests remain immutable"), Read(Pair.Key) == Pair.Value);
    const FString Lighting = FMazelineShaderPatchManager::Decode(Read(F.Shaders / Files[4]));
    TestTrue(TEXT("Both local light classes excluded"), Lighting.Contains(TEXT("!LightData.bRadialLight && !LightData.bRectLight")));
    TestTrue(TEXT("Ramp-only custom-data carrier enabled"), FMazelineShaderPatchManager::Decode(Read(F.Shaders / Files[1])).Contains(TEXT("GetMazelineRamp0(MaterialParameters)")));
    TestTrue(TEXT("Unlit expression previews skip custom-output requirements"), FMazelineShaderPatchManager::Decode(Read(F.Shaders / Files[1])).Contains(TEXT("#if MATERIAL_SHADINGMODEL_DEFAULT_LIT && !SUBSTRATE_ENABLED")));
    TestTrue(TEXT("Restore five byte-exact originals"), Manager.Execute(TEXT("restore"), FString(), Message, bChanged) && F.IsOriginal());
    auto Invalid = F.Original;
    Invalid[Files[4]] = FMazelineShaderPatchManager::Utf8(TEXT("changed light dispatch"));
    FMazelineShaderPatchManager::FBundle Generated;
    TMap<int32, FString> Functions;
    Functions.Add(2, Code);
    TestFalse(TEXT("Light dispatch drift fails before mutation"), FMazelineShaderPatchManager::Generate(Invalid, Functions, Generated, Message));
    TestTrue(TEXT("Reapply ramp"), Manager.Execute(TEXT("apply"), F.Registry, Message, bChanged));
    Write(F.Shaders / Files[4], TEXT("external light shader edit"));
    TestFalse(TEXT("External fifth-file edit blocks restore"), Manager.Execute(TEXT("restore"), FString(), Message, bChanged));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMazelineDefaultLitParameters, "Mazeline.NativePatch.DefaultLitParameters", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMazelineDefaultLitParameters::RunTest(const FString&)
{
    UMaterial* Material = NewObject<UMaterial>(GetTransientPackage());
    Material->SetShadingModel(MSM_ClearCoat);
    auto* Sharpness = NewObject<UMaterialExpressionScalarParameter>(Material);
    auto* Threshold = NewObject<UMaterialExpressionScalarParameter>(Material);
    Material->GetExpressionCollection().AddExpression(Sharpness);
    Material->GetExpressionCollection().AddExpression(Threshold);
    Material->GetExpressionInputForProperty(MP_Specular)->Connect(0, Sharpness);
    Material->GetExpressionInputForProperty(MP_AmbientOcclusion)->Connect(0, Threshold);
    TestTrue(TEXT("Seed previous carrier"), UMazelineMaterialLibrary::ConnectGGXParameters(Material, Sharpness, Threshold));
    TestTrue(TEXT("Migrate existing graph"), UMazelineMaterialLibrary::MigrateGGXParametersToDefaultLit(Material, Sharpness, Threshold));
    TestTrue(TEXT("Authoring model is Default Lit"), Material->GetShadingModels().HasOnlyShadingModel(MSM_DefaultLit));
    TestTrue(TEXT("Specular input survives"), Material->GetExpressionInputForProperty(MP_Specular)->Expression == Sharpness);
    TestTrue(TEXT("AO input survives"), Material->GetExpressionInputForProperty(MP_AmbientOcclusion)->Expression == Threshold);
    TestNull(TEXT("Clear Coat pin disconnected"), Material->GetExpressionInputForProperty(MP_CustomData0)->Expression);
    TestNull(TEXT("Clear Coat Roughness pin disconnected"), Material->GetExpressionInputForProperty(MP_CustomData1)->Expression);
    TestTrue(TEXT("Sharpness readback"), UMazelineMaterialLibrary::GetGGXParameterInput(Material, false) == Sharpness);
    TestTrue(TEXT("Threshold readback"), UMazelineMaterialLibrary::GetGGXParameterInput(Material, true) == Threshold);
    TestTrue(TEXT("Repeat migration"), UMazelineMaterialLibrary::MigrateGGXParametersToDefaultLit(Material, Sharpness, Threshold));
    TArray<UMaterialExpressionCustomOutput*> Outputs;
    Material->GetAllCustomOutputExpressions(Outputs);
    TestEqual(TEXT("Exactly one output node"), Outputs.Num(), 1);
    TestFalse(TEXT("Refuse conflicting output connection"), UMazelineMaterialLibrary::MigrateGGXParametersToDefaultLit(Material, Threshold, Sharpness));
    TestTrue(TEXT("Conflict leaves original input"), UMazelineMaterialLibrary::GetGGXParameterInput(Material, false) == Sharpness);
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
