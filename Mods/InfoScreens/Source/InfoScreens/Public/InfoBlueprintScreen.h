#pragma once

#include "CoreMinimal.h"
#include "InfoScreen.h"
#include "InfoBlueprintScreen.generated.h"

class AFGBlueprintProxy;

/** A manual display bound to one exact placed blueprint, never a proximity association. */
UCLASS()
class INFOSCREENS_API AInfoBlueprintScreen : public AInfoScreen
{
    GENERATED_BODY()

public:
    AInfoBlueprintScreen();
    virtual void BeginPlay() override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
    virtual void PreSerializedToBlueprint() override;
    virtual void PostSerializedToBlueprint() override;
    virtual void PostSerializedFromBlueprint(bool IsBlueprintWorld = false) override;

    void SetBlueprintAnchor(AFGBuildable* Anchor);
    void RefreshBlueprint(float UpdateInterval);
    AFGBlueprintProxy* GetTargetBlueprintProxy() const { return TargetBlueprintProxy; }

private:
    void HandleBlueprintLayoutChanged();
    UPROPERTY(SaveGame, Replicated) TObjectPtr<AFGBlueprintProxy> TargetBlueprintProxy;
    UPROPERTY(Transient) TObjectPtr<AFGBlueprintProxy> SerializationTarget;
    UPROPERTY(Transient) TObjectPtr<AActor> SerializationSource;
    UPROPERTY(Transient) FInfoScreenSnapshot SamplingSnapshot;
    double LastSampleTime = -1.0;
    bool bLastOutputs = false;
};
