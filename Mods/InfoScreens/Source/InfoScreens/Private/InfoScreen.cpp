#include "InfoScreen.h"

#include "Components/BoxComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/WidgetComponent.h"
#include "Engine/StaticMesh.h"
#include "FGCharacterPlayer.h"
#include "FGFreightWagon.h"
#include "FGHUD.h"
#include "FGUseableInterface.h"
#include "GameFramework/PlayerController.h"
#include "InfoScreenContent.h"
#include "InfoScreenEditorWidget.h"
#include "InfoScreenSubsystem.h"
#include "InfoScreenWidget.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"

#define LOCTEXT_NAMESPACE "InfoScreens"

namespace
{
    constexpr int32 PanelCount = 4;
    constexpr float PanelWidth = 240.0f;
    constexpr float PanelHeight = 180.0f;
    constexpr float PanelOffset = 8.0f;
    // CL513885 DefaultEngine.ini mappings; SDK stubs do not define the TC_* globals.
    constexpr ECollisionChannel InteractChannel = ECC_GameTraceChannel13;
    constexpr ECollisionChannel BuildGunChannel = ECC_GameTraceChannel5;

    FBox SourceBounds(const AActor* Source)
    {
        if (IsValid(Source))
        {
            const FBox Bounds = Source->CalculateComponentsBoundingBoxInLocalSpace(true);
            if (Bounds.IsValid && !Bounds.GetExtent().IsNearlyZero())
            {
                return Bounds;
            }
        }
        return FBox(FVector(-100.0f, -100.0f, 0.0f), FVector(100.0f, 100.0f, 200.0f));
    }
}

AInfoScreen::AInfoScreen()
{
    bReplicates = true;
    SetReplicateMovement(true);
    bAlwaysRelevant = false;
    NetCullDistanceSquared = FMath::Square(10000.0f);
    NetUpdateFrequency = 2.0f;
    MinNetUpdateFrequency = 0.1f;
    mDisplayName = LOCTEXT("ScreenName", "Info Screen");
    mDescription = LOCTEXT("ScreenDescription", "Displays live information from a nearby supported building or freight car. Interact to adjust its layout.");
    mHologramClass = AInfoScreenHologram::StaticClass();

    if (!GetRootComponent())
    {
        SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("ScreenRoot")));
    }
    GetRootComponent()->SetMobility(EComponentMobility::Movable);

    static ConstructorHelpers::FObjectFinder<UStaticMesh> Mesh(
        TEXT("/InfoScreens/Meshes/SM_InfoScreenFrame.SM_InfoScreenFrame"));
    for (int32 Index = 0; Index < PanelCount; ++Index)
    {
        UStaticMeshComponent* PanelFrame = CreateDefaultSubobject<UStaticMeshComponent>(
            *FString::Printf(TEXT("PanelFrame%d"), Index));
        PanelFrame->SetupAttachment(GetRootComponent());
        PanelFrame->SetMobility(EComponentMobility::Movable);
        PanelFrame->SetStaticMesh(Mesh.Object);
        PanelFrame->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        PanelFrame->SetGenerateOverlapEvents(false);
        PanelFrame->SetCastShadow(false);
        PanelFrame->SetVisibility(Index == 0);
        if (Index != 0)
        {
            PanelFrame->ComponentTags.Add(TEXT("InfoScreenSecondary"));
        }
        PanelFrames.Add(PanelFrame);

        UBoxComponent* Collision = CreateDefaultSubobject<UBoxComponent>(
            *FString::Printf(TEXT("PanelInteraction%d"), Index));
        Collision->SetupAttachment(PanelFrame);
        Collision->SetBoxExtent(FVector(4.0f, PanelWidth * 0.5f + 4.0f, PanelHeight * 0.5f + 4.0f));
        Collision->SetRelativeLocation(FVector(-2.0f, 0.0f, 0.0f));
        Collision->SetCollisionObjectType(ECC_WorldDynamic);
        Collision->SetCollisionResponseToAllChannels(ECR_Ignore);
        Collision->SetCollisionResponseToChannel(InteractChannel, ECR_Block);
        Collision->SetCollisionResponseToChannel(BuildGunChannel, ECR_Block);
        Collision->SetCollisionEnabled(Index == 0 ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision);
        Collision->SetGenerateOverlapEvents(false);
        Collision->ComponentTags.Add(TEXT("InfoScreenInteraction"));
        PanelCollisions.Add(Collision);
    }
    Frame = PanelFrames[0];
}

