#pragma once

#include "Materials/MaterialExpressionCustomOutput.h"
#include "MaterialExpressionMazelineRampOutput.generated.h"

/** Row index from the Custom expression; the light shader evaluates the LUT. */
UCLASS(CollapseCategories, HideCategories=Object, meta=(DisplayName="Mazeline Ramp Output"))
class MAZELINEMATERIALRUNTIME_API UMaterialExpressionMazelineRampOutput : public UMaterialExpressionCustomOutput
{
    GENERATED_BODY()
public:
    UMaterialExpressionMazelineRampOutput(const FObjectInitializer& ObjectInitializer);
    UPROPERTY(meta=(RequiredInput="true"))
    FExpressionInput RampRow;
    virtual int32 GetNumOutputs() const override { return 1; }
    virtual FString GetFunctionName() const override { return TEXT("GetMazelineRamp"); }
    virtual FString GetDisplayName() const override { return TEXT("Mazeline Ramp Output"); }
#if WITH_EDITOR
    virtual int32 Compile(FMaterialCompiler* Compiler, int32 OutputIndex) override;
    virtual void GetCaption(TArray<FString>& OutCaptions) const override;
    virtual FExpressionInput* GetInput(int32 InputIndex) override;
    virtual FName GetInputName(int32 InputIndex) const override;
    virtual EMaterialValueType GetInputValueType(int32 InputIndex) override { return MCT_Float; }
#endif
};
