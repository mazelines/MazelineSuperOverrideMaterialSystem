#include "SMazelineMaterialSettings.h"
#include "Editor.h"
#include "HAL/PlatformProcess.h"
#include "Misc/ScopedSlowTask.h"
#include "UnrealEdMisc.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Styling/AppStyle.h"

void SMazelineMaterialSettings::Construct(const FArguments& Args)
{
    Refresh();
    auto Button = [this](const TCHAR* Label, const TCHAR* Action)
    {
        const FString Operation(Action);
        return SNew(SButton)
            .Text(FText::FromString(Label))
            .IsEnabled_Lambda([this, Operation] { return CanRun(Operation); })
            .OnClicked_Lambda([this, Operation] { return Run(Operation); });
    };
    ChildSlot
    [
        SNew(SScrollBox)
        + SScrollBox::Slot().Padding(16)
        [
            SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 12)
            [ SNew(STextBlock).Text(FText::FromString(TEXT("Mazeline Super Override Material"))).Font(FAppStyle::GetFontStyle("DetailsView.CategoryFontStyle")) ]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 12)
            [ SNew(STextBlock).Text(this, &SMazelineMaterialSettings::StatusText).AutoWrapText(true) ]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 12)
            [ SNew(STextBlock).Text(FText::FromString(TEXT("Apply scans connected Custom Expressions, registers their Curve Atlas inputs, and updates both rendering paths. Restore returns all five engine shaders to their backed-up originals. Restart after patching; curve key edits and RampRow changes update without repatching."))).AutoWrapText(true) ]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 12)
            [
                SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(8, 8))
                + SWrapBox::Slot() [ Button(TEXT("Apply / Update Patch"), TEXT("apply")) ]
                + SWrapBox::Slot() [ Button(TEXT("Restore Original"), TEXT("restore")) ]
                + SWrapBox::Slot() [ Button(TEXT("Recover Interrupted Operation"), TEXT("recover")) ]
                + SWrapBox::Slot()
                [ SNew(SButton).Text(FText::FromString(TEXT("Refresh Status"))).IsEnabled_Lambda([this] { return !bBusy; })
                    .OnClicked_Lambda([this] { Refresh(); return FReply::Handled(); }) ]
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 12)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)
                [ SNew(SButton).Text(FText::FromString(TEXT("Open Backups")))
                    .OnClicked_Lambda([this] { FPlatformProcess::ExploreFolder(*Status.BackupDirectory); return FReply::Handled(); }) ]
                + SHorizontalBox::Slot().AutoWidth()
                [ SNew(SButton).Text(FText::FromString(TEXT("Restart Editor")))
                    .IsEnabled_Lambda([this] { return !bBusy && FMazelineShaderPatchManager::RestartRequired() && Status.State != TEXT("interrupted"); })
                    .OnClicked_Lambda([] { FUnrealEdMisc::Get().RestartEditor(true); return FReply::Handled(); }) ]
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 12)
            [ SNew(STextBlock).Text_Lambda([this] { return FText::FromString(LastResult); }).AutoWrapText(true)
                .ColorAndOpacity_Lambda([this] { return bLastSuccess ? FSlateColor::UseForeground() : FSlateColor(FLinearColor(1, 0.25f, 0.15f)); }) ]
            + SVerticalBox::Slot().AutoHeight()
            [ SNew(STextBlock).Text(FText::FromString(TEXT("UE 5.8.4 / Windows DX11 SM5 / Opaque Default Lit. Deferred supports IDs 1-3 and reuses two GBuffer flags. This patch affects every project using this engine installation. Existing Python-era backups are preserved and supported."))).AutoWrapText(true) ]
        ]
    ];
    RegisterActiveTimer(3.0f, FWidgetActiveTimerDelegate::CreateLambda([this](double, float)
    {
        if (!bBusy) Refresh();
        return EActiveTimerReturnType::Continue;
    }));
}
void SMazelineMaterialSettings::Refresh()
{
    Error.Empty();
    FMazelineShaderPatchManager::Current().Inspect(Status, Error);
}
FText SMazelineMaterialSettings::StatusText() const
{
    return FText::FromString(FString::Printf(TEXT("Patch state: %s%s\nEngine: %s\nProject owner: %s\nBackups: %s\n%s%s"),
        *Status.State, FMazelineShaderPatchManager::RestartRequired() ? TEXT("  |  Restart required") : TEXT(""),
        *FMazelineShaderPatchManager::Current().GetEngine(),
        Status.Owner.IsEmpty() ? TEXT("(none)") : *Status.Owner, *Status.BackupDirectory,
        *Status.Detail, Error.IsEmpty() ? TEXT("") : *(TEXT("\n") + Error)));
}
bool SMazelineMaterialSettings::CanRun(const FString& Action) const
{
    if (bBusy || (GEditor && GEditor->PlayWorld) || !Error.IsEmpty() || !Status.bOwnedByProject) return false;
    if (Action == TEXT("recover")) return Status.State == TEXT("interrupted");
    if (Status.State == TEXT("interrupted") || Status.State == TEXT("conflict")) return false;
    if (Action == TEXT("restore")) return Status.State == TEXT("applied");
    return Status.State == TEXT("unmanaged") || Status.State == TEXT("restored") || Status.State == TEXT("applied");
}
FReply SMazelineMaterialSettings::Run(const FString& Action)
{
    bBusy = true;
    FScopedSlowTask Progress(1.0f, FText::FromString(TEXT("Updating Mazeline engine shader patch...")));
    Progress.MakeDialog();
    bLastSuccess = FMazelineShaderPatchManager::RunCurrent(Action, LastResult);
    UE_LOG(LogTemp, Display, TEXT("MAZELINE_SETTINGS_RESULT %s %s: %s"), *Action, bLastSuccess ? TEXT("success") : TEXT("failed"), *LastResult);
    bBusy = false;
    Refresh();
    return FReply::Handled();
}
