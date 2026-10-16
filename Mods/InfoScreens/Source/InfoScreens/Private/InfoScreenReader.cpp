#include "InfoScreenTypes.h"

#include "Buildables/FGBuildableGenerator.h"
#include "Buildables/FGBuildableGeneratorFuel.h"
#include "Buildables/FGBuildableGeneratorNuclear.h"
#include "Buildables/FGBuildableManufacturer.h"
#include "Buildables/FGBuildablePipeReservoir.h"
#include "Buildables/FGBuildablePowerStorage.h"
#include "Buildables/FGBuildableResourceExtractor.h"
#include "Buildables/FGBuildableResourceSink.h"
#include "Buildables/FGBuildableStorage.h"
#include "FGFluidIntegrantInterface.h"
#include "FGInventoryComponent.h"
#include "FGPowerInfoComponent.h"
#include "FGRecipe.h"
#include "FGResourceSinkSubsystem.h"

#define LOCTEXT_NAMESPACE "InfoScreenReader"

namespace InfoScreenReader::Detail
{
    void ResetSnapshot(FInfoScreenSnapshot& Snapshot)
    {
        // Keep the allocated row buffer across periodic samples.
        Snapshot.Rows.Reset();
        Snapshot.Kind = EInfoScreenKind::Unlinked;
        Snapshot.Title = FText::GetEmpty();
        Snapshot.Detail = FText::GetEmpty();
        Snapshot.Status = FText::GetEmpty();
        Snapshot.Icon = nullptr;
        Snapshot.FillRatio = 0.0f;
        Snapshot.bHasFill = false;
        Snapshot.PowerMW = 0.0;
        Snapshot.PowerCapacityMW = 0.0;
        Snapshot.ClockPercent = 0.0;
        Snapshot.ProductivityPercent = 0.0;
        Snapshot.bHasPower = false;
        Snapshot.bHasProduction = false;
    }

    bool InventoryReady(const UFGInventoryComponent* Inventory)
    {
        // Do not sample before the inventory component has entered gameplay.
        // HasBegunPlay also becomes false after EndPlay.
        const AActor* Owner = IsValid(Inventory) ? Inventory->GetOwner() : nullptr;
        return IsValid(Owner) && Owner->HasAuthority() && Owner->GetNetMode() != NM_Client
            && !Owner->IsActorBeingDestroyed() && Inventory->HasBegunPlay()
            && !Inventory->IsBeingDestroyed();
    }

    double DisplayAmount(double Amount, EResourceForm Form)
    {
        return Form == EResourceForm::RF_LIQUID || Form == EResourceForm::RF_GAS
            ? Amount * 0.001 : Amount;
    }

    double ConfiguredRate(double Amount, double CycleSeconds, double ProductionBoost)
    {
        return CycleSeconds > 0.0 ? Amount * 60.0 * ProductionBoost / CycleSeconds : 0.0;
    }

    static EInfoScreenUnit ItemUnit(TSubclassOf<UFGItemDescriptor> Item)
    {
        const EResourceForm Form = Item ? UFGItemDescriptor::GetForm(Item) : EResourceForm::RF_SOLID;
        return Form == EResourceForm::RF_LIQUID || Form == EResourceForm::RF_GAS
            ? EInfoScreenUnit::CubicMeters : EInfoScreenUnit::Items;
    }

    static double ItemAmount(double Amount, TSubclassOf<UFGItemDescriptor> Item)
    {
        return Item ? DisplayAmount(Amount, UFGItemDescriptor::GetForm(Item)) : Amount;
    }

    static FInfoScreenRow& AddRow(FInfoScreenSnapshot& Snapshot, const FText& Label,
        double Amount, EInfoScreenUnit Unit, TSubclassOf<UFGItemDescriptor> Item = nullptr)
    {
        FInfoScreenRow& Row = Snapshot.Rows.AddDefaulted_GetRef();
        Row.Label = Label;
        Row.Amount = Amount;
        Row.Unit = Unit;
        Row.Item = Item;
        return Row;
    }

    static void SetFill(FInfoScreenSnapshot& Snapshot, double Amount, double Capacity)
    {
        Snapshot.bHasFill = Capacity > 0.0;
        Snapshot.FillRatio = Snapshot.bHasFill
            ? static_cast<float>(FMath::Clamp(Amount / Capacity, 0.0, 1.0)) : 0.0f;
    }

