#include "InfoBlueprintScreen.h"

#include "Buildables/FGBuildableBlueprintDesigner.h"
#include "Engine/World.h"
#include "FGBlueprintProxy.h"
#include "FGLightweightBuildableSubsystem.h"
#include "InfoBlueprintReader.h"
#include "InfoScreenContent.h"
#include "Net/UnrealNetwork.h"

#define LOCTEXT_NAMESPACE "InfoScreens"

AInfoBlueprintScreen::AInfoBlueprintScreen()
{
    mDisplayName = LOCTEXT("BlueprintScreenName", "Blueprint Info Screen");
    mDescription = LOCTEXT("BlueprintScreenDescription", "Displays configured input or output rates for one placed blueprint. Interact to choose the displayed rates.");
    mHologramClass = AInfoBlueprintScreenHologram::StaticClass();
}

void AInfoBlueprintScreen::BeginPlay()
{
    Super::BeginPlay();
    if (HasAuthority())
    {
        OnLayoutChanged.AddUObject(this, &AInfoBlueprintScreen::HandleBlueprintLayoutChanged);
        Source = nullptr;
        RefreshBlueprint(0.0f);
    }
}

void AInfoBlueprintScreen::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(AInfoBlueprintScreen, TargetBlueprintProxy);
}

void AInfoBlueprintScreen::SetBlueprintAnchor(AFGBuildable* Anchor)
{
    if (!HasAuthority() || !IsValid(Anchor) || Anchor->GetWorld() != GetWorld())
        return;

    AFGBlueprintProxy* Proxy = Anchor->GetBlueprintProxy();
    bool ValidMember = IsValid(Proxy) && Proxy->GetWorld() == GetWorld()
        && Proxy->GetBuildables().Contains(Anchor);
    if (Anchor->GetIsLightweightTemporary())
    {
        FLightweightBuildableInstanceRef Instance;
        Instance.InitializeFromTemporary(Anchor);
        if (const FRuntimeBuildableInstanceData* Data = Instance.ResolveBuildableInstanceData())
        {
            Proxy = Data->BlueprintProxy;
            ValidMember = IsValid(Proxy) && Proxy->GetWorld() == GetWorld();
        }
    }
    if (ValidMember)
        TargetBlueprintProxy = Proxy;
    else if (!IsValid(GetBlueprintDesigner()) ||
        (Anchor != GetBlueprintDesigner() && Anchor->GetBlueprintDesigner() != GetBlueprintDesigner()))
        return;

    Source = nullptr;
    LastSampleTime = -1.0;
    ForceNetUpdate();
}

void AInfoBlueprintScreen::HandleBlueprintLayoutChanged()
{
    if (GetLayout().bBlueprintOutputs != bLastOutputs)
        RefreshBlueprint(0.0f);
}

void AInfoBlueprintScreen::RefreshBlueprint(float UpdateInterval)
{
    if (!HasAuthority() || !GetWorld())
        return;

    // Membership assigned by blueprint construction supersedes any serialized designer/old-instance reference.
    AFGBlueprintProxy* OwnProxy = GetBlueprintProxy();
    const bool TargetChanged = IsValid(OwnProxy) && TargetBlueprintProxy != OwnProxy;
    if (TargetChanged)
    {
        TargetBlueprintProxy = OwnProxy;
        ForceNetUpdate();
    }
    const bool TargetLost = TargetBlueprintProxy != nullptr && !IsValid(TargetBlueprintProxy);
    if (TargetLost)
    {
        TargetBlueprintProxy = nullptr;
        ForceNetUpdate();
    }
    const double Now = GetWorld()->GetTimeSeconds();
    const bool Outputs = GetLayout().bBlueprintOutputs;
    if (LastSampleTime >= 0.0 && Now - LastSampleTime < UpdateInterval && Outputs == bLastOutputs
        && !TargetChanged && !TargetLost)
        return;
    LastSampleTime = Now;
    bLastOutputs = Outputs;

    AFGBuildableBlueprintDesigner* Designer = GetBlueprintDesigner();
    const bool InDesigner = IsValid(Designer);
    if (InDesigner)
    {
        InfoBlueprintReader::Read(Designer->GetBuildablesInBlueprintDesigner(), Outputs, SamplingSnapshot);
        const FString Name = Designer->GetCurrentBlueprintRecord().BlueprintName;
        SamplingSnapshot.Title = Name.IsEmpty() ? LOCTEXT("BlueprintDesignerTitle", "Blueprint Designer") : FText::FromString(Name);
    }
    else if (IsValid(TargetBlueprintProxy))
    {
        InfoBlueprintReader::Read(TargetBlueprintProxy->GetBuildables(), Outputs, SamplingSnapshot);
        const FText& Name = TargetBlueprintProxy->GetBlueprintName();
        SamplingSnapshot.Title = Name.IsEmpty() ? LOCTEXT("BlueprintTitle", "Blueprint") : Name;
    }
    else
    {
        InfoBlueprintReader::Read({}, Outputs, SamplingSnapshot);
        SamplingSnapshot.Title = LOCTEXT("BlueprintScreenName", "Blueprint Info Screen");
        SamplingSnapshot.Status = LOCTEXT("BlueprintUnlinked", "Unlinked: place on a member of a blueprint");
    }
    SamplingSnapshot.Detail = Outputs ? LOCTEXT("BlueprintOutputs", "Configured recipe outputs / min")
        : LOCTEXT("BlueprintInputs", "Configured recipe inputs / min");
    if (InDesigner || IsValid(TargetBlueprintProxy))
        SamplingSnapshot.Status = SamplingSnapshot.Rows.IsEmpty()
            ? LOCTEXT("BlueprintEmpty", "No configured item rates for this mode") : FText::GetEmpty();
    UpdateSnapshot(SamplingSnapshot);
}

void AInfoBlueprintScreen::PreSerializedToBlueprint()
{
    Super::PreSerializedToBlueprint();
    // References outside the blueprint must never be embedded in its reusable template.
    SerializationTarget = TargetBlueprintProxy;
    SerializationSource = Source;
    TargetBlueprintProxy = nullptr;
    Source = nullptr;
}

void AInfoBlueprintScreen::PostSerializedToBlueprint()
{
    TargetBlueprintProxy = SerializationTarget;
    Source = SerializationSource;
    SerializationTarget = nullptr;
    SerializationSource = nullptr;
    Super::PostSerializedToBlueprint();
}

void AInfoBlueprintScreen::PostSerializedFromBlueprint(bool IsBlueprintWorld)
{
    Super::PostSerializedFromBlueprint(IsBlueprintWorld);
    TargetBlueprintProxy = nullptr;
    Source = nullptr;
    LastSampleTime = -1.0;
}

#undef LOCTEXT_NAMESPACE
