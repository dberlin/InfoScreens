#include "InfoScreenTypes.h"
#include "InfoFactoryAccess.h"

#include "Buildables/FGBuildableGenerator.h"
#include "Buildables/FGBuildableGeneratorFuel.h"
#include "Buildables/FGBuildableGeneratorNuclear.h"
#include "Buildables/FGBuildableManufacturer.h"
#include "Buildables/FGBuildablePipeReservoir.h"
#include "Buildables/FGBuildablePowerStorage.h"
#include "Buildables/FGBuildableResourceExtractor.h"
#include "Buildables/FGBuildableResourceSink.h"
#include "Buildables/FGBuildableStorage.h"
#include "Buildables/FGBuildableTrainPlatformCargo.h"
#include "FGFluidIntegrantInterface.h"
#include "FGFreightWagon.h"
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

    void FreightFluidSummary(FInfoScreenSnapshot& Snapshot, TSubclassOf<UFGItemDescriptor> Item,
        double Amount, double Capacity)
    {
        Snapshot.Kind = EInfoScreenKind::FluidBuffer;
        Snapshot.Icon = Item;
        Snapshot.Status = Amount > 0.0 ? LOCTEXT("FluidStored", "Fluid stored") : LOCTEXT("Empty", "Empty");
        FInfoScreenRow& Content = AddRow(Snapshot,
            Item ? UFGItemDescriptor::GetItemName(Item) : LOCTEXT("FluidContent", "Fluid content"),
            DisplayAmount(Amount, EResourceForm::RF_LIQUID), EInfoScreenUnit::CubicMeters, Item);
        Content.Capacity = DisplayAmount(Capacity, EResourceForm::RF_LIQUID);
        Content.bHasCapacity = true;
        SetFill(Snapshot, Amount, Capacity);
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

    static void ReadInventory(const UFGInventoryComponent* Inventory, FInfoScreenSnapshot& Snapshot,
        const AFGFreightWagon* Freight = nullptr)
    {
        Snapshot.Kind = EInfoScreenKind::Storage;
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
        const EResourceForm Form = Freight && Item ? UFGItemDescriptor::GetForm(Item) : EResourceForm::RF_SOLID;
        const bool FluidCargo = Form == EResourceForm::RF_LIQUID || Form == EResourceForm::RF_GAS ||
            (!Item && Freight && Freight->GetFreightCargoType() == EFreightCargoType::FCT_Liquid);
        if (Freight && FluidCargo && !Mixed)
        {
            // A loaded wagon's slot overrides are the authoritative item capacity. When empty
            // but still typed liquid, no descriptor exists; use its configured scaled liters/slot.
            const double FluidCapacity = Item ? Capacity :
                static_cast<double>(Slots) * FMath::Max(Freight->GetScaledFluidStackSize(), 0);
            FreightFluidSummary(Snapshot, Item, Amount, FluidCapacity);
        }
        else
        {
            StorageSummary(Snapshot, Item, Amount, Occupied, Slots, Capacity, Mixed);
        }
    }

    static void FactoryTelemetry(AFGBuildableFactory* Factory, FInfoScreenSnapshot& Snapshot)
    {
        Snapshot.ClockPercent = InfoFactoryAccess::GetCurrentPotential(Factory) * 100.0;
        Snapshot.ProductivityPercent = InfoFactoryAccess::GetProductivity(Factory) * 100.0;
        Snapshot.bHasProduction = true;
        Snapshot.Status = InfoFactoryAccess::IsProductionPaused(Factory) ? LOCTEXT("Standby", "Standby")
            : InfoFactoryAccess::IsProducing(Factory) ? LOCTEXT("Producing", "Producing") : LOCTEXT("Idle", "Idle");
        if (const UFGPowerInfoComponent* Power = InfoFactoryAccess::GetPowerInfo(Factory))
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
        const TSubclassOf<UFGRecipe> RecipeClass = InfoFactoryAccess::GetCurrentRecipe(Source);
        if (!RecipeClass)
        {
            Snapshot.Status = LOCTEXT("NoRecipe", "No recipe selected");
            return;
        }
        const UFGRecipe* Recipe = RecipeClass.GetDefaultObject();
        Snapshot.Detail = Recipe->GetDisplayName();
        static const FObjectProperty* InputProperty = InfoFactoryAccess::RequireProperty<FObjectProperty>(AFGBuildableManufacturer::StaticClass(), TEXT("mInputInventory"));
        static const FObjectProperty* OutputProperty = InfoFactoryAccess::RequireProperty<FObjectProperty>(AFGBuildableManufacturer::StaticClass(), TEXT("mOutputInventory"));
        const UFGInventoryComponent* Input = Cast<UFGInventoryComponent>(InputProperty->GetObjectPropertyValue_InContainer(Source));
        const UFGInventoryComponent* Output = Cast<UFGInventoryComponent>(OutputProperty->GetObjectPropertyValue_InContainer(Source));
        const bool InputReady = InventoryReady(Input);
        const bool OutputReady = InventoryReady(Output);
        if ((!Recipe->GetIngredients().IsEmpty() && !InputReady)
            || (!Recipe->GetProducts().IsEmpty() && !OutputReady))
        {
            Snapshot.Status = LOCTEXT("InventoryUnavailable", "Inventory unavailable");
        }
        const double Cycle = InfoFactoryAccess::GetProductionCycleTime(Source);
        if (InputReady)
        {
            for (const FItemAmount& Ingredient : Recipe->GetIngredients())
            {
                if (!Ingredient.ItemClass) continue;
                FInfoScreenRow& Row = InventoryItem(Snapshot, Input, Ingredient.ItemClass,
                    LOCTEXT("Input", "Input buffer"));
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
                    LOCTEXT("Output", "Output buffer"));
                Row.bHasRate = Cycle > 0.0;
                Row.PerMinute = ConfiguredRate(ItemAmount(Product.Amount, Product.ItemClass), Cycle,
                    InfoFactoryAccess::GetCurrentProductionBoost(Source));
                if (!Snapshot.Icon) Snapshot.Icon = Product.ItemClass;
            }
        }
    }

    static void ReadExtractor(AFGBuildableResourceExtractor* Source, FInfoScreenSnapshot& Snapshot)
    {
        Snapshot.Kind = EInfoScreenKind::Extractor;
        FactoryTelemetry(Source, Snapshot);
        static const FObjectProperty* ResourceProperty = InfoFactoryAccess::RequireProperty<FObjectProperty>(AFGBuildableResourceExtractorBase::StaticClass(), TEXT("mExtractableResource"));
        const TScriptInterface<IFGExtractableResourceInterface> Resource(ResourceProperty->GetObjectPropertyValue_InContainer(Source));
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
        static UFunction* OutputFunction = AFGBuildableResourceExtractorBase::StaticClass()->FindFunctionByName(TEXT("GetOutputInventory"));
        const UFGInventoryComponent* Output = InfoFactoryAccess::InvokeGetter<UFGInventoryComponent*>(Source, OutputFunction);
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
        Snapshot.Status = InfoFactoryAccess::IsProductionPaused(Source) ? LOCTEXT("Standby", "Standby")
            : InfoFactoryAccess::IsProducing(Source) ? LOCTEXT("Generating", "Generating") : LOCTEXT("Idle", "Idle");
        Snapshot.ClockPercent = InfoFactoryAccess::GetCurrentPotential(Source) * 100.0;
        Snapshot.ProductivityPercent = InfoFactoryAccess::GetProductivity(Source) * 100.0;
        Snapshot.bHasProduction = true;
        if (const UFGPowerInfoComponent* Power = InfoFactoryAccess::GetPowerInfo(Source))
        {
            Snapshot.bHasPower = true;
            Snapshot.PowerMW = Power->GetBaseProduction() + Power->GetRegulatedDynamicProduction()
                + Power->GetActualProductionBoost();
            Snapshot.PowerCapacityMW = Source->GetPowerProductionCapacity();
        }
        AFGBuildableGeneratorFuel* FuelGenerator = Cast<AFGBuildableGeneratorFuel>(Source);
        if (!FuelGenerator) return;
        static const FObjectProperty* FuelProperty = InfoFactoryAccess::RequireProperty<FObjectProperty>(AFGBuildableGeneratorFuel::StaticClass(), TEXT("mFuelInventory"));
        const UFGInventoryComponent* Fuel = Cast<UFGInventoryComponent>(FuelProperty->GetObjectPropertyValue_InContainer(FuelGenerator));
        if (!InventoryReady(Fuel))
        {
            Snapshot.Status = LOCTEXT("InventoryUnavailable", "Inventory unavailable");
        }
        else
        {
            static const FBoolProperty* RequiresSupplemental = InfoFactoryAccess::RequireProperty<FBoolProperty>(AFGBuildableGeneratorFuel::StaticClass(), TEXT("mRequiresSupplementalResource"));
            static const FClassProperty* SupplementalClass = InfoFactoryAccess::RequireProperty<FClassProperty>(AFGBuildableGeneratorFuel::StaticClass(), TEXT("mSupplementalResourceClass"));
            const TSubclassOf<UFGItemDescriptor> Supplemental = RequiresSupplemental->GetPropertyValue_InContainer(FuelGenerator)
                ? Cast<UClass>(SupplementalClass->GetObjectPropertyValue_InContainer(FuelGenerator)) : nullptr;
            AppendInventory(Snapshot, Fuel, LOCTEXT("FuelBuffer", "Fuel buffer"), Supplemental);
            static const FClassProperty* FuelClass = InfoFactoryAccess::RequireProperty<FClassProperty>(AFGBuildableGeneratorFuel::StaticClass(), TEXT("mCurrentFuelClass"));
            const TSubclassOf<UFGItemDescriptor> CurrentFuel = Cast<UClass>(FuelClass->GetObjectPropertyValue_InContainer(FuelGenerator));
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
            static const FObjectProperty* WasteProperty = InfoFactoryAccess::RequireProperty<FObjectProperty>(AFGBuildableGeneratorNuclear::StaticClass(), TEXT("mOutputInventory"));
            const UFGInventoryComponent* Waste = Cast<UFGInventoryComponent>(WasteProperty->GetObjectPropertyValue_InContainer(Nuclear));
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

bool InfoScreenReader::Supports(const AActor* Source)
{
    return IsValid(Source) && !Source->IsActorBeingDestroyed()
        && (Source->IsA<AFGFreightWagon>() || Source->IsA<AFGBuildableStorage>() || Source->IsA<AFGBuildablePipeReservoir>()
            || Source->IsA<AFGBuildableTrainPlatformCargo>()
            || Source->IsA<AFGBuildableManufacturer>() || Source->IsA<AFGBuildableResourceExtractor>()
            || Source->IsA<AFGBuildableGenerator>() || Source->IsA<AFGBuildablePowerStorage>()
            || Source->IsA<AFGBuildableResourceSink>());
}

bool InfoScreenReader::Read(AActor* Source, FInfoScreenSnapshot& OutSnapshot)
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
    const AFGBuildable* Building = Cast<AFGBuildable>(Source);
    OutSnapshot.Title = Building ? Building->mDisplayName : LOCTEXT("FreightWagon", "Freight Car");
    if (!Source->HasActorBegunPlay())
    {
        OutSnapshot.Status = LOCTEXT("SourceNotReady", "Source not initialized");
        return true;
    }
    if (AFGFreightWagon* Freight = Cast<AFGFreightWagon>(Source))
        ReadInventory(Freight->GetFreightInventory(), OutSnapshot, Freight);
    else if (AFGBuildableStorage* Storage = Cast<AFGBuildableStorage>(Source))
    {
        static const FObjectProperty* Inventory = InfoFactoryAccess::RequireProperty<FObjectProperty>(AFGBuildableStorage::StaticClass(), TEXT("mStorageInventory"));
        ReadInventory(Cast<UFGInventoryComponent>(Inventory->GetObjectPropertyValue_InContainer(Storage)), OutSnapshot);
    }
    else if (AFGBuildableTrainPlatformCargo* Platform = Cast<AFGBuildableTrainPlatformCargo>(Source))
    {
        static const FObjectProperty* Inventory = InfoFactoryAccess::RequireProperty<FObjectProperty>(AFGBuildableTrainPlatformCargo::StaticClass(), TEXT("mInventory"));
        ReadInventory(Cast<UFGInventoryComponent>(Inventory->GetObjectPropertyValue_InContainer(Platform)), OutSnapshot);
    }
    else if (AFGBuildablePipeReservoir* Reservoir = Cast<AFGBuildablePipeReservoir>(Source))
    {
        OutSnapshot.Kind = EInfoScreenKind::FluidBuffer;
        static const FStructProperty* FluidBox = InfoFactoryAccess::RequireProperty<FStructProperty>(AFGBuildablePipeReservoir::StaticClass(), TEXT("mFluidBox"));
        FluidSummary(OutSnapshot, *FluidBox->ContainerPtrToValuePtr<FFluidBox>(Reservoir), Reservoir->GetFluidDescriptor());
    }
    else if (AFGBuildableManufacturer* Manufacturer = Cast<AFGBuildableManufacturer>(Source)) ReadManufacturer(Manufacturer, OutSnapshot);
    else if (AFGBuildableResourceExtractor* Extractor = Cast<AFGBuildableResourceExtractor>(Source)) ReadExtractor(Extractor, OutSnapshot);
    else if (AFGBuildableGenerator* Generator = Cast<AFGBuildableGenerator>(Source)) ReadGenerator(Generator, OutSnapshot);
    else if (AFGBuildablePowerStorage* Battery = Cast<AFGBuildablePowerStorage>(Source))
    {
        static const FFloatProperty* Capacity = InfoFactoryAccess::RequireProperty<FFloatProperty>(AFGBuildablePowerStorage::StaticClass(), TEXT("mPowerStoreCapacity"));
        BatterySummary(OutSnapshot, Battery->GetPowerStore(), Capacity->GetPropertyValue_InContainer(Battery), Battery->GetPowerInput(), Battery->GetPowerOutput());
    }
    else if (AFGBuildableResourceSink* Sink = Cast<AFGBuildableResourceSink>(Source)) ReadSink(Sink, OutSnapshot);
    return true;
}

#undef LOCTEXT_NAMESPACE
