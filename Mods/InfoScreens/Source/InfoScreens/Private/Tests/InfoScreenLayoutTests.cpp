#include "InfoScreenSubsystem.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInfoScreenLayoutLimitsTest, "InfoScreens.Configuration.LayoutLimits",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FInfoScreenLayoutLimitsTest::RunTest(const FString& Parameters)
{
    FInfoScreenLayout Layout;
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
