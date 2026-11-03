#pragma once

#include "CoreMinimal.h"
#include "Engine/Texture2D.h"
#include "Subsystems/EngineSubsystem.h"
#include "MazelineRampSettings.generated.h"

/** Generated from the Custom expression RampLUT connections by Apply / Update. */
UCLASS(Config=Game, DefaultConfig)
class MAZELINEMATERIALRUNTIME_API UMazelineRampSettings : public UObject
{
    GENERATED_BODY()
public:
    // Function IDs 1..3. Keys preserve empty slots and shared texture paths in ini files.
    UPROPERTY(Config)
    TMap<int32, TSoftObjectPtr<UTexture2D>> AtlasesByFunction;
};

/** Holds Curve Atlases and the view extension that binds their GPU lookup array. */
UCLASS()
class MAZELINEMATERIALRUNTIME_API UMazelineRampSubsystem : public UEngineSubsystem
{
    GENERATED_BODY()
public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;
    void ReloadAtlases();

private:
    UPROPERTY(Transient)
    TArray<TObjectPtr<UTexture2D>> LoadedAtlases;
    TSharedPtr<class ISceneViewExtension, ESPMode::ThreadSafe> ViewExtension;
};
