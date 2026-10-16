#pragma once

#include "CoreMinimal.h"
#include "FGRecipe.h"
#include "FGSchematic.h"
#include "Hologram/FGBuildableHologram.h"
#include "Resources/FGBuildingDescriptor.h"
#include "Unlocks/FGUnlockRecipe.h"
#include "InfoScreenContent.generated.h"

/** A single-click, surface-mounted screen. Association is resolved by the subsystem. */
UCLASS()
class INFOSCREENS_API AInfoScreenHologram : public AFGBuildableHologram
{
    GENERATED_BODY()
public:
    AInfoScreenHologram();
    virtual bool IsValidHitResult(const FHitResult& Hit) const override;
    virtual bool TrySnapToActor(const FHitResult& Hit) override;
    virtual void SetHologramLocationAndRotation(const FHitResult& Hit) override;
protected:
    virtual USceneComponent* SetupComponent(USceneComponent* AttachParent, UActorComponent* ComponentTemplate,
        const FName& ComponentName, const FName& AttachSocketName) override;
};

/** Surface placement with a serialized, server-validated blueprint member selection. */
UCLASS()
class INFOSCREENS_API AInfoBlueprintScreenHologram : public AInfoScreenHologram
{
    GENERATED_BODY()
public:
    virtual bool IsValidHitResult(const FHitResult& Hit) const override;
    virtual void SetHologramLocationAndRotation(const FHitResult& Hit) override;
    virtual void SerializeConstructMessage(FArchive& Ar, FNetConstructionID Id) override;
protected:
    virtual void CheckValidPlacement() override;
    virtual void ConfigureActor(AFGBuildable* Buildable) const override;
private:
    bool CaptureBlueprintMember(const FHitResult& Hit, AFGBuildable*& OutAnchor,
        class AFGBlueprintProxy*& OutProxy, TSubclassOf<AFGBuildable>& OutLightweightClass,
        int32& OutLightweightIndex, FTransform& OutLightweightTransform) const;
    AFGBuildable* ResolveSelectedAnchor() const;
    UPROPERTY() TObjectPtr<AFGBuildable> SelectedAnchor;
    UPROPERTY() TObjectPtr<class AFGBlueprintProxy> SelectedProxy;
    UPROPERTY() TSubclassOf<AFGBuildable> SelectedLightweightClass;
    int32 SelectedLightweightIndex = INDEX_NONE;
    FTransform SelectedLightweightTransform;
};

UCLASS()
class INFOSCREENS_API UInfoScreenDescriptor : public UFGBuildingDescriptor
{
    GENERATED_BODY()
public:
    UInfoScreenDescriptor();
};

UCLASS()
class INFOSCREENS_API UInfoScreenLargeDescriptor : public UInfoScreenDescriptor
{
    GENERATED_BODY()
public:
    UInfoScreenLargeDescriptor();
};

UCLASS()
class INFOSCREENS_API UInfoBlueprintScreenDescriptor : public UInfoScreenDescriptor
{
    GENERATED_BODY()
public:
    UInfoBlueprintScreenDescriptor();
};

UCLASS()
class INFOSCREENS_API UInfoScreenRecipe : public UFGRecipe
{
    GENERATED_BODY()
public:
    UInfoScreenRecipe();
};

UCLASS()
class INFOSCREENS_API UInfoScreenLargeRecipe : public UFGRecipe
{
    GENERATED_BODY()
public:
    UInfoScreenLargeRecipe();
};

UCLASS()
class INFOSCREENS_API UInfoBlueprintScreenRecipe : public UFGRecipe
{
    GENERATED_BODY()
public:
    UInfoBlueprintScreenRecipe();
};

UCLASS()
class INFOSCREENS_API UInfoScreenRecipeUnlock : public UFGUnlockRecipe
{
    GENERATED_BODY()
public:
    UInfoScreenRecipeUnlock();
};

/** Register and auto-unlock this schematic from the native root world module. */
UCLASS()
class INFOSCREENS_API UInfoScreenSchematic : public UFGSchematic
{
    GENERATED_BODY()
public:
    UInfoScreenSchematic();
};
