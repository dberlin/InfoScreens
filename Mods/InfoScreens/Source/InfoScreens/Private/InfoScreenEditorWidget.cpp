#include "InfoScreenEditorWidget.h"

#include "InfoScreen.h"
#include "InfoBlueprintScreen.h"
#include "InfoScreenRemoteCall.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CheckBox.h"
#include "Components/ComboBoxString.h"
#include "Components/EditableTextBox.h"
#include "Components/GridPanel.h"
#include "Components/GridSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/World.h"
#include "FGPlayerController.h"
#include "InputCoreTypes.h"
#include "Containers/UnrealString.h"
#include "Styling/CoreStyle.h"
#include "UI/FGGameUI.h"
#include "Widgets/SNullWidget.h"

#define LOCTEXT_NAMESPACE "InfoScreenEditor"

namespace
{
    const FLinearColor Orange(1.0f, 0.36f, 0.035f);
    // Source-local centimeters. Keys edit the draft; Apply sends it to the authority.
    constexpr double KeyboardStepCentimeters = 1.0;

    UTextBlock* MakeEditorText(UWidgetTree* Tree, const FText& Value, int32 Size = 18,
        const FLinearColor& Color = FLinearColor::White)
    {
        UTextBlock* Text = Tree->ConstructWidget<UTextBlock>();
        Text->SetText(Value);
        Text->SetFont(FCoreStyle::GetDefaultFontStyle("Regular", Size));
        Text->SetColorAndOpacity(FSlateColor(Color));
        return Text;
    }

    FText EditNumber(double Value)
    {
        // Invariant decimal syntax matches LexTryParseString; never group coordinates.
        return FText::FromString(FString::Printf(TEXT("%.6f"), Value));
    }
}

UInfoScreenEditorWidget::UInfoScreenEditorWidget(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
    mUseKeyboard = true;
    mUseMouse = true;
    mDisablePlayerActions = true;
    mDisableBuildGunActions = true;
    mDisablePlayerEquipmentManagement = true;
    mDisableVisualizationModeActions = true;
    mSupportsCaching = false;
    mSupportsStacking = false;
    mCustomTickRate = 0.0f;
    mCallCustomTickOnConstruct = false;
    SetDesiredHorizontalAlignment(HAlign_Center);
    SetDesiredVerticalAlignment(VAlign_Center);
    SetIsFocusable(true);
}

TSharedRef<SWidget> UInfoScreenEditorWidget::RebuildWidget()
{
    if (IsRunningDedicatedServer() || (GetWorld() && GetWorld()->GetNetMode() == NM_DedicatedServer))
    {
        return SNullWidget::NullWidget;
    }
    if (!WidgetTree)
    {
        Initialize();
    }
    if (!WidgetTree->RootWidget)
    {
        BuildTree();
    }
    return Super::RebuildWidget();
}

