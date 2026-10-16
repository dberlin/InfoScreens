#include "InfoScreenWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/ProgressBar.h"
#include "Components/ScaleBox.h"
#include "Components/ScaleBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/World.h"
#include "Styling/CoreStyle.h"
#include "Widgets/SNullWidget.h"

#define LOCTEXT_NAMESPACE "InfoScreenWidget"

namespace
{
    const FLinearColor Orange(1.0f, 0.36f, 0.035f);

    UTextBlock* MakeText(UWidgetTree* Tree, int32 FontSize, const FLinearColor& Color)
    {
        UTextBlock* Text = Tree->ConstructWidget<UTextBlock>();
        Text->SetFont(FCoreStyle::GetDefaultFontStyle("Regular", FontSize));
        Text->SetColorAndOpacity(FSlateColor(Color));
        return Text;
    }

    FText Number(double Value)
    {
        FNumberFormattingOptions Options;
        Options.SetMaximumFractionalDigits(2);
        return FMath::IsFinite(Value) ? FText::AsNumber(Value, &Options) : LOCTEXT("Unavailable", "—");
    }

    FText Unit(EInfoScreenUnit Value)
    {
        switch (Value)
        {
        case EInfoScreenUnit::Items: return LOCTEXT("Items", "items");
        case EInfoScreenUnit::Slots: return LOCTEXT("Slots", "slots");
        case EInfoScreenUnit::CubicMeters: return LOCTEXT("CubicMeters", "m³");
        case EInfoScreenUnit::Megawatts: return LOCTEXT("Megawatts", "MW");
        case EInfoScreenUnit::MegawattHours: return LOCTEXT("MegawattHours", "MWh");
        case EInfoScreenUnit::Seconds: return LOCTEXT("Seconds", "s");
        case EInfoScreenUnit::Points: return LOCTEXT("Points", "points");
        case EInfoScreenUnit::Coupons: return LOCTEXT("Coupons", "coupons");
        case EInfoScreenUnit::Percent: return LOCTEXT("Percent", "%");
        }
        return FText::GetEmpty();
    }
}

TSharedRef<SWidget> UInfoScreenWidget::RebuildWidget()
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
    RefreshSnapshot();
    return Super::RebuildWidget();
}

void UInfoScreenWidget::BuildTree()
{
    UBorder* Background = WidgetTree->ConstructWidget<UBorder>();
    Background->SetBrushColor(FLinearColor::Black);
    Background->SetPadding(FMargin(26.0f, 20.0f));
    Background->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    WidgetTree->RootWidget = Background;

    UVerticalBox* Content = WidgetTree->ConstructWidget<UVerticalBox>();
    Background->SetContent(Content);
    UHorizontalBox* Header = WidgetTree->ConstructWidget<UHorizontalBox>();
    Content->AddChildToVerticalBox(Header)->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 14.0f));

    USizeBox* IconBox = WidgetTree->ConstructWidget<USizeBox>();
    IconBox->SetWidthOverride(112.0f);
    IconBox->SetHeightOverride(112.0f);
    MainIcon = WidgetTree->ConstructWidget<UImage>();
    IconBox->SetContent(MainIcon);
    Header->AddChildToHorizontalBox(IconBox)->SetPadding(FMargin(0.0f, 0.0f, 20.0f, 0.0f));

    UVerticalBox* Heading = WidgetTree->ConstructWidget<UVerticalBox>();
    UHorizontalBoxSlot* HeadingSlot = Header->AddChildToHorizontalBox(Heading);
    HeadingSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    HeadingSlot->SetVerticalAlignment(VAlign_Center);
    Title = MakeText(WidgetTree, 34, Orange);
    Title->SetAutoWrapText(true);
    Heading->AddChildToVerticalBox(Title);
    Detail = MakeText(WidgetTree, 23, FLinearColor::White);
    Detail->SetAutoWrapText(true);
    Heading->AddChildToVerticalBox(Detail)->SetPadding(FMargin(0.0f, 6.0f, 0.0f, 0.0f));

    Metrics = WidgetTree->ConstructWidget<UVerticalBox>();
    MetricsScale = WidgetTree->ConstructWidget<UScaleBox>();
    MetricsScale->SetStretch(EStretch::Fill);
    MetricsScale->SetStretchDirection(EStretchDirection::DownOnly);
    USizeBox* MetricsWidth = WidgetTree->ConstructWidget<USizeBox>();
    // Match the 1024-pixel render target minus the background's horizontal padding.
    MetricsWidth->SetWidthOverride(972.0f);
    MetricsWidth->SetContent(Metrics);
    MetricsScale->SetContent(MetricsWidth);
    CastChecked<UScaleBoxSlot>(MetricsWidth->Slot)->SetVerticalAlignment(VAlign_Top);
    Content->AddChildToVerticalBox(MetricsScale)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));

    FillText = MakeText(WidgetTree, 21, FLinearColor::White);
    Content->AddChildToVerticalBox(FillText)->SetPadding(FMargin(0.0f, 8.0f, 0.0f, 4.0f));
    Fullness = WidgetTree->ConstructWidget<UProgressBar>();
    Fullness->SetFillColorAndOpacity(Orange);
    USizeBox* BarBox = WidgetTree->ConstructWidget<USizeBox>();
    BarBox->SetHeightOverride(18.0f);
    BarBox->SetContent(Fullness);
    Content->AddChildToVerticalBox(BarBox);
    Footer = MakeText(WidgetTree, 21, FLinearColor::White);
    Footer->SetAutoWrapText(true);
    Content->AddChildToVerticalBox(Footer)->SetPadding(FMargin(0.0f, 12.0f, 0.0f, 6.0f));
    Status = MakeText(WidgetTree, 22, Orange);
    Status->SetAutoWrapText(true);
    Content->AddChildToVerticalBox(Status);
}

