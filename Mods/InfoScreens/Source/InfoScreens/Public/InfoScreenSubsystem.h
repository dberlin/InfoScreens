#pragma once

#include "CoreMinimal.h"
#include "FGSaveInterface.h"
#include "Subsystem/ModSubsystem.h"
#include "InfoScreenTypes.h"
#include "InfoScreenSubsystem.generated.h"

class AInfoScreen;

USTRUCT()
struct FInfoScreenSavedLayout
{
    GENERATED_BODY()
    UPROPERTY(SaveGame) TObjectPtr<AActor> Source;
    UPROPERTY(SaveGame) FInfoScreenLayout Layout;
};

UCLASS()
class INFOSCREENS_API AInfoScreenSubsystem : public AModSubsystem, public IFGSaveInterface
{
    GENERATED_BODY()
public:
    AInfoScreenSubsystem();
    static AInfoScreenSubsystem* Get(UWorld* World);
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual bool ShouldSave_Implementation() const override { return true; }
    virtual bool NeedTransform_Implementation() override { return false; }
    virtual void PreSaveGame_Implementation(int32 SaveVersion, int32 GameVersion) override;
    virtual void GatherDependencies_Implementation(TArray<UObject*>& Dependencies) override;

    void RegisterScreen(AInfoScreen* Screen);
    void UnregisterScreen(AInfoScreen* Screen);
    bool ApplyLayout(AInfoScreen* Screen, const FInfoScreenLayout& Layout);
    bool SavePreset(AInfoScreen* Screen, const FInfoScreenLayout& Layout);
    bool LoadPreset(AInfoScreen* Screen);
    void SetModdedContainers(bool Enabled);
    void ToggleLargeSides();
    bool SetUpdateInterval(float Seconds);
    bool SetActivationDistance(float Centimeters);
    static bool IsValidLayout(const FInfoScreenLayout& Layout);

private:
    UPROPERTY(SaveGame) TArray<FInfoScreenSavedLayout> SavedLayouts;
    UPROPERTY(SaveGame) TArray<FInfoScreenPreset> Presets;
    UPROPERTY(SaveGame) bool bModdedContainers = false;
    UPROPERTY(SaveGame) bool bLargeSides = false;
    UPROPERTY(SaveGame) float UpdateInterval = 10.0f;
    UPROPERTY(SaveGame) float ActivationDistance = 10000.0f;

    struct FCachedSnapshot
    {
        FInfoScreenSnapshot Snapshot;
        double SampledAt = -DBL_MAX;
        uint64 Revision = 0;
    };
    struct FScreenState
    {
        TWeakObjectPtr<AActor> LastSource;
        uint64 Revision = 0;
        double NextAssociationAttempt = 0.0;
    };
    TSet<TWeakObjectPtr<AActor>> Sources;
    TSet<TWeakObjectPtr<AActor>> PendingSources;
    TMap<TWeakObjectPtr<AInfoScreen>, FScreenState> Screens;
    TMap<TWeakObjectPtr<AActor>, TWeakObjectPtr<AInfoScreen>> AutomaticScreens;
    TMap<TWeakObjectPtr<AActor>, FCachedSnapshot> Snapshots;
    FDelegateHandle SpawnHandle;
    FTimerHandle RefreshTimer;
    bool bStarted = false;

    void ObserveActor(AActor* Actor);
    void RefreshScreens();
    void ResetTimer();
    void EnsureAutomaticScreen(AActor* Source);
    bool IsAutomaticSource(const AActor* Source) const;
    bool IsNearPlayer(const AInfoScreen* Screen) const;
    AActor* FindSource(const AInfoScreen* Screen) const;
    FInfoScreenLayout FindLayout(AActor* Source) const;
};
