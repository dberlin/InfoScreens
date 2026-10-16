#include "InfoBlueprintReader.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Buildables/FGBuildableManufacturer.h"
#include "FGRecipe.h"
#include "Misc/AutomationTest.h"
#include "Resources/FGResourceDescriptor.h"
#include "UObject/UnrealType.h"
#include <initializer_list>

namespace InfoBlueprintReader::Detail
{
    bool AccumulateRecipe(const UFGRecipe* Recipe, bool Outputs, double ManufacturingSpeed,
        double Potential, double ProductionBoost, FInfoScreenSnapshot& Snapshot);
    void FinishSnapshot(FInfoScreenSnapshot& Snapshot, int32 Machines, bool Outputs);
}

namespace
{
    UFGRecipe* MakeRecipe(double Seconds, std::initializer_list<FItemAmount> Inputs,
        std::initializer_list<FItemAmount> Outputs)
    {
        UFGRecipe* Recipe = NewObject<UFGRecipe>();
        // These are isolated transient recipe instances, never game recipe defaults.
        const_cast<TArray<FItemAmount>&>(Recipe->GetIngredients()).Append(Inputs.begin(), Inputs.size());
        const_cast<TArray<FItemAmount>&>(Recipe->GetProducts()).Append(Outputs.begin(), Outputs.size());
        FFloatProperty* Duration = FindFProperty<FFloatProperty>(UFGRecipe::StaticClass(), TEXT("mManufactoringDuration"));
        check(Duration);
        Duration->SetPropertyValue_InContainer(Recipe, Seconds);
        return Recipe;
    }

