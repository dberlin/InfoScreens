#include "InfoScreenWidget.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Blueprint/WidgetTree.h"
#include "Components/PanelWidget.h"
#include "Components/ScaleBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Framework/Application/SlateApplication.h"
#include "Layout/ArrangedChildren.h"
#include "Misc/AutomationTest.h"

namespace
{
    UTextBlock* FindText(UInfoScreenWidget* Widget, const FText& Text)
    {
        UTextBlock* Result = nullptr;
        Widget->WidgetTree->ForEachWidget([&](UWidget* Child)
        {
            if (UTextBlock* Block = Cast<UTextBlock>(Child); Block && Block->GetText().EqualTo(Text))
            {
                Result = Block;
            }
        });
        return Result;
    }

    FVector2D DesiredPanelSize(UInfoScreenWidget* Widget)
    {
        const TSharedRef<SWidget> SlateWidget = Widget->TakeWidget();
        SlateWidget->SlatePrepass(1.0f);
        return SlateWidget->GetDesiredSize();
    }

    bool RegionContentFits(UInfoScreenWidget* Widget, FName Name)
    {
        USizeBox* Area = Cast<USizeBox>(Widget->WidgetTree->FindWidget(Name));
        if (!Area)
        {
            return false;
        }
        UScaleBox* Scale = Cast<UScaleBox>(Area->GetContent());
        USizeBox* Width = Scale ? Cast<USizeBox>(Scale->GetContent()) : nullptr;
        if (!Width || !Width->GetContent() || !Width->GetContent()->GetCachedWidget().IsValid())
        {
            return false;
        }
        const FVector2D Size = Width->GetContent()->GetDesiredSize();
        return Size.X <= 972.0f && Size.Y <= Area->GetHeightOverride() + 0.01f;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInfoScreenWidgetFontFitTest, "InfoScreens.Rendering.FontFit",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FInfoScreenWidgetFontFitTest::RunTest(const FString& Parameters)
{
    if (!FSlateApplication::IsInitialized())
    {
        AddWarning(TEXT("Skipping client font fitting: Slate is unavailable in this process"));
        return true;
    }

    UInfoScreenWidget* Widget = NewObject<UInfoScreenWidget>();
    FInfoScreenSnapshot Snapshot;
    Snapshot.Kind = EInfoScreenKind::Blueprint;
    Snapshot.Title = FText::FromString(TEXT("Blueprint"));
    FInfoScreenRow Row;
    Row.Label = FText::FromString(TEXT("Iron"));
    Row.bHasAmount = true;
    Row.Amount = 12;
    Snapshot.Rows.Add(Row);
    Widget->SetSnapshot(Snapshot);
    // UWidget caches only weak Slate references. Keep the rendered hierarchy alive
    // across size/geometry assertions, just as the real widget component does.
    const TSharedRef<SWidget> RenderedWidget = Widget->TakeWidget();
    FVector2D Size = DesiredPanelSize(Widget);
    UTextBlock* Label = FindText(Widget, Row.Label);
    if (!TestNotNull(TEXT("The actual row label is present"), Label))
    {
        return false;
    }
    const int32 SparseFont = Label->GetFont().Size;
    TestTrue(TEXT("One-row blueprints grow beyond the former fixed font"), SparseFont > 24);
    TestTrue(TEXT("Auto-fit respects both panel dimensions"), Size.X <= 1024.01f && Size.Y <= 768.01f);

    if (SparseFont < 256)
    {
        Widget->SetSnapshot(Snapshot, SparseFont + 1);
        DesiredPanelSize(Widget);
        TestFalse(TEXT("The next larger item font exceeds its actual region"), RegionContentFits(Widget, TEXT("ItemListRegion")));
    }

    Widget->SetSnapshot(Snapshot, 64);
    TestEqual(TEXT("Manual font size is not silently auto-fitted"), Label->GetFont().Size, 64.0f);
    Widget->SetSnapshot(Snapshot);
    TestEqual(TEXT("Returning to auto-fit restores the fitting size"), Label->GetFont().Size, static_cast<float>(SparseFont));

    FInfoScreenRow Copper = Row;
    Copper.Label = FText::FromString(TEXT("Copper"));
    Copper.Amount = 6;
    Snapshot.Rows.Add(Copper);
    Widget->SetSnapshot(Snapshot);
    Size = DesiredPanelSize(Widget);
    TestTrue(TEXT("Two-row blueprints still grow beyond the former fixed font"), Label->GetFont().Size > 24);
    TestTrue(TEXT("Two-row blueprints fit both dimensions"), Size.X <= 1024.01f && Size.Y <= 768.01f);
    Snapshot.Rows.SetNum(1);

    Widget->SetSnapshot(Snapshot, 64, 32);
    UTextBlock* Heading = FindText(Widget, Snapshot.Title);
    if (!TestNotNull(TEXT("The heading is present"), Heading))
    {
        return false;
    }
    TestEqual(TEXT("Manual heading size is independent of manual items"), Heading->GetFont().Size, 32.0f);
    Widget->SetSnapshot(Snapshot, 64, 128);
    TestEqual(TEXT("Changing the heading preserves the selected item size"), Label->GetFont().Size, 64.0f);
    TestEqual(TEXT("Oversized manual headings are not silently auto-fitted"), Heading->GetFont().Size, 128.0f);
    Widget->SetSnapshot(Snapshot, 128, 32);
    TestEqual(TEXT("Changing items preserves the selected heading size"), Heading->GetFont().Size, 32.0f);

    Row.Label = FText::FromString(TEXT("Very long ingredient name with no truncation of its final characters"));
    Row.bHasRate = true;
    Row.PerMinute = 987654321.25;
    Row.bHasCapacity = true;
    Row.Capacity = 1234567890.75;
    Snapshot.Rows[0] = Row;
    Snapshot.Detail = FText::FromString(TEXT("All ingredients required"));
    Snapshot.Status = FText::FromString(TEXT("Waiting for materials"));
    Snapshot.bHasFill = true;
    Snapshot.FillRatio = 0.5f;
    Snapshot.bHasPower = true;
    Snapshot.PowerMW = 1234;
    Snapshot.PowerCapacityMW = 5678;
    Widget->SetSnapshot(Snapshot);
    Size = DesiredPanelSize(Widget);
    TestTrue(TEXT("Long labels, amounts, rates and ancillary content fit together"), Size.X <= 1024.01f && Size.Y <= 768.01f);
    TestTrue(TEXT("Long row contents fit their own actual region"), RegionContentFits(Widget, TEXT("ItemListRegion")));
    TestTrue(TEXT("Heading contents fit independently"), RegionContentFits(Widget, TEXT("HeadingRegion")));
    TestTrue(TEXT("Footer and status fit without constraining item widths"), RegionContentFits(Widget, TEXT("SummaryRegion")));
    Label = FindText(Widget, Row.Label);
    TestTrue(TEXT("Width-constrained content reduces the font"), Label && Label->GetFont().Size < SparseFont);
    if (Label)
    {
        const TSharedRef<SWidget> SlateRow = Label->GetParent()->TakeWidget();
        FArrangedChildren Children(EVisibility::Visible);
        SlateRow->ArrangeChildren(FGeometry::MakeRoot(FVector2D(972.0f, SlateRow->GetDesiredSize().Y),
            FSlateLayoutTransform()), Children);
        float PreviousRight = 0.0f;
        int32 TextColumns = 0;
        for (int32 Index = 0; Index < Children.Num(); ++Index)
        {
            const FArrangedWidget& Child = Children[Index];
            const float Left = Child.Geometry.GetAbsolutePosition().X;
            const float Right = Left + Child.Geometry.GetLocalSize().X;
            TestTrue(TEXT("Measured columns do not overlap"), Left >= PreviousRight);
            TestTrue(TEXT("Each allocated column contains its full text"),
                Child.Geometry.GetLocalSize().X + 0.01f >= Child.Widget->GetDesiredSize().X);
            PreviousRight = Right;
            ++TextColumns;
        }
        TestEqual(TEXT("Label, amount and rate all participate in the real Slate layout"), TextColumns, 3);
    }
    if (!Label)
    {
        return false;
    }

    const int32 WidthConstrainedItemFont = Label->GetFont().Size;
    const FText ShortTitle = Snapshot.Title;
    Snapshot.Title = FText::FromString(TEXT("An exceptionally long blueprint heading must fit independently without shrinking the ingredient list's width-constrained font"));
    Widget->SetSnapshot(Snapshot);
    DesiredPanelSize(Widget);
    TestEqual(TEXT("A long heading alone does not shrink width-constrained items"), Label->GetFont().Size, static_cast<float>(WidthConstrainedItemFont));
    Heading = FindText(Widget, Snapshot.Title);
    if (!TestNotNull(TEXT("The long heading remains present in full"), Heading))
    {
        return false;
    }
    const int32 FittingHeadingFont = Heading->GetFont().Size;
    TestTrue(TEXT("The long heading fits its independent region"), RegionContentFits(Widget, TEXT("HeadingRegion")));
    if (FittingHeadingFont < 256)
    {
        Widget->SetSnapshot(Snapshot, 0, FittingHeadingFont + 1);
        DesiredPanelSize(Widget);
        TestFalse(TEXT("The next larger heading font exceeds its region"), RegionContentFits(Widget, TEXT("HeadingRegion")));
    }
    Snapshot.Title = ShortTitle;

    Snapshot.Rows.Reset();
    for (int32 Index = 0; Index < 40; ++Index)
    {
        FInfoScreenRow Ingredient = Row;
        Ingredient.Label = FText::FromString(FString::Printf(TEXT("Ingredient %02d"), Index));
        Snapshot.Rows.Add(Ingredient);
    }
    Widget->SetSnapshot(Snapshot);
    Size = DesiredPanelSize(Widget);
    TestTrue(TEXT("Many-row blueprints fit vertically without dropping rows"), Size.X <= 1024.01f && Size.Y <= 768.01f);
    TestTrue(TEXT("All rows fit the actual available item height"), RegionContentFits(Widget, TEXT("ItemListRegion")));
    int32 VisibleLabels = 0;
    Widget->WidgetTree->ForEachWidget([&](UWidget* Child)
    {
        if (UTextBlock* Block = Cast<UTextBlock>(Child); Block && Block->GetText().ToString().StartsWith(TEXT("Ingredient "))
            && Block->GetParent()->GetVisibility() != ESlateVisibility::Collapsed)
        {
            ++VisibleLabels;
        }
    });
    TestEqual(TEXT("Every ingredient remains in the visible layout"), VisibleLabels, 40);

    Snapshot.Rows.Reset();
    Snapshot.Kind = EInfoScreenKind::Storage;
    Widget->SetSnapshot(Snapshot);
    Size = DesiredPanelSize(Widget);
    TestTrue(TEXT("Empty-row status and footer content also auto-fit"), Size.X <= 1024.01f && Size.Y <= 768.01f);
    return true;
}
#endif
