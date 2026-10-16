#pragma once

#include "CoreMinimal.h"
#include "Resources/FGItemDescriptor.h"
#include "InfoScreenTypes.generated.h"

UENUM()
enum class EInfoScreenKind : uint8
{
    Unlinked, Storage, FluidBuffer, Manufacturer, Extractor, Generator, PowerStorage, Sink, Blueprint
};

UENUM()
enum class EInfoScreenUnit : uint8
{
    Items, Slots, CubicMeters, Megawatts, MegawattHours, Seconds, Points, Coupons, Percent
};

/** Numeric authority data; descriptor classes let each client resolve its own icons. */
USTRUCT()
struct INFOSCREENS_API FInfoScreenRow
{
    GENERATED_BODY()

    UPROPERTY() TSubclassOf<UFGItemDescriptor> Item;
    UPROPERTY() FText Label;
    UPROPERTY() double Amount = 0.0;
    UPROPERTY() double Capacity = 0.0;
    UPROPERTY() double PerMinute = 0.0;
    UPROPERTY() EInfoScreenUnit Unit = EInfoScreenUnit::Items;
    UPROPERTY() bool bHasAmount = true;
    UPROPERTY() bool bHasCapacity = false;
    UPROPERTY() bool bHasRate = false;
};

USTRUCT()
struct INFOSCREENS_API FInfoScreenSnapshot
{
    GENERATED_BODY()

    UPROPERTY() EInfoScreenKind Kind = EInfoScreenKind::Unlinked;
    UPROPERTY() FText Title;
    UPROPERTY() FText Detail;
    UPROPERTY() FText Status;
    UPROPERTY() TSubclassOf<UFGItemDescriptor> Icon;
    UPROPERTY() TArray<FInfoScreenRow> Rows;
    UPROPERTY() float FillRatio = 0.0f;
    UPROPERTY() bool bHasFill = false;
    UPROPERTY() double PowerMW = 0.0;
    UPROPERTY() double PowerCapacityMW = 0.0;
    UPROPERTY() double ClockPercent = 0.0;
    UPROPERTY() double ProductivityPercent = 0.0;
    UPROPERTY() bool bHasPower = false;
    UPROPERTY() bool bHasProduction = false;
};

USTRUCT()
struct INFOSCREENS_API FInfoScreenLayout
{
    GENERATED_BODY()

    UPROPERTY(SaveGame) FTransform Front = FTransform::Identity;
    UPROPERTY(SaveGame) FTransform Back = FTransform(FRotator(0.0, 180.0, 0.0));
    UPROPERTY(SaveGame) bool bBackVisible = true;
    UPROPERTY(SaveGame) bool bBlueprintOutputs = false;
};

USTRUCT()
struct INFOSCREENS_API FInfoScreenPreset
{
    GENERATED_BODY()

    UPROPERTY(SaveGame) TSubclassOf<class AFGBuildable> SourceClass;
    UPROPERTY(SaveGame) FInfoScreenLayout Layout;
};

/** Server-only adapters. No client inventory access, widgets, or render resources. */
namespace InfoScreenReader
{
    INFOSCREENS_API bool Supports(const class AFGBuildable* Source);
    INFOSCREENS_API bool Read(class AFGBuildable* Source, FInfoScreenSnapshot& OutSnapshot);
}
