#include "MazelineShaderPatchCommandlet.h"
#include "MazelineShaderPatchManager.h"
#include "Misc/Parse.h"

UMazelineShaderPatchCommandlet::UMazelineShaderPatchCommandlet()
{
    IsClient = false; IsServer = false; IsEditor = true; LogToConsole = true;
}
int32 UMazelineShaderPatchCommandlet::Main(const FString& Params)
{
    FString Action = TEXT("status"), Message;
    FParse::Value(*Params, TEXT("Action="), Action);
    Action.ToLowerInline();
    bool bSuccess = false;
    if (Action == TEXT("status"))
    {
        FMazelinePatchStatus Status;
        bSuccess = FMazelineShaderPatchManager::Current().Inspect(Status, Message);
        UE_LOG(LogTemp, Display, TEXT("MAZELINE_NATIVE_STATUS state=%s matches=%s owner=%s backup=%s"),
            *Status.State, Status.bFilesMatch ? TEXT("yes") : TEXT("no"), *Status.Owner, *Status.BackupDirectory);
    }
    else bSuccess = FMazelineShaderPatchManager::RunCurrent(Action, Message);
    if (bSuccess) { UE_LOG(LogTemp, Display, TEXT("MAZELINE_NATIVE_RESULT %s success: %s"), *Action, *Message); }
    else { UE_LOG(LogTemp, Error, TEXT("MAZELINE_NATIVE_RESULT %s failed: %s"), *Action, *Message); }
    return bSuccess ? 0 : 1;
}
