#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

struct FMazelinePatchStatus
{
    FString State = TEXT("unmanaged");
    FString Owner;
    FString BackupDirectory;
    FString ActiveSnapshot;
    FString Detail;
    bool bFilesMatch = false;
    bool bOwnedByProject = true;
};

// Uses the same format-1 snapshots, hashes, journal and lock as the old Python
// manager. No Python interpreter or external helper process is used.
class FMazelineShaderPatchManager
{
public:
    using FBundle = TMap<FString, TArray<uint8>>;
    FMazelineShaderPatchManager(const FString& EngineRoot, const FString& ProjectFile);
    static FMazelineShaderPatchManager Current();
    static bool RunCurrent(const FString& Action, FString& Message);
    static bool RestartRequired();
    bool Inspect(FMazelinePatchStatus& Status, FString& Error);
    bool Execute(const FString& Action, const FString& RegistryFile, FString& Message, bool& bChanged);
    static bool BuildRegistry(FString& RegistryFile, FString& Error);
    static bool Generate(const FBundle& Original, const TMap<int32, FString>& Functions, FBundle& Patched, FString& Error);
    static FString Hash(const TArray<uint8>& Bytes);
    static TArray<uint8> Utf8(const FString& Text);
    static FString Decode(const TArray<uint8>& Bytes);
    FString GetStore() const { return Store; }
    FString GetEngine() const { return Engine; }
    FString GetProject() const { return Project; }

#if WITH_DEV_AUTOMATION_TESTS
    // Deterministic I/O failure injection for interrupted transaction tests.
    int32 FailBeforeShaderWrite = INDEX_NONE;
#endif

private:
    FString Engine, Project, Store;
    bool Validate(FString& Error) const;
    bool ReadCurrent(FBundle& Bundle, FString& Error) const;
    bool ReadState(TSharedPtr<FJsonObject>& State, FString& Error) const;
    bool ReadSnapshot(const FString& Id, FBundle& Bundle, FString& Error) const;
    bool Snapshot(const FBundle& Bundle, const FString& Type, FString& Id, FString& Error) const;
    bool Ready(const TSharedPtr<FJsonObject>& State, FBundle& Before, FString& Error) const;
    bool Owns(const TSharedPtr<FJsonObject>& State, FString& Error) const;
    bool Transition(TSharedPtr<FJsonObject>& State, const FString& Destination, const FString& Owner, bool& bChanged, FString& Error);
    bool WriteShaders(const FBundle& Before, const FBundle& After, bool& bChanged, FString& Error);
    bool EnsureNoOtherEditors(FString& Error) const;
};