void UInfoScreenEditorWidget::BuildTree()
{
    USizeBox* Size = WidgetTree->ConstructWidget<USizeBox>();
    Size->SetWidthOverride(1040.0f);
    WidgetTree->RootWidget = Size;
    UBorder* Background = WidgetTree->ConstructWidget<UBorder>();
    Background->SetBrushColor(FLinearColor(0.015f, 0.015f, 0.015f, 0.98f));
    Background->SetPadding(FMargin(20.0f));
    Size->SetContent(Background);
    UVerticalBox* Content = WidgetTree->ConstructWidget<UVerticalBox>();
    Background->SetContent(Content);

    Content->AddChildToVerticalBox(MakeEditorText(WidgetTree,
        LOCTEXT("Title", "INFO SCREEN · LAYOUT"), 28, Orange));
    UTextBlock* Description = MakeEditorText(WidgetTree,
        LOCTEXT("Description", "Transforms are relative to the container or freight car (or this standalone screen). Locations use centimeters, rotations use degrees. Changes remain a draft until Apply or Save preset."));
    Description->SetAutoWrapText(true);
    Content->AddChildToVerticalBox(Description)->SetPadding(FMargin(0.0f, 6.0f, 0.0f, 12.0f));

    BlueprintOptions = WidgetTree->ConstructWidget<UHorizontalBox>();
    BlueprintOptions->SetVisibility(ESlateVisibility::Collapsed);
    Content->AddChildToVerticalBox(BlueprintOptions)->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 14.0f));
    BlueprintOptions->AddChildToHorizontalBox(MakeEditorText(WidgetTree,
        LOCTEXT("BlueprintFlow", "Blueprint configured rates:")))->SetVerticalAlignment(VAlign_Center);
    BlueprintFlow = WidgetTree->ConstructWidget<UComboBoxString>();
    BlueprintFlow->AddOption(TEXT("Inputs"));
    BlueprintFlow->AddOption(TEXT("Outputs"));
    BlueprintFlow->SetSelectedIndex(0);
    BlueprintFlow->OnSelectionChanged.AddDynamic(this, &UInfoScreenEditorWidget::OnBlueprintFlowChanged);
    BlueprintOptions->AddChildToHorizontalBox(BlueprintFlow)->SetPadding(FMargin(12.0f, 0.0f));

    UHorizontalBox* TransformGroups = WidgetTree->ConstructWidget<UHorizontalBox>();
    Content->AddChildToVerticalBox(TransformGroups);
    BuildTransformFields(TransformGroups, LOCTEXT("Front", "FRONT"));
    BuildTransformFields(TransformGroups, LOCTEXT("Back", "BACK"));

    UHorizontalBox* Options = WidgetTree->ConstructWidget<UHorizontalBox>();
    Content->AddChildToVerticalBox(Options)->SetPadding(FMargin(0.0f, 10.0f, 0.0f, 8.0f));
    ShowBack = WidgetTree->ConstructWidget<UCheckBox>();
    ShowBack->SetContent(MakeEditorText(WidgetTree, LOCTEXT("ShowBack", "Show back screen")));
    ShowBack->OnCheckStateChanged.AddDynamic(this, &UInfoScreenEditorWidget::OnBackVisibilityChanged);
    Options->AddChildToHorizontalBox(ShowBack)->SetPadding(FMargin(0.0f, 0.0f, 36.0f, 0.0f));
    Options->AddChildToHorizontalBox(MakeEditorText(WidgetTree, LOCTEXT("KeyboardTarget", "Keyboard target:")))
        ->SetVerticalAlignment(VAlign_Center);
    MovementTarget = WidgetTree->ConstructWidget<UComboBoxString>();
    MovementTarget->AddOption(TEXT("Front"));
    MovementTarget->AddOption(TEXT("Back"));
    MovementTarget->SetSelectedIndex(0);
    Options->AddChildToHorizontalBox(MovementTarget)->SetPadding(FMargin(10.0f, 0.0f));
    UHorizontalBox* FontOptions = WidgetTree->ConstructWidget<UHorizontalBox>();
    Content->AddChildToVerticalBox(FontOptions)->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 6.0f));
    FontOptions->AddChildToHorizontalBox(MakeEditorText(WidgetTree, LOCTEXT("HeadingFontSize", "Heading font:")))
        ->SetVerticalAlignment(VAlign_Center);
    AutoFitHeading = WidgetTree->ConstructWidget<UCheckBox>(UCheckBox::StaticClass(), TEXT("AutoFitHeadingCheckBox"));
    AutoFitHeading->SetContent(MakeEditorText(WidgetTree, LOCTEXT("AutoFit", "Auto-fit")));
    AutoFitHeading->SetToolTipText(LOCTEXT("AutoFitHeadingHelp", "Fit heading text independently of the item list."));
    AutoFitHeading->OnCheckStateChanged.AddDynamic(this, &UInfoScreenEditorWidget::OnAutoFitHeadingChanged);
    FontOptions->AddChildToHorizontalBox(AutoFitHeading)->SetPadding(FMargin(10.0f, 0.0f));
    HeadingFontSizeField = WidgetTree->ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass(), TEXT("HeadingFontSizeField"));
    HeadingFontSizeField->WidgetStyle.SetFont(FCoreStyle::GetDefaultFontStyle("Regular", 18));
    HeadingFontSizeField->SetSelectAllTextWhenFocused(true);
    HeadingFontSizeField->SetClearKeyboardFocusOnCommit(true);
    HeadingFontSizeField->SetToolTipText(LOCTEXT("ManualHeadingFontHelp", "Manual heading font: 1 to 256 in 1024x768 sign units. Large sizes may clip."));
    HeadingFontSizeField->OnTextChanged.AddDynamic(this, &UInfoScreenEditorWidget::OnDraftTextChanged);
    USizeBox* HeadingFontSizeBox = WidgetTree->ConstructWidget<USizeBox>();
    HeadingFontSizeBox->SetWidthOverride(80.0f);
    HeadingFontSizeBox->SetContent(HeadingFontSizeField);
    FontOptions->AddChildToHorizontalBox(HeadingFontSizeBox)->SetPadding(FMargin(0.0f, 0.0f, 24.0f, 0.0f));
    FontOptions->AddChildToHorizontalBox(MakeEditorText(WidgetTree, LOCTEXT("FontSize", "Items font:")))
        ->SetVerticalAlignment(VAlign_Center);
    AutoFit = WidgetTree->ConstructWidget<UCheckBox>(UCheckBox::StaticClass(), TEXT("AutoFitFontCheckBox"));
    AutoFit->SetContent(MakeEditorText(WidgetTree, LOCTEXT("AutoFit", "Auto-fit")));
    AutoFit->SetToolTipText(LOCTEXT("AutoFitHelp", "Fit item-list text independently of the heading."));
    AutoFit->OnCheckStateChanged.AddDynamic(this, &UInfoScreenEditorWidget::OnAutoFitChanged);
    FontOptions->AddChildToHorizontalBox(AutoFit)->SetPadding(FMargin(10.0f, 0.0f));
    FontSizeField = WidgetTree->ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass(), TEXT("FontSizeField"));
    FontSizeField->WidgetStyle.SetFont(FCoreStyle::GetDefaultFontStyle("Regular", 18));
    FontSizeField->SetSelectAllTextWhenFocused(true);
    FontSizeField->SetClearKeyboardFocusOnCommit(true);
    FontSizeField->SetToolTipText(LOCTEXT("ManualFontHelp", "Manual font size: 1 to 256 in 1024x768 sign units. Large sizes may clip. Enable Auto-fit to fit all content."));
    FontSizeField->OnTextChanged.AddDynamic(this, &UInfoScreenEditorWidget::OnDraftTextChanged);
    USizeBox* FontSizeBox = WidgetTree->ConstructWidget<USizeBox>();
    FontSizeBox->SetWidthOverride(80.0f);
    FontSizeBox->SetContent(FontSizeField);
    FontOptions->AddChildToHorizontalBox(FontSizeBox);

    UTextBlock* Hint = MakeEditorText(WidgetTree,
        LOCTEXT("Hint", "Keyboard move (outside text fields): Y / H = +X / −X, U / J = +Y / −Y, T / G = +Z / −Z. Each press moves the selected draft by 1 cm. Click a label or press Tab out of a field, then Apply. Escape closes without applying drafts."));
    Hint->SetAutoWrapText(true);
    Content->AddChildToVerticalBox(Hint)->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 8.0f));
    UTextBlock* PresetHint = MakeEditorText(WidgetTree,
        LOCTEXT("PresetHint", "Save preset applies this draft to every screen on the same source type and to future sources. Load preset restores that type's saved server layout."), 17);
    PresetHint->SetAutoWrapText(true);
    Content->AddChildToVerticalBox(PresetHint)->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 10.0f));

    Status = MakeEditorText(WidgetTree, FText::GetEmpty(), 18, Orange);
    Status->SetAutoWrapText(true);
    Content->AddChildToVerticalBox(Status)->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 14.0f));

    UHorizontalBox* Actions = WidgetTree->ConstructWidget<UHorizontalBox>();
    Content->AddChildToVerticalBox(Actions);
    const auto AddButton = [this, Actions](const FText& Label)
    {
        UButton* Button = WidgetTree->ConstructWidget<UButton>();
        UTextBlock* Text = MakeEditorText(WidgetTree, Label, 19);
        Text->SetMargin(FMargin(14.0f, 10.0f));
        Button->SetContent(Text);
        Actions->AddChildToHorizontalBox(Button)->SetPadding(FMargin(0.0f, 0.0f, 12.0f, 0.0f));
        return Button;
    };
    AddButton(LOCTEXT("Apply", "Apply"))->OnClicked.AddDynamic(this, &UInfoScreenEditorWidget::Apply);
    AddButton(LOCTEXT("Save", "Save preset for this type"))->OnClicked.AddDynamic(this, &UInfoScreenEditorWidget::SavePreset);
    AddButton(LOCTEXT("Load", "Load preset"))->OnClicked.AddDynamic(this, &UInfoScreenEditorWidget::LoadPreset);
    UButton* CloseButton = AddButton(LOCTEXT("Close", "Close"));
    CloseButton->OnClicked.AddDynamic(this, &UInfoScreenEditorWidget::Close);
    SetDefaultFocusWidget(CloseButton);
}

