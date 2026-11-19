#pragma once

#include "Buildables/FGBuildableManufacturer.h"
#include "FGPowerInfoComponent.h"
#include "FGRecipe.h"
#include "UObject/UnrealType.h"

// CL520140 changed the factory base layout. Resolve fields from the running game,
// not the SDK's inline getters. Each descriptor is cached once; sampling allocates nothing.
namespace InfoFactoryAccess
{
    template<class PropertyType> inline const PropertyType* RequireProperty(UClass* Class, const TCHAR* Name)
    {
        const PropertyType* Property = FindFProperty<PropertyType>(Class, Name);
        if (!Property)
            UE_LOG(LogTemp, Fatal, TEXT("InfoScreens: missing or incompatible runtime property %s.%s"), *Class->GetName(), Name);
        return Property;
    }

    inline bool IsProductionPaused(const AFGBuildableFactory* Source)
    {
        static const FBoolProperty* Property = RequireProperty<FBoolProperty>(AFGBuildableFactory::StaticClass(), TEXT("mIsProductionPaused"));
        return Property->GetPropertyValue_InContainer(Source);
    }

    inline float GetCurrentPotential(const AFGBuildableFactory* Source)
    {
        static const FFloatProperty* Property = RequireProperty<FFloatProperty>(AFGBuildableFactory::StaticClass(), TEXT("mCurrentPotential"));
        return Property->GetPropertyValue_InContainer(Source);
    }

    inline float GetPendingPotential(const AFGBuildableFactory* Source)
    {
        static const FFloatProperty* Property = RequireProperty<FFloatProperty>(AFGBuildableFactory::StaticClass(), TEXT("mPendingPotential"));
        return Property->GetPropertyValue_InContainer(Source);
    }

    inline float GetCurrentProductionBoost(const AFGBuildableFactory* Source)
    {
        static const FFloatProperty* Property = RequireProperty<FFloatProperty>(AFGBuildableFactory::StaticClass(), TEXT("mCurrentProductionBoost"));
        return Property->GetPropertyValue_InContainer(Source);
    }

    inline float GetPendingProductionBoost(const AFGBuildableFactory* Source)
    {
        static const FFloatProperty* Property = RequireProperty<FFloatProperty>(AFGBuildableFactory::StaticClass(), TEXT("mPendingProductionBoost"));
        return Property->GetPropertyValue_InContainer(Source);
    }

    inline float GetManufacturingSpeed(const AFGBuildableManufacturer* Source)
    {
        static const FFloatProperty* Property = RequireProperty<FFloatProperty>(AFGBuildableManufacturer::StaticClass(), TEXT("mManufacturingSpeed"));
        return Property->GetPropertyValue_InContainer(Source);
    }

    inline TSubclassOf<UFGRecipe> GetCurrentRecipe(const AFGBuildableManufacturer* Source)
    {
        static const FClassProperty* Property = RequireProperty<FClassProperty>(AFGBuildableManufacturer::StaticClass(), TEXT("mCurrentRecipe"));
        return Cast<UClass>(Property->GetObjectPropertyValue_InContainer(Source));
    }

    inline UFGPowerInfoComponent* GetPowerInfo(const AFGBuildableFactory* Source)
    {
        static const FObjectProperty* Property = RequireProperty<FObjectProperty>(AFGBuildableFactory::StaticClass(), TEXT("mPowerInfo"));
        return Cast<UFGPowerInfoComponent>(Property->GetObjectPropertyValue_InContainer(Source));
    }

    // Let the game's own thunk dispatch virtual getters with its current vtable layout.
    template<class Result> inline Result InvokeGetter(const UObject* Source, UFunction* Function)
    {
        check(Function && Function->NumParms == 1 && Function->ReturnValueOffset == 0 && Function->ParmsSize == sizeof(Result));
        Result Value{};
        const_cast<UObject*>(Source)->ProcessEvent(Function, &Value);
        return Value;
    }

    inline float GetProductivity(const AFGBuildableFactory* Source)
    {
        static UFunction* Function = AFGBuildableFactory::StaticClass()->FindFunctionByName(TEXT("GetProductivity"));
        return InvokeGetter<float>(Source, Function);
    }

    inline bool IsProducing(const AFGBuildableFactory* Source)
    {
        static UFunction* Function = AFGBuildableFactory::StaticClass()->FindFunctionByName(TEXT("IsProducing"));
        return InvokeGetter<bool>(Source, Function);
    }

    inline float GetProductionCycleTime(const AFGBuildableFactory* Source)
    {
        static UFunction* Function = AFGBuildableFactory::StaticClass()->FindFunctionByName(TEXT("GetProductionCycleTime"));
        return InvokeGetter<float>(Source, Function);
    }

}
