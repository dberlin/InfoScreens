#include "InfoScreenSubsystem.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInfoScreenLayoutLimitsTest, "InfoScreens.Configuration.LayoutLimits",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FInfoScreenLayoutLimitsTest::RunTest(const FString& Parameters)
{
    FInfoScreenLayout Layout;
    TestEqual(TEXT("Absent legacy font settings default to auto-fit"), Layout.FontSize, 0);
    TestEqual(TEXT("Absent legacy heading settings default to auto-fit"), Layout.HeadingFontSize, 0);
    TestTrue(TEXT("Auto-fit is a valid font setting"), AInfoScreenSubsystem::IsValidLayout(Layout));
    Layout.FontSize = 1;
    TestTrue(TEXT("Smallest manual font is allowed"), AInfoScreenSubsystem::IsValidLayout(Layout));
    Layout.FontSize = 256;
    TestTrue(TEXT("Largest manual font is allowed"), AInfoScreenSubsystem::IsValidLayout(Layout));
    Layout.FontSize = -1;
    TestFalse(TEXT("Negative font sizes are rejected"), AInfoScreenSubsystem::IsValidLayout(Layout));
    Layout.FontSize = 257;
    TestFalse(TEXT("Font sizes above the manual limit are rejected"), AInfoScreenSubsystem::IsValidLayout(Layout));
    Layout.FontSize = 0;
    Layout.HeadingFontSize = 1;
    TestTrue(TEXT("Manual heading font with auto-fit items is allowed"), AInfoScreenSubsystem::IsValidLayout(Layout));
    Layout.HeadingFontSize = 256;
    Layout.FontSize = 48;
    TestTrue(TEXT("Largest manual heading font with independent manual items is allowed"), AInfoScreenSubsystem::IsValidLayout(Layout));
    Layout.HeadingFontSize = -1;
    TestFalse(TEXT("Negative heading font is rejected independently of items"), AInfoScreenSubsystem::IsValidLayout(Layout));
    Layout.HeadingFontSize = 257;
    TestFalse(TEXT("Oversized heading font is rejected independently of items"), AInfoScreenSubsystem::IsValidLayout(Layout));
    Layout.HeadingFontSize = 0;
    TestTrue(TEXT("Auto-fit heading with manual items is allowed"), AInfoScreenSubsystem::IsValidLayout(Layout));
    Layout.FontSize = 0;

    Layout.Front = FTransform(FRotator(15.0, 90.0, -30.0), FVector(10000.0, -10000.0, 0.0), FVector(0.01, 20.0, 1.0));
    TestTrue(TEXT("Boundary positions and nonuniform positive scales are allowed"), AInfoScreenSubsystem::IsValidLayout(Layout));

    Layout.Front.SetLocation(FVector(10000.01, 0.0, 0.0));
    TestFalse(TEXT("A remote panel cannot be moved outside the allowed edit volume"), AInfoScreenSubsystem::IsValidLayout(Layout));
    Layout.Front.SetLocation(FVector::ZeroVector);
    Layout.Back.SetScale3D(FVector(1.0, 0.0, 1.0));
    TestFalse(TEXT("Zero scale cannot create an uneditable invisible panel"), AInfoScreenSubsystem::IsValidLayout(Layout));
    Layout.Back.SetScale3D(FVector(1.0, -1.0, 1.0));
    TestFalse(TEXT("Negative scale cannot invert interaction and rendering"), AInfoScreenSubsystem::IsValidLayout(Layout));
    Layout.Back.SetScale3D(FVector(20.001, 1.0, 1.0));
    TestFalse(TEXT("Oversized back panel is rejected independently of front panel"), AInfoScreenSubsystem::IsValidLayout(Layout));
    Layout.bBackVisible = false;
    TestFalse(TEXT("Hiding a panel does not bypass validation of its saved transform"), AInfoScreenSubsystem::IsValidLayout(Layout));
    return true;
}
#endif
