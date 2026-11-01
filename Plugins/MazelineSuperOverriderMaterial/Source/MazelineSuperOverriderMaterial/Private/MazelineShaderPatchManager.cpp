#include "MazelineShaderPatchManager.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "HAL/FileManager.h"
#include "Interfaces/IPluginManager.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpression.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Internationalization/Regex.h"
#include "Editor.h"
#include "RenderingThread.h"
#include "ShaderCompiler.h"
#include "ShaderCore.h"
#include "Windows/WindowsHWrapper.h"
#include <bcrypt.h>
#include <tlhelp32.h>

namespace MazelinePatch
{
bool bRestartRequired = false;
const TArray<FString> Files = { TEXT("ShadingCommon.ush"), TEXT("ShadingModelsMaterial.ush"), TEXT("ShadingModels.ush") };
const FString Anchor = TEXT("FDirectLighting DefaultLitBxDF( FGBufferData GBuffer, half3 N, half3 V, FAreaLight AreaLight, FShadowTerms Shadow )");
const FString ForwardMarker = TEXT("// MAZELINE_SHADER_OVERRIDE_PATCH_FORWARD_V1");
const FString DeferredMarker = TEXT("MAZELINE_DEFERRED_PROBE_V1");

bool Fail(FString& Error, const FString& Message) { Error = Message; return false; }
FString Full(const FString& Path)
{
    FString Result = FPaths::ConvertRelativePathToFull(Path);
    FPaths::NormalizeFilename(Result);
    FPaths::CollapseRelativeDirectories(Result);
    while (Result.EndsWith(TEXT("/"))) Result.LeftChopInline(1);
    return Result;
}
bool Same(const FString& A, const FString& B) { return Full(A).Equals(Full(B), ESearchCase::IgnoreCase); }
bool Matches(const FRegexPattern& Pattern, const FString& Text)
{
    FRegexMatcher Matcher(Pattern, Text);
    return Matcher.FindNext();
}
bool Equal(const FMazelineShaderPatchManager::FBundle& A, const FMazelineShaderPatchManager::FBundle& B)
{
    if (A.Num() != B.Num()) return false;
    for (const FString& File : Files)
        if (!A.Contains(File) || !B.Contains(File) || A[File] != B[File]) return false;
    return true;
}
bool ReadBytes(const FString& Path, TArray<uint8>& Bytes, FString& Error)
{
    return FFileHelper::LoadFileToArray(Bytes, *Path) || Fail(Error, TEXT("Cannot read: ") + Path);
}
bool ReadJson(const FString& Path, TSharedPtr<FJsonObject>& Value, FString& Error)
{
    FString Text;
    if (!FFileHelper::LoadFileToString(Text, *Path)) return Fail(Error, TEXT("Cannot read JSON: ") + Path);
    return FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Value) && Value.IsValid()
        ? true : Fail(Error, TEXT("Invalid JSON: ") + Path);
}
bool AtomicWrite(const FString& Path, const TArray<uint8>& Bytes, FString& Error)
{
    const FString Temporary = Path + TEXT(".mazeline-") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
    HANDLE Handle = CreateFileW(*Temporary, GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (Handle == INVALID_HANDLE_VALUE) return Fail(Error, FString::Printf(TEXT("Cannot create %s (Windows error %u)"), *Temporary, GetLastError()));
    DWORD Written = 0;
    const bool bWritten = WriteFile(Handle, Bytes.GetData(), Bytes.Num(), &Written, nullptr) && Written == static_cast<DWORD>(Bytes.Num()) && FlushFileBuffers(Handle);
    const DWORD WriteError = GetLastError();
    CloseHandle(Handle);
    if (!bWritten)
    {
        DeleteFileW(*Temporary);
        return Fail(Error, FString::Printf(TEXT("Write failed: %s (Windows error %u)"), *Path, WriteError));
    }
    if (!MoveFileExW(*Temporary, *Path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    {
        const DWORD MoveError = GetLastError();
        DeleteFileW(*Temporary);
        return Fail(Error, FString::Printf(TEXT("Replace failed: %s (Windows error %u)"), *Path, MoveError));
    }
    return true;
}
bool WriteJson(const FString& Path, const TSharedRef<FJsonObject>& Value, FString& Error)
{
    FString Text;
    if (!FJsonSerializer::Serialize(Value, TJsonWriterFactory<>::Create(&Text))) return Fail(Error, TEXT("JSON serialization failed"));
    return AtomicWrite(Path, FMazelineShaderPatchManager::Utf8(Text + TEXT("\n")), Error);
}
FString Field(const TSharedPtr<FJsonObject>& Object, const TCHAR* Key)
{
    FString Result;
    if (Object) Object->TryGetStringField(Key, Result);
    return Result;
}
TSharedPtr<FJsonObject> ObjectField(const TSharedPtr<FJsonObject>& Object, const TCHAR* Key)
{
    const TSharedPtr<FJsonObject>* Result = nullptr;
    return Object && Object->TryGetObjectField(Key, Result) ? *Result : nullptr;
}
class FLock
{
    HANDLE Handle = INVALID_HANDLE_VALUE;
    OVERLAPPED Overlapped = {};
    bool bLocked = false;
public:
    bool Acquire(const FString& Store, FString& Error)
    {
        if (!IFileManager::Get().MakeDirectory(*Store, true)) return Fail(Error, TEXT("Cannot create backup directory: ") + Store);
        Handle = CreateFileW(*(Store / TEXT("operation.lock")), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (Handle == INVALID_HANDLE_VALUE) return Fail(Error, TEXT("Cannot open engine patch lock"));
        bLocked = LockFileEx(Handle, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY, 0, 1, 0, &Overlapped);
        return bLocked || Fail(Error, TEXT("Another patch operation holds this engine lock"));
    }
    ~FLock()
    {
        if (bLocked) UnlockFileEx(Handle, 0, 1, 0, &Overlapped);
        if (Handle != INVALID_HANDLE_VALUE) CloseHandle(Handle);
    }
};
bool ReplaceOnce(FString& Source, const FString& Old, const FString& New, FString& Error)
{
    const int32 At = Source.Find(Old, ESearchCase::CaseSensitive);
    if (At == INDEX_NONE || Source.Find(Old, ESearchCase::CaseSensitive, ESearchDir::FromStart, At + Old.Len()) != INDEX_NONE)
        return Fail(Error, TEXT("Expected exactly one shader anchor: ") + Old.Left(100));
    Source = Source.Left(At) + New + Source.Mid(At + Old.Len());
    return true;
}
bool SourceCode(const FString& Path, FString& Code, FString& Error)
{
    if (!FFileHelper::LoadFileToString(Code, *Path)) return Fail(Error, TEXT("Cannot read HLSL: ") + Path);
    Code.ReplaceInline(TEXT("\r\n"), TEXT("\n"));
    Code.ReplaceInline(TEXT("\r"), TEXT("\n"));
    if (!Matches(FRegexPattern(TEXT("\\bvoid\\s+MazelineEvaluateShaderOverride\\s*\\(")), Code))
        return Fail(Error, TEXT("Missing MazelineEvaluateShaderOverride function: ") + Path);
    if (Matches(FRegexPattern(TEXT("(?m)^\\s*#\\s*include\\b")), Code))
        return Fail(Error, TEXT("Nested includes are not supported: ") + Path);
    return true;
}
bool LoadRegistry(const FString& Path, const FString& Project, TMap<int32, FString>& Functions, FString& Error)
{
    TSharedPtr<FJsonObject> Json;
    if (!ReadJson(Path, Json, Error)) return false;
    FString Owner = Field(Json, TEXT("project"));
    if (!Owner.IsEmpty() && !Same(Owner, Project)) return Fail(Error, TEXT("Registry belongs to another project"));
    const TArray<TSharedPtr<FJsonValue>>* Rows = nullptr;
    if (!Json->TryGetArrayField(TEXT("functions"), Rows) || Rows->IsEmpty()) return Fail(Error, TEXT("Registry has no functions"));
    for (const TSharedPtr<FJsonValue>& Value : *Rows)
    {
        const TSharedPtr<FJsonObject>* Row = nullptr;
        double Id = 0;
        if (!Value->TryGetObject(Row) || !(*Row)->TryGetNumberField(TEXT("id"), Id) || Id < 1 || Id > 3 || Id != FMath::FloorToDouble(Id) || Functions.Contains(static_cast<int32>(Id)))
            return Fail(Error, TEXT("Function IDs must be unique integers 1, 2, or 3"));
        FString Source = Field(*Row, TEXT("source"));
        if (Source.IsEmpty()) return Fail(Error, TEXT("Registry source is empty"));
        if (FPaths::IsRelative(Source)) Source = FPaths::GetPath(Path) / Source;
        FString Code;
        if (!SourceCode(Source, Code, Error)) return false;
        const FString Expected = Field(*Row, TEXT("sha256"));
        if (!Expected.IsEmpty() && Expected != FMazelineShaderPatchManager::Hash(FMazelineShaderPatchManager::Utf8(Code)))
            return Fail(Error, TEXT("HLSL changed since registry preparation: ") + Source);
        Functions.Add(static_cast<int32>(Id), MoveTemp(Code));
    }
    return true;
}
}

using namespace MazelinePatch;

TArray<uint8> FMazelineShaderPatchManager::Utf8(const FString& Text)
{
    FTCHARToUTF8 Converted(*Text, Text.Len());
    TArray<uint8> Result;
    Result.Append(reinterpret_cast<const uint8*>(Converted.Get()), Converted.Length());
    return Result;
}
FString FMazelineShaderPatchManager::Decode(const TArray<uint8>& Bytes)
{
    FUTF8ToTCHAR Converted(reinterpret_cast<const ANSICHAR*>(Bytes.GetData()), Bytes.Num());
    return FString(Converted.Length(), Converted.Get());
}
FString FMazelineShaderPatchManager::Hash(const TArray<uint8>& Bytes)
{
    BCRYPT_ALG_HANDLE Algorithm = nullptr;
    BCRYPT_HASH_HANDLE Handle = nullptr;
    uint8 Digest[32] = {};
    bool bOk = BCryptOpenAlgorithmProvider(&Algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) >= 0;
    if (bOk) bOk = BCryptCreateHash(Algorithm, &Handle, nullptr, 0, nullptr, 0, 0) >= 0;
    if (bOk) bOk = BCryptHashData(Handle, const_cast<PUCHAR>(Bytes.GetData()), Bytes.Num(), 0) >= 0;
    if (bOk) bOk = BCryptFinishHash(Handle, Digest, sizeof(Digest), 0) >= 0;
    if (Handle) BCryptDestroyHash(Handle);
    if (Algorithm) BCryptCloseAlgorithmProvider(Algorithm, 0);
    return bOk ? BytesToHex(Digest, sizeof(Digest)).ToLower() : FString();
}
FMazelineShaderPatchManager::FMazelineShaderPatchManager(const FString& EngineRoot, const FString& ProjectFile)
    : Engine(Full(EngineRoot)), Project(Full(ProjectFile)), Store(Engine / TEXT("Engine/Saved/MazelineShaderOverride")) {}
FMazelineShaderPatchManager FMazelineShaderPatchManager::Current()
{
    return FMazelineShaderPatchManager(FPaths::RootDir(), FPaths::GetProjectFilePath());
}
bool FMazelineShaderPatchManager::RestartRequired() { return MazelinePatch::bRestartRequired; }
bool FMazelineShaderPatchManager::RunCurrent(const FString& Action, FString& Message)
{
    if (GEditor && GEditor->PlayWorld) return Fail(Message, TEXT("End Play In Editor before changing engine shaders."));
    FString RegistryFile;
    if ((Action == TEXT("apply") || Action == TEXT("prepare")) && !BuildRegistry(RegistryFile, Message)) return false;
    if (Action == TEXT("prepare")) { Message = TEXT("Native C++ registry prepared: ") + RegistryFile; return true; }
    // Drain this editor's in-flight readers before replacing the source files.
    if (GShaderCompilingManager) GShaderCompilingManager->FinishAllCompilation();
    FlushRenderingCommands();
    FMazelineShaderPatchManager Manager = Current();
    bool bChanged = false;
    const bool bSuccess = Manager.Execute(Action, RegistryFile, Message, bChanged);
    if (bChanged)
    {
        MazelinePatch::bRestartRequired = true;
        FlushShaderFileCache();
    }
    if (MazelinePatch::bRestartRequired) Message += TEXT("\nRestart the editor to load the new shader state.");
    return bSuccess;
}
bool FMazelineShaderPatchManager::Validate(FString& Error) const
{
    TSharedPtr<FJsonObject> Version;
    if (!ReadJson(Engine / TEXT("Engine/Build/Build.version"), Version, Error)) return false;
    double Major = 0, Minor = 0, Patch = 0;
    if (!Version->TryGetNumberField(TEXT("MajorVersion"), Major) || !Version->TryGetNumberField(TEXT("MinorVersion"), Minor) ||
        !Version->TryGetNumberField(TEXT("PatchVersion"), Patch) || Major != 5 || Minor != 8 || Patch != 4)
        return Fail(Error, TEXT("Only the installed UE 5.8.4 shader layout is supported"));
    if (!Project.EndsWith(TEXT(".uproject")) || !IFileManager::Get().FileExists(*Project))
        return Fail(Error, TEXT("Project file is missing: ") + Project);
    // Reject redirected patch targets and snapshots rather than following a junction.
    TArray<FString> Paths = { Engine / TEXT("Engine/Shaders"), Engine / TEXT("Engine/Shaders/Private"), Engine / TEXT("Engine/Saved"), Store };
    for (const FString& File : Files) Paths.Add(Engine / TEXT("Engine/Shaders/Private") / File);
    for (const FString& Path : Paths)
    {
        DWORD Attributes = GetFileAttributesW(*Path);
        if (Attributes != INVALID_FILE_ATTRIBUTES && (Attributes & FILE_ATTRIBUTE_REPARSE_POINT))
            return Fail(Error, TEXT("Redirected patch/backup paths are not supported: ") + Path);
    }
    return true;
}
bool FMazelineShaderPatchManager::ReadCurrent(FBundle& Bundle, FString& Error) const
{
    for (const FString& File : Files)
        if (!ReadBytes(Engine / TEXT("Engine/Shaders/Private") / File, Bundle.FindOrAdd(File), Error)) return false;
    return true;
}
bool FMazelineShaderPatchManager::ReadState(TSharedPtr<FJsonObject>& State, FString& Error) const
{
    if (!IFileManager::Get().FileExists(*(Store / TEXT("state.json")))) { State.Reset(); return true; }
    if (!ReadJson(Store / TEXT("state.json"), State, Error)) return false;
    double Format = 0;
    return State->TryGetNumberField(TEXT("format"), Format) && Format == 1 && Same(Field(State, TEXT("engine")), Engine)
        ? true : Fail(Error, TEXT("Patch state format or engine identity mismatch"));
}
bool FMazelineShaderPatchManager::ReadSnapshot(const FString& Id, FBundle& Bundle, FString& Error) const
{
    if (!Matches(FRegexPattern(TEXT("^[0-9a-f]{32}$")), Id)) return Fail(Error, TEXT("Invalid snapshot ID"));
    const FString Directory = Store / Id;
    if (GetFileAttributesW(*Directory) & FILE_ATTRIBUTE_REPARSE_POINT) return Fail(Error, TEXT("Snapshot is missing or redirected"));
    TSharedPtr<FJsonObject> Manifest;
    if (!ReadJson(Directory / TEXT("manifest.json"), Manifest, Error)) return false;
    TSharedPtr<FJsonObject> Hashes = ObjectField(Manifest, TEXT("files"));
    if (!Hashes || Hashes->Values.Num() != Files.Num()) return Fail(Error, TEXT("Invalid snapshot manifest"));
    for (const FString& File : Files)
    {
        if (!ReadBytes(Directory / File, Bundle.FindOrAdd(File), Error)) return false;
        const FString Expected = Field(Hashes, *File);
        if (Expected.Len() != 64 || Hash(Bundle[File]) != Expected) return Fail(Error, TEXT("Snapshot checksum mismatch: ") + File);
    }
    return true;
}
bool FMazelineShaderPatchManager::Snapshot(const FBundle& Bundle, const FString& Type, FString& Id, FString& Error) const
{
    Id = FGuid::NewGuid().ToString(EGuidFormats::Digits).ToLower();
    const FString Directory = Store / Id;
    if (!IFileManager::Get().MakeDirectory(*Directory, true)) return Fail(Error, TEXT("Cannot create snapshot directory"));
    TSharedRef<FJsonObject> Hashes = MakeShared<FJsonObject>();
    for (const FString& File : Files)
    {
        const FString Digest = Hash(Bundle[File]);
        if (Digest.Len() != 64) return Fail(Error, TEXT("SHA-256 calculation failed"));
        if (!AtomicWrite(Directory / File, Bundle[File], Error)) return false;
        Hashes->SetStringField(File, Digest);
    }
    TSharedRef<FJsonObject> Details = MakeShared<FJsonObject>();
    Details->SetStringField(TEXT("type"), Type);
    Details->SetStringField(TEXT("project"), Project);
    Details->SetStringField(TEXT("implementation"), TEXT("native-cpp"));
    TSharedRef<FJsonObject> Manifest = MakeShared<FJsonObject>();
    Manifest->SetObjectField(TEXT("files"), Hashes);
    Manifest->SetObjectField(TEXT("details"), Details);
    return WriteJson(Directory / TEXT("manifest.json"), Manifest, Error);
}
bool FMazelineShaderPatchManager::Owns(const TSharedPtr<FJsonObject>& State, FString& Error) const
{
    FString Owner = Field(ObjectField(State, TEXT("pending")), TEXT("owner"));
    if (Owner.IsEmpty()) Owner = Field(State, TEXT("owner"));
    return Owner.IsEmpty() || Same(Owner, Project) ? true : Fail(Error, TEXT("This engine patch belongs to another project: ") + Owner);
}
bool FMazelineShaderPatchManager::Ready(const TSharedPtr<FJsonObject>& State, FBundle& Before, FString& Error) const
{
    if (!Owns(State, Error)) return false;
    if (State->HasField(TEXT("pending"))) return Fail(Error, TEXT("Interrupted transaction found. Use Recover first."));
    if (!ReadSnapshot(Field(State, TEXT("active")), Before, Error)) return false;
    FBundle CurrentBundle;
    return ReadCurrent(CurrentBundle, Error) && (Equal(CurrentBundle, Before) || Fail(Error, TEXT("Engine shaders have external changes; nothing was overwritten")));
}
bool FMazelineShaderPatchManager::WriteShaders(const FBundle& Before, const FBundle& After, bool& bChanged, FString& Error)
{
    FBundle CurrentBundle;
    if (!ReadCurrent(CurrentBundle, Error)) return false;
    if (!Equal(CurrentBundle, Before)) return Fail(Error, TEXT("Shader changed during preparation; use Recover"));
    for (const FString& File : Files)
    {
        const FString Path = Engine / TEXT("Engine/Shaders/Private") / File;
        TArray<uint8> CurrentBytes;
        if (!ReadBytes(Path, CurrentBytes, Error)) return false;
        if (CurrentBytes != Before[File]) return Fail(Error, TEXT("External edit during transaction: ") + File);
        if (CurrentBytes == After[File]) continue;
#if WITH_DEV_AUTOMATION_TESTS
        if (FailBeforeShaderWrite == 0) return Fail(Error, TEXT("Injected shader write failure"));
        if (FailBeforeShaderWrite > 0) --FailBeforeShaderWrite;
#endif
        if (!AtomicWrite(Path, After[File], Error)) return false;
        bChanged = true;
    }
    return ReadCurrent(CurrentBundle, Error) && (Equal(CurrentBundle, After) || Fail(Error, TEXT("Shader verification failed; use Recover")));
}
bool FMazelineShaderPatchManager::Transition(TSharedPtr<FJsonObject>& State, const FString& Destination, const FString& Owner, bool& bChanged, FString& Error)
{
    FBundle Before, After;
    if (!Ready(State, Before, Error) || !ReadSnapshot(Destination, After, Error)) return false;
    TSharedRef<FJsonObject> Pending = MakeShared<FJsonObject>();
    Pending->SetStringField(TEXT("destination"), Destination);
    Pending->SetStringField(TEXT("owner"), Project);
    State->SetObjectField(TEXT("pending"), Pending);
    if (!WriteJson(Store / TEXT("state.json"), State.ToSharedRef(), Error)) return false;
    if (!WriteShaders(Before, After, bChanged, Error)) return false;
    State->RemoveField(TEXT("pending"));
    State->SetStringField(TEXT("active"), Destination);
    if (Owner.IsEmpty()) State->SetField(TEXT("owner"), MakeShared<FJsonValueNull>());
    else State->SetStringField(TEXT("owner"), Owner);
    return WriteJson(Store / TEXT("state.json"), State.ToSharedRef(), Error);
}
bool FMazelineShaderPatchManager::EnsureNoOtherEditors(FString& Error) const
{
    HANDLE SnapshotHandle = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (SnapshotHandle == INVALID_HANDLE_VALUE) return Fail(Error, TEXT("Cannot enumerate engine processes"));
    PROCESSENTRY32W Entry = {}; Entry.dwSize = sizeof(Entry);
    bool bOk = Process32FirstW(SnapshotHandle, &Entry) != 0;
    if (!bOk) { CloseHandle(SnapshotHandle); return Fail(Error, TEXT("Cannot inspect engine processes")); }
    do
    {
        const FString Name(Entry.szExeFile);
        if (!Name.StartsWith(TEXT("UnrealEditor"), ESearchCase::IgnoreCase) && !Name.Equals(TEXT("ShaderCompileWorker.exe"), ESearchCase::IgnoreCase)) continue;
        if (Entry.th32ProcessID == GetCurrentProcessId() || (Name.Equals(TEXT("ShaderCompileWorker.exe"), ESearchCase::IgnoreCase) && Entry.th32ParentProcessID == GetCurrentProcessId())) continue;
        HANDLE Process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, false, Entry.th32ProcessID);
        WCHAR Buffer[32768]; DWORD Size = UE_ARRAY_COUNT(Buffer);
        const bool bReadable = Process && QueryFullProcessImageNameW(Process, 0, Buffer, &Size);
        if (Process) CloseHandle(Process);
        if (!bReadable || Full(FString(Size, Buffer)).StartsWith(Engine + TEXT("/"), ESearchCase::IgnoreCase))
        {
            CloseHandle(SnapshotHandle);
            return Fail(Error, FString::Printf(TEXT("Close the other engine editor/compiler first (PID %u)"), Entry.th32ProcessID));
        }
    } while (Process32NextW(SnapshotHandle, &Entry));
    CloseHandle(SnapshotHandle);
    return true;
}
bool FMazelineShaderPatchManager::Inspect(FMazelinePatchStatus& Status, FString& Error)
{
    Status = FMazelinePatchStatus();
    Status.BackupDirectory = Store;
    if (!Validate(Error)) { Status.State = TEXT("unsupported"); return false; }
    FLock Lock;
    if (!Lock.Acquire(Store, Error)) { Status.State = TEXT("busy"); return false; }
    TSharedPtr<FJsonObject> State;
    if (!ReadState(State, Error)) { Status.State = TEXT("conflict"); return false; }
    if (!State)
    {
        FBundle CurrentBundle;
        if (!ReadCurrent(CurrentBundle, Error)) return false;
        for (const auto& Pair : CurrentBundle)
            if (Decode(Pair.Value).Contains(TEXT("MAZELINE_")))
            { Status.State = TEXT("conflict"); return Fail(Error, TEXT("Unmanaged Mazeline patch found; preserve its original backups")); }
        Status.Detail = TEXT("No managed patch is installed.");
        return true;
    }
    Status.Owner = Field(State, TEXT("owner"));
    Status.ActiveSnapshot = Field(State, TEXT("active"));
    FString OwnershipError;
    Status.bOwnedByProject = Owns(State, OwnershipError);
    FBundle Active, Baseline, CurrentBundle;
    if (!ReadSnapshot(Field(State, TEXT("baseline")), Baseline, Error) || !ReadSnapshot(Status.ActiveSnapshot, Active, Error) || !ReadCurrent(CurrentBundle, Error))
    { Status.State = TEXT("conflict"); return false; }
    Status.bFilesMatch = Equal(Active, CurrentBundle);
    Status.State = State->HasField(TEXT("pending")) ? TEXT("interrupted") : !Status.bFilesMatch ? TEXT("conflict") : Equal(Active, Baseline) ? TEXT("restored") : TEXT("applied");
    Status.Detail = !Status.bOwnedByProject ? OwnershipError : Status.bFilesMatch ? TEXT("Engine files match the recorded SHA-256 snapshot.") : TEXT("Engine files differ from the active snapshot. No external changes will be overwritten.");
    return true;
}
bool FMazelineShaderPatchManager::Execute(const FString& Action, const FString& RegistryFile, FString& Message, bool& bChanged)
{
    bChanged = false;
    if (!Validate(Message)) return false;
    FLock Lock;
    if (!Lock.Acquire(Store, Message) || !EnsureNoOtherEditors(Message)) return false;
    TSharedPtr<FJsonObject> State;
    if (!ReadState(State, Message)) return false;
    if (Action == TEXT("recover"))
    {
        if (!State || !ObjectField(State, TEXT("pending"))) return Fail(Message, TEXT("No interrupted transaction to recover"));
        if (!Owns(State, Message)) return false;
        FBundle Before, After, CurrentBundle;
        if (!ReadSnapshot(Field(State, TEXT("active")), Before, Message) ||
            !ReadSnapshot(Field(ObjectField(State, TEXT("pending")), TEXT("destination")), After, Message) || !ReadCurrent(CurrentBundle, Message)) return false;
        for (const FString& File : Files)
            if (CurrentBundle[File] != Before[File] && CurrentBundle[File] != After[File])
                return Fail(Message, TEXT("External edits found; recovery refused: ") + File);
        if (!WriteShaders(CurrentBundle, Before, bChanged, Message)) return false;
        State->RemoveField(TEXT("pending"));
        if (!WriteJson(Store / TEXT("state.json"), State.ToSharedRef(), Message)) return false;
        Message = TEXT("Interrupted transaction rolled back. Previous state verified.");
        return true;
    }
    FBundle Originals, Active;
    if (State && !Ready(State, Active, Message)) return false;
    if (Action == TEXT("restore"))
    {
        if (!State) return Fail(Message, TEXT("No managed original backup exists"));
        if (Field(State, TEXT("active")) != Field(State, TEXT("baseline")) &&
            !Transition(State, Field(State, TEXT("baseline")), FString(), bChanged, Message)) return false;
        Message = TEXT("Original engine shaders restored and verified. Backups retained.");
        return true;
    }
    if (Action != TEXT("apply")) return Fail(Message, TEXT("Unknown patch action"));
    if (State ? !ReadSnapshot(Field(State, TEXT("baseline")), Originals, Message) : !ReadCurrent(Originals, Message)) return false;
    TMap<int32, FString> Functions;
    if (!LoadRegistry(RegistryFile, Project, Functions, Message)) return false;
    FBundle Patched;
    if (!Generate(Originals, Functions, Patched, Message)) return false;
    if (!State)
    {
        FString Baseline;
        if (!Snapshot(Originals, TEXT("baseline"), Baseline, Message)) return false;
        State = MakeShared<FJsonObject>();
        State->SetNumberField(TEXT("format"), 1);
        State->SetStringField(TEXT("engine"), Engine);
        State->SetStringField(TEXT("baseline"), Baseline);
        State->SetStringField(TEXT("active"), Baseline);
        State->SetField(TEXT("owner"), MakeShared<FJsonValueNull>());
        if (!WriteJson(Store / TEXT("state.json"), State.ToSharedRef(), Message)) return false;
        Active = Originals;
    }
    if (Equal(Active, Patched)) { Message = TEXT("Already applied. Shader files and current function registry match."); return true; }
    FString Destination;
    if (!Snapshot(Patched, TEXT("override"), Destination, Message) || !Transition(State, Destination, Project, bChanged, Message)) return false;
    Message = TEXT("Forward and Deferred patches applied and verified.");
    return true;
}

bool FMazelineShaderPatchManager::Generate(const FBundle& Original, const TMap<int32, FString>& Functions, FBundle& Patched, FString& Error)
{
    if (Original.Num() != Files.Num() || Functions.IsEmpty()) return Fail(Error, TEXT("Incomplete shader bundle or empty registry"));
    TArray<int32> Ids;
    Functions.GetKeys(Ids);
    Ids.Sort();
    for (int32 Id : Ids) if (Id < 1 || Id > 3) return Fail(Error, TEXT("Function ID must be 1, 2, or 3"));
    for (const FString& File : Files)
    {
        if (!Original.Contains(File)) return Fail(Error, TEXT("Missing shader: ") + File);
        FString Source = Decode(Original[File]);
        if (Source.Contains(TEXT("MAZELINE_"))) return Fail(Error, TEXT("Unmanaged patch marker found in original: ") + File);
        const FString NL = Source.Contains(TEXT("\r\n")) ? TEXT("\r\n") : TEXT("\n");
        auto Lines = [&NL](const TArray<FString>& Values) { return FString::Join(Values, *NL); };
        if (File == TEXT("ShadingCommon.ush"))
        {
            FRegexMatcher Matcher(FRegexPattern(TEXT("(?m)^#define SHADINGMODELID_NUM[\\t ]+14\\r?$")), Source);
            if (!Matcher.FindNext()) return Fail(Error, TEXT("Unexpected shading-model ID definition"));
            FString Old = Matcher.GetCaptureGroup(0);
            if (Old.EndsWith(TEXT("\r"))) Old.LeftChopInline(1);
            if (Matcher.FindNext()) return Fail(Error, TEXT("Duplicate shading-model ID definition"));
            if (!ReplaceOnce(Source, Old, TEXT("#define SHADINGMODELID_MAZELINE 14 // ") + DeferredMarker + NL + TEXT("#define SHADINGMODELID_NUM 15"), Error)) return false;
        }
        else if (File == TEXT("ShadingModelsMaterial.ush"))
        {
            FString Old = TEXT("\tGBuffer.ShadingModelID = ShadingModel;");
            if (!ReplaceOnce(Source, Old, Lines({Old, TEXT("\t// ") + DeferredMarker,
                TEXT("#if !FORWARD_SHADING && !SUBSTRATE_ENABLED && !SHADING_PATH_MOBILE && defined(MAZELINE_SHADER_OVERRIDE) && MAZELINE_SHADER_OVERRIDE"),
                TEXT("\tif (ShadingModel == SHADINGMODELID_DEFAULT_LIT)"), TEXT("\t{"),
                TEXT("\t\tGBuffer.ShadingModelID = SHADINGMODELID_MAZELINE;"),
                TEXT("\t\tGBuffer.PerObjectGBufferData = min((float)MAZELINE_SHADER_OVERRIDE, 3.0f) / 3.0f;"),
                TEXT("\t}"), TEXT("#endif")}), Error)) return false;
        }
        else
        {
            FString Entry = Anchor + NL + TEXT("{") + NL;
            FString Forward = Lines({TEXT("\t") + ForwardMarker,
                TEXT("#if FORWARD_SHADING && defined(MAZELINE_SHADER_OVERRIDE) && MAZELINE_SHADER_OVERRIDE"),
                TEXT("\tif (GBuffer.ShadingModelID == SHADINGMODELID_DEFAULT_LIT)"), TEXT("\t{"),
                TEXT("\t\tfloat3 OverrideDiffuse = GBuffer.DiffuseColor;"),
                TEXT("\t\tfloat OverrideRoughness = GBuffer.Roughness;"),
                TEXT("\t\tMazelineEvaluateShaderOverride(GBuffer.BaseColor, GBuffer.Metallic, N, V, AreaLight.DiffuseL, OverrideDiffuse, OverrideRoughness);"),
                TEXT("\t\tGBuffer.DiffuseColor = OverrideDiffuse;"), TEXT("\t\tGBuffer.Roughness = saturate(OverrideRoughness);"),
                TEXT("\t}"), TEXT("#endif")}) + NL;
            if (!ReplaceOnce(Source, Entry, Entry + Forward, Error)) return false;
            TArray<FString> Definitions = {TEXT("// ") + DeferredMarker + TEXT(" generated function registry"), TEXT("#if !FORWARD_SHADING && !SHADING_PATH_MOBILE")};
            for (int32 Id : Ids)
            {
                FString Code = Functions[Id];
                Code.ReplaceInline(TEXT("\r\n"), TEXT("\n"));
                Code.ReplaceInline(TEXT("\n"), *NL);
                Definitions.Add(FString::Printf(TEXT("#define MazelineEvaluateShaderOverride MazelineDeferredFunction%d"), Id));
                Definitions.Add(Code);
                Definitions.Add(TEXT("#undef MazelineEvaluateShaderOverride"));
            }
            Definitions.Add(TEXT("#endif"));
            if (!ReplaceOnce(Source, Anchor, Lines(Definitions) + NL + Anchor, Error)) return false;
            TArray<FString> Dispatch = {TEXT("\t// ") + DeferredMarker,
                TEXT("#if !FORWARD_SHADING && !SHADING_PATH_MOBILE"),
                TEXT("\tif (GBuffer.ShadingModelID == SHADINGMODELID_MAZELINE)"), TEXT("\t{"),
                TEXT("\t\tuint FunctionId = (uint)round(GBuffer.PerObjectGBufferData * 3.0f);"),
                TEXT("\t\tfloat3 OverrideDiffuse = GBuffer.DiffuseColor;"), TEXT("\t\tfloat OverrideRoughness = GBuffer.Roughness;")};
            for (int32 Id : Ids)
            {
                Dispatch.Add(FString::Printf(TEXT("\t\tif (FunctionId == %d)"), Id));
                Dispatch.Add(FString::Printf(TEXT("\t\t\tMazelineDeferredFunction%d(GBuffer.BaseColor, GBuffer.Metallic, N, V, AreaLight.DiffuseL, OverrideDiffuse, OverrideRoughness);"), Id));
            }
            Dispatch.Append({TEXT("\t\tGBuffer.DiffuseColor = OverrideDiffuse;"), TEXT("\t\tGBuffer.Roughness = saturate(OverrideRoughness);"), TEXT("\t}"), TEXT("#endif")});
            if (!ReplaceOnce(Source, Entry, Entry + Lines(Dispatch) + NL, Error)) return false;
            FString Case = TEXT("\t\tcase SHADINGMODELID_DEFAULT_LIT:");
            if (!ReplaceOnce(Source, Case, TEXT("\t\tcase SHADINGMODELID_MAZELINE: // ") + DeferredMarker + NL + Case, Error)) return false;
        }
        Patched.Add(File, Utf8(Source));
    }
    return true;
}

bool FMazelineShaderPatchManager::BuildRegistry(FString& RegistryFile, FString& Error)
{
    const FString Project = Full(FPaths::GetProjectFilePath());
    const FString ShaderRoot = Full(FPaths::ProjectDir() / TEXT("Shaders"));
    IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
    Registry.ScanPathsSynchronous({TEXT("/Game")});
    Registry.WaitForCompletion();
    TArray<FAssetData> Assets;
    Registry.GetAssetsByPath(TEXT("/Game"), Assets, true);
    TMap<int32, FString> Paths, Hashes;
    for (const FAssetData& Asset : Assets)
    {
        if (Asset.AssetClassPath != UMaterial::StaticClass()->GetClassPathName()) continue;
        UMaterial* Material = Cast<UMaterial>(Asset.GetAsset());
        if (!Material) return Fail(Error, TEXT("Cannot load material: ") + Asset.GetObjectPathString());
        TArray<UMaterialExpression*> Queue;
        for (EMaterialProperty Property : {MP_BaseColor, MP_Roughness, MP_Metallic})
            if (FExpressionInput* Input = Material->GetExpressionInputForProperty(Property)) Queue.Add(Input->Expression);
        TSet<UMaterialExpression*> Visited;
        TSet<int32> MaterialIds;
        while (!Queue.IsEmpty())
        {
            UMaterialExpression* Node = Queue.Pop(EAllowShrinking::No);
            if (!Node || Visited.Contains(Node)) continue;
            Visited.Add(Node);
            if (UMaterialExpressionCustom* Custom = Cast<UMaterialExpressionCustom>(Node))
            {
                const FCustomDefine* Define = nullptr;
                for (const FCustomDefine& Candidate : Custom->AdditionalDefines)
                {
                    if (Candidate.DefineName != TEXT("MAZELINE_SHADER_OVERRIDE")) continue;
                    if (Define) return Fail(Error, TEXT("Duplicate override define: ") + Custom->GetPathName());
                    Define = &Candidate;
                }
                if (Define)
                {
                    if (Material->MaterialDomain != MD_Surface || Material->GetBlendMode() != BLEND_Opaque || !Material->GetShadingModels().HasOnlyShadingModel(MSM_DefaultLit))
                        return Fail(Error, TEXT("Override requires Opaque Default Lit: ") + Material->GetPathName());
                    if (Define->DefineValue != TEXT("1") && Define->DefineValue != TEXT("2") && Define->DefineValue != TEXT("3"))
                        return Fail(Error, TEXT("Override define must be 1, 2, or 3: ") + Custom->GetPathName());
                    const int32 Id = FCString::Atoi(*Define->DefineValue);
                    MaterialIds.Add(Id);
                    if (MaterialIds.Num() > 1) return Fail(Error, TEXT("Material selects multiple function IDs: ") + Material->GetPathName());
                    TArray<FString> Candidates;
                    FString Code;
                    for (const FString& Include : Custom->IncludeFilePaths)
                    {
                        if (!Include.StartsWith(TEXT("/Project/"), ESearchCase::CaseSensitive)) continue;
                        FString Path = Full(ShaderRoot / Include.Mid(9));
                        if (!Path.StartsWith(ShaderRoot + TEXT("/"), ESearchCase::IgnoreCase) || !IFileManager::Get().FileExists(*Path))
                            return Fail(Error, TEXT("Invalid project shader path: ") + Include);
                        FString Text;
                        if (!FFileHelper::LoadFileToString(Text, *Path)) return Fail(Error, TEXT("Cannot read: ") + Path);
                        if (Matches(FRegexPattern(TEXT("\\bvoid\\s+MazelineEvaluateShaderOverride\\s*\\(")), Text))
                        {
                            if (!SourceCode(Path, Code, Error)) return false;
                            Candidates.Add(Path);
                        }
                    }
                    if (Candidates.Num() != 1) return Fail(Error, TEXT("Exactly one override HLSL include is required: ") + Custom->GetPathName());
                    const FString Digest = Hash(Utf8(Code));
                    if (Digest.Len() != 64) return Fail(Error, TEXT("SHA-256 calculation failed"));
                    if (Hashes.Contains(Id) && Hashes[Id] != Digest) return Fail(Error, FString::Printf(TEXT("Different HLSL functions share ID %d"), Id));
                    Paths.Add(Id, Candidates[0]);
                    Hashes.Add(Id, Digest);
                }
            }
            for (FExpressionInputIterator Input(Node); Input; ++Input)
                Queue.Add(Input->Expression);
        }
    }
    if (Paths.IsEmpty()) return Fail(Error, TEXT("No connected override functions found in /Game"));
    TArray<int32> Ids;
    Paths.GetKeys(Ids); Ids.Sort();
    TArray<TSharedPtr<FJsonValue>> Functions;
    for (int32 Id : Ids)
    {
        TSharedRef<FJsonObject> Row = MakeShared<FJsonObject>();
        Row->SetNumberField(TEXT("id"), Id);
        Row->SetStringField(TEXT("source"), Paths[Id]);
        Row->SetStringField(TEXT("sha256"), Hashes[Id]);
        Functions.Add(MakeShared<FJsonValueObject>(Row));
    }
    TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
    Json->SetNumberField(TEXT("schema"), 1);
    Json->SetStringField(TEXT("project"), Project);
    Json->SetArrayField(TEXT("functions"), Functions);
    RegistryFile = Full(FPaths::ProjectSavedDir() / TEXT("Mazeline/deferred_registry.json"));
    if (!IFileManager::Get().MakeDirectory(*FPaths::GetPath(RegistryFile), true)) return Fail(Error, TEXT("Cannot create project registry directory"));
    return WriteJson(RegistryFile, Json, Error);
}