void UInfoScreenWidget::AddMetricRow()
{
    FInfoScreenMetricWidgets Row;
    Row.Root = WidgetTree->ConstructWidget<UHorizontalBox>();
    Metrics->AddChildToVerticalBox(Row.Root)->SetPadding(FMargin(0.0f, 4.0f));
    Row.IconBox = WidgetTree->ConstructWidget<USizeBox>();
    Row.IconBox->SetWidthOverride(36.0f);
    Row.IconBox->SetHeightOverride(36.0f);
    Row.Icon = WidgetTree->ConstructWidget<UImage>();
    Row.IconBox->SetContent(Row.Icon);
    Row.Root->AddChildToHorizontalBox(Row.IconBox)->SetPadding(FMargin(0.0f, 0.0f, 10.0f, 0.0f));
    Row.Label = MakeText(WidgetTree, 24, FLinearColor::White);
    Row.Amount = MakeText(WidgetTree, 24, FLinearColor::White);
    Row.Rate = MakeText(WidgetTree, 23, Orange);
    Row.Amount->SetJustification(ETextJustify::Right);
    Row.Rate->SetJustification(ETextJustify::Right);
    UHorizontalBoxSlot* LabelSlot = Row.Root->AddChildToHorizontalBox(Row.Label);
    FSlateChildSize LabelSize(ESlateSizeRule::Fill);
    LabelSize.Value = 0.46f;
    LabelSlot->SetSize(LabelSize);
    LabelSlot->SetVerticalAlignment(VAlign_Center);
    UHorizontalBoxSlot* AmountSlot = Row.Root->AddChildToHorizontalBox(Row.Amount);
    FSlateChildSize AmountSize(ESlateSizeRule::Fill);
    AmountSize.Value = 0.32f;
    AmountSlot->SetSize(AmountSize);
    AmountSlot->SetPadding(FMargin(8.0f, 0.0f));
    AmountSlot->SetVerticalAlignment(VAlign_Center);
    UHorizontalBoxSlot* RateSlot = Row.Root->AddChildToHorizontalBox(Row.Rate);
    FSlateChildSize RateSize(ESlateSizeRule::Fill);
    RateSize.Value = 0.22f;
    RateSlot->SetSize(RateSize);
    RateSlot->SetPadding(FMargin(12.0f, 0.0f, 0.0f, 0.0f));
    RateSlot->SetVerticalAlignment(VAlign_Center);
    MetricRows.Add(Row);
}

void UInfoScreenWidget::SetSnapshot(const FInfoScreenSnapshot& InSnapshot)
{
    if (IsRunningDedicatedServer() || (GetWorld() && GetWorld()->GetNetMode() == NM_DedicatedServer))
    {
        return;
    }
    Snapshot = InSnapshot;
    RefreshSnapshot();
}