    void StorageSummary(FInfoScreenSnapshot& Snapshot, TSubclassOf<UFGItemDescriptor> Item,
        double Amount, int32 Occupied, int32 Slots, double Capacity, bool Mixed)
    {
        Snapshot.Kind = EInfoScreenKind::Storage;
        Snapshot.Icon = !Mixed && Occupied > 0 ? Item : nullptr;
        const bool SingleItem = !Mixed && Occupied > 0 && Item != nullptr;
        Snapshot.Status = Occupied == 0 ? LOCTEXT("Empty", "Empty")
            : Mixed ? LOCTEXT("Mixed", "Mixed items") : LOCTEXT("Stored", "Stored items");
        if (SingleItem)
        {
            FInfoScreenRow& Row = AddRow(Snapshot, UFGItemDescriptor::GetItemName(Item),
                ItemAmount(Amount, Item), ItemUnit(Item), Item);
            Row.Capacity = ItemAmount(Capacity, Item);
            Row.bHasCapacity = true;
            SetFill(Snapshot, Amount, Capacity);
        }
        else
        {
            FInfoScreenRow& Row = AddRow(Snapshot, LOCTEXT("OccupiedSlots", "Occupied slots"),
                Occupied, EInfoScreenUnit::Slots);
            Row.Capacity = Slots;
            Row.bHasCapacity = true;
            SetFill(Snapshot, Occupied, Slots);
        }
    }

    void FluidSummary(FInfoScreenSnapshot& Snapshot, const FFluidBox& Box,
        TSubclassOf<UFGItemDescriptor> Item)
    {
        Snapshot.Kind = EInfoScreenKind::FluidBuffer;
        Snapshot.Icon = Item;
        if (Item) Snapshot.Detail = UFGItemDescriptor::GetItemName(Item);
        Snapshot.Status = Box.Content > 0.0f ? LOCTEXT("FluidStored", "Fluid stored") : LOCTEXT("Empty", "Empty");
        FInfoScreenRow& Content = AddRow(Snapshot, LOCTEXT("FluidContent", "Fluid content"),
            Box.Content, EInfoScreenUnit::CubicMeters, Item);
        Content.Capacity = Box.MaxContent;
        Content.bHasCapacity = true;
        SetFill(Snapshot, Box.Content, Box.MaxContent);
        FInfoScreenRow& Fill = AddRow(Snapshot, LOCTEXT("FluidInflow", "Inflow"), 0.0,
            EInfoScreenUnit::CubicMeters, Item);
        Fill.bHasAmount = false;
        Fill.bHasRate = true;
        Fill.PerMinute = Box.FlowFill * 60.0;
        FInfoScreenRow& Drain = AddRow(Snapshot, LOCTEXT("FluidOutflow", "Outflow"), 0.0,
            EInfoScreenUnit::CubicMeters, Item);
        Drain.bHasAmount = false;
        Drain.bHasRate = true;
        Drain.PerMinute = Box.FlowDrain * 60.0;
    }

    void BatterySummary(FInfoScreenSnapshot& Snapshot, double Stored, double Capacity,
        double InputMW, double OutputMW)
    {
        Snapshot.Kind = EInfoScreenKind::PowerStorage;
        FInfoScreenRow& Energy = AddRow(Snapshot, LOCTEXT("StoredEnergy", "Stored energy"),
            Stored, EInfoScreenUnit::MegawattHours);
        Energy.bHasCapacity = true;
        Energy.Capacity = Capacity;
        SetFill(Snapshot, Stored, Capacity);
        if (InputMW > 0.0)
        {
            Snapshot.Status = LOCTEXT("Charging", "Charging");
            AddRow(Snapshot, LOCTEXT("ChargePower", "Charging power"), InputMW, EInfoScreenUnit::Megawatts);
            AddRow(Snapshot, LOCTEXT("TimeFull", "Time until full"),
                FMath::Max(Capacity - Stored, 0.0) * 3600.0 / InputMW, EInfoScreenUnit::Seconds);
        }
        else if (OutputMW > 0.0)
        {
            Snapshot.Status = LOCTEXT("Discharging", "Discharging");
            AddRow(Snapshot, LOCTEXT("DischargePower", "Discharging power"), OutputMW, EInfoScreenUnit::Megawatts);
            AddRow(Snapshot, LOCTEXT("TimeEmpty", "Time until empty"),
                FMath::Max(Stored, 0.0) * 3600.0 / OutputMW, EInfoScreenUnit::Seconds);
        }
        else
        {
            Snapshot.Status = LOCTEXT("Idle", "Idle");
        }
    }

