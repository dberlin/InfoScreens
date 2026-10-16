#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "InfoScreenTypes.h"
#include "InfoScreenWidget.generated.h"

class UHorizontalBox;
class UImage;
class UProgressBar;
class UScaleBox;
class USizeBox;
class UTextBlock;
class UVerticalBox;

USTRUCT()
struct FInfoScreenMetricWidgets
{
    GENERATED_BODY()

    UPROPERTY(Transient) TObjectPtr<UHorizontalBox> Root;
    UPROPERTY(Transient) TObjectPtr<USizeBox> IconBox;
    UPROPERTY(Transient) TObjectPtr<UImage> Icon;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> Label;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> Amount;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> Rate;
};

USTRUCT()
struct FInfoScreenTextRegion
{
    GENERATED_BODY()

    UPROPERTY(Transient) TObjectPtr<USizeBox> Area;
    UPROPERTY(Transient) TObjectPtr<USizeBox> Width;
    UPROPERTY(Transient) TObjectPtr<UScaleBox> Scale;
    UPROPERTY(Transient) TObjectPtr<UWidget> Content;
};

/** Client presentation of authoritative numeric snapshots; never reads source inventories. */
UCLASS()
class INFOSCREENS_API UInfoScreenWidget : public UUserWidget
{
    GENERATED_BODY()

public:
    void SetSnapshot(const FInfoScreenSnapshot& InSnapshot, int32 InFontSize = 0, int32 InHeadingFontSize = 0);

protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;

private:
    void BuildTree();
    void RefreshSnapshot();
    void AddMetricRow();
    void ApplyItemFontSize(int32 FontSize);
    void ApplyHeadingFontSize(int32 FontSize);
    void ApplySummaryFontSize(int32 FontSize);
    void FitContent();

    int32 RequestedFontSize = 0;
    int32 RequestedHeadingFontSize = 0;
    UPROPERTY(Transient) TObjectPtr<UVerticalBox> Content;
    UPROPERTY(Transient) TObjectPtr<UHorizontalBox> Header;
    UPROPERTY(Transient) TObjectPtr<USizeBox> MainIconBox;
    UPROPERTY(Transient) FInfoScreenTextRegion HeadingRegion;
    UPROPERTY(Transient) FInfoScreenTextRegion ItemsRegion;
    UPROPERTY(Transient) FInfoScreenTextRegion SummaryRegion;
    UPROPERTY(Transient) TObjectPtr<UVerticalBox> Summary;
    UPROPERTY(Transient) TObjectPtr<USizeBox> BarBox;

    UPROPERTY(Transient) FInfoScreenSnapshot Snapshot;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> Title;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> Detail;
    UPROPERTY(Transient) TObjectPtr<UImage> MainIcon;
    UPROPERTY(Transient) TObjectPtr<UVerticalBox> Metrics;
    UPROPERTY(Transient) TArray<FInfoScreenMetricWidgets> MetricRows;
    UPROPERTY(Transient) TObjectPtr<UProgressBar> Fullness;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> FillText;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> Footer;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> Status;
};
