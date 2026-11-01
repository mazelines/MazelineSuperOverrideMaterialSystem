#pragma once
#include "Widgets/SCompoundWidget.h"
#include "MazelineShaderPatchManager.h"

class SMazelineMaterialSettings : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SMazelineMaterialSettings) {}
    SLATE_END_ARGS()
    void Construct(const FArguments& Args);
private:
    FMazelinePatchStatus Status;
    FString Error, LastResult;
    bool bBusy = false;
    bool bLastSuccess = true;
    void Refresh();
    FReply Run(const FString& Action);
    bool CanRun(const FString& Action) const;
    FText StatusText() const;
};
