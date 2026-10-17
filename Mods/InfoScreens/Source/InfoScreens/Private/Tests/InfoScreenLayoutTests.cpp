#include "InfoScreenSubsystem.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "InfoScreen.h"
#include "Buildables/FGCentralStorageContainer.h"
#include "Buildables/FGBuildableTrainPlatformCargo.h"
#include "Buildables/FGBuildableTrainPlatformEmpty.h"
#include "Buildables/FGBuildableRailroadStation.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "PhysicsEngine/BodySetup.h"
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
    if (!Platform->GetRootComponent())
    {
        USceneComponent* Root = NewObject<USceneComponent>(Platform);
        Platform->SetRootComponent(Root);
        Root->RegisterComponent();
    }
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
    TestTrue(TEXT("Solid panel stays centered laterally and vertically between its ports"),
        FMath::IsNearlyZero(Solid.Front.GetLocation().Y, 0.001)
        && FMath::IsNearlyEqual(Solid.Front.GetLocation().Z, 200.0, 0.001));
    // Measured LOD0 support projection, omitted by the asset's simple collision.
    const double SupportProjection = 20.6522216796875;
    const double SolidClearance = -595.0 - SupportProjection
        - Solid.Front.TransformPosition(FVector(-5.0, 0.0, 0.0)).X;
    TestTrue(TEXT("Solid frame clears the visible center support without floating away"),
        SolidClearance >= 0.0 && SolidClearance <= 2.0);
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
    TestTrue(TEXT("Liquid panel stays centered laterally and vertically between its ports"),
        FMath::IsNearlyZero(Fluid.Front.GetLocation().X, 0.001)
        && FMath::IsNearlyEqual(Fluid.Front.GetLocation().Z, 200.0, 0.001));
    // The liquid platform has a different rendered center housing.
    const double LiquidSupportProjection = 101.1025390625;
    const double FluidClearance = Fluid.Front.TransformPosition(FVector(-5.0, 0.0, 0.0)).Y
        - (825.0 + LiquidSupportProjection);
    TestTrue(TEXT("Liquid frame clears its visible center housing"),
        FluidClearance >= 0.0 && FluidClearance <= 2.0);
    TestFalse(TEXT("Liquid platform also defaults to one screen"), Fluid.bBackVisible);
    TestTrue(TEXT("Cargo platform is a supported inventory source"), InfoScreenReader::Supports(Platform));
    TestFalse(TEXT("An empty platform is not an inventory source"),
        InfoScreenReader::Supports(World->SpawnActor<AFGBuildableTrainPlatformEmpty>()));
    TestFalse(TEXT("A station is not an inventory source"),
        InfoScreenReader::Supports(World->SpawnActor<AFGBuildableRailroadStation>()));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInfoScreenStorageMountTest, "InfoScreens.Configuration.StorageMount",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FInfoScreenStorageMountTest::RunTest(const FString& Parameters)
{
    UWorld::InitializationValues Settings;
    Settings.AllowAudioPlayback(false).CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false);
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true,
        ERHIFeatureLevel::Num, &Settings);
    if (!TestNotNull(TEXT("Storage mounting world"), World)) return false;
    ON_SCOPE_EXIT { World->DestroyWorld(false); };

    for (const double Height : {400.0, 800.0})
    {
        AFGBuildableStorage* Storage = World->SpawnActor<AFGCentralStorageContainer>();
        if (!TestNotNull(TEXT("Concrete native storage fixture"), Storage)) return false;
        if (!Storage->GetRootComponent())
        {
            USceneComponent* Root = NewObject<USceneComponent>(Storage);
            Storage->SetRootComponent(Root);
            Root->RegisterComponent();
        }
        Storage->SetActorTransform(FTransform(FRotator(0, 37, 0), FVector(12000, -8000, 500)));
        UStaticMesh* Mesh = NewObject<UStaticMesh>(Storage);
        Mesh->CreateBodySetup();
        const auto AddHull = [&](const FBox& Bounds)
        {
            FKConvexElem& Hull = Mesh->GetBodySetup()->AggGeom.ConvexElems.AddDefaulted_GetRef();
            for (int32 Corner = 0; Corner < 8; ++Corner)
                Hull.VertexData.Add(FVector(
                    Corner & 1 ? Bounds.Max.X : Bounds.Min.X,
                    Corner & 2 ? Bounds.Max.Y : Bounds.Min.Y,
                    Corner & 4 ? Bounds.Max.Z : Bounds.Min.Z));
            Hull.UpdateElemBox();
        };
        AddHull(FBox(FVector(-230,-400,0), FVector(230,400,Height)));
        AddHull(FBox(FVector(-40,400,50), FVector(40,520,150)));
        AddHull(FBox(FVector(230,-20,50), FVector(260,20,60)));
        UStaticMeshComponent* Body = NewObject<UStaticMeshComponent>(Storage, TEXT("FGColoredInstanceMeshProxy"));
        Body->SetupAttachment(Storage->GetRootComponent());
        Body->SetStaticMesh(Mesh);
        Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Body->SetRelativeLocation(FVector(20,-15,30));
        Body->RegisterComponent();
        Body->SetHiddenInGame(true);

        for (const double Side : {-1.0, 1.0})
        {
            UBoxComponent* Ladder = NewObject<UBoxComponent>(Storage);
            Ladder->SetupAttachment(Storage->GetRootComponent());
            Ladder->SetBoxExtent(FVector(50,50,Height/2));
            Ladder->SetRelativeLocation(FVector(Side*300,0,Height/2));
            Ladder->SetHiddenInGame(true);
            Ladder->RegisterComponent();
        }
        const FInfoScreenLayout Layout = AInfoScreen::DefaultContainerLayout(Storage);
        const double FrontClearance = Layout.Front.TransformPosition(FVector(-5,0,0)).X - 250.0;
        const double BackClearance = -210.0 - Layout.Back.TransformPosition(FVector(-5,0,0)).X;
        TestTrue(TEXT("Front backing meets the body rather than ladders or small trim"),
            FrontClearance >= -0.01 && FrontClearance <= 2.0);
        TestTrue(TEXT("Back backing meets the same translated body"),
            BackClearance >= -0.01 && BackClearance <= 2.0);
        TestTrue(TEXT("Both storage heights use their main body's center"),
            FMath::IsNearlyEqual(Layout.Front.GetLocation().Y, -15.0, 0.01)
            && FMath::IsNearlyEqual(Layout.Front.GetLocation().Z, Height/2+30, 0.01)
            && FMath::IsNearlyEqual(Layout.Back.GetLocation().Z, Height/2+30, 0.01));
        TestTrue(TEXT("Both broad faces remain visible"), Layout.bBackVisible);
        TestTrue(TEXT("Rotating storage does not rotate its local layout"),
            Layout.Front.GetRotation().GetForwardVector().Equals(FVector(1,0,0), 0.001));
    }
    return true;
}
#endif