    static double CapacityForItem(const UFGInventoryComponent* Inventory,
        TSubclassOf<UFGItemDescriptor> Item)
    {
        double Capacity = 0.0;
        for (int32 Index = 0, Slots = Inventory->GetSizeLinear(); Index < Slots; ++Index)
        {
            if (Inventory->IsItemAllowed(Item, Index))
            {
                Capacity += FMath::Max(Inventory->GetSlotSize(Index, Item), 0);
            }
        }
        return Capacity;
    }

    static void ReadStorage(AFGBuildableStorage* Source, FInfoScreenSnapshot& Snapshot)
    {
        Snapshot.Kind = EInfoScreenKind::Storage;
        const UFGInventoryComponent* Inventory = Source->GetStorageInventory();
        if (!InventoryReady(Inventory))
        {
            Snapshot.Status = LOCTEXT("InventoryUnavailable", "Inventory unavailable");
            return;
        }
        TSubclassOf<UFGItemDescriptor> Item;
        int32 Occupied = 0;
        double Amount = 0.0;
        bool Mixed = false;
        const int32 Slots = Inventory->GetSizeLinear();
        FInventoryStack Stack;
        for (int32 Index = 0; Index < Slots; ++Index)
        {
            // The public accessor copies a single stack; no temporary array of the inventory.
            if (!Inventory->GetStackFromIndex(Index, Stack) || !Stack.HasItems())
            {
                continue;
            }
            ++Occupied;
            const TSubclassOf<UFGItemDescriptor> StackItem = Stack.Item.GetItemClass();
            if (!Item)
            {
                Item = StackItem;
            }
            else if (Item != StackItem)
            {
                Mixed = true;
            }
            Amount += Stack.NumItems;
        }
        const double Capacity = Item && !Mixed ? CapacityForItem(Inventory, Item) : 0.0;
        StorageSummary(Snapshot, Item, Amount, Occupied, Slots, Capacity, Mixed);
    }

    static void FactoryTelemetry(AFGBuildableFactory* Factory, FInfoScreenSnapshot& Snapshot)
    {
        Snapshot.ClockPercent = Factory->GetCurrentPotential() * 100.0;
        Snapshot.ProductivityPercent = Factory->GetProductivity() * 100.0;
        Snapshot.bHasProduction = true;
        Snapshot.Status = Factory->IsProductionPaused() ? LOCTEXT("Standby", "Standby")
            : Factory->IsProducing() ? LOCTEXT("Producing", "Producing") : LOCTEXT("Idle", "Idle");
        if (const UFGPowerInfoComponent* Power = Factory->GetPowerInfo())
        {
            Snapshot.bHasPower = true;
            Snapshot.PowerMW = Power->GetActualConsumption();
            Snapshot.PowerCapacityMW = Power->GetTargetConsumption();
        }
    }

    static FInfoScreenRow& InventoryItem(FInfoScreenSnapshot& Snapshot,
        const UFGInventoryComponent* Inventory, TSubclassOf<UFGItemDescriptor> Item, const FText& Role)
    {
        FInfoScreenRow& Row = AddRow(Snapshot,
            FText::Format(LOCTEXT("RoleItem", "{0}: {1}"), Role, UFGItemDescriptor::GetItemName(Item)),
            ItemAmount(Inventory->GetNumItems(Item), Item), ItemUnit(Item), Item);
        Row.bHasCapacity = true;
        Row.Capacity = ItemAmount(CapacityForItem(Inventory, Item), Item);
        return Row;
    }

