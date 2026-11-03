#include "MaterialExpressionMazelineRampOutput.h"
#include "MaterialCompiler.h"

UMaterialExpressionMazelineRampOutput::UMaterialExpressionMazelineRampOutput(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
#if WITH_EDITORONLY_DATA
    Outputs.Reset();
    MenuCategories.Add(NSLOCTEXT("Mazeline", "MaterialCategory", "Mazeline"));
#endif
}
#if WITH_EDITOR
int32 UMaterialExpressionMazelineRampOutput::Compile(FMaterialCompiler* Compiler, int32 OutputIndex)
{
    if (OutputIndex != 0 || !RampRow.GetTracedInput().Expression)
        return CompilerError(Compiler, TEXT("Connect the Custom expression RampIndex output to RampRow"));
    return Compiler->CustomOutput(this, OutputIndex, Compiler->ForceCast(RampRow.Compile(Compiler), MCT_Float1));
}
void UMaterialExpressionMazelineRampOutput::GetCaption(TArray<FString>& OutCaptions) const { OutCaptions.Add(TEXT("Mazeline Ramp Output")); }
FExpressionInput* UMaterialExpressionMazelineRampOutput::GetInput(int32 Index) { return Index == 0 ? &RampRow : nullptr; }
FName UMaterialExpressionMazelineRampOutput::GetInputName(int32 Index) const { return Index == 0 ? FName(TEXT("RampRow")) : NAME_None; }
#endif
