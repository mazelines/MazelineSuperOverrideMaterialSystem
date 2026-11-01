#pragma once
#include "Commandlets/Commandlet.h"
#include "MazelineShaderPatchCommandlet.generated.h"

UCLASS()
class UMazelineShaderPatchCommandlet : public UCommandlet
{
    GENERATED_BODY()
public:
    UMazelineShaderPatchCommandlet();
    virtual int32 Main(const FString& Params) override;
};