    static void ReadManufacturer(AFGBuildableManufacturer* Source, FInfoScreenSnapshot& Snapshot)
    {
        Snapshot.Kind = EInfoScreenKind::Manufacturer;
        FactoryTelemetry(Source, Snapshot);
        const TSubclassOf<UFGRecipe> RecipeClass = Source->GetCurrentRecipe();
        if (!RecipeClass)
        {
            Snapshot.Status = LOCTEXT("NoRecipe", "No recipe selected");
            return;
        }
        const UFGRecipe* Recipe = RecipeClass.GetDefaultObject();
        Snapshot.Detail = Recipe->GetDisplayName();
        const UFGInventoryComponent* Input = Source->GetInputInventory();
        const UFGInventoryComponent* Output = Source->GetOutputInventory();
        const bool InputReady = InventoryReady(Input);
        const bool OutputReady = InventoryReady(Output);
        if ((!Recipe->GetIngredients().IsEmpty() && !InputReady)
            || (!Recipe->GetProducts().IsEmpty() && !OutputReady))
        {
            Snapshot.Status = LOCTEXT("InventoryUnavailable", "Inventory unavailable");
        }
        const double Cycle = Source->GetProductionCycleTime();
        if (InputReady)
        {
            for (const FItemAmount& Ingredient : Recipe->GetIngredients())
            {
                if (!Ingredient.ItemClass) continue;
                FInfoScreenRow& Row = InventoryItem(Snapshot, Input, Ingredient.ItemClass,
                    LOCTEXT("Input", "Input buffer (configured rate)"));
                Row.bHasRate = Cycle > 0.0;
                Row.PerMinute = ConfiguredRate(ItemAmount(Ingredient.Amount, Ingredient.ItemClass), Cycle, 1.0);
            }
        }
        if (OutputReady)
        {
            for (const FItemAmount& Product : Recipe->GetProducts())
            {
                if (!Product.ItemClass) continue;
                FInfoScreenRow& Row = InventoryItem(Snapshot, Output, Product.ItemClass,
                    LOCTEXT("Output", "Output buffer (configured rate)"));
                Row.bHasRate = Cycle > 0.0;
                Row.PerMinute = ConfiguredRate(ItemAmount(Product.Amount, Product.ItemClass), Cycle,
                    Source->GetCurrentProductionBoost());
                if (!Snapshot.Icon) Snapshot.Icon = Product.ItemClass;
            }
        }
    }

    static void ReadExtractor(AFGBuildableResourceExtractor* Source, FInfoScreenSnapshot& Snapshot)
    {
        Snapshot.Kind = EInfoScreenKind::Extractor;
        FactoryTelemetry(Source, Snapshot);
        const TScriptInterface<IFGExtractableResourceInterface> Resource = Source->GetExtractableResource();
        if (!IsValid(Resource.GetObject()) || !Resource.GetInterface())
        {
            Snapshot.Status = LOCTEXT("NoResource", "No resource source");
            return;
        }
        const TSubclassOf<UFGItemDescriptor> Item = Resource->GetResourceClass();
        if (!Item)
        {
            Snapshot.Status = LOCTEXT("NoResource", "No resource source");
            return;
        }
        Snapshot.Icon = Item;
        Snapshot.Detail = UFGItemDescriptor::GetItemName(Item);
        const UFGInventoryComponent* Output = Source->GetOutputInventory();
        if (!InventoryReady(Output))
        {
            Snapshot.Status = LOCTEXT("InventoryUnavailable", "Inventory unavailable");
            return;
        }
        FInfoScreenRow& Row = InventoryItem(Snapshot, Output, Item, LOCTEXT("ExtractedOutput", "Extracted output"));
        // This API already converts liquid/gas inventory units to m3/min.
        Row.bHasRate = true;
        Row.PerMinute = Source->GetExtractionPerMinute();
        if (!Resource->HasAnyResources()) Snapshot.Status = LOCTEXT("Depleted", "Resource depleted");
    }

    static void AppendInventory(FInfoScreenSnapshot& Snapshot, const UFGInventoryComponent* Inventory,
        const FText& Role, TSubclassOf<UFGItemDescriptor> Exclude = nullptr)
    {
        FInventoryStack Stack;
        for (int32 Index = 0, Slots = Inventory->GetSizeLinear(); Index < Slots; ++Index)
        {
            if (!Inventory->GetStackFromIndex(Index, Stack) || !Stack.HasItems()) continue;
            const TSubclassOf<UFGItemDescriptor> Item = Stack.Item.GetItemClass();
            if (Item == Exclude) continue;
            // Add one aggregate row per descriptor, even if it occupies several slots.
            if (Snapshot.Rows.ContainsByPredicate([Item](const FInfoScreenRow& Row) { return Row.Item == Item; })) continue;
            InventoryItem(Snapshot, Inventory, Item, Role);
            if (!Snapshot.Icon) Snapshot.Icon = Item;
        }
    }

