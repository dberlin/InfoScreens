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

    FInfoScreenTextRegion MakeRegion(UWidgetTree* Tree, UVerticalBox* Panel, UWidget* Content, FName Name)
    {
        FInfoScreenTextRegion Region;
        Region.Content = Content;
        Region.Area = Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), Name);
        Region.Area->SetWidthOverride(972.0f);
        Region.Area->SetClipping(EWidgetClipping::ClipToBounds);
        Panel->AddChildToVerticalBox(Region.Area);
        Region.Scale = Tree->ConstructWidget<UScaleBox>();
        Region.Scale->SetStretchDirection(EStretchDirection::DownOnly);
        Region.Area->SetContent(Region.Scale);
        Region.Width = Tree->ConstructWidget<USizeBox>();
        Region.Scale->SetContent(Region.Width);
        CastChecked<UScaleBoxSlot>(Region.Width->Slot)->SetHorizontalAlignment(HAlign_Left);
        CastChecked<UScaleBoxSlot>(Region.Width->Slot)->SetVerticalAlignment(VAlign_Top);
        Region.Width->SetContent(Content);
        return Region;
    }

    float FitRegion(FInfoScreenTextRegion& Region, int32 RequestedFont, float HeightBudget,
        int32 AutoMaximum, TFunctionRef<void(int32)> ApplyFont, bool FillHeight = false)
    {
        const TSharedRef<SWidget> SlateContent = Region.Content->TakeWidget();
        const auto Measure = [&](int32 FontSize)
        {
            ApplyFont(FontSize);
            // Font/slot setters dirty descendants. Dirty the region too so each
            // prepass computes a fresh natural size, never the previous candidate.
            SlateContent->Invalidate(EInvalidateWidgetReason::Layout);
            SlateContent->SlatePrepass(1.0f);
            return SlateContent->GetDesiredSize();
        };
        int32 AppliedFont = RequestedFont;
        if (RequestedFont == 0)
        {
            int32 Low = 1;
            int32 High = AutoMaximum;
            AppliedFont = 1;
            while (Low <= High)
            {
                const int32 Candidate = Low + (High - Low) / 2;
                const FVector2D Size = Measure(Candidate);
                if (Size.X <= 972.0f && Size.Y <= HeightBudget)
                {
                    AppliedFont = Candidate;
                    Low = Candidate + 1;
                }
                else
                {
                    High = Candidate - 1;
                }
            }
        }
        const FVector2D Size = Measure(AppliedFont);
        const float Height = FillHeight ? HeightBudget : FMath::Min(HeightBudget, static_cast<float>(Size.Y));
        Region.Area->SetHeightOverride(Height);
        Region.Area->SetVisibility(Height > 0.0f ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
        // Reserve natural column widths even if size 1 cannot fit. Only auto mode
        // may uniformly scale that extreme case; manual choices clip, never distort.
        Region.Width->SetWidthOverride(FMath::Max(972.0f, static_cast<float>(Size.X)));
        Region.Scale->SetStretch(RequestedFont == 0 ? EStretch::ScaleToFit : EStretch::None);
        return Height;
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
    Background->SetClipping(EWidgetClipping::ClipToBounds);
    WidgetTree->RootWidget = Background;

    Content = WidgetTree->ConstructWidget<UVerticalBox>();
    Background->SetContent(Content);
    Header = WidgetTree->ConstructWidget<UHorizontalBox>();
    HeadingRegion = MakeRegion(WidgetTree, Content, Header, TEXT("HeadingRegion"));

    MainIconBox = WidgetTree->ConstructWidget<USizeBox>();
    MainIcon = WidgetTree->ConstructWidget<UImage>();
    MainIconBox->SetContent(MainIcon);
    Header->AddChildToHorizontalBox(MainIconBox);

    UVerticalBox* Heading = WidgetTree->ConstructWidget<UVerticalBox>();
    UHorizontalBoxSlot* HeadingSlot = Header->AddChildToHorizontalBox(Heading);
    HeadingSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    HeadingSlot->SetVerticalAlignment(VAlign_Center);
    Title = MakeText(WidgetTree, 34, Orange);
    Heading->AddChildToVerticalBox(Title);
    Detail = MakeText(WidgetTree, 23, FLinearColor::White);
    Heading->AddChildToVerticalBox(Detail);

    Metrics = WidgetTree->ConstructWidget<UVerticalBox>();
    ItemsRegion = MakeRegion(WidgetTree, Content, Metrics, TEXT("ItemListRegion"));
    Summary = WidgetTree->ConstructWidget<UVerticalBox>();
    SummaryRegion = MakeRegion(WidgetTree, Content, Summary, TEXT("SummaryRegion"));
    FillText = MakeText(WidgetTree, 21, FLinearColor::White);
    Summary->AddChildToVerticalBox(FillText);
    Fullness = WidgetTree->ConstructWidget<UProgressBar>();
    Fullness->SetFillColorAndOpacity(Orange);
    BarBox = WidgetTree->ConstructWidget<USizeBox>();
    BarBox->SetContent(Fullness);
    Summary->AddChildToVerticalBox(BarBox);
    Footer = MakeText(WidgetTree, 21, FLinearColor::White);
    Summary->AddChildToVerticalBox(Footer);
    Status = MakeText(WidgetTree, 22, Orange);
    Summary->AddChildToVerticalBox(Status);
}

void UInfoScreenWidget::AddMetricRow()
{
    FInfoScreenMetricWidgets Row;
    Row.Root = WidgetTree->ConstructWidget<UHorizontalBox>();
    Metrics->AddChildToVerticalBox(Row.Root);
    Row.IconBox = WidgetTree->ConstructWidget<USizeBox>();
    Row.Icon = WidgetTree->ConstructWidget<UImage>();
    Row.IconBox->SetContent(Row.Icon);
    Row.Root->AddChildToHorizontalBox(Row.IconBox);
    Row.Label = MakeText(WidgetTree, 24, FLinearColor::White);
    Row.Amount = MakeText(WidgetTree, 24, FLinearColor::White);
    Row.Rate = MakeText(WidgetTree, 24, Orange);
    Row.Amount->SetJustification(ETextJustify::Right);
    Row.Rate->SetJustification(ETextJustify::Right);
    UHorizontalBoxSlot* LabelSlot = Row.Root->AddChildToHorizontalBox(Row.Label);
    LabelSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    LabelSlot->SetVerticalAlignment(VAlign_Center);
    // Numeric columns reserve their real text widths rather than arbitrary percentages.
    // The fitting pass includes the label's desired width before allocating the remainder.
    Row.Root->AddChildToHorizontalBox(Row.Amount)->SetVerticalAlignment(VAlign_Center);
    Row.Root->AddChildToHorizontalBox(Row.Rate)->SetVerticalAlignment(VAlign_Center);
    MetricRows.Add(Row);
}

void UInfoScreenWidget::SetSnapshot(const FInfoScreenSnapshot& InSnapshot, int32 InFontSize, int32 InHeadingFontSize)
{
    if (IsRunningDedicatedServer() || (GetWorld() && GetWorld()->GetNetMode() == NM_DedicatedServer))
    {
        return;
    }
    Snapshot = InSnapshot;
    RequestedFontSize = FMath::Clamp(InFontSize, 0, 256);
    RequestedHeadingFontSize = FMath::Clamp(InHeadingFontSize, 0, 256);
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
    MainIconBox->SetVisibility(Snapshot.Icon ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
    Title->SetVisibility(Snapshot.Title.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
    Detail->SetVisibility(Snapshot.Detail.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
    Header->SetVisibility(Snapshot.Icon || !Snapshot.Title.IsEmpty() || !Snapshot.Detail.IsEmpty()
        ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);

    while (MetricRows.Num() < Snapshot.Rows.Num())
    {
        AddMetricRow();
    }
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
        Widgets.Amount->SetVisibility(Row.bHasAmount ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
        Widgets.Rate->SetVisibility(Row.bHasRate ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
        Widgets.Icon->SetBrushFromTexture(Row.Item ? UFGItemDescriptor::GetSmallIcon(Row.Item) : nullptr);
        Widgets.IconBox->SetVisibility(Row.Item ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
        Widgets.Label->SetText(!Row.Label.IsEmpty() ? Row.Label :
            (Row.Item ? UFGItemDescriptor::GetItemName(Row.Item) : FText::GetEmpty()));
        Widgets.Amount->SetText(!Row.bHasAmount ? FText::GetEmpty() : (Row.bHasCapacity
            ? FText::Format(LOCTEXT("AmountCapacity", "{0} / {1} {2}"), Number(Row.Amount), Number(Row.Capacity), Unit(Row.Unit))
            : FText::Format(LOCTEXT("Amount", "{0} {1}"), Number(Row.Amount), Unit(Row.Unit))));
        Widgets.Rate->SetText(Row.bHasRate
            ? FText::Format(LOCTEXT("Rate", "{0} {1}/min"), Number(Row.PerMinute), Unit(Row.Unit))
            : FText::GetEmpty());
    }

    const float Ratio = FMath::IsFinite(Snapshot.FillRatio) ? FMath::Clamp(Snapshot.FillRatio, 0.0f, 1.0f) : 0.0f;
    Fullness->SetPercent(Ratio);
    BarBox->SetVisibility(Snapshot.bHasFill ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
    FillText->SetVisibility(Snapshot.bHasFill ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
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
    Footer->SetVisibility(Footer->GetText().IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
    Status->SetVisibility(Snapshot.Status.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
    FitContent();
}

void UInfoScreenWidget::ApplyHeadingFontSize(int32 FontSize)
{
    const float Scale = FontSize / 34.0f;
    Title->SetFont(FCoreStyle::GetDefaultFontStyle("Regular", FontSize));
    Detail->SetFont(FCoreStyle::GetDefaultFontStyle("Regular", FMath::Max(1, FMath::RoundToInt(23.0f * Scale))));
    MainIconBox->SetWidthOverride(112.0f * Scale);
    MainIconBox->SetHeightOverride(112.0f * Scale);
    CastChecked<UHorizontalBoxSlot>(MainIconBox->Slot)->SetPadding(FMargin(0.0f, 0.0f, 20.0f * Scale, 0.0f));
    CastChecked<UVerticalBoxSlot>(Detail->Slot)->SetPadding(FMargin(0.0f, 6.0f * Scale, 0.0f, 0.0f));
}

void UInfoScreenWidget::ApplySummaryFontSize(int32 FontSize)
{
    const float Scale = FontSize / 24.0f;
    const FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle("Regular", FontSize);
    FillText->SetFont(Font);
    Footer->SetFont(Font);
    Status->SetFont(Font);
    CastChecked<UVerticalBoxSlot>(FillText->Slot)->SetPadding(FMargin(0.0f, 8.0f * Scale, 0.0f, 4.0f * Scale));
    CastChecked<UVerticalBoxSlot>(Footer->Slot)->SetPadding(FMargin(0.0f, 12.0f * Scale, 0.0f, 6.0f * Scale));
    BarBox->SetHeightOverride(18.0f * Scale);
}

void UInfoScreenWidget::ApplyItemFontSize(int32 FontSize)
{
    const float Scale = FontSize / 24.0f;
    const FSlateFontInfo DataFont = FCoreStyle::GetDefaultFontStyle("Regular", FontSize);
    for (int32 Index = 0; Index < Snapshot.Rows.Num(); ++Index)
    {
        FInfoScreenMetricWidgets& Row = MetricRows[Index];
        Row.Label->SetFont(DataFont);
        Row.Amount->SetFont(DataFont);
        Row.Rate->SetFont(DataFont);
        Row.IconBox->SetWidthOverride(36.0f * Scale);
        Row.IconBox->SetHeightOverride(36.0f * Scale);
        CastChecked<UVerticalBoxSlot>(Row.Root->Slot)->SetPadding(FMargin(0.0f, 4.0f * Scale));
        CastChecked<UHorizontalBoxSlot>(Row.IconBox->Slot)->SetPadding(FMargin(0.0f, 0.0f, 10.0f * Scale, 0.0f));
        CastChecked<UHorizontalBoxSlot>(Row.Amount->Slot)->SetPadding(FMargin(8.0f * Scale, 0.0f));
        CastChecked<UHorizontalBoxSlot>(Row.Rate->Slot)->SetPadding(FMargin(12.0f * Scale, 0.0f, 0.0f, 0.0f));
    }
}

void UInfoScreenWidget::FitContent()
{
    // Independent width constraints prevent a long heading or footer from shrinking
    // the item list. Reserve only actual header/summary height, bounded to leave the
    // majority of the 972x728 content panel available for data.
    const float HeadingHeight = FitRegion(HeadingRegion, RequestedHeadingFontSize, 728.0f * 0.28f, 256,
        [this](int32 FontSize) { ApplyHeadingFontSize(FontSize); });
    const float SummaryHeight = FitRegion(SummaryRegion, 0, 728.0f * 0.24f, 24,
        [this](int32 FontSize) { ApplySummaryFontSize(FontSize); });
    FitRegion(ItemsRegion, RequestedFontSize, 728.0f - HeadingHeight - SummaryHeight, 256,
        [this](int32 FontSize) { ApplyItemFontSize(FontSize); }, true);
}

#undef LOCTEXT_NAMESPACE
