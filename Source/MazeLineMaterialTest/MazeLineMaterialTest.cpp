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
#include "Engine/StaticMeshActor.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/Light.h"
#include "Engine/PointLight.h"
#include "Engine/SpotLight.h"
#include "Engine/RectLight.h"
#include "Components/LightComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/LocalLightComponent.h"
#include "Curves/CurveLinearColorAtlas.h"
#include "Curves/CurveLinearColor.h"

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
        if (!bParametersReady)
        {
            bParametersReady = true;
            float Sharpness = 0.5f, Threshold = 0.25f, RampRow = 2.0f;
            const bool bSharpness = FParse::Value(FCommandLine::Get(), TEXT("MazelineSharpness="), Sharpness);
            const bool bRampRow = FParse::Value(FCommandLine::Get(), TEXT("MazelineRampRow="), RampRow);
            if (bSharpness || bRampRow)
            {
                FParse::Value(FCommandLine::Get(), TEXT("MazelineThreshold="), Threshold);
                int32 Count = 0;
                for (TActorIterator<AStaticMeshActor> Actor(World); Actor; ++Actor)
                {
                    UStaticMeshComponent* Mesh = Actor->GetStaticMeshComponent();
                    UMaterialInterface* Source = Mesh ? Mesh->GetMaterial(0) : nullptr;
                    if (!Source || Source->GetMaterial()->GetName() != TEXT("M_Toon")) continue;
                    UMaterialInstanceDynamic* Instance = Mesh->CreateDynamicMaterialInstance(0);
                    if (bSharpness)
                    {
                        Instance->SetScalarParameterValue(TEXT("Sharpness"), Sharpness);
                        Instance->SetScalarParameterValue(TEXT("Threshold"), Threshold);
                    }
                    if (bRampRow) Instance->SetScalarParameterValue(TEXT("RampRow"), RampRow);
                    ++Count;
                }
                UE_LOG(LogTemp, Display, TEXT("MAZELINE_DYNAMIC_GGX count=%d sharpness=%g threshold=%g"), Count, Sharpness, Threshold);
                if (bRampRow) UE_LOG(LogTemp, Display, TEXT("MAZELINE_DYNAMIC_RAMP count=%d row=%g"), Count, RampRow);
            }
            FString LightType;
            if (FParse::Value(FCommandLine::Get(), TEXT("MazelineLightType="), LightType))
            {
                ALight* Selected = nullptr;
                for (TActorIterator<ALight> Light(World); Light; ++Light)
                {
                    if (LightType == TEXT("Directional") && Light->GetLightComponent()->IsA<UDirectionalLightComponent>()) Selected = *Light;
                    else Light->GetLightComponent()->SetVisibility(false);
                }
                const FVector Position(-350, -200, 300);
                const FRotator Rotation = (-Position).Rotation();
                if (LightType == TEXT("Point")) Selected = World->SpawnActor<APointLight>(Position, Rotation);
                if (LightType == TEXT("Spot")) Selected = World->SpawnActor<ASpotLight>(Position, Rotation);
                if (LightType == TEXT("Rect")) Selected = World->SpawnActor<ARectLight>(Position, Rotation);
                if (Selected)
                {
                    ULightComponent* Light = Selected->GetLightComponent();
                    Light->SetMobility(EComponentMobility::Movable);
                    Light->SetCastShadows(false);
                    if (auto* Local = Cast<ULocalLightComponent>(Light))
                    {
                        // Spawn combines the class default root rotation (Spot is -90 degrees).
                        // Set the final world direction explicitly so the cone hits the sphere.
                        Local->SetWorldLocationAndRotation(Position, Rotation);
                        Local->SetAttenuationRadius(2000.f);
                        Local->SetIntensity(15000.f);
                    }
                    Light->SetSpecularScale(FParse::Param(FCommandLine::Get(), TEXT("MazelineDiffuseOnly")) ? 0.f : 1.f);
                    Light->SetDiffuseScale(FParse::Param(FCommandLine::Get(), TEXT("MazelineSpecularOnly")) ? 0.f : 1.f);
                    UE_LOG(LogTemp, Display, TEXT("MAZELINE_RAMP_LIGHT type=%s"), *LightType);
                }
            }
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
#if WITH_EDITOR
        if (!bCurveUpdated && Elapsed > 5.f && FParse::Param(FCommandLine::Get(), TEXT("MazelineRampEdit")))
        {
            bCurveUpdated = true;
            auto* Atlas = LoadObject<UCurveLinearColorAtlas>(nullptr, TEXT("/Game/MazelineTests/Ramps/CA_ToonDiffuse.CA_ToonDiffuse"));
            if (Atlas && !Atlas->GradientCurves.IsEmpty() && Atlas->GradientCurves[0])
            {
                UCurveLinearColor* Curve = Atlas->GradientCurves[0];
                for (int32 Channel = 0; Channel < 3; ++Channel)
                    for (auto It = Curve->FloatCurves[Channel].GetKeyHandleIterator(); It; ++It)
                    {
                        const FKeyHandle Handle = *It;
                        Curve->FloatCurves[Channel].SetKeyValue(Handle, Curve->FloatCurves[Channel].GetKeyValue(Handle) * .2f);
                    }
                Curve->OnUpdateCurve.Broadcast(Curve, EPropertyChangeType::ValueSet);
                UE_LOG(LogTemp, Display, TEXT("MAZELINE_RAMP_LIVE_UPDATE row=0 in_memory_only=yes"));
            }
        }
#endif
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
    bool bParametersReady = false;
    bool bCurveUpdated = false;
};

IMPLEMENT_PRIMARY_GAME_MODULE(FMazeLineMaterialTestModule, MazeLineMaterialTest, "MazeLineMaterialTest");
