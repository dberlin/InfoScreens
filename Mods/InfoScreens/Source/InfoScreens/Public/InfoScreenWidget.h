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

/** Client presentation of authoritative numeric snapshots; never reads source inventories. */
UCLASS()
class INFOSCREENS_API UInfoScreenWidget : public UUserWidget
{
    GENERATED_BODY()

public:
    void SetSnapshot(const FInfoScreenSnapshot& InSnapshot);

protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;

private:
    void BuildTree();
    void RefreshSnapshot();
    void AddMetricRow();

    UPROPERTY(Transient) FInfoScreenSnapshot Snapshot;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> Title;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> Detail;
    UPROPERTY(Transient) TObjectPtr<UImage> MainIcon;
    UPROPERTY(Transient) TObjectPtr<UVerticalBox> Metrics;
    UPROPERTY(Transient) TObjectPtr<UScaleBox> MetricsScale;
    UPROPERTY(Transient) TArray<FInfoScreenMetricWidgets> MetricRows;
    UPROPERTY(Transient) TObjectPtr<UProgressBar> Fullness;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> FillText;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> Footer;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> Status;
};
