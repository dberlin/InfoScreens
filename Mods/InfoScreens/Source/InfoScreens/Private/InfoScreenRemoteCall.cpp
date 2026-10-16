#include "InfoScreenRemoteCall.h"

#include "InfoScreen.h"
#include "InfoScreenEditorWidget.h"
#include "InfoScreenSubsystem.h"
#include "FGCharacterPlayer.h"
#include "Net/UnrealNetwork.h"

void UInfoScreenRemoteCall::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(UInfoScreenRemoteCall, bReplicationAnchor);
}

void UInfoScreenRemoteCall::SetEditor(UInfoScreenEditorWidget* InEditor)
{
    if (Editor.Get() != InEditor)
    {
        Requests.Invalidate();
        Editor = InEditor;
    }
}

void UInfoScreenRemoteCall::ClearEditor(UInfoScreenEditorWidget* InEditor)
{
    if (Editor.Get() == InEditor)
        SetEditor(nullptr);
}

uint64 UInfoScreenRemoteCall::BeginEditRequest(UInfoScreenEditorWidget* InEditor)
{
    SetEditor(InEditor);
    return Requests.Begin();
}

bool UInfoScreenRemoteCall::CanEdit(const AInfoScreen* Screen) const
{
    const AFGCharacterPlayer* Character = GetOwnerPlayerCharacter();
    if (!IsValid(Screen) || !IsValid(Character) || !Screen->HasAuthority() ||
        Screen->GetWorld() != GetWorld() || Character->GetWorld() != GetWorld())
        return false;
    constexpr double MaximumDistanceSquared = 1000.0 * 1000.0;
    return Screen->IsWithinDisplayDistance(Character->GetActorLocation(), MaximumDistanceSquared);
}

void UInfoScreenRemoteCall::ServerApplyLayout_Implementation(AInfoScreen* Screen, FInfoScreenLayout Layout, uint64 RequestId)
{
    AInfoScreenSubsystem* Subsystem = AInfoScreenSubsystem::Get(GetWorld());
    const bool Applied = CanEdit(Screen) && Subsystem && Subsystem->ApplyLayout(Screen, Layout);
    Reply(Screen, RequestId, Applied, Applied ? NSLOCTEXT("InfoScreens", "LayoutApplied", "Layout saved for everyone in this session.") :
        NSLOCTEXT("InfoScreens", "LayoutRejected", "Layout rejected. Stay near the screen; use finite positions within 100 m and scales from 0.01 to 20."));
}

void UInfoScreenRemoteCall::ServerSavePreset_Implementation(AInfoScreen* Screen, FInfoScreenLayout Layout, uint64 RequestId)
{
    AInfoScreenSubsystem* Subsystem = AInfoScreenSubsystem::Get(GetWorld());
    const bool Applied = CanEdit(Screen) && Subsystem && Subsystem->SavePreset(Screen, Layout);
    Reply(Screen, RequestId, Applied, Applied ? NSLOCTEXT("InfoScreens", "PresetSaved", "Preset saved and applied to every container of this type.") :
        NSLOCTEXT("InfoScreens", "PresetRejected", "Could not save preset. Use a nearby automatic container screen and a valid layout."));
}

void UInfoScreenRemoteCall::ServerLoadPreset_Implementation(AInfoScreen* Screen, uint64 RequestId)
{
    AInfoScreenSubsystem* Subsystem = AInfoScreenSubsystem::Get(GetWorld());
    const bool Applied = CanEdit(Screen) && Subsystem && Subsystem->LoadPreset(Screen);
    Reply(Screen, RequestId, Applied, Applied ? NSLOCTEXT("InfoScreens", "PresetLoaded", "Preset loaded.") :
        NSLOCTEXT("InfoScreens", "NoPreset", "No preset found, or this container screen is no longer nearby."));
}

void UInfoScreenRemoteCall::Reply(AInfoScreen* Screen, uint64 RequestId, bool Applied, const FText& Message)
{
    ClientEditResult(Screen, RequestId, Applied,
        Applied ? Screen->GetLayout() : FInfoScreenLayout(),
        Applied ? Screen->GetLayoutRevision() : 0, Message);
}

void UInfoScreenRemoteCall::ClientEditResult_Implementation(AInfoScreen* Screen, uint64 RequestId, bool Applied,
    FInfoScreenLayout Layout, int64 Revision, const FText& Message)
{
    if (Requests.Matches(RequestId) && Editor.IsValid() && Editor->IsEditingScreen(Screen))
    {
        Requests.Invalidate();
        Editor->HandleEditResult(Applied, Layout, Revision, Message);
    }
}