void UInfoScreenEditorWidget::BuildTransformFields(UHorizontalBox* Parent, const FText& Heading)
{
    UVerticalBox* Group = WidgetTree->ConstructWidget<UVerticalBox>();
    UHorizontalBoxSlot* GroupSlot = Parent->AddChildToHorizontalBox(Group);
    GroupSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    GroupSlot->SetPadding(FMargin(0.0f, 0.0f, 14.0f, 0.0f));
    Group->AddChildToVerticalBox(MakeEditorText(WidgetTree, Heading, 23, Orange))
        ->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 8.0f));
    UGridPanel* Grid = WidgetTree->ConstructWidget<UGridPanel>();
    Group->AddChildToVerticalBox(Grid);
    const FText AxisLabels[] = {
        LOCTEXT("XAndPitch", "X / Pitch"), LOCTEXT("YAndYaw", "Y / Yaw"), LOCTEXT("ZAndRoll", "Z / Roll")
    };
    const FText RowLabels[] = {
        LOCTEXT("Location", "Location"), LOCTEXT("Rotation", "Rotation"), LOCTEXT("Scale", "Scale")
    };
    for (int32 Axis = 0; Axis < 3; ++Axis)
    {
        Grid->AddChildToGrid(MakeEditorText(WidgetTree, AxisLabels[Axis], 17), 0, Axis + 1)
            ->SetPadding(FMargin(4.0f));
    }
    for (int32 Row = 0; Row < 3; ++Row)
    {
        UGridSlot* LabelSlot = Grid->AddChildToGrid(MakeEditorText(WidgetTree, RowLabels[Row], 17), Row + 1, 0);
        LabelSlot->SetVerticalAlignment(VAlign_Center);
        LabelSlot->SetPadding(FMargin(0.0f, 0.0f, 8.0f, 0.0f));
        for (int32 Axis = 0; Axis < 3; ++Axis)
        {
            UEditableTextBox* Field = WidgetTree->ConstructWidget<UEditableTextBox>();
            Field->WidgetStyle.SetFont(FCoreStyle::GetDefaultFontStyle("Regular", 18));
            Field->SetSelectAllTextWhenFocused(true);
            Field->SetClearKeyboardFocusOnCommit(true);
            Field->OnTextChanged.AddDynamic(this, &UInfoScreenEditorWidget::OnDraftTextChanged);
            USizeBox* FieldSize = WidgetTree->ConstructWidget<USizeBox>();
            FieldSize->SetWidthOverride(120.0f);
            FieldSize->SetContent(Field);
            Grid->AddChildToGrid(FieldSize, Row + 1, Axis + 1)->SetPadding(FMargin(3.0f));
            Fields.Add(Field);
        }
    }
}

