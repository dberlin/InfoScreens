#include "InfoBlueprintReader.h"

#include "Buildables/FGBuildableManufacturer.h"
#include "FGRecipe.h"

#define LOCTEXT_NAMESPACE "InfoBlueprintReader"

namespace InfoScreenReader::Detail
{
    void ResetSnapshot(FInfoScreenSnapshot& Snapshot);
}

namespace InfoBlueprintReader::Detail
{
    bool AccumulateRecipe(const UFGRecipe* Recipe, bool Outputs, double ManufacturingSpeed,
        double Potential, double ProductionBoost, FInfoScreenSnapshot& Snapshot)
    {
        if (!IsValid(Recipe)) return false;
        const double Duration = Recipe->GetManufacturingDuration();
        const double Boost = Outputs ? ProductionBoost : 1.0;
        if (!FMath::IsFinite(Duration) || Duration <= 0.0
            || !FMath::IsFinite(ManufacturingSpeed) || ManufacturingSpeed <= 0.0
            || !FMath::IsFinite(Potential) || Potential <= 0.0
            || !FMath::IsFinite(Boost) || Boost <= 0.0) return false;

        // Configured throughput, independent of power, inventory, progress and productivity.
        // Pending values are the user's target settings, even while the machine is idle
        // or still finishing a cycle using its previous current settings.
        const double CyclesPerMinute = 60.0 * ManufacturingSpeed * Potential / Duration;
        const TArray<FItemAmount>& Items = Outputs ? Recipe->GetProducts() : Recipe->GetIngredients();
        for (const FItemAmount& Item : Items)
        {
            if (!IsValid(Item.ItemClass.Get()) || Item.Amount <= 0) continue;
            const EResourceForm Form = UFGItemDescriptor::GetForm(Item.ItemClass);
            const bool Fluid = Form == EResourceForm::RF_LIQUID || Form == EResourceForm::RF_GAS;
            const double Rate = Item.Amount * CyclesPerMinute * Boost * (Fluid ? 0.001 : 1.0);
            if (!FMath::IsFinite(Rate)) continue;
            FInfoScreenRow* Row = Snapshot.Rows.FindByPredicate(
                [&Item](const FInfoScreenRow& Candidate) { return Candidate.Item == Item.ItemClass; });
            if (!Row)
            {
                Row = &Snapshot.Rows.AddDefaulted_GetRef();
                Row->Item = Item.ItemClass;
                Row->Label = UFGItemDescriptor::GetItemName(Item.ItemClass);
                Row->Unit = Fluid ? EInfoScreenUnit::CubicMeters : EInfoScreenUnit::Items;
                Row->bHasAmount = false;
                Row->bHasRate = true;
            }
            Row->PerMinute += Rate;
        }
        return true;
    }

    void FinishSnapshot(FInfoScreenSnapshot& Snapshot, int32 Machines, bool Outputs)
    {
        // Descriptor package/name ordering is stable across membership iteration and
        // does not allocate full path strings for every sort comparison.
        Snapshot.Rows.Sort([](const FInfoScreenRow& Left, const FInfoScreenRow& Right)
        {
            const FName LeftPackage = Left.Item->GetOutermost()->GetFName();
            const FName RightPackage = Right.Item->GetOutermost()->GetFName();
            return LeftPackage == RightPackage
                ? Left.Item->GetFName().LexicalLess(Right.Item->GetFName())
                : LeftPackage.LexicalLess(RightPackage);
        });
        Snapshot.Kind = EInfoScreenKind::Blueprint;
        Snapshot.Title = LOCTEXT("Title", "Blueprint");
        Snapshot.Detail = LOCTEXT("Scope", "Configured recipe machines only; excludes extractors and generators");
        Snapshot.Status = Machines == 0 ? LOCTEXT("NoMachines", "No configured recipe machines")
            : Outputs ? FText::Format(LOCTEXT("Outputs", "Configured outputs · {0} recipe machines"), FText::AsNumber(Machines))
            : FText::Format(LOCTEXT("Inputs", "Configured inputs · {0} recipe machines"), FText::AsNumber(Machines));
        Snapshot.Icon = Snapshot.Rows.IsEmpty() ? nullptr : Snapshot.Rows[0].Item;
    }
}

void InfoBlueprintReader::Read(const TArray<AFGBuildable*>& Buildings, bool Outputs,
    FInfoScreenSnapshot& OutSnapshot)
{
    InfoScreenReader::Detail::ResetSnapshot(OutSnapshot);
    int32 Machines = 0;
    for (AFGBuildable* Building : Buildings)
    {
        if (!IsValid(Building) || Building->IsActorBeingDestroyed()
            || !Building->HasActorBegunPlay() || !Building->HasAuthority()
            || Building->GetNetMode() == NM_Client) continue;
        const AFGBuildableManufacturer* Machine = Cast<AFGBuildableManufacturer>(Building);
        if (!Machine) continue;
        const TSubclassOf<UFGRecipe> RecipeClass = Machine->GetCurrentRecipe();
        const UFGRecipe* Recipe = RecipeClass ? RecipeClass.GetDefaultObject() : nullptr;
        if (Detail::AccumulateRecipe(Recipe, Outputs, Machine->GetManufacturingSpeed(),
            Machine->GetPendingPotential(), Machine->GetPendingProductionBoost(), OutSnapshot)) ++Machines;
    }
    Detail::FinishSnapshot(OutSnapshot, Machines, Outputs);
}

#undef LOCTEXT_NAMESPACE