    static void ReadGenerator(AFGBuildableGenerator* Source, FInfoScreenSnapshot& Snapshot)
    {
        Snapshot.Kind = EInfoScreenKind::Generator;
        Snapshot.Status = Source->IsProductionPaused() ? LOCTEXT("Standby", "Standby")
            : Source->IsProducing() ? LOCTEXT("Generating", "Generating") : LOCTEXT("Idle", "Idle");
        Snapshot.ClockPercent = Source->GetCurrentPotential() * 100.0;
        Snapshot.ProductivityPercent = Source->GetProductivity() * 100.0;
        Snapshot.bHasProduction = true;
        if (const UFGPowerInfoComponent* Power = Source->GetPowerInfo())
        {
            Snapshot.bHasPower = true;
            Snapshot.PowerMW = Power->GetBaseProduction() + Power->GetRegulatedDynamicProduction()
                + Power->GetActualProductionBoost();
            Snapshot.PowerCapacityMW = Source->GetPowerProductionCapacity();
        }
        AFGBuildableGeneratorFuel* FuelGenerator = Cast<AFGBuildableGeneratorFuel>(Source);
        if (!FuelGenerator) return;
        const UFGInventoryComponent* Fuel = FuelGenerator->GetFuelInventory();
        if (!InventoryReady(Fuel))
        {
            Snapshot.Status = LOCTEXT("InventoryUnavailable", "Inventory unavailable");
        }
        else
        {
            const TSubclassOf<UFGItemDescriptor> Supplemental = FuelGenerator->GetRequiresSupplementalResource()
                ? FuelGenerator->GetSupplementalResourceClass() : nullptr;
            AppendInventory(Snapshot, Fuel, LOCTEXT("FuelBuffer", "Fuel buffer"), Supplemental);
            const TSubclassOf<UFGItemDescriptor> CurrentFuel = FuelGenerator->GetCurrentFuelClass();
            if (CurrentFuel)
            {
                if (!Snapshot.Rows.ContainsByPredicate([CurrentFuel](const FInfoScreenRow& Row) { return Row.Item == CurrentFuel; }))
                    InventoryItem(Snapshot, Fuel, CurrentFuel, LOCTEXT("FuelBuffer", "Fuel buffer"));
                Snapshot.Icon = CurrentFuel;
                AddRow(Snapshot, LOCTEXT("FuelBurn", "Current fuel burn progress"),
                    FuelGenerator->GetFuelAmount() * 100.0, EInfoScreenUnit::Percent, CurrentFuel);
            }
            else if (Snapshot.Rows.IsEmpty())
            {
                AddRow(Snapshot, LOCTEXT("EmptyFuelBuffer", "Fuel buffer occupied slots"), 0.0, EInfoScreenUnit::Slots);
            }
            if (Supplemental)
            {
                FInfoScreenRow& Row = InventoryItem(Snapshot, Fuel, Supplemental, LOCTEXT("SupplementalBuffer", "Supplemental buffer"));
                Row.bHasRate = true;
                Row.PerMinute = FuelGenerator->GetSupplementalConsumptionRateCurrent() * 60.0;
                AddRow(Snapshot, LOCTEXT("LoadedSupplemental", "Loaded supplemental remaining"),
                    FuelGenerator->GetSupplementalAmount() * 100.0, EInfoScreenUnit::Percent, Supplemental);
            }
        }
        if (const AFGBuildableGeneratorNuclear* Nuclear = Cast<AFGBuildableGeneratorNuclear>(Source))
        {
            const UFGInventoryComponent* Waste = Nuclear->GetWasteInventory();
            if (InventoryReady(Waste))
            {
                const int32 PreviousRows = Snapshot.Rows.Num();
                AppendInventory(Snapshot, Waste, LOCTEXT("Waste", "Nuclear waste"));
                if (Snapshot.Rows.Num() == PreviousRows)
                {
                    FInfoScreenRow& Row = AddRow(Snapshot, LOCTEXT("WasteSlots", "Nuclear waste occupied slots"), 0.0, EInfoScreenUnit::Slots);
                    Row.bHasCapacity = true;
                    Row.Capacity = Waste->GetSizeLinear();
                }
            }
            else Snapshot.Status = LOCTEXT("WasteUnavailable", "Waste inventory unavailable");
        }
    }

