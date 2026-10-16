#pragma once

#include "CoreMinimal.h"
#include "Buildables/FGBuildable.h"
#include "InfoScreenTypes.h"
#include "InfoScreen.generated.h"

class UBoxComponent;
class UStaticMeshComponent;
class UWidgetComponent;

DECLARE_MULTICAST_DELEGATE(FInfoScreenLayoutChanged);

UCLASS()
class INFOSCREENS_API AInfoScreen : public AFGBuildable
{
    GENERATED_BODY()

public:
    AInfoScreen();
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
    virtual bool ShouldSave_Implementation() const override;
    // Attached displays move after replication; the factory proximity index requires static positions.
    virtual bool ShouldBeConsideredForBase_Implementation() override { return false; }
    virtual bool CanDismantle_Implementation() const override;
    virtual void Dismantle_Implementation() override;
    virtual void GetDismantleRefund_Implementation(TArray<FInventoryStack>& Refund, bool NoBuildCost) const override;
    virtual void UpdateUseState_Implementation(AFGCharacterPlayer* Character, const FVector& Location,
        UPrimitiveComponent* Component, FUseState& State) override;
    virtual void OnUse_Implementation(AFGCharacterPlayer* Character, const FUseState& State) override;
    virtual bool IsUseable_Implementation() const override;
    virtual FText GetLookAtDecription_Implementation(AFGCharacterPlayer* Character, const FUseState& State) const override;

    AActor* GetSource() const { return Source; }
    FInfoScreenLayoutChanged OnLayoutChanged;
    bool IsAutomatic() const { return bAutomatic; }
    const FInfoScreenLayout& GetLayout() const { return Layout; }
    const FInfoScreenSnapshot& GetSnapshot() const { return Snapshot; }
    int64 GetLayoutRevision() const { return LayoutRevision; }
    bool IsWithinDisplayDistance(const FVector& Position, double DistanceSquared) const;
    void InitializeAutomatic(AActor* Target, const FInfoScreenLayout& InitialLayout, bool LargeSides);
    void SetSource(AActor* Target);
    void ApplyLayout(const FInfoScreenLayout& NewLayout);
    void SetLargeSides(bool Enabled);
    void UpdateSnapshot(const FInfoScreenSnapshot& NewSnapshot);
    static FInfoScreenLayout DefaultContainerLayout(const AActor* Target);

protected:
    UPROPERTY(SaveGame, ReplicatedUsing=OnRep_Presentation) TObjectPtr<AActor> Source;
    UPROPERTY(ReplicatedUsing=OnRep_Presentation) bool bAutomatic = false;
    UPROPERTY(SaveGame, ReplicatedUsing=OnRep_Layout) FInfoScreenLayout Layout;
    UPROPERTY(SaveGame, ReplicatedUsing=OnRep_Layout) int64 LayoutRevision = 0;
    UPROPERTY(ReplicatedUsing=OnRep_Presentation) bool bLargeSides = false;
    UPROPERTY(ReplicatedUsing=OnRep_Presentation) FInfoScreenSnapshot Snapshot;
    UPROPERTY() TObjectPtr<UStaticMeshComponent> Frame;
    UPROPERTY() TArray<TObjectPtr<UWidgetComponent>> Panels;
    UPROPERTY() TArray<TObjectPtr<UBoxComponent>> PanelCollisions;
    float DisplayScale = 1.0f;
    UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> PanelFrames;

    UFUNCTION() void OnRep_Presentation();
    UFUNCTION() void OnRep_Layout();
    void RefreshPresentation();
};

UCLASS()
class INFOSCREENS_API AInfoScreenLarge : public AInfoScreen
{
    GENERATED_BODY()
public:
    AInfoScreenLarge();
};