AInfoScreenLarge::AInfoScreenLarge()
{
    DisplayScale = 2.0f;
    Frame->SetRelativeScale3D(FVector(DisplayScale));
    mDisplayName = LOCTEXT("LargeScreenName", "Big Info Screen");
}

void AInfoScreen::BeginPlay()
{
    Super::BeginPlay();
    if (GetNetMode() != NM_DedicatedServer)
    {
        Panels.SetNum(PanelCount);
    }
    RefreshPresentation();
    if (HasAuthority())
    {
        if (AInfoScreenSubsystem* Subsystem = AInfoScreenSubsystem::Get(GetWorld()))
        {
            Subsystem->RegisterScreen(this);
        }
    }
}

void AInfoScreen::EndPlay(const EEndPlayReason::Type Reason)
{
    if (HasAuthority())
    {
        if (AInfoScreenSubsystem* Subsystem = AInfoScreenSubsystem::Get(GetWorld()))
        {
            Subsystem->UnregisterScreen(this);
        }
    }
    OnLayoutChanged.Clear();
    Super::EndPlay(Reason);
}

void AInfoScreen::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(AInfoScreen, Source);
    DOREPLIFETIME(AInfoScreen, Layout);
    DOREPLIFETIME(AInfoScreen, LayoutRevision);
    DOREPLIFETIME(AInfoScreen, Snapshot);
    DOREPLIFETIME(AInfoScreen, bAutomatic);
    DOREPLIFETIME(AInfoScreen, bLargeSides);
}

bool AInfoScreen::ShouldSave_Implementation() const
{
    return !bAutomatic && Super::ShouldSave_Implementation();
}

bool AInfoScreen::CanDismantle_Implementation() const
{
    return !bAutomatic && Super::CanDismantle_Implementation();
}

void AInfoScreen::Dismantle_Implementation()
{
    if (!bAutomatic)
        Super::Dismantle_Implementation();
}

void AInfoScreen::GetDismantleRefund_Implementation(TArray<FInventoryStack>& Refund, bool NoBuildCost) const
{
    if (!bAutomatic)
        Super::GetDismantleRefund_Implementation(Refund, NoBuildCost);
}

bool AInfoScreen::IsWithinDisplayDistance(const FVector& Position, double DistanceSquared) const
{
    if (FVector::DistSquared(Position, GetActorLocation()) <= DistanceSquared)
        return true;
    for (const UStaticMeshComponent* PanelFrame : PanelFrames)
        if (PanelFrame->IsVisible() &&
            FVector::DistSquared(Position, PanelFrame->GetComponentLocation()) <= DistanceSquared)
            return true;
    return false;
}