void UInfoScreenWidget::RefreshSnapshot()
{
    if (!Title)
    {
        return;
    }
    Title->SetText(Snapshot.Title);
    Detail->SetText(Snapshot.Detail);
    MainIcon->SetBrushFromTexture(Snapshot.Icon ? UFGItemDescriptor::GetBigIcon(Snapshot.Icon) : nullptr);
    MainIcon->SetVisibility(Snapshot.Icon ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Hidden);

    // A blueprint can contain more distinct ingredients than one machine; keep every row visible.
    MetricsScale->SetStretch(Snapshot.Kind == EInfoScreenKind::Blueprint ? EStretch::ScaleToFit : EStretch::Fill);
    while (MetricRows.Num() < Snapshot.Rows.Num())
    {
        AddMetricRow();
    }
    const int32 FontSize = Snapshot.Rows.Num() > 10 ? 19 : 24;
    for (int32 Index = 0; Index < MetricRows.Num(); ++Index)
    {
        FInfoScreenMetricWidgets& Widgets = MetricRows[Index];
        if (!Snapshot.Rows.IsValidIndex(Index))
        {
            Widgets.Root->SetVisibility(ESlateVisibility::Collapsed);
            continue;
        }
        const FInfoScreenRow& Row = Snapshot.Rows[Index];
        Widgets.Root->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
        const bool Compact = Snapshot.Rows.Num() > 10;
        Widgets.IconBox->SetWidthOverride(Compact ? 24.0f : 36.0f);
        Widgets.IconBox->SetHeightOverride(Compact ? 24.0f : 36.0f);
        if (UVerticalBoxSlot* RowSlot = Cast<UVerticalBoxSlot>(Widgets.Root->Slot))
        {
            RowSlot->SetPadding(FMargin(0.0f, Compact ? 2.0f : 4.0f));
        }
        Widgets.Amount->SetVisibility(Row.bHasAmount ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
        Widgets.Rate->SetVisibility(Row.bHasRate ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
        Widgets.Icon->SetBrushFromTexture(Row.Item ? UFGItemDescriptor::GetSmallIcon(Row.Item) : nullptr);
        Widgets.Icon->SetVisibility(Row.Item ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Hidden);
        Widgets.Label->SetText(!Row.Label.IsEmpty() ? Row.Label :
            (Row.Item ? UFGItemDescriptor::GetItemName(Row.Item) : FText::GetEmpty()));
        Widgets.Label->SetFont(FCoreStyle::GetDefaultFontStyle("Regular", FontSize));
        Widgets.Amount->SetFont(FCoreStyle::GetDefaultFontStyle("Regular", FontSize));
        Widgets.Rate->SetFont(FCoreStyle::GetDefaultFontStyle("Regular", FontSize));
        Widgets.Amount->SetText(!Row.bHasAmount ? FText::GetEmpty() : (Row.bHasCapacity
            ? FText::Format(LOCTEXT("AmountCapacity", "{0} / {1} {2}"), Number(Row.Amount), Number(Row.Capacity), Unit(Row.Unit))
            : FText::Format(LOCTEXT("Amount", "{0} {1}"), Number(Row.Amount), Unit(Row.Unit))));
        Widgets.Rate->SetText(Row.bHasRate
            ? FText::Format(LOCTEXT("Rate", "{0} {1}/min"), Number(Row.PerMinute), Unit(Row.Unit))
            : FText::GetEmpty());
    }

    const float Ratio = FMath::IsFinite(Snapshot.FillRatio) ? FMath::Clamp(Snapshot.FillRatio, 0.0f, 1.0f) : 0.0f;
    Fullness->SetPercent(Ratio);
    Fullness->SetVisibility(Snapshot.bHasFill ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Hidden);
    FillText->SetText(Snapshot.bHasFill
        ? FText::Format(LOCTEXT("Fullness", "Fullness: {0}%"), Number(Ratio * 100.0)) : FText::GetEmpty());

    FText Power = FText::GetEmpty();
    if (Snapshot.bHasPower)
    {
        Power = FText::Format(LOCTEXT("Power", "Power: {0} / {1} MW"), Number(Snapshot.PowerMW), Number(Snapshot.PowerCapacityMW));
    }
    const FText Production = Snapshot.bHasProduction
        ? FText::Format(LOCTEXT("Production", "Clock: {0}%    Productivity: {1}%"),
            Number(Snapshot.ClockPercent), Number(Snapshot.ProductivityPercent)) : FText::GetEmpty();
    Footer->SetText(!Power.IsEmpty() && !Production.IsEmpty()
        ? FText::Format(LOCTEXT("Footer", "{0}    {1}"), Power, Production)
        : (!Power.IsEmpty() ? Power : Production));
    Status->SetText(Snapshot.Status);
}

#undef LOCTEXT_NAMESPACE