    double RateFor(const FInfoScreenSnapshot& Snapshot, TSubclassOf<UFGItemDescriptor> Item)
    {
        const FInfoScreenRow* Row = Snapshot.Rows.FindByPredicate(
            [Item](const FInfoScreenRow& Candidate) { return Candidate.Item == Item; });
        return Row ? Row->PerMinute : -1.0;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInfoBlueprintAggregateTest, "InfoScreens.Blueprint.ConfiguredAggregation",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FInfoBlueprintAggregateTest::RunTest(const FString& Parameters)
{
    using namespace InfoBlueprintReader::Detail;
    const TSubclassOf<UFGItemDescriptor> Sulfur = UFGItemDescriptor::StaticClass();
    const TSubclassOf<UFGItemDescriptor> Coal = UFGResourceDescriptor::StaticClass();
    const UFGRecipe* Recipe = MakeRecipe(12.0, {{Sulfur, 5}, {Coal, 10}}, {{Coal, 3}});
    FInfoScreenSnapshot Snapshot;
    for (int32 Index = 0; Index < 4; ++Index)
    {
        TestTrue(TEXT("Recipe machine contributes without inventories or runtime productivity"),
            AccumulateRecipe(Recipe, false, 1.0, 1.0, 2.0, Snapshot));
    }
    FinishSnapshot(Snapshot, 4, false);
    TestEqual(TEXT("Four 25/min sulfur inputs total 100/min"), RateFor(Snapshot, Sulfur), 100.0);
    TestEqual(TEXT("Four 50/min coal inputs total 200/min"), RateFor(Snapshot, Coal), 200.0);
    TestEqual(TEXT("Exactly one row per descriptor"), Snapshot.Rows.Num(), 2);
    for (const FInfoScreenRow& Row : Snapshot.Rows)
    {
        TestFalse(TEXT("Configured rows do not fabricate inventory amounts"), Row.bHasAmount);
        TestFalse(TEXT("Configured rows do not fabricate inventory capacities"), Row.bHasCapacity);
        TestTrue(TEXT("Configured rows expose rates"), Row.bHasRate);
    }

    Snapshot.Rows.Reset();
    AccumulateRecipe(Recipe, true, 1.0, 1.0, 2.0, Snapshot);
    TestEqual(TEXT("Output selection excludes ingredients"), Snapshot.Rows.Num(), 1);
    TestEqual(TEXT("Outputs include configured amplification"), RateFor(Snapshot, Coal), 30.0);
    Snapshot.Rows.Reset();
    AccumulateRecipe(Recipe, false, 2.0, 1.5, 4.0, Snapshot);
    TestEqual(TEXT("Manufacturing speed and target clock scale inputs without amplification"), RateFor(Snapshot, Sulfur), 75.0);
    Snapshot.Rows.Reset();
    AccumulateRecipe(Recipe, true, 2.0, 1.5, 4.0, Snapshot);
    TestEqual(TEXT("Target clock and target amplification scale outputs"), RateFor(Snapshot, Coal), 180.0);

    const UFGRecipe* OtherRecipe = MakeRecipe(60.0, {{Coal, 7}, {Sulfur, 11}, {Coal, 13}}, {});
    Snapshot.Rows.Reset();
    AccumulateRecipe(Recipe, false, 1.0, 1.0, 1.0, Snapshot);
    AccumulateRecipe(OtherRecipe, false, 1.0, 1.0, 1.0, Snapshot);
    FinishSnapshot(Snapshot, 2, false);
    TestEqual(TEXT("Same item across recipes and ingredient positions groups together"), RateFor(Snapshot, Coal), 70.0);
    TestEqual(TEXT("Different recipes retain each item's total"), RateFor(Snapshot, Sulfur), 36.0);
    const TSubclassOf<UFGItemDescriptor> First = Snapshot.Rows[0].Item;
    Snapshot.Rows.Reset();
    AccumulateRecipe(OtherRecipe, false, 1.0, 1.0, 1.0, Snapshot);
    AccumulateRecipe(Recipe, false, 1.0, 1.0, 1.0, Snapshot);
    FinishSnapshot(Snapshot, 2, false);
    TestTrue(TEXT("Membership order does not alter row order"), Snapshot.Rows[0].Item == First);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInfoBlueprintFluidTest, "InfoScreens.Blueprint.FluidUnits",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FInfoBlueprintFluidTest::RunTest(const FString& Parameters)
{
    using namespace InfoBlueprintReader::Detail;
    UFGItemDescriptor* Descriptor = GetMutableDefault<UFGItemDescriptor>();
    FEnumProperty* FormProperty = FindFProperty<FEnumProperty>(UFGItemDescriptor::StaticClass(), TEXT("mForm"));
    check(FormProperty);
    EResourceForm* Form = FormProperty->ContainerPtrToValuePtr<EResourceForm>(Descriptor);
    TGuardValue<EResourceForm> RestoreForm(*Form, EResourceForm::RF_LIQUID);
    const UFGRecipe* Recipe = MakeRecipe(6.0, {{UFGItemDescriptor::StaticClass(), 1500}}, {{UFGItemDescriptor::StaticClass(), 2000}});
    FInfoScreenSnapshot Snapshot;
    AccumulateRecipe(Recipe, false, 1.0, 1.0, 1.0, Snapshot);
    TestEqual(TEXT("Liquid liters convert to cubic meters per minute"), Snapshot.Rows[0].PerMinute, 15.0);
    TestEqual(TEXT("Liquid unit is cubic meters"), Snapshot.Rows[0].Unit, EInfoScreenUnit::CubicMeters);
    *Form = EResourceForm::RF_GAS;
    Snapshot.Rows.Reset();
    AccumulateRecipe(Recipe, true, 1.0, 1.0, 2.0, Snapshot);
    TestEqual(TEXT("Gas liters convert before output amplification"), Snapshot.Rows[0].PerMinute, 40.0);
    TestEqual(TEXT("Gas unit is cubic meters"), Snapshot.Rows[0].Unit, EInfoScreenUnit::CubicMeters);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInfoBlueprintEmptyTest, "InfoScreens.Blueprint.EmptyAndInvalidMembership",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FInfoBlueprintEmptyTest::RunTest(const FString& Parameters)
{
    using namespace InfoBlueprintReader::Detail;
    FInfoScreenSnapshot Snapshot;
    Snapshot.Rows.AddDefaulted();
    Snapshot.bHasPower = Snapshot.bHasProduction = Snapshot.bHasFill = true;
    Snapshot.Icon = UFGItemDescriptor::StaticClass();
    InfoBlueprintReader::Read({}, false, Snapshot);
    TestEqual(TEXT("Empty membership is still a blueprint snapshot"), Snapshot.Kind, EInfoScreenKind::Blueprint);
    TestEqual(TEXT("No membership means no rates"), Snapshot.Rows.Num(), 0);
    TestFalse(TEXT("Old source telemetry clears"), Snapshot.bHasPower || Snapshot.bHasProduction || Snapshot.bHasFill);
    TestTrue(TEXT("Old source icon clears"), Snapshot.Icon == nullptr);
    InfoBlueprintReader::Read({nullptr, GetMutableDefault<AFGBuildableManufacturer>()}, true, Snapshot);
    TestEqual(TEXT("Null and non-live class defaults cannot contribute"), Snapshot.Rows.Num(), 0);
    TestFalse(TEXT("Missing recipes cannot contribute"), AccumulateRecipe(nullptr, false, 1.0, 1.0, 1.0, Snapshot));
    const UFGRecipe* InvalidCycle = MakeRecipe(0.0, {{UFGItemDescriptor::StaticClass(), 10}}, {});
    TestFalse(TEXT("Invalid duration cannot produce infinite rates"), AccumulateRecipe(InvalidCycle, false, 1.0, 1.0, 1.0, Snapshot));
    TestEqual(TEXT("Invalid recipes leave no fabricated rates"), Snapshot.Rows.Num(), 0);
    const UFGRecipe* Recipe = MakeRecipe(60.0, {{nullptr, 1}, {UFGItemDescriptor::StaticClass(), 10}},
        {{UFGItemDescriptor::StaticClass(), 5}});
    TestFalse(TEXT("Zero manufacturing speed contributes no rate"), AccumulateRecipe(Recipe, false, 0.0, 1.0, 1.0, Snapshot));
    TestFalse(TEXT("Zero target potential contributes no rate"), AccumulateRecipe(Recipe, false, 1.0, 0.0, 1.0, Snapshot));
    TestFalse(TEXT("Zero output boost contributes no output rate"), AccumulateRecipe(Recipe, true, 1.0, 1.0, 0.0, Snapshot));
    TestTrue(TEXT("Inputs are independent of output boost"), AccumulateRecipe(Recipe, false, 1.0, 1.0, 0.0, Snapshot));
    TestEqual(TEXT("Invalid item descriptors are skipped"), Snapshot.Rows.Num(), 1);
    TestEqual(TEXT("Valid ingredients retain their configured rate"), Snapshot.Rows[0].PerMinute, 10.0);
    return true;
}
#endif