void AInfoScreen::InitializeAutomatic(AActor* Target, const FInfoScreenLayout& InitialLayout, bool LargeSides)
{
    if (!HasAuthority() || !IsValid(Target))
    {
        return;
    }
    bAutomatic = true;
    Source = Target;
    Layout = InitialLayout;
    bLargeSides = LargeSides;
    AttachToActor(Target, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
    SetActorRelativeTransform(FTransform::Identity);
    OnRep_Layout();
    ForceNetUpdate();
}

void AInfoScreen::SetSource(AActor* Target)
{
    if (!HasAuthority() || Source == Target)
    {
        return;
    }
    Source = Target;
    if (bAutomatic && IsValid(Target))
    {
        AttachToActor(Target, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
        SetActorRelativeTransform(FTransform::Identity);
    }
    RefreshPresentation();
    ForceNetUpdate();
}

void AInfoScreen::ApplyLayout(const FInfoScreenLayout& NewLayout)
{
    if (!HasAuthority())
    {
        return;
    }
    Layout = NewLayout;
    ++LayoutRevision;
    OnRep_Layout();
    ForceNetUpdate();
}

void AInfoScreen::SetLargeSides(bool Enabled)
{
    if (HasAuthority() && bLargeSides != Enabled)
    {
        bLargeSides = Enabled;
        RefreshPresentation();
        ForceNetUpdate();
    }
}

void AInfoScreen::UpdateSnapshot(const FInfoScreenSnapshot& NewSnapshot)
{
    if (HasAuthority())
    {
        Snapshot = NewSnapshot;
        RefreshPresentation();
        ForceNetUpdate();
    }
}

FInfoScreenLayout AInfoScreen::DefaultContainerLayout(const AActor* Target)
{
    const FBox Bounds = SourceBounds(Target);
    const FVector Center = Bounds.GetCenter();
    if (IsValid(Target) && Target->IsA<AFGFreightWagon>())
    {
        // Wagons run along X. Center panels on the upper body, above wheels and away
        // from both couplers. Keep this layout independent of changing cargo visibility.
        const float Scale = FMath::Clamp(static_cast<float>(FMath::Min(
            Bounds.GetSize().X / (PanelWidth + 16.0f),
            Bounds.GetSize().Z * 0.6 / (PanelHeight + 16.0f))), 0.35f, 1.5f);
        const double Height = Bounds.Min.Z + Bounds.GetSize().Z * 0.65;
        FInfoScreenLayout Result;
        Result.Front = FTransform(FRotator(0.0f, 90.0f, 0.0f),
            FVector(Center.X, Bounds.Max.Y + PanelOffset, Height), FVector(Scale));
        Result.Back = FTransform(FRotator(0.0f, -90.0f, 0.0f),
            FVector(Center.X, Bounds.Min.Y - PanelOffset, Height), FVector(Scale));
        return Result;
    }
    // +X is the front normal of both our frame mesh and UWidgetComponent.
    const float Scale = FMath::Clamp(static_cast<float>(Bounds.GetSize().Y) / (PanelWidth + 16.0f), 0.35f, 1.0f);
    FInfoScreenLayout Result;
    Result.Front = FTransform(FRotator::ZeroRotator,
        FVector(Bounds.Max.X + PanelOffset, Center.Y, Center.Z), FVector(Scale));
    Result.Back = FTransform(FRotator(0.0f, 180.0f, 0.0f),
        FVector(Bounds.Min.X - PanelOffset, Center.Y, Center.Z), FVector(Scale));
    return Result;
}

void AInfoScreen::OnRep_Presentation()
{
    RefreshPresentation();
}

void AInfoScreen::OnRep_Layout()
{
    RefreshPresentation();
    OnLayoutChanged.Broadcast();
}

void AInfoScreen::RefreshPresentation()
{
    FTransform Transforms[PanelCount] = {Layout.Front, Layout.Back, FTransform::Identity, FTransform::Identity};
    const bool Freight = IsValid(Source) && Source->IsA<AFGFreightWagon>();
    // Freight's primary pair already occupies its long sides; never add coupler/end panels.
    bool Visible[PanelCount] = {true, bAutomatic && Layout.bBackVisible,
        bAutomatic && bLargeSides && !Freight, bAutomatic && bLargeSides && !Freight};
    if (bAutomatic && bLargeSides && !Freight)
    {
        const FBox Bounds = SourceBounds(Source);
        const FVector Center = Bounds.GetCenter();
        const float SideScale = FMath::Clamp(static_cast<float>(Bounds.GetSize().X) / (PanelWidth + 16.0f), 0.5f, 2.0f);
        Transforms[2] = FTransform(FRotator(0.0f, 90.0f, 0.0f),
            FVector(Center.X, Bounds.Max.Y + PanelOffset, Center.Z), FVector(SideScale));
        Transforms[3] = FTransform(FRotator(0.0f, -90.0f, 0.0f),
            FVector(Center.X, Bounds.Min.Y - PanelOffset, Center.Z), FVector(SideScale));
    }
    // Source references can resolve after the initial actor replication bunch.
    if (bAutomatic && IsValid(Source) && GetAttachParentActor() != Source)
    {
        AttachToActor(Source, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
        SetActorRelativeTransform(FTransform::Identity);
    }
    for (int32 Index = 0; Index < PanelCount; ++Index)
    {
        FTransform Transform = Transforms[Index];
        Transform.MultiplyScale3D(FVector(DisplayScale));
        PanelFrames[Index]->SetRelativeTransform(Transform);
        PanelFrames[Index]->SetVisibility(Visible[Index], true);
        PanelCollisions[Index]->SetCollisionEnabled(Visible[Index]
            ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision);
        PanelCollisions[Index]->SetCollisionResponseToChannel(BuildGunChannel, bAutomatic ? ECR_Ignore : ECR_Block);
        if (Panels.IsValidIndex(Index))
        {
            if (!Visible[Index])
            {
                if (Panels[Index])
                    Panels[Index]->DestroyComponent();
                Panels[Index] = nullptr;
                continue;
            }
            if (!Panels[Index])
            {
                UWidgetComponent* Panel = NewObject<UWidgetComponent>(this);
                Panel->SetupAttachment(PanelFrames[Index]);
                Panel->SetWidgetSpace(EWidgetSpace::World);
                Panel->SetDrawSize(FVector2D(1024.0f, 768.0f));
                Panel->SetPivot(FVector2D(0.5f, 0.5f));
                Panel->SetRelativeLocation(FVector(0.5f, 0.0f, 0.0f));
                Panel->SetRelativeScale3D(FVector(PanelWidth / 1024.0f));
                Panel->SetCollisionEnabled(ECollisionEnabled::NoCollision);
                Panel->SetGenerateOverlapEvents(false);
                Panel->SetTwoSided(false);
                Panel->SetTickWhenOffscreen(false);
                Panel->SetManuallyRedraw(true);
                Panel->SetWidgetClass(UInfoScreenWidget::StaticClass());
                Panels[Index] = Panel;
                Panel->RegisterComponent();
                Panel->InitWidget();
            }
            if (UInfoScreenWidget* Widget = Cast<UInfoScreenWidget>(Panels[Index]->GetUserWidgetObject()))
            {
                Widget->SetSnapshot(Snapshot);
                Panels[Index]->RequestRedraw();
            }
        }
    }
}

void AInfoScreen::UpdateUseState_Implementation(AFGCharacterPlayer* Character, const FVector& Location,
    UPrimitiveComponent* Component, FUseState& State)
{
    State.SetUseState(UFGUseState_Valid::StaticClass());
}

void AInfoScreen::OnUse_Implementation(AFGCharacterPlayer* Character, const FUseState& State)
{
    if (GetNetMode() == NM_DedicatedServer || !IsValid(Character) || !Character->IsLocallyControlled())
    {
        return;
    }
    if (APlayerController* Controller = Cast<APlayerController>(Character->GetController()))
    {
        if (AFGHUD* HUD = Cast<AFGHUD>(Controller->GetHUD()))
        {
            HUD->OpenInteractUI(UInfoScreenEditorWidget::StaticClass(), this);
        }
    }
}

bool AInfoScreen::IsUseable_Implementation() const
{
    return true;
}

FText AInfoScreen::GetLookAtDecription_Implementation(AFGCharacterPlayer* Character, const FUseState& State) const
{
    return LOCTEXT("EditScreen", "Configure Info Screen");
}

#undef LOCTEXT_NAMESPACE
