#include "InfoScreenTypes.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "FGFluidIntegrantInterface.h"
#include "FGInventoryComponent.h"
#include "Misc/AutomationTest.h"

// Internal presentation functions are also used by the concrete authority readers.
namespace InfoScreenReader::Detail
{
    void ResetSnapshot(FInfoScreenSnapshot& Snapshot);
    bool InventoryReady(const UFGInventoryComponent* Inventory);
    void StorageSummary(FInfoScreenSnapshot& Snapshot, TSubclassOf<UFGItemDescriptor> Item,
        double Amount, int32 Occupied, int32 Slots, double Capacity, bool Mixed);
    void FluidSummary(FInfoScreenSnapshot& Snapshot, const FFluidBox& Box,
        TSubclassOf<UFGItemDescriptor> Item);
    void FreightFluidSummary(FInfoScreenSnapshot& Snapshot, TSubclassOf<UFGItemDescriptor> Item,
        double Amount, double Capacity);
    void BatterySummary(FInfoScreenSnapshot& Snapshot, double Stored, double Capacity,
        double InputMW, double OutputMW);
    double DisplayAmount(double Amount, EResourceForm Form);
    double ConfiguredRate(double Amount, double CycleSeconds, double ProductionBoost);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInfoScreenStorageSummaryTest, "InfoScreens.Reader.StorageSummary",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FInfoScreenStorageSummaryTest::RunTest(const FString& Parameters)
{
    using namespace InfoScreenReader::Detail;
    FInfoScreenSnapshot Snapshot;
    StorageSummary(Snapshot, nullptr, 0.0, 0, 24, 0.0, false);
    TestEqual(TEXT("Empty inventory reports slots, not zero item capacity"), Snapshot.Rows[0].Unit, EInfoScreenUnit::Slots);
    TestEqual(TEXT("Empty slot capacity"), Snapshot.Rows[0].Capacity, 24.0);
    TestEqual(TEXT("Empty fullness"), Snapshot.FillRatio, 0.0f);

    ResetSnapshot(Snapshot);
    // A real descriptor class, not an unrelated UClass cast into an item type.
    StorageSummary(Snapshot, UFGItemDescriptor::StaticClass(), 150.0, 2, 24, 2400.0, false);
    TestEqual(TEXT("Single-item quantity"), Snapshot.Rows[0].Amount, 150.0);
    TestEqual(TEXT("Single-item capacity uses item limits, not occupied slots"), Snapshot.Rows[0].Capacity, 2400.0);
    TestEqual(TEXT("Partially occupied single-item slots use item fullness"), Snapshot.FillRatio, 0.0625f);
    TestTrue(TEXT("Single-item icon resolves from descriptor"), Snapshot.Icon == UFGItemDescriptor::StaticClass());

    ResetSnapshot(Snapshot);
    StorageSummary(Snapshot, UFGItemDescriptor::StaticClass(), 150.0, 2, 24, 2400.0, true);
    TestEqual(TEXT("Mixed quantity is occupied slots"), Snapshot.Rows[0].Amount, 2.0);
    TestEqual(TEXT("Mixed unit explicitly describes slots"), Snapshot.Rows[0].Unit, EInfoScreenUnit::Slots);
    TestTrue(TEXT("Mixed contents must not inherit first-item icon"), Snapshot.Icon == nullptr);
    TestTrue(TEXT("Mixed fullness uses occupied slots"), FMath::IsNearlyEqual(Snapshot.FillRatio, 1.0f / 12.0f));

    ResetSnapshot(Snapshot);
    StorageSummary(Snapshot, nullptr, 0.0, 0, 0, 0.0, false);
    TestFalse(TEXT("Zero-slot source has no defined fullness"), Snapshot.bHasFill);
    TestTrue(TEXT("Zero-slot ratio stays finite"), FMath::IsFinite(Snapshot.FillRatio));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInfoScreenUnitsTest, "InfoScreens.Reader.UnitsAndRates",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FInfoScreenUnitsTest::RunTest(const FString& Parameters)
{
    using namespace InfoScreenReader::Detail;
    TestEqual(TEXT("Solids retain item units"), DisplayAmount(1500.0, EResourceForm::RF_SOLID), 1500.0);
    TestEqual(TEXT("Liquid inventory liters become cubic meters"), DisplayAmount(1500.0, EResourceForm::RF_LIQUID), 1.5);
    TestEqual(TEXT("Gas inventory liters become cubic meters"), DisplayAmount(1500.0, EResourceForm::RF_GAS), 1.5);
    TestEqual(TEXT("Output boost multiplies configured per-minute production"), ConfiguredRate(3.0, 4.0, 2.0), 90.0);
    TestEqual(TEXT("Input consumption does not inherit output amplification"), ConfiguredRate(3.0, 4.0, 1.0), 45.0);
    TestEqual(TEXT("Zero cycle duration cannot produce an infinite rate"), ConfiguredRate(3.0, 0.0, 2.0), 0.0);

    FFluidBox Box;
    Box.Content = 110.0f;
    Box.MaxContent = 100.0f;
    Box.FlowFill = 2.0f;
    Box.FlowDrain = 0.5f;
    FInfoScreenSnapshot Snapshot;
    FluidSummary(Snapshot, Box, nullptr);
    TestEqual(TEXT("Actual fluid content preserves overpressure"), Snapshot.Rows[0].Amount, 110.0);
    TestEqual(TEXT("Actual fluid maximum is not quantized UI content"), Snapshot.Rows[0].Capacity, 100.0);
    TestEqual(TEXT("Only display fill ratio is clamped"), Snapshot.FillRatio, 1.0f);
    TestEqual(TEXT("Fill cubic meters per second become per minute"), Snapshot.Rows[1].PerMinute, 120.0);
    TestEqual(TEXT("Drain cubic meters per second become per minute"), Snapshot.Rows[2].PerMinute, 30.0);
    TestFalse(TEXT("Inflow is a rate, not a fabricated zero inventory"), Snapshot.Rows[1].bHasAmount);
    TestFalse(TEXT("Outflow is a rate, not a fabricated zero inventory"), Snapshot.Rows[2].bHasAmount);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInfoScreenFreightFluidTest, "InfoScreens.Reader.FreightCargoTransitions",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FInfoScreenFreightFluidTest::RunTest(const FString& Parameters)
{
    using namespace InfoScreenReader::Detail;
    FInfoScreenSnapshot Snapshot;
    // Inventory quantities are liters, unlike a pipe reservoir's already-m³ FFluidBox.
    FreightFluidSummary(Snapshot, UFGItemDescriptor::StaticClass(), 800000.0, 1600000.0);
    TestEqual(TEXT("Freight liquid quantity converts to cubic meters"), Snapshot.Rows[0].Amount, 800.0);
    TestEqual(TEXT("Freight capacity converts to cubic meters"), Snapshot.Rows[0].Capacity, 1600.0);
    TestEqual(TEXT("Freight fluid is not displayed in slots"), Snapshot.Rows[0].Unit, EInfoScreenUnit::CubicMeters);
    TestEqual(TEXT("Half-full freight uses quantity, not occupied slots"), Snapshot.FillRatio, 0.5f);
    TestEqual(TEXT("Freight has no fabricated pipe flow rates"), Snapshot.Rows.Num(), 1);

    ResetSnapshot(Snapshot);
    FreightFluidSummary(Snapshot, nullptr, 0.0, 1600000.0);
    TestEqual(TEXT("Empty fluid freight retains actual volume capacity"), Snapshot.Rows[0].Capacity, 1600.0);
    TestEqual(TEXT("Empty fluid freight has zero volume"), Snapshot.Rows[0].Amount, 0.0);
    TestTrue(TEXT("Empty fluid freight clears cargo icon"), Snapshot.Icon == nullptr);
    TestEqual(TEXT("Empty fluid freight is not full"), Snapshot.FillRatio, 0.0f);

    ResetSnapshot(Snapshot);
    StorageSummary(Snapshot, UFGItemDescriptor::StaticClass(), 150.0, 2, 32, 3200.0, false);
    TestEqual(TEXT("Changing to solid cargo restores item counts"), Snapshot.Rows[0].Amount, 150.0);
    TestEqual(TEXT("Changing to solid cargo restores item capacity"), Snapshot.Rows[0].Capacity, 3200.0);
    TestEqual(TEXT("Changing to solid cargo restores item units"), Snapshot.Rows[0].Unit, EInfoScreenUnit::Items);

    ResetSnapshot(Snapshot);
    FreightFluidSummary(Snapshot, nullptr, 0.0, 0.0);
    TestFalse(TEXT("Uninitialized volume capacity has no fullness"), Snapshot.bHasFill);
    TestTrue(TEXT("Uninitialized volume capacity remains finite"), FMath::IsFinite(Snapshot.FillRatio));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInfoScreenBatteryTest, "InfoScreens.Reader.BatteryStatesAndETA",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FInfoScreenBatteryTest::RunTest(const FString& Parameters)
{
    using namespace InfoScreenReader::Detail;
    FInfoScreenSnapshot Snapshot;
    BatterySummary(Snapshot, 25.0, 100.0, 50.0, 0.0);
    TestEqual(TEXT("Stored energy is MWh"), Snapshot.Rows[0].Unit, EInfoScreenUnit::MegawattHours);
    TestEqual(TEXT("Charge power is MW"), Snapshot.Rows[1].Unit, EInfoScreenUnit::Megawatts);
    TestEqual(TEXT("75 MWh at 50 MW takes 5400 seconds"), Snapshot.Rows[2].Amount, 5400.0);
    const FText Charging = Snapshot.Status;

    ResetSnapshot(Snapshot);
    BatterySummary(Snapshot, 25.0, 100.0, 0.0, 50.0);
    TestEqual(TEXT("25 MWh at 50 MW takes 1800 seconds"), Snapshot.Rows[2].Amount, 1800.0);
    TestFalse(TEXT("Discharging is distinguishable from charging"), Snapshot.Status.EqualTo(Charging));

    ResetSnapshot(Snapshot);
    BatterySummary(Snapshot, 25.0, 100.0, 0.0, 0.0);
    TestEqual(TEXT("Idle battery omits undefined ETA"), Snapshot.Rows.Num(), 1);
    TestTrue(TEXT("Idle fullness is finite"), FMath::IsFinite(Snapshot.FillRatio));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInfoScreenLifecycleTest, "InfoScreens.Reader.Lifecycle",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FInfoScreenLifecycleTest::RunTest(const FString& Parameters)
{
    using namespace InfoScreenReader::Detail;
    UFGInventoryComponent* Inventory = NewObject<UFGInventoryComponent>();
    TestFalse(TEXT("A constructed inventory is not yet safe to sample"), InventoryReady(Inventory));
    TestFalse(TEXT("A missing inventory is not ready"), InventoryReady(nullptr));
    TestFalse(TEXT("A missing source is not supported"), InfoScreenReader::Supports(nullptr));

    FInfoScreenSnapshot Snapshot;
    BatterySummary(Snapshot, 25.0, 100.0, 50.0, 0.0);
    Snapshot.bHasPower = true;
    Snapshot.bHasProduction = true;
    Snapshot.PowerMW = 50.0;
    Snapshot.Icon = UFGItemDescriptor::StaticClass();
    TestFalse(TEXT("Missing source rejects a read"), InfoScreenReader::Read(nullptr, Snapshot));
    TestEqual(TEXT("Source loss clears old telemetry"), Snapshot.Rows.Num(), 0);
    TestEqual(TEXT("Source loss clears kind"), Snapshot.Kind, EInfoScreenKind::Unlinked);
    TestFalse(TEXT("Source loss clears power presence"), Snapshot.bHasPower);
    TestFalse(TEXT("Source loss clears production presence"), Snapshot.bHasProduction);
    TestFalse(TEXT("Source loss clears fullness presence"), Snapshot.bHasFill);
    TestTrue(TEXT("Source loss clears old item icon"), Snapshot.Icon == nullptr);
    return true;
}
#endif
