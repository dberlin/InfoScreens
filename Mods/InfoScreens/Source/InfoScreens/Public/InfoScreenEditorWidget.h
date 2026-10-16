#pragma once

#include "CoreMinimal.h"
#include "UI/FGInteractWidget.h"
#include "InfoScreenTypes.h"
#include "InfoScreenEditState.h"
#include "InfoScreenEditorWidget.generated.h"

class AInfoScreen;
class UCheckBox;
class UComboBoxString;
class UEditableTextBox;
class UHorizontalBox;
class UInfoScreenRemoteCall;
class UTextBlock;
class UVerticalBox;

/** Native game interaction menu; drafts never modify the replicated screen. */
UCLASS()
class INFOSCREENS_API UInfoScreenEditorWidget : public UFGInteractWidget
{
    GENERATED_BODY()

public:
    UInfoScreenEditorWidget(const FObjectInitializer& ObjectInitializer);
    void SetStatus(const FText& Message);
    void RefreshLayoutFromScreen();
    bool IsEditingScreen(const AInfoScreen* Target) const;
    void HandleEditResult(bool Applied, const FInfoScreenLayout& Layout, int64 Revision, const FText& Message);

protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual void NativeConstruct() override;
    virtual void NativeDestruct() override;
    virtual void Init_Implementation() override;
    virtual void OnEscapePressed_Implementation() override;
    virtual FReply NativeOnPreviewKeyDown(const FGeometry& Geometry, const FKeyEvent& Event) override;
    virtual FReply NativeOnKeyDown(const FGeometry& Geometry, const FKeyEvent& Event) override;

private:
    void BuildTree();
    void BuildTransformFields(UHorizontalBox* Parent, const FText& Heading);
    void BindScreen();
    bool ReadDraft(FInfoScreenLayout& OutLayout);
    UInfoScreenRemoteCall* GetRemoteCall();
    bool IsEditingNumber() const;
    void OnAuthorityLayoutChanged();
    void WriteLayoutFields(const FInfoScreenLayout& Layout);
    UFUNCTION() void OnDraftTextChanged(const FText& Text);
    UFUNCTION() void OnBackVisibilityChanged(bool Checked);
    UFUNCTION() void OnBlueprintFlowChanged(FString SelectedItem, ESelectInfo::Type SelectionType);

    UFUNCTION() void Apply();
    UFUNCTION() void SavePreset();
    UFUNCTION() void LoadPreset();
    UFUNCTION() void Close();

    UPROPERTY(Transient) TWeakObjectPtr<AInfoScreen> Screen;
    UPROPERTY(Transient) TWeakObjectPtr<UInfoScreenRemoteCall> RemoteCall;
    UPROPERTY(Transient) TArray<TObjectPtr<UEditableTextBox>> Fields;
    UPROPERTY(Transient) TObjectPtr<UCheckBox> ShowBack;
    UPROPERTY(Transient) TObjectPtr<UComboBoxString> MovementTarget;
    UPROPERTY(Transient) TObjectPtr<UHorizontalBox> BlueprintOptions;
    UPROPERTY(Transient) TObjectPtr<UComboBoxString> BlueprintFlow;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> Status;
    FDelegateHandle LayoutChangedHandle;
    FInfoScreenDraftState EditState;
    bool bWritingFields = false;
};
