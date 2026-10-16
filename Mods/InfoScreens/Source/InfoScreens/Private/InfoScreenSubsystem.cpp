#include "InfoScreenSubsystem.h"

#include "InfoScreen.h"
#include "InfoBlueprintScreen.h"
#include "Buildables/FGBuildableStorage.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Subsystem/SubsystemActorManager.h"
#include "TimerManager.h"

namespace
{
constexpr double AssociationRadius = 1000.0;

bool IsValidPanelTransform(const FTransform& Transform)
{
    if (Transform.ContainsNaN() || !Transform.GetRotation().IsNormalized())
        return false;
    const FVector Location = Transform.GetLocation();
    const FVector Scale = Transform.GetScale3D();
    return Location.GetAbsMax() <= 10000.0 && Scale.GetMin() >= 0.01 && Scale.GetMax() <= 20.0;
}
}

AInfoScreenSubsystem::AInfoScreenSubsystem()
{
    ReplicationPolicy = ESubsystemReplicationPolicy::SpawnOnServer;
    PrimaryActorTick.bCanEverTick = false;
}

AInfoScreenSubsystem* AInfoScreenSubsystem::Get(UWorld* World)
{
    if (World)
        if (USubsystemActorManager* Manager = World->GetSubsystem<USubsystemActorManager>())
            return Manager->GetSubsystemActor<AInfoScreenSubsystem>();
    return nullptr;
}

void AInfoScreenSubsystem::BeginPlay()
{
    Super::BeginPlay();
    if (!HasAuthority())
        return;
    bStarted = true;
    SpawnHandle = GetWorld()->AddOnActorSpawnedHandler(
        FOnActorSpawned::FDelegate::CreateUObject(this, &AInfoScreenSubsystem::ObserveActor));
    for (TActorIterator<AFGBuildable> It(GetWorld()); It; ++It)
        ObserveActor(*It);
    ResetTimer();
}

void AInfoScreenSubsystem::EndPlay(const EEndPlayReason::Type Reason)
{
    bStarted = false;
    GetWorldTimerManager().ClearTimer(RefreshTimer);
    if (SpawnHandle.IsValid())
        GetWorld()->RemoveOnActorSpawnedHandler(SpawnHandle);
    Super::EndPlay(Reason);
}

void AInfoScreenSubsystem::ResetTimer()
{
    if (bStarted)
        GetWorldTimerManager().SetTimer(RefreshTimer, this, &AInfoScreenSubsystem::RefreshScreens,
            FMath::Min(UpdateInterval, 1.0f), true, 0.01f);
}

void AInfoScreenSubsystem::ObserveActor(AActor* Actor)
{
    if (AInfoScreen* Screen = Cast<AInfoScreen>(Actor))
        RegisterScreen(Screen);
    else if (AFGBuildable* Source = Cast<AFGBuildable>(Actor); InfoScreenReader::Supports(Source))
        PendingSources.Add(Source);
}

void AInfoScreenSubsystem::RegisterScreen(AInfoScreen* Screen)
{
    if (HasAuthority() && IsValid(Screen))
    {
        Screens.FindOrAdd(Screen);
        Screen->SetNetCullDistanceSquared(FMath::Square(ActivationDistance + 20000.0f));
    }
}

void AInfoScreenSubsystem::UnregisterScreen(AInfoScreen* Screen)
{
    Screens.Remove(Screen);
    if (Screen && Screen->IsAutomatic())
        AutomaticScreens.Remove(Screen->GetSource());
}

bool AInfoScreenSubsystem::IsAutomaticSource(const AFGBuildable* Source) const
{
    if (!IsValid(Source) || !Source->IsA<AFGBuildableStorage>())
        return false;
    const FString ClassPath = Source->GetClass()->GetPathName();
    return bModdedContainers || ClassPath.StartsWith(TEXT("/Game/")) ||
        ClassPath.StartsWith(TEXT("/Script/FactoryGame."));
}

FInfoScreenLayout AInfoScreenSubsystem::FindLayout(AFGBuildable* Source) const
{
    for (const FInfoScreenSavedLayout& Saved : SavedLayouts)
        if (Saved.Source == Source)
            return Saved.Layout;
    for (const FInfoScreenPreset& Preset : Presets)
        if (Preset.SourceClass == Source->GetClass())
            return Preset.Layout;
    return AInfoScreen::DefaultContainerLayout(Source);
}

