#include "Modules/ModuleManager.h"
#include "Camera/CameraActor.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "UnrealClient.h"

// Opt-in packaged-render harness in the test project, never in the plugin.
class FMazeLineMaterialTestModule : public FDefaultGameModuleImpl
{
    virtual void StartupModule() override
    {
        if (FParse::Value(FCommandLine::Get(), TEXT("MazelineCapture="), CapturePath))
        {
            TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateRaw(this, &FMazeLineMaterialTestModule::Tick));
        }
    }

    virtual void ShutdownModule() override
    {
        FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
    }

    bool Tick(float DeltaSeconds)
    {
        UWorld* World = GEngine && GEngine->GameViewport ? GEngine->GameViewport->GetWorld() : nullptr;
        APlayerController* Player = World ? World->GetFirstPlayerController() : nullptr;
        if (!World || !Player || !World->IsGameWorld())
        {
            return true;
        }
        if (!bCameraReady)
        {
            for (TActorIterator<ACameraActor> Camera(World); Camera; ++Camera)
            {
                Player->SetViewTarget(*Camera);
                Player->ConsoleCommand(TEXT("r.EyeAdaptationQuality 0"), false);
                Player->ConsoleCommand(TEXT("r.AntiAliasingMethod 0"), false);
                Player->ConsoleCommand(TEXT("r.MotionBlurQuality 0"), false);
                bCameraReady = true;
                break;
            }
        }
        Elapsed += DeltaSeconds;
        if (bCameraReady && Elapsed > 10.0f && !bRequested)
        {
            if (IFileManager::Get().FileExists(*CapturePath))
            {
                UE_LOG(LogTemp, Error, TEXT("MAZELINE_PACKAGED_CAPTURE_REFUSED existing file: %s"), *CapturePath);
                FPlatformMisc::RequestExit(false);
                return false;
            }
            FScreenshotRequest::RequestScreenshot(CapturePath, false, false);
            bRequested = true;
            UE_LOG(LogTemp, Display, TEXT("MAZELINE_PACKAGED_CAPTURE_REQUESTED %s"), *CapturePath);
        }
        if (bRequested && IFileManager::Get().FileSize(*CapturePath) > 0)
        {
            UE_LOG(LogTemp, Display, TEXT("MAZELINE_PACKAGED_CAPTURE_COMPLETE %s"), *CapturePath);
            FPlatformMisc::RequestExit(false);
            return false;
        }
        if (Elapsed > 45.0f)
        {
            UE_LOG(LogTemp, Error, TEXT("MAZELINE_PACKAGED_CAPTURE_TIMEOUT"));
            FPlatformMisc::RequestExit(false);
            return false;
        }
        return true;
    }

    FString CapturePath;
    FTSTicker::FDelegateHandle TickHandle;
    float Elapsed = 0.0f;
    bool bCameraReady = false;
    bool bRequested = false;
};

IMPLEMENT_PRIMARY_GAME_MODULE(FMazeLineMaterialTestModule, MazeLineMaterialTest, "MazeLineMaterialTest");
