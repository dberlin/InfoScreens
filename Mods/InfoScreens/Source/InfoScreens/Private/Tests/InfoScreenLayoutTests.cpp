#include "InfoScreenSubsystem.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "InfoScreen.h"
#include "Buildables/FGBuildableTrainPlatformCargo.h"
#include "Buildables/FGBuildableTrainPlatformEmpty.h"
#include "Buildables/FGBuildableRailroadStation.h"
#include "Components/BoxComponent.h"
#include "Engine/World.h"
#include "FGFactoryConnectionComponent.h"
#include "FGPipeConnectionComponent.h"
#include "Misc/ScopeExit.h"
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInfoScreenCargoPlatformLayoutTest, "InfoScreens.Configuration.CargoPlatformLayout",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FInfoScreenCargoPlatformLayoutTest::RunTest(const FString& Parameters)
{
    UWorld::InitializationValues Settings;
    Settings.AllowAudioPlayback(false).CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false);
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true,
        ERHIFeatureLevel::Num, &Settings);
    if (!TestNotNull(TEXT("Isolated platform layout world"), World)) return false;
    ON_SCOPE_EXIT { World->DestroyWorld(false); };

    AFGBuildableTrainPlatformCargo* Platform = World->SpawnActor<AFGBuildableTrainPlatformCargo>();
    if (!TestNotNull(TEXT("Native cargo platform"), Platform)) return false;
    Platform->SetActorTransform(FTransform(FRotator(0.0, 37.0, 0.0), FVector(12000.0, -8000.0, 500.0)));
    UBoxComponent* Body = NewObject<UBoxComponent>(Platform);
    Body->SetupAttachment(Platform->GetRootComponent());
    Body->SetBoxExtent(FVector(600.0, 800.0, 300.0));
    Body->SetRelativeLocation(FVector(10.0, 20.0, 300.0));
    Body->RegisterComponent();

    // Both input and output connector normals point OUT of the building. They must
    // select the -X side even when the building is rotated and has no belts attached.
    for (int32 Index = 0; Index < 2; ++Index)
    {
        UFGFactoryConnectionComponent* Port = NewObject<UFGFactoryConnectionComponent>(Platform);
        Port->SetupAttachment(Platform->GetRootComponent());
        Port->SetDirection(Index == 0 ? EFactoryConnectionDirection::FCD_INPUT
            : EFactoryConnectionDirection::FCD_OUTPUT);
        Port->SetRelativeLocationAndRotation(FVector(-595.0, Index == 0 ? -400.0 : 400.0,
            Index == 0 ? 100.0 : 300.0), FRotator(0.0, 180.0, 0.0));
        Port->RegisterComponent();
    }
    const FInfoScreenLayout Solid = AInfoScreen::DefaultContainerLayout(Platform);
    TestTrue(TEXT("Solid platform front faces outward on its actual I/O side"),
        Solid.Front.GetRotation().GetForwardVector().Equals(FVector(-1.0, 0.0, 0.0), 0.001));
    TestTrue(TEXT("Solid panel is centered between its I/O ports"),
        Solid.Front.GetLocation().Equals(FVector(-595.0, 0.0, 200.0), 0.001));
    TestTrue(TEXT("Solid frame backing meets the wall"),
        FMath::IsNearlyEqual(Solid.Front.TransformPosition(FVector(-5.0, 0.0, 0.0)).X, -590.0, 0.001));
    TestFalse(TEXT("Only one platform screen is visible by default"), Solid.bBackVisible);

    TInlineComponentArray<UFGFactoryConnectionComponent*> Conveyors(Platform);
    for (UFGFactoryConnectionComponent* Port : Conveyors)
        Port->DestroyComponent();
    // A liquid platform has pipe ports instead of conveyor ports. Snap-only
    // attachments are not cargo I/O and must not affect the chosen side.
    for (int32 Index = 0; Index < 3; ++Index)
    {
        UFGPipeConnectionComponent* Port = NewObject<UFGPipeConnectionComponent>(Platform);
        Port->SetupAttachment(Platform->GetRootComponent());
        Port->SetPipeConnectionType(Index == 2 ? EPipeConnectionType::PCT_SNAP_ONLY
            : Index == 0 ? EPipeConnectionType::PCT_CONSUMER : EPipeConnectionType::PCT_PRODUCER);
        Port->SetRelativeLocationAndRotation(FVector(Index == 0 ? -400.0 : 400.0, 825.0, 200.0),
            FRotator(0.0, Index == 2 ? -90.0 : 90.0, 0.0));
        Port->RegisterComponent();
    }
    const FInfoScreenLayout Fluid = AInfoScreen::DefaultContainerLayout(Platform);
    TestTrue(TEXT("Liquid platform front follows pipe I/O normals"),
        Fluid.Front.GetRotation().GetForwardVector().Equals(FVector(0.0, 1.0, 0.0), 0.001));
    TestTrue(TEXT("Liquid panel is centered between its I/O ports"),
        Fluid.Front.GetLocation().Equals(FVector(0.0, 825.0, 200.0), 0.001));
    TestTrue(TEXT("Liquid frame backing meets the wall"),
        FMath::IsNearlyEqual(Fluid.Front.TransformPosition(FVector(-5.0, 0.0, 0.0)).Y, 820.0, 0.001));
    TestFalse(TEXT("Liquid platform also defaults to one screen"), Fluid.bBackVisible);
    TestTrue(TEXT("Cargo platform is a supported inventory source"), InfoScreenReader::Supports(Platform));
    TestFalse(TEXT("An empty platform is not an inventory source"),
        InfoScreenReader::Supports(World->SpawnActor<AFGBuildableTrainPlatformEmpty>()));
    TestFalse(TEXT("A station is not an inventory source"),
        InfoScreenReader::Supports(World->SpawnActor<AFGBuildableRailroadStation>()));
    return true;
}
#endif