void AInfoScreenSubsystem::EnsureAutomaticScreen(AFGBuildable* Source)
{
    if (!IsAutomaticSource(Source))
        return;
    if (const TWeakObjectPtr<AInfoScreen>* Existing = AutomaticScreens.Find(Source); Existing && Existing->IsValid())
        return;
    AInfoScreen* Screen = GetWorld()->SpawnActorDeferred<AInfoScreen>(AInfoScreen::StaticClass(),
        Source->GetActorTransform(), nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
    if (Screen)
    {
        Screen->InitializeAutomatic(Source, FindLayout(Source), bLargeSides);
        AutomaticScreens.Add(Source, Screen);
        Screen->FinishSpawning(Source->GetActorTransform());
    }
}

bool AInfoScreenSubsystem::IsNearPlayer(const AInfoScreen* Screen) const
{
    const double DistanceSquared = FMath::Square(static_cast<double>(ActivationDistance));
    for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
    {
        const APlayerController* Controller = It->Get();
        const APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
        if (Pawn && Screen->IsWithinDisplayDistance(Pawn->GetActorLocation(), DistanceSquared))
            return true;
    }
    return false;
}

AFGBuildable* AInfoScreenSubsystem::FindSource(const AInfoScreen* Screen) const
{
    AFGBuildable* Best = nullptr;
    double BestDistance = DBL_MAX;
    double BestCenterDistance = DBL_MAX;
    const FVector Position = Screen->GetActorLocation();
    for (const TWeakObjectPtr<AFGBuildable>& Candidate : Sources)
    {
        AFGBuildable* Source = Candidate.Get();
        if (!Source || !Source->HasActorBegunPlay())
            continue;
        FVector Center, Extent;
        Source->GetActorBounds(true, Center, Extent);
        const double Distance = FBox(Center - Extent, Center + Extent).ComputeSquaredDistanceToPoint(Position);
        if (Distance > FMath::Square(AssociationRadius))
            continue;
        const double CenterDistance = FVector::DistSquared(Center, Position);
        const bool Better = Distance < BestDistance ||
            (Distance == BestDistance && (CenterDistance < BestCenterDistance ||
                (CenterDistance == BestCenterDistance && Best && Source->GetPathName() < Best->GetPathName())));
        if (Better)
        {
            Best = Source;
            BestDistance = Distance;
            BestCenterDistance = CenterDistance;
        }
    }
    return Best;
}

void AInfoScreenSubsystem::RefreshScreens()
{
    for (auto It = PendingSources.CreateIterator(); It; ++It)
    {
        AFGBuildable* Source = It->Get();
        if (!Source)
        {
            It.RemoveCurrent();
            continue;
        }
        if (!Source->HasActorBegunPlay())
            continue;
        Sources.Add(Source);
        EnsureAutomaticScreen(Source);
        It.RemoveCurrent();
    }
    for (auto It = Sources.CreateIterator(); It; ++It)
        if (!It->IsValid())
            It.RemoveCurrent();
    for (auto It = Snapshots.CreateIterator(); It; ++It)
        if (!It.Key().IsValid())
            It.RemoveCurrent();

    const double Now = GetWorld()->GetTimeSeconds();
    TArray<TWeakObjectPtr<AInfoScreen>, TInlineAllocator<8>> DestroyedScreens;
    for (auto It = Screens.CreateIterator(); It; ++It)
    {
        AInfoScreen* Screen = It.Key().Get();
        if (!Screen)
        {
            It.RemoveCurrent();
            continue;
        }
        if (!Screen->HasActorBegunPlay())
            continue;
        if (AInfoBlueprintScreen* BlueprintScreen = Cast<AInfoBlueprintScreen>(Screen))
        {
            if (IsNearPlayer(Screen))
                BlueprintScreen->RefreshBlueprint(UpdateInterval);
            continue;
        }
        AFGBuildable* Source = Screen->GetSource();
        if (!IsValid(Source))
        {
            if (Screen->IsAutomatic())
            {
                DestroyedScreens.Add(Screen);
                continue;
            }
            if (Now >= It.Value().NextAssociationAttempt)
            {
                Source = FindSource(Screen);
                Screen->SetSource(Source);
                It.Value().NextAssociationAttempt = Now + 2.0;
                if (!Source)
                {
                    FInfoScreenSnapshot Unlinked;
                    Unlinked.Title = NSLOCTEXT("InfoScreens", "UnlinkedTitle", "Info Screen");
                    Unlinked.Status = NSLOCTEXT("InfoScreens", "UnlinkedStatus", "No supported building within 10 m");
                    Screen->UpdateSnapshot(Unlinked);
                }
            }
            if (!IsValid(Source))
                continue;
        }
        if (!IsNearPlayer(Screen))
            continue;
        FCachedSnapshot& Cached = Snapshots.FindOrAdd(Source);
        if (Now - Cached.SampledAt >= UpdateInterval)
        {
            InfoScreenReader::Read(Source, Cached.Snapshot);
            Cached.SampledAt = Now;
            ++Cached.Revision;
        }
        FScreenState& State = It.Value();
        if (State.LastSource != Source || State.Revision != Cached.Revision)
        {
            Screen->UpdateSnapshot(Cached.Snapshot);
            State.LastSource = Source;
            State.Revision = Cached.Revision;
        }
    }
    for (const TWeakObjectPtr<AInfoScreen>& Screen : DestroyedScreens)
        if (Screen.IsValid())
            Screen->Destroy();
}

bool AInfoScreenSubsystem::IsValidLayout(const FInfoScreenLayout& Layout)
{
    return IsValidPanelTransform(Layout.Front) && IsValidPanelTransform(Layout.Back);
}

bool AInfoScreenSubsystem::ApplyLayout(AInfoScreen* Screen, const FInfoScreenLayout& Layout)
{
    if (!HasAuthority() || !IsValid(Screen) || Screen->GetWorld() != GetWorld() || !IsValidLayout(Layout))
        return false;
    Screen->ApplyLayout(Layout);
    if (AInfoBlueprintScreen* BlueprintScreen = Cast<AInfoBlueprintScreen>(Screen))
        BlueprintScreen->RefreshBlueprint(UpdateInterval);
    if (Screen->IsAutomatic() && IsValid(Screen->GetSource()))
    {
        FInfoScreenSavedLayout* Saved = SavedLayouts.FindByPredicate(
            [Screen](const FInfoScreenSavedLayout& Entry) { return Entry.Source == Screen->GetSource(); });
        if (!Saved)
        {
            Saved = &SavedLayouts.AddDefaulted_GetRef();
            Saved->Source = Screen->GetSource();
        }
        Saved->Layout = Layout;
    }
    return true;
}

bool AInfoScreenSubsystem::SavePreset(AInfoScreen* Screen, const FInfoScreenLayout& Layout)
{
    if (!HasAuthority() || !IsValid(Screen) || !Screen->IsAutomatic() ||
        !IsValid(Screen->GetSource()) || !ApplyLayout(Screen, Layout))
        return false;
    UClass* SourceClass = Screen->GetSource()->GetClass();
    FInfoScreenPreset* Preset = Presets.FindByPredicate(
        [SourceClass](const FInfoScreenPreset& Entry) { return Entry.SourceClass == SourceClass; });
    if (!Preset)
    {
        Preset = &Presets.AddDefaulted_GetRef();
        Preset->SourceClass = SourceClass;
    }
    Preset->Layout = Layout;
    for (const auto& Pair : AutomaticScreens)
        if (Pair.Key.IsValid() && Pair.Value.IsValid() && Pair.Value.Get() != Screen &&
            Pair.Key->GetClass() == SourceClass)
            ApplyLayout(Pair.Value.Get(), Layout);
    return true;
}

bool AInfoScreenSubsystem::LoadPreset(AInfoScreen* Screen)
{
    if (!HasAuthority() || !IsValid(Screen) || !Screen->IsAutomatic() || !IsValid(Screen->GetSource()))
        return false;
    for (const FInfoScreenPreset& Preset : Presets)
        if (Preset.SourceClass == Screen->GetSource()->GetClass())
            return ApplyLayout(Screen, Preset.Layout);
    return false;
}

void AInfoScreenSubsystem::SetModdedContainers(bool Enabled)
{
    if (!HasAuthority())
        return;
    bModdedContainers = Enabled;
    TArray<TWeakObjectPtr<AInfoScreen>, TInlineAllocator<8>> Remove;
    for (const auto& Pair : AutomaticScreens)
        if (!IsAutomaticSource(Pair.Key.Get()) && Pair.Value.IsValid())
            Remove.Add(Pair.Value);
    for (const auto& Screen : Remove)
        if (Screen.IsValid())
            Screen->Destroy();
    for (const auto& Source : Sources)
        if (Source.IsValid())
            EnsureAutomaticScreen(Source.Get());
}

void AInfoScreenSubsystem::ToggleLargeSides()
{
    if (!HasAuthority())
        return;
    bLargeSides = !bLargeSides;
    for (const auto& Pair : AutomaticScreens)
        if (Pair.Value.IsValid())
            Pair.Value->SetLargeSides(bLargeSides);
}

bool AInfoScreenSubsystem::SetUpdateInterval(float Seconds)
{
    if (!HasAuthority() || !FMath::IsFinite(Seconds) || Seconds < 0.1f || Seconds > 3600.0f)
        return false;
    UpdateInterval = Seconds;
    ResetTimer();
    return true;
}

bool AInfoScreenSubsystem::SetActivationDistance(float Centimeters)
{
    if (!HasAuthority() || !FMath::IsFinite(Centimeters) || Centimeters < 100.0f || Centimeters > 1000000.0f)
        return false;
    ActivationDistance = Centimeters;
    for (const auto& Pair : Screens)
        if (Pair.Key.IsValid())
            Pair.Key->SetNetCullDistanceSquared(FMath::Square(ActivationDistance + 20000.0f));
    return true;
}

void AInfoScreenSubsystem::PreSaveGame_Implementation(int32 SaveVersion, int32 GameVersion)
{
    SavedLayouts.RemoveAll([](const FInfoScreenSavedLayout& Entry) { return !IsValid(Entry.Source); });
}

void AInfoScreenSubsystem::GatherDependencies_Implementation(TArray<UObject*>& Dependencies)
{
    for (const FInfoScreenSavedLayout& Saved : SavedLayouts)
        if (IsValid(Saved.Source))
            Dependencies.AddUnique(Saved.Source);
}