void UInfoScreenEditorWidget::NativeConstruct()
{
    if (IsRunningDedicatedServer() || (GetWorld() && GetWorld()->GetNetMode() == NM_DedicatedServer))
    {
        return;
    }
    Super::NativeConstruct();
    BindScreen();
}

void UInfoScreenEditorWidget::Init_Implementation()
{
    Super::Init_Implementation();
    BindScreen();
}

void UInfoScreenEditorWidget::BindScreen()
{
    AInfoScreen* InteractedScreen = Cast<AInfoScreen>(mInteractObject);
    if (Screen.Get() != InteractedScreen)
    {
        if (Screen.IsValid())
        {
            Screen->OnLayoutChanged.Remove(LayoutChangedHandle);
        }
        LayoutChangedHandle.Reset();
        Screen = InteractedScreen;
        if (InteractedScreen)
        {
            LayoutChangedHandle = InteractedScreen->OnLayoutChanged.AddUObject(this,
                &UInfoScreenEditorWidget::OnAuthorityLayoutChanged);
        }
    }
    if (UInfoScreenRemoteCall* Calls = GetRemoteCall())
    {
        Calls->SetEditor(this);
    }
    RefreshLayoutFromScreen();
    if (BlueprintOptions)
        BlueprintOptions->SetVisibility(InteractedScreen && InteractedScreen->IsA<AInfoBlueprintScreen>()
            ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
    if (!InteractedScreen)
    {
        SetStatus(LOCTEXT("NoScreen", "This screen is no longer available. Close the editor and try again."));
    }
}

void UInfoScreenEditorWidget::NativeDestruct()
{
    if (Screen.IsValid())
    {
        Screen->OnLayoutChanged.Remove(LayoutChangedHandle);
    }
    LayoutChangedHandle.Reset();
    Screen.Reset();
    if (RemoteCall.IsValid())
    {
        RemoteCall->ClearEditor(this);
    }
    RemoteCall.Reset();
    Super::NativeDestruct();
}

void UInfoScreenEditorWidget::RefreshLayoutFromScreen()
{
    if (!Screen.IsValid())
        return;
    EditState.Reset(Screen->GetLayoutRevision());
    WriteLayoutFields(Screen->GetLayout());
}

bool UInfoScreenEditorWidget::IsEditingScreen(const AInfoScreen* Target) const
{
    return Target && Screen.Get() == Target;
}

void UInfoScreenEditorWidget::OnAuthorityLayoutChanged()
{
    if (!Screen.IsValid())
        return;
    if (EditState.ObserveLayout(Screen->GetLayoutRevision()))
        WriteLayoutFields(Screen->GetLayout());
    else if (EditState.IsDirty())
        SetStatus(LOCTEXT("DraftPreserved", "The server layout changed. Your unsaved draft has been kept; Apply to use it."));
}

void UInfoScreenEditorWidget::HandleEditResult(bool Applied, const FInfoScreenLayout& Layout,
    int64 Revision, const FText& Message)
{
    if (EditState.AcceptResult(Applied, Revision))
        WriteLayoutFields(Layout);
    SetStatus(Message);
    if (Applied && EditState.IsDirty())
        SetStatus(FText::Format(LOCTEXT("ResultDraftKept", "{0} Your newer unsaved draft has been kept."), Message));
}

void UInfoScreenEditorWidget::OnDraftTextChanged(const FText& Text)
{
    if (!bWritingFields)
        EditState.ChangeDraft();
}

void UInfoScreenEditorWidget::OnBackVisibilityChanged(bool Checked)
{
    if (!bWritingFields)
        EditState.ChangeDraft();
}

void UInfoScreenEditorWidget::OnAutoFitChanged(bool Checked)
{
    if (FontSizeField)
        FontSizeField->SetIsEnabled(!Checked);
    if (!bWritingFields)
        EditState.ChangeDraft();
}

void UInfoScreenEditorWidget::OnAutoFitHeadingChanged(bool Checked)
{
    if (HeadingFontSizeField)
        HeadingFontSizeField->SetIsEnabled(!Checked);
    if (!bWritingFields)
        EditState.ChangeDraft();
}

void UInfoScreenEditorWidget::OnBlueprintFlowChanged(FString SelectedItem, ESelectInfo::Type SelectionType)
{
    if (!bWritingFields)
        EditState.ChangeDraft();
}

void UInfoScreenEditorWidget::WriteLayoutFields(const FInfoScreenLayout& Layout)
{
    if (Fields.Num() != 18 || !ShowBack || !AutoFit || !FontSizeField || !AutoFitHeading || !HeadingFontSizeField)
        return;
    TGuardValue<bool> WritingGuard(bWritingFields, true);
    const FTransform* Transforms[] = { &Layout.Front, &Layout.Back };
    for (int32 Side = 0; Side < 2; ++Side)
    {
        const FVector Location = Transforms[Side]->GetLocation();
        const FRotator Rotation = Transforms[Side]->Rotator();
        const FVector Scale = Transforms[Side]->GetScale3D();
        const double Values[] = {
            Location.X, Location.Y, Location.Z, Rotation.Pitch, Rotation.Yaw, Rotation.Roll,
            Scale.X, Scale.Y, Scale.Z
        };
        for (int32 Field = 0; Field < 9; ++Field)
        {
            Fields[Side * 9 + Field]->SetText(EditNumber(Values[Field]));
        }
    }
    ShowBack->SetIsChecked(Layout.bBackVisible);
    AutoFit->SetIsChecked(Layout.FontSize == 0);
    FontSizeField->SetText(FText::FromString(FString::FromInt(Layout.FontSize == 0 ? 48 : Layout.FontSize)));
    FontSizeField->SetIsEnabled(Layout.FontSize != 0);
    AutoFitHeading->SetIsChecked(Layout.HeadingFontSize == 0);
    HeadingFontSizeField->SetText(FText::FromString(FString::FromInt(Layout.HeadingFontSize == 0 ? 48 : Layout.HeadingFontSize)));
    HeadingFontSizeField->SetIsEnabled(Layout.HeadingFontSize != 0);
    if (BlueprintFlow)
        BlueprintFlow->SetSelectedIndex(Layout.bBlueprintOutputs ? 1 : 0);
}

void UInfoScreenEditorWidget::SetStatus(const FText& Message)
{
    if (Status)
    {
        Status->SetText(Message);
    }
}

UInfoScreenRemoteCall* UInfoScreenEditorWidget::GetRemoteCall()
{
    if (!RemoteCall.IsValid())
    {
        if (AFGPlayerController* Controller = Cast<AFGPlayerController>(GetOwningPlayer()))
        {
            RemoteCall = Controller->GetRemoteCallObjectOfClass<UInfoScreenRemoteCall>();
        }
    }
    return RemoteCall.Get();
}

bool UInfoScreenEditorWidget::ReadDraft(FInfoScreenLayout& OutLayout)
{
    if (!Screen.IsValid() || Fields.Num() != 18 || !ShowBack || !AutoFit || !FontSizeField
        || !AutoFitHeading || !HeadingFontSizeField)
    {
        SetStatus(LOCTEXT("UnavailableScreen", "The screen is no longer available."));
        return false;
    }
    int32 FontSizes[] = { 0, 0 };
    const UCheckBox* AutoFitChecks[] = { AutoFit.Get(), AutoFitHeading.Get() };
    UEditableTextBox* FontFields[] = { FontSizeField.Get(), HeadingFontSizeField.Get() };
    for (int32 Index = 0; Index < 2; ++Index)
    {
        if (AutoFitChecks[Index]->IsChecked())
            continue;
        const FString Text = FontFields[Index]->GetText().ToString().TrimStartAndEnd();
        bool Valid = !Text.IsEmpty();
        for (const TCHAR Character : Text)
        {
            if (Character < TEXT('0') || Character > TEXT('9'))
            {
                Valid = false;
                break;
            }
            FontSizes[Index] = FontSizes[Index] * 10 + Character - TEXT('0');
            if (FontSizes[Index] > 256)
            {
                Valid = false;
                break;
            }
        }
        if (!Valid || FontSizes[Index] < 1)
        {
            SetStatus(LOCTEXT("InvalidFontSize", "Manual heading and items fonts must be whole numbers from 1 to 256. Enable each Auto-fit to fit that content."));
            FontFields[Index]->SetKeyboardFocus();
            return false;
        }
    }
    double Values[18];
    for (int32 Index = 0; Index < 18; ++Index)
    {
        const FString Text = Fields[Index]->GetText().ToString().TrimStartAndEnd();
        if (Text.IsEmpty() || !Text.IsNumeric() || !LexTryParseString(Values[Index], *Text) || !FMath::IsFinite(Values[Index]))
        {
            SetStatus(LOCTEXT("InvalidNumber", "Every field must contain a finite number. Use a decimal point, without unit suffixes or thousands separators."));
            Fields[Index]->SetKeyboardFocus();
            return false;
        }
    }
    for (int32 Side = 0; Side < 2; ++Side)
    {
        const int32 Offset = Side * 9;
        const FTransform Transform(FRotator(Values[Offset + 3], Values[Offset + 4], Values[Offset + 5]),
            FVector(Values[Offset], Values[Offset + 1], Values[Offset + 2]),
            FVector(Values[Offset + 6], Values[Offset + 7], Values[Offset + 8]));
        if (Side == 0)
        {
            OutLayout.Front = Transform;
        }
        else
        {
            OutLayout.Back = Transform;
        }
    }
    OutLayout.bBackVisible = ShowBack->IsChecked();
    OutLayout.FontSize = FontSizes[0];
    OutLayout.HeadingFontSize = FontSizes[1];
    OutLayout.bBlueprintOutputs = Screen->IsA<AInfoBlueprintScreen>() && BlueprintFlow
        && BlueprintFlow->GetSelectedIndex() == 1;
    return true;
}

void UInfoScreenEditorWidget::Apply()
{
    FInfoScreenLayout Draft;
    if (!ReadDraft(Draft))
    {
        return;
    }
    if (UInfoScreenRemoteCall* Calls = GetRemoteCall())
    {
        EditState.BeginRequest();
        SetStatus(LOCTEXT("Applying", "Waiting for the server to apply this layout…"));
        Calls->ServerApplyLayout(Screen.Get(), Draft, Calls->BeginEditRequest(this));
    }
    else
    {
        SetStatus(LOCTEXT("NoRemoteCall", "The server edit connection is not available yet. Try again shortly."));
    }
}

void UInfoScreenEditorWidget::SavePreset()
{
    FInfoScreenLayout Draft;
    if (!ReadDraft(Draft))
    {
        return;
    }
    if (UInfoScreenRemoteCall* Calls = GetRemoteCall())
    {
        EditState.BeginRequest();
        SetStatus(LOCTEXT("Saving", "Waiting for the server to save and apply this type's preset…"));
        Calls->ServerSavePreset(Screen.Get(), Draft, Calls->BeginEditRequest(this));
    }
    else
    {
        SetStatus(LOCTEXT("NoRemoteCall", "The server edit connection is not available yet. Try again shortly."));
    }
}

void UInfoScreenEditorWidget::LoadPreset()
{
    if (!Screen.IsValid())
    {
        SetStatus(LOCTEXT("UnavailableScreen", "The screen is no longer available."));
        return;
    }
    if (UInfoScreenRemoteCall* Calls = GetRemoteCall())
    {
        EditState.BeginRequest();
        SetStatus(LOCTEXT("Loading", "Waiting for the server's saved preset…"));
        Calls->ServerLoadPreset(Screen.Get(), Calls->BeginEditRequest(this));
    }
    else
    {
        SetStatus(LOCTEXT("NoRemoteCall", "The server edit connection is not available yet. Try again shortly."));
    }
}

void UInfoScreenEditorWidget::Close()
{
    if (AFGPlayerController* Controller = Cast<AFGPlayerController>(GetOwningPlayer()))
    {
        if (UFGGameUI* GameUI = Controller->GetGameUI())
        {
            GameUI->PopWidget(this);
        }
    }
}

void UInfoScreenEditorWidget::OnEscapePressed_Implementation()
{
    Close();
}

FReply UInfoScreenEditorWidget::NativeOnPreviewKeyDown(const FGeometry& Geometry, const FKeyEvent& Event)
{
    if (Event.GetKey() == EKeys::Escape)
    {
        Close();
        return FReply::Handled();
    }
    return Super::NativeOnPreviewKeyDown(Geometry, Event);
}

bool UInfoScreenEditorWidget::IsEditingNumber() const
{
    for (const UEditableTextBox* Field : Fields)
    {
        if (Field && (Field->HasKeyboardFocus() || Field->HasFocusedDescendants()))
        {
            return true;
        }
    }
    return (FontSizeField && (FontSizeField->HasKeyboardFocus() || FontSizeField->HasFocusedDescendants()))
        || (HeadingFontSizeField && (HeadingFontSizeField->HasKeyboardFocus() || HeadingFontSizeField->HasFocusedDescendants()));
}

FReply UInfoScreenEditorWidget::NativeOnKeyDown(const FGeometry& Geometry, const FKeyEvent& Event)
{
    if (IsEditingNumber() || Fields.Num() != 18 || !MovementTarget || Event.IsControlDown() || Event.IsAltDown())
    {
        return Super::NativeOnKeyDown(Geometry, Event);
    }
    const FKey Key = Event.GetKey();
    int32 Axis = INDEX_NONE;
    double Direction = 1.0;
    if (Key == EKeys::Y || Key == EKeys::H)
    {
        Axis = 0;
        Direction = Key == EKeys::Y ? 1.0 : -1.0;
    }
    else if (Key == EKeys::U || Key == EKeys::J)
    {
        Axis = 1;
        Direction = Key == EKeys::U ? 1.0 : -1.0;
    }
    else if (Key == EKeys::T || Key == EKeys::G)
    {
        Axis = 2;
        Direction = Key == EKeys::T ? 1.0 : -1.0;
    }
    if (Axis == INDEX_NONE)
    {
        return Super::NativeOnKeyDown(Geometry, Event);
    }
    const int32 Index = (MovementTarget->GetSelectedIndex() == 1 ? 9 : 0) + Axis;
    const FString Text = Fields[Index]->GetText().ToString().TrimStartAndEnd();
    double Value = 0.0;
    if (Text.IsEmpty() || !Text.IsNumeric() || !LexTryParseString(Value, *Text) || !FMath::IsFinite(Value) ||
        !FMath::IsFinite(Value + Direction * KeyboardStepCentimeters))
    {
        SetStatus(LOCTEXT("InvalidMovement", "Enter a finite location before using keyboard movement."));
        return FReply::Handled();
    }
    Fields[Index]->SetText(EditNumber(Value + Direction * KeyboardStepCentimeters));
    SetStatus(LOCTEXT("DraftMoved", "Draft moved by 1 cm. Apply to save the change on the server."));
    return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
