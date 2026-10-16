#include "InfoScreenContent.h"

#include "AbstractInstanceManager.h"
#include "Buildables/FGBuildableBlueprintDesigner.h"
#include "Components/BoxComponent.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "Equipment/FGBuildGun.h"
#include "FGCategory.h"
#include "FGBlueprintProxy.h"
#include "FGConstructDisqualifier.h"
#include "FGLightweightBuildableSubsystem.h"
#include "InfoBlueprintScreen.h"
#include "InfoScreen.h"
#include "Math/RotationMatrix.h"
#include "UObject/ConstructorHelpers.h"

#define LOCTEXT_NAMESPACE "InfoScreens"

AInfoScreenHologram::AInfoScreenHologram()
{
    SetNeedsValidFloor(false);
    mMaxPlacementFloorAngle = 90.0f;
}

bool AInfoScreenHologram::IsValidHitResult(const FHitResult& Hit) const
{
    return Hit.bBlockingHit && IsValid(Hit.GetActor()) && !Hit.ImpactNormal.IsNearlyZero()
        && !Hit.GetActor()->IsA<AInfoScreen>() && !Hit.GetActor()->IsA<AFGHologram>();
}

bool AInfoScreenHologram::TrySnapToActor(const FHitResult& Hit)
{
    // All surfaces use the same placement transform; no target-selection step is required.
    return false;
}

void AInfoScreenHologram::SetHologramLocationAndRotation(const FHitResult& Hit)
{
    const FVector Normal = Hit.ImpactNormal.GetSafeNormal();
    const FVector Up = FMath::Abs(Normal.Z) < 0.99f ? FVector::UpVector : FVector::ForwardVector;
    const FQuat SurfaceRotation = FRotationMatrix::MakeFromXZ(Normal, Up).ToQuat();
    const FQuat UserRotation(FVector::ForwardVector, FMath::DegreesToRadians(ApplyScrollRotationTo(0.0f)));
    // The original mesh occupies [-5, -1] cm in X, with its widget at X=0.
    SetActorLocationAndRotation(Hit.ImpactPoint + Normal * 6.0f, SurfaceRotation * UserRotation);
}

USceneComponent* AInfoScreenHologram::SetupComponent(USceneComponent* AttachParent, UActorComponent* ComponentTemplate,
    const FName& ComponentName, const FName& AttachSocketName)
{
    // Automatic-only faces and interaction boxes must not appear in the build preview.
    if (ComponentTemplate->ComponentHasTag(TEXT("InfoScreenSecondary")) || ComponentTemplate->IsA<UBoxComponent>())
    {
        return nullptr;
    }
    return Super::SetupComponent(AttachParent, ComponentTemplate, ComponentName, AttachSocketName);
}

bool AInfoBlueprintScreenHologram::CaptureBlueprintMember(const FHitResult& Hit, AFGBuildable*& OutAnchor,
    AFGBlueprintProxy*& OutProxy, TSubclassOf<AFGBuildable>& OutLightweightClass,
    int32& OutLightweightIndex, FTransform& OutLightweightTransform) const
{
    OutAnchor = Cast<AFGBuildable>(Hit.GetActor());
    OutProxy = nullptr;
    OutLightweightClass = nullptr;
    OutLightweightIndex = INDEX_NONE;
    if (!Super::IsValidHitResult(Hit))
        return false;
    if (!OutAnchor)
    {
        if (AAbstractInstanceManager* Manager = AAbstractInstanceManager::GetInstanceManager(GetWorld()))
        {
            FInstanceHandle Handle;
            if (Manager->ResolveHit(Hit, Handle))
            {
                OutAnchor = Cast<AFGBuildable>(AAbstractInstanceManager::GetOwnerByHandle(Handle));
                if (!OutAnchor)
                {
                    FLightweightBuildableInstanceRef Instance;
                    if (AFGLightweightBuildableSubsystem::ResolveLightweightInstance(Handle, Instance))
                        OutAnchor = Instance.SpawnTemporaryBuildable();
                }
            }
        }
    }
    if (!IsValid(OutAnchor) || OutAnchor->IsA<AInfoScreen>())
        return false;
    if (OutAnchor->GetIsLightweightTemporary())
    {
        FLightweightBuildableInstanceRef Instance;
        Instance.InitializeFromTemporary(OutAnchor);
        const FRuntimeBuildableInstanceData* Data = Instance.ResolveBuildableInstanceData();
        if (!Data || !IsValid(Data->BlueprintProxy))
            return false;
        OutProxy = Data->BlueprintProxy;
        OutLightweightClass = OutAnchor->GetClass();
        OutLightweightIndex = OutAnchor->GetRuntimeDataIndex();
        OutLightweightTransform = Data->Transform;
        OutAnchor = nullptr; // Local pooled actors must never be sent across the network.
        return true;
    }
    OutProxy = OutAnchor->GetBlueprintProxy();
    if (IsValid(OutProxy))
        return OutProxy->GetBuildables().Contains(OutAnchor);
    // Hit validation precedes moving the hologram into the designer; final placement checks its actual designer.
    AFGBuildableBlueprintDesigner* Designer = Cast<AFGBuildableBlueprintDesigner>(OutAnchor);
    if (!Designer)
        Designer = OutAnchor->GetBlueprintDesigner();
    return IsValid(Designer) && (OutAnchor == Designer
        || Designer->GetBuildablesInBlueprintDesigner().Contains(OutAnchor));
}