    static void ReadSink(AFGBuildableResourceSink* Source, FInfoScreenSnapshot& Snapshot)
    {
        Snapshot.Kind = EInfoScreenKind::Sink;
        Snapshot.Detail = LOCTEXT("GlobalSink", "Global totals across all AWESOME Sinks");
        const AFGResourceSinkSubsystem* Sink = AFGResourceSinkSubsystem::Get(Source->GetWorld());
        if (!IsValid(Sink) || !Sink->HasActorBegunPlay())
        {
            Snapshot.Status = LOCTEXT("SinkUnavailable", "Global sink statistics unavailable");
            return;
        }
        Snapshot.Status = LOCTEXT("GlobalSinkStatus", "Global statistics");
        Snapshot.Icon = Sink->GetCouponClass();
        AddRow(Snapshot, LOCTEXT("Coupons", "Global available coupons"), Sink->GetNumCoupons(),
            EInfoScreenUnit::Coupons, Sink->GetCouponClass());
        for (const EResourceSinkTrack Track : {EResourceSinkTrack::RST_Default, EResourceSinkTrack::RST_Exploration})
        {
            const bool Exploration = Track == EResourceSinkTrack::RST_Exploration;
            AddRow(Snapshot, Exploration ? LOCTEXT("ExplorationTotal", "Global exploration total")
                : LOCTEXT("DefaultTotal", "Global default total"), Sink->GetNumTotalPoints(Track), EInfoScreenUnit::Points);
            AddRow(Snapshot, Exploration ? LOCTEXT("ExplorationRemaining", "Global exploration to next coupon")
                : LOCTEXT("DefaultRemaining", "Global default to next coupon"), Sink->GetNumPointsToNextCoupon(Track), EInfoScreenUnit::Points);
            AddRow(Snapshot, Exploration ? LOCTEXT("ExplorationProgress", "Global exploration coupon progress")
                : LOCTEXT("DefaultProgress", "Global default coupon progress"),
                Sink->GetProgressionTowardsNextCoupon(Track) * 100.0, EInfoScreenUnit::Percent);
        }
    }
}

bool InfoScreenReader::Supports(const AFGBuildable* Source)
{
    return IsValid(Source) && !Source->IsActorBeingDestroyed()
        && (Source->IsA<AFGBuildableStorage>() || Source->IsA<AFGBuildablePipeReservoir>()
            || Source->IsA<AFGBuildableManufacturer>() || Source->IsA<AFGBuildableResourceExtractor>()
            || Source->IsA<AFGBuildableGenerator>() || Source->IsA<AFGBuildablePowerStorage>()
            || Source->IsA<AFGBuildableResourceSink>());
}

bool InfoScreenReader::Read(AFGBuildable* Source, FInfoScreenSnapshot& OutSnapshot)
{
    using namespace Detail;
    ResetSnapshot(OutSnapshot);
    // Authority and net mode must be checked before even obtaining an inventory pointer.
    if (!IsValid(Source) || Source->GetNetMode() == NM_Client || !Source->HasAuthority()
        || !Supports(Source))
    {
        OutSnapshot.Status = LOCTEXT("SourceUnavailable", "Source unavailable");
        return false;
    }
    OutSnapshot.Title = Source->mDisplayName;
    if (!Source->HasActorBegunPlay())
    {
        OutSnapshot.Status = LOCTEXT("SourceNotReady", "Source not initialized");
        return true;
    }
    if (AFGBuildableStorage* Storage = Cast<AFGBuildableStorage>(Source)) ReadStorage(Storage, OutSnapshot);
    else if (AFGBuildablePipeReservoir* Reservoir = Cast<AFGBuildablePipeReservoir>(Source))
    {
        OutSnapshot.Kind = EInfoScreenKind::FluidBuffer;
        if (const FFluidBox* Box = Reservoir->GetFluidBox()) FluidSummary(OutSnapshot, *Box, Reservoir->GetFluidDescriptor());
        else OutSnapshot.Status = LOCTEXT("FluidUnavailable", "Fluid buffer unavailable");
    }
    else if (AFGBuildableManufacturer* Manufacturer = Cast<AFGBuildableManufacturer>(Source)) ReadManufacturer(Manufacturer, OutSnapshot);
    else if (AFGBuildableResourceExtractor* Extractor = Cast<AFGBuildableResourceExtractor>(Source)) ReadExtractor(Extractor, OutSnapshot);
    else if (AFGBuildableGenerator* Generator = Cast<AFGBuildableGenerator>(Source)) ReadGenerator(Generator, OutSnapshot);
    else if (AFGBuildablePowerStorage* Battery = Cast<AFGBuildablePowerStorage>(Source))
        BatterySummary(OutSnapshot, Battery->GetPowerStore(), Battery->GetPowerStoreCapacity(), Battery->GetPowerInput(), Battery->GetPowerOutput());
    else if (AFGBuildableResourceSink* Sink = Cast<AFGBuildableResourceSink>(Source)) ReadSink(Sink, OutSnapshot);
    return true;
}

#undef LOCTEXT_NAMESPACE
