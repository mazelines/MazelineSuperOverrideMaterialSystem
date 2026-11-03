#include "MazelineMaterialLibrary.h"
#include "MaterialExpressionMazelineGGXOutput.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpression.h"
#include "Curves/CurveLinearColor.h"
#include "Curves/CurveLinearColorAtlas.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Misc/PackageName.h"

UCurveLinearColorAtlas* UMazelineMaterialLibrary::CreateToonRampExamples(const FString& PackageRoot)
{
    if (!PackageRoot.StartsWith(TEXT("/Game/")) || !FPackageName::IsValidLongPackageName(PackageRoot)) return nullptr;
    const FString AtlasPackage = PackageRoot / TEXT("CA_ToonDiffuse");
    if (FPackageName::DoesPackageExist(AtlasPackage))
        return LoadObject<UCurveLinearColorAtlas>(nullptr, *(AtlasPackage + TEXT(".CA_ToonDiffuse")));
    const TArray<FString> Names = {TEXT("CR_ToonLinear"), TEXT("CR_ToonSoft"), TEXT("CR_ToonSteps")};
    for (const FString& Name : Names)
        if (FPackageName::DoesPackageExist(PackageRoot / Name)) return nullptr;
    TArray<UCurveLinearColor*> Curves;
    for (int32 Style = 0; Style < Names.Num(); ++Style)
    {
        UPackage* Package = CreatePackage(*(PackageRoot / Names[Style]));
        auto* Curve = NewObject<UCurveLinearColor>(Package, *Names[Style], RF_Public | RF_Standalone | RF_Transactional);
        // UCurveLinearColor's constructor adds black/white keys at 0 and 1.
        // AddKey does not replace existing keys at the same time.
        for (FRichCurve& Channel : Curve->FloatCurves) Channel.Reset();
        auto Add = [Curve](float Time, const FLinearColor& Color, ERichCurveInterpMode Mode)
        {
            for (int32 Channel = 0; Channel < 4; ++Channel)
            {
                const FKeyHandle Key = Curve->FloatCurves[Channel].AddKey(Time, Color.Component(Channel));
                Curve->FloatCurves[Channel].SetKeyInterpMode(Key, Mode);
            }
        };
        if (Style == 0)
        {
            Add(0.f, FLinearColor(0, 0, 0, 1), RCIM_Linear);
            Add(1.f, FLinearColor::White, RCIM_Linear);
        }
        else if (Style == 1)
        {
            Add(0.f, FLinearColor(.06f, .09f, .16f, 1), RCIM_Cubic);
            Add(.4f, FLinearColor(.3f, .4f, .55f, 1), RCIM_Cubic);
            Add(.8f, FLinearColor(.9f, .95f, 1.f, 1), RCIM_Cubic);
            Add(1.f, FLinearColor::White, RCIM_Cubic);
            for (FRichCurve& Channel : Curve->FloatCurves) Channel.AutoSetTangents();
        }
        else
        {
            Add(0.f, FLinearColor(.12f, .12f, .12f, 1), RCIM_Constant);
            Add(.25f, FLinearColor(.5f, .5f, .5f, 1), RCIM_Constant);
            Add(.65f, FLinearColor::White, RCIM_Constant);
            Add(1.f, FLinearColor::White, RCIM_Constant);
        }
        FAssetRegistryModule::AssetCreated(Curve);
        Curve->MarkPackageDirty();
        Curves.Add(Curve);
    }
    auto* Atlas = NewObject<UCurveLinearColorAtlas>(CreatePackage(*AtlasPackage), TEXT("CA_ToonDiffuse"), RF_Public | RF_Standalone | RF_Transactional);
    Atlas->TextureSize = 256;
    Atlas->NeverStream = true;
    for (auto* Curve : Curves) Atlas->GradientCurves.Add(Curve);
    Atlas->PostEditChange();
    Atlas->UpdateTextures();
    FAssetRegistryModule::AssetCreated(Atlas);
    Atlas->MarkPackageDirty();
    return Atlas;
}

bool UMazelineMaterialLibrary::RemoveConstructorKeysFromRampExample(UCurveLinearColor* Curve)
{
    if (!Curve || !Curve->GetPathName().StartsWith(TEXT("/Game/MazelineTests/Ramps/CR_Toon"))) return false;
    TArray<FKeyHandle> Remove[4];
    for (int32 Channel = 0; Channel < 4; ++Channel)
    {
        const FRichCurve& Keys = Curve->FloatCurves[Channel];
        for (float Time : {0.f, 1.f})
        {
            int32 Count = 0;
            FKeyHandle ConstructorKey;
            bool bFound = false;
            const float Value = Channel == 3 ? 1.f : Time;
            for (auto It = Keys.GetKeyHandleIterator(); It; ++It)
            {
                const auto& Key = Keys.GetKey(*It);
                if (Key.Time != Time) continue;
                ++Count;
                if (!bFound && Key.Value == Value && Key.InterpMode == RCIM_Linear)
                {
                    ConstructorKey = *It;
                    bFound = true;
                }
            }
            // Fail before mutation if this is not exactly the original duplicate pattern.
            if (Count != 2 || !bFound) return false;
            Remove[Channel].Add(ConstructorKey);
        }
    }
    Curve->Modify();
    for (int32 Channel = 0; Channel < 4; ++Channel)
        for (FKeyHandle Key : Remove[Channel]) Curve->FloatCurves[Channel].DeleteKey(Key);
    Curve->OnUpdateCurve.Broadcast(Curve, EPropertyChangeType::ValueSet);
    Curve->MarkPackageDirty();
    return true;
}

