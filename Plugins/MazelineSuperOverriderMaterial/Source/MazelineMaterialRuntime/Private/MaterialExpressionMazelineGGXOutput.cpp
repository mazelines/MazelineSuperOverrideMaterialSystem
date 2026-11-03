#include "MaterialExpressionMazelineGGXOutput.h"
#include "MaterialCompiler.h"

UMaterialExpressionMazelineGGXOutput::UMaterialExpressionMazelineGGXOutput(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
#if WITH_EDITORONLY_DATA
    Outputs.Reset();
    MenuCategories.Add(NSLOCTEXT("Mazeline", "MaterialCategory", "Mazeline"));
#endif
}

#if WITH_EDITOR
int32 UMaterialExpressionMazelineGGXOutput::Compile(FMaterialCompiler* Compiler, int32 OutputIndex)
{
    FExpressionInput* Input = GetInput(OutputIndex);
    if (!Input || !Input->GetTracedInput().Expression)
        return CompilerError(Compiler, TEXT("Connect Sharpness and Threshold to Mazeline GGX Output"));
    return Compiler->CustomOutput(this, OutputIndex, Compiler->ForceCast(Input->Compile(Compiler), MCT_Float1));
}

void UMaterialExpressionMazelineGGXOutput::GetCaption(TArray<FString>& OutCaptions) const
{
    OutCaptions.Add(TEXT("Mazeline GGX Output"));
}

FExpressionInput* UMaterialExpressionMazelineGGXOutput::GetInput(int32 InputIndex)
{
    return InputIndex == 0 ? &Sharpness : InputIndex == 1 ? &Threshold : nullptr;
}

FName UMaterialExpressionMazelineGGXOutput::GetInputName(int32 InputIndex) const
{
    return InputIndex == 0 ? FName(TEXT("Sharpness")) : InputIndex == 1 ? FName(TEXT("Threshold")) : NAME_None;
}
#endif
