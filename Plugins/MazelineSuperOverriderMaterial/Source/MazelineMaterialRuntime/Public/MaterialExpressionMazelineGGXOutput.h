#pragma once

#include "Materials/MaterialExpressionCustomOutput.h"
#include "MaterialExpressionMazelineGGXOutput.generated.h"

/** Per-pixel GGX controls for an opted-in Opaque Default Lit material. */
UCLASS(CollapseCategories, HideCategories=Object, meta=(DisplayName="Mazeline GGX Output"))
class MAZELINEMATERIALRUNTIME_API UMaterialExpressionMazelineGGXOutput : public UMaterialExpressionCustomOutput
{
    GENERATED_BODY()
public:
    UMaterialExpressionMazelineGGXOutput(const FObjectInitializer& ObjectInitializer);

    UPROPERTY(meta=(RequiredInput="true"))
    FExpressionInput Sharpness;

    UPROPERTY(meta=(RequiredInput="true"))
    FExpressionInput Threshold;

    virtual int32 GetNumOutputs() const override { return 2; }
    virtual FString GetFunctionName() const override { return TEXT("GetMazelineGGX"); }
    virtual FString GetDisplayName() const override { return TEXT("Mazeline GGX Output"); }
#if WITH_EDITOR
    virtual int32 Compile(FMaterialCompiler* Compiler, int32 OutputIndex) override;
    virtual void GetCaption(TArray<FString>& OutCaptions) const override;
    virtual FExpressionInput* GetInput(int32 InputIndex) override;
    virtual FName GetInputName(int32 InputIndex) const override;
    virtual EMaterialValueType GetInputValueType(int32 InputIndex) override { return MCT_Float; }
#endif
};