UMaterialExpression* UMazelineMaterialLibrary::GetGGXParameterInput(UMaterial* Material, bool bThreshold)
{
    if (!Material) return nullptr;
    TArray<UMaterialExpressionCustomOutput*> Outputs;
    Material->GetAllCustomOutputExpressions(Outputs);
    for (UMaterialExpressionCustomOutput* Output : Outputs)
        if (auto* GGX = Cast<UMaterialExpressionMazelineGGXOutput>(Output))
            return bThreshold ? GGX->Threshold.Expression : GGX->Sharpness.Expression;
    const FExpressionInput* Input = Material->GetExpressionInputForProperty(bThreshold ? MP_CustomData1 : MP_CustomData0);
    return Input ? Input->Expression : nullptr;
}

bool UMazelineMaterialLibrary::MigrateGGXParametersToDefaultLit(UMaterial* Material, UMaterialExpression* Sharpness, UMaterialExpression* Threshold)
{
    if (!Material || !Sharpness || !Threshold || Sharpness->GetTypedOuter<UMaterial>() != Material || Threshold->GetTypedOuter<UMaterial>() != Material) return false;
    if (!Material->GetShadingModels().HasOnlyShadingModel(MSM_DefaultLit) && !Material->GetShadingModels().HasOnlyShadingModel(MSM_ClearCoat)) return false;
    FExpressionInput* X = Material->GetExpressionInputForProperty(MP_CustomData0);
    FExpressionInput* Y = Material->GetExpressionInputForProperty(MP_CustomData1);
    if (!X || !Y || (X->Expression && X->Expression != Sharpness) || (Y->Expression && Y->Expression != Threshold)) return false;
    TArray<UMaterialExpressionCustomOutput*> Outputs;
    Material->GetAllCustomOutputExpressions(Outputs);
    UMaterialExpressionMazelineGGXOutput* GGX = nullptr;
    for (UMaterialExpressionCustomOutput* Output : Outputs)
    {
        if (auto* Candidate = Cast<UMaterialExpressionMazelineGGXOutput>(Output))
        {
            if (GGX) return false;
            GGX = Candidate;
        }
    }
    if (GGX && ((GGX->Sharpness.Expression && GGX->Sharpness.Expression != Sharpness) || (GGX->Threshold.Expression && GGX->Threshold.Expression != Threshold))) return false;
    Material->Modify();
    if (!GGX)
    {
        GGX = NewObject<UMaterialExpressionMazelineGGXOutput>(Material, NAME_None, RF_Transactional);
        GGX->Material = Material;
        GGX->MaterialExpressionEditorX = 100;
        GGX->MaterialExpressionEditorY = 550;
        GGX->UpdateMaterialExpressionGuid(true, true);
        Material->GetExpressionCollection().AddExpression(GGX);
    }
    GGX->Modify();
    GGX->Sharpness.Connect(0, Sharpness);
    GGX->Threshold.Connect(0, Threshold);
    *X = FExpressionInput();
    *Y = FExpressionInput();
    Material->SetShadingModel(MSM_DefaultLit);
    return true;
}

bool UMazelineMaterialLibrary::ConnectGGXParameters(UMaterial* Material, UMaterialExpression* Sharpness, UMaterialExpression* Threshold)
{
    if (!Material || !Sharpness || !Threshold || Sharpness->GetTypedOuter<UMaterial>() != Material || Threshold->GetTypedOuter<UMaterial>() != Material) return false;
    FExpressionInput* X = Material->GetExpressionInputForProperty(MP_CustomData0);
    FExpressionInput* Y = Material->GetExpressionInputForProperty(MP_CustomData1);
    if (!X || !Y || (X->Expression && X->Expression != Sharpness) || (Y->Expression && Y->Expression != Threshold)) return false;
    Material->Modify();
    X->Connect(0, Sharpness);
    Y->Connect(0, Threshold);
    // UE 5.8 prefers inline constants even when an expression is connected.
    // These pins must compile their expressions so instance uniforms are read.
    Material->GetEditorOnlyData()->ClearCoat.UseConstant = false;
    Material->GetEditorOnlyData()->ClearCoatRoughness.UseConstant = false;
    return true;
}