bool AInfoBlueprintScreenHologram::IsValidHitResult(const FHitResult& Hit) const
{
    AFGBuildable* Anchor = nullptr;
    AFGBlueprintProxy* Proxy = nullptr;
    TSubclassOf<AFGBuildable> LightweightClass;
    int32 Index = INDEX_NONE;
    FTransform Transform;
    return CaptureBlueprintMember(Hit, Anchor, Proxy, LightweightClass, Index, Transform);
}

void AInfoBlueprintScreenHologram::SetHologramLocationAndRotation(const FHitResult& Hit)
{
    Super::SetHologramLocationAndRotation(Hit);
    AFGBuildable* Anchor = nullptr;
    AFGBlueprintProxy* Proxy = nullptr;
    CaptureBlueprintMember(Hit, Anchor, Proxy, SelectedLightweightClass,
        SelectedLightweightIndex, SelectedLightweightTransform);
    SelectedAnchor = Anchor;
    SelectedProxy = Proxy;
}

void AInfoBlueprintScreenHologram::SerializeConstructMessage(FArchive& Ar, FNetConstructionID Id)
{
    Super::SerializeConstructMessage(Ar, Id);
    AFGBuildable* Anchor = SelectedAnchor;
    AFGBlueprintProxy* Proxy = SelectedProxy;
    UClass* LightweightClass = SelectedLightweightClass.Get();
    Ar << Anchor;
    Ar << Proxy;
    Ar << LightweightClass;
    Ar << SelectedLightweightIndex;
    Ar << SelectedLightweightTransform;
    if (Ar.IsLoading())
    {
        SelectedAnchor = Anchor;
        SelectedProxy = Proxy;
        SelectedLightweightClass = LightweightClass;
    }
}

AFGBuildable* AInfoBlueprintScreenHologram::ResolveSelectedAnchor() const
{
    if (SelectedLightweightClass && SelectedLightweightIndex != INDEX_NONE)
    {
        if (AFGLightweightBuildableSubsystem* Subsystem = AFGLightweightBuildableSubsystem::Get(GetWorld()))
        {
            FRuntimeBuildableInstanceData* Data = Subsystem->GetRuntimeDataForBuildableClassAndIndex(
                SelectedLightweightClass, SelectedLightweightIndex);
            // Runtime indices are reusable: validate the original transform as well as exact proxy.
            if (Data && Data->Transform.Equals(SelectedLightweightTransform) && IsValid(SelectedProxy)
                && Data->BlueprintProxy == SelectedProxy && SelectedProxy->GetWorld() == GetWorld())
            {
                bool Spawned = false;
                if (FInstanceToTemporaryBuildable* Temporary = Subsystem->FindOrSpawnBuildableForRuntimeData(
                    SelectedLightweightClass, Data, SelectedLightweightIndex, Spawned))
                    return Temporary->Buildable;
            }
        }
        return nullptr;
    }
    if (!IsValid(SelectedAnchor) || SelectedAnchor->GetWorld() != GetWorld()
        || SelectedAnchor->IsA<AInfoScreen>() || SelectedAnchor->GetIsLightweightTemporary())
        return nullptr;
    if (IsValid(SelectedProxy))
        return SelectedProxy->GetWorld() == GetWorld() && SelectedAnchor->GetBlueprintProxy() == SelectedProxy
            && SelectedProxy->GetBuildables().Contains(SelectedAnchor) ? SelectedAnchor.Get() : nullptr;
    AFGBuildableBlueprintDesigner* Designer = GetBlueprintDesigner();
    return IsValid(Designer) && (SelectedAnchor == Designer
        || (SelectedAnchor->GetBlueprintDesigner() == Designer
            && Designer->GetBuildablesInBlueprintDesigner().Contains(SelectedAnchor)))
        ? SelectedAnchor.Get() : nullptr;
}

void AInfoBlueprintScreenHologram::CheckValidPlacement()
{
    Super::CheckValidPlacement();
    if (!ResolveSelectedAnchor())
        AddConstructDisqualifier(UFGCDInvalidPlacement::StaticClass());
}

void AInfoBlueprintScreenHologram::ConfigureActor(AFGBuildable* Buildable) const
{
    Super::ConfigureActor(Buildable);
    if (AInfoBlueprintScreen* Screen = Cast<AInfoBlueprintScreen>(Buildable))
        Screen->SetBlueprintAnchor(ResolveSelectedAnchor());
}

