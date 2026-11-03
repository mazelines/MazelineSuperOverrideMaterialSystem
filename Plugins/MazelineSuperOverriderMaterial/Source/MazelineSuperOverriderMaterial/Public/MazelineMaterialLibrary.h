#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"
#include "MazelineMaterialLibrary.generated.h"

class UMaterial;
class UMaterialExpression;
class UCurveLinearColorAtlas;
class UCurveLinearColor;

/** Editor helpers for reading and migrating the GGX control graph. */
UCLASS()
class MAZELINESUPEROVERRIDERMATERIAL_API UMazelineMaterialLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    /** Legacy Clear Coat carrier wiring, retained for migration tests. */
    UFUNCTION(BlueprintCallable, Category="Mazeline|Material")
    static bool ConnectGGXParameters(UMaterial* Material, UMaterialExpression* Sharpness, UMaterialExpression* Threshold);

    UFUNCTION(BlueprintPure, Category="Mazeline|Material")
    static UMaterialExpression* GetGGXParameterInput(UMaterial* Material, bool bThreshold);

    /** Move existing controls to a dedicated output and select Default Lit. */
    UFUNCTION(BlueprintCallable, Category="Mazeline|Material")
    static bool MigrateGGXParametersToDefaultLit(UMaterial* Material, UMaterialExpression* Sharpness, UMaterialExpression* Threshold);

    /** Create three editable curves and their shared LUT. Existing assets are not overwritten. */
    UFUNCTION(BlueprintCallable, Category="Mazeline|Material")
    static UCurveLinearColorAtlas* CreateToonRampExamples(const FString& PackageRoot);

    /** Explicit migration of the initial example's duplicated constructor endpoint keys. */
    UFUNCTION(BlueprintCallable, Category="Mazeline|Material")
    static bool RemoveConstructorKeysFromRampExample(UCurveLinearColor* Curve);
};