UInfoScreenDescriptor::UInfoScreenDescriptor()
{
    mBuildableClass = AInfoScreen::StaticClass();
    mDisplayName = LOCTEXT("ScreenName", "Info Screen");
    mDescription = LOCTEXT("ScreenDescription", "A live information display for storage and production buildings. Automatically links to a nearby building.");
    mStackSize = EStackSize::SS_ONE;
    mMenuPriority = 100.0f;
    static ConstructorHelpers::FClassFinder<UFGCategory> Organization(
        TEXT("/Game/FactoryGame/Interface/UI/InGame/BuildMenu/BuildCategories/BC_Organization"));
    static ConstructorHelpers::FClassFinder<UFGCategory> Signs(
        TEXT("/Game/FactoryGame/Interface/UI/InGame/BuildMenu/BuildCategories/Sub_Organisation/SC_Signs"));
    mCategory = Organization.Class;
    mSubCategories.Add(Signs.Class);
    static ConstructorHelpers::FObjectFinder<UTexture2D> Icon(TEXT("/InfoScreens/UI/T_InfoScreenIcon.T_InfoScreenIcon"));
    mSmallIcon = Icon.Object;
    mPersistentBigIcon = Icon.Object;
}

UInfoScreenLargeDescriptor::UInfoScreenLargeDescriptor()
{
    mBuildableClass = AInfoScreenLarge::StaticClass();
    mDisplayName = LOCTEXT("LargeScreenName", "Big Info Screen");
    mMenuPriority = 101.0f;
}

UInfoBlueprintScreenDescriptor::UInfoBlueprintScreenDescriptor()
{
    mBuildableClass = AInfoBlueprintScreen::StaticClass();
    mDisplayName = LOCTEXT("BlueprintScreenName", "Blueprint Info Screen");
    mDescription = LOCTEXT("BlueprintScreenDescription", "Displays configured input or output rates for exactly one placed blueprint. Place against one of its buildings or foundations.");
    mMenuPriority = 102.0f;
}

UInfoScreenRecipe::UInfoScreenRecipe()
{
    mDisplayNameOverride = true;
    mDisplayName = LOCTEXT("ScreenName", "Info Screen");
    mManufactoringDuration = 1.0f;
    mProduct.Add(FItemAmount(UInfoScreenDescriptor::StaticClass(), 1));
    static ConstructorHelpers::FClassFinder<UFGItemDescriptor> ReinforcedPlate(
        TEXT("/Game/FactoryGame/Resource/Parts/IronPlateReinforced/Desc_IronPlateReinforced"));
    mIngredients.Add(FItemAmount(ReinforcedPlate.Class, 2));
    mProducedIn.Add(TSoftClassPtr<UObject>(AFGBuildGun::StaticClass()));
}

UInfoScreenLargeRecipe::UInfoScreenLargeRecipe()
{
    mDisplayNameOverride = true;
    mDisplayName = LOCTEXT("LargeScreenName", "Big Info Screen");
    mManufactoringDuration = 1.0f;
    mProduct.Add(FItemAmount(UInfoScreenLargeDescriptor::StaticClass(), 1));
    // The reference Big Info Screen recipe is intentionally free.
    mProducedIn.Add(TSoftClassPtr<UObject>(AFGBuildGun::StaticClass()));
}

UInfoBlueprintScreenRecipe::UInfoBlueprintScreenRecipe()
{
    mDisplayNameOverride = true;
    mDisplayName = LOCTEXT("BlueprintScreenName", "Blueprint Info Screen");
    mManufactoringDuration = 1.0f;
    mProduct.Add(FItemAmount(UInfoBlueprintScreenDescriptor::StaticClass(), 1));
    static ConstructorHelpers::FClassFinder<UFGItemDescriptor> ReinforcedPlate(
        TEXT("/Game/FactoryGame/Resource/Parts/IronPlateReinforced/Desc_IronPlateReinforced"));
    mIngredients.Add(FItemAmount(ReinforcedPlate.Class, 2));
    mProducedIn.Add(TSoftClassPtr<UObject>(AFGBuildGun::StaticClass()));
}

UInfoScreenRecipeUnlock::UInfoScreenRecipeUnlock()
{
    mRecipes.Add(UInfoScreenRecipe::StaticClass());
    mRecipes.Add(UInfoScreenLargeRecipe::StaticClass());
    mRecipes.Add(UInfoBlueprintScreenRecipe::StaticClass());
}

UInfoScreenSchematic::UInfoScreenSchematic()
{
    mType = ESchematicType::EST_Custom;
    mDisplayName = LOCTEXT("SchematicName", "Info Screens");
    mDescription = LOCTEXT("SchematicDescription", "Information screens for storage, fluids, production, resources and power.");
    mTechTier = 0;
    mUnlocks.Add(CreateDefaultSubobject<UInfoScreenRecipeUnlock>(TEXT("ScreenRecipes")));
}

#undef LOCTEXT_NAMESPACE
