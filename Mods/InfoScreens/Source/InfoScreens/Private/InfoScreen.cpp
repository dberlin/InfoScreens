#include "InfoScreen.h"

#include "Buildables/FGBuildableStorage.h"
#include "Buildables/FGBuildableTrainPlatformCargo.h"
#include "Components/BoxComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/WidgetComponent.h"
#include "Engine/StaticMesh.h"
#include "FGCharacterPlayer.h"
#include "FGFactoryConnectionComponent.h"
#include "FGFreightWagon.h"
#include "FGHUD.h"
#include "FGPipeConnectionComponent.h"
#include "FGUseableInterface.h"
#include "GameFramework/PlayerController.h"
#include "InfoScreenContent.h"
#include "InfoScreenEditorWidget.h"
#include "InfoScreenSubsystem.h"
#include "InfoScreenWidget.h"
#include "Net/UnrealNetwork.h"
#include "PhysicsEngine/BodySetup.h"
#include "UObject/ConstructorHelpers.h"

#define LOCTEXT_NAMESPACE "InfoScreens"

namespace
{
    constexpr int32 PanelCount = 4;
    constexpr float PanelWidth = 240.0f;
    constexpr float PanelHeight = 180.0f;
    constexpr float PanelOffset = 8.0f;
    constexpr float PanelBackingDepth = 5.0f;
    // CL513885 LOD0 center geometry projects 20.65/101.10 cm beyond solid/fluid
    // I/O planes. Simple collision omits it; round outward and retain 1 cm clearance.
    constexpr float CargoPlatformSupportProjection = 21.0f;
    constexpr float FluidPlatformSupportProjection = 102.0f;
    // CL513885 DefaultEngine.ini mappings; SDK stubs do not define the TC_* globals.
    constexpr ECollisionChannel InteractChannel = ECC_GameTraceChannel13;
    constexpr ECollisionChannel BuildGunChannel = ECC_GameTraceChannel5;

    FBox SourceBounds(const AActor* Source)
    {
        if (IsValid(Source))
        {
            if (Source->IsA<AFGBuildableStorage>())
            {
                TInlineComponentArray<UStaticMeshComponent*> Meshes(Source);
                for (const UStaticMeshComponent* Mesh : Meshes)
                {
                    if (Mesh->GetFName() != TEXT("FGColoredInstanceMeshProxy") || !Mesh->GetStaticMesh())
                        continue;
                    const FTransform MeshToActor = Mesh->GetComponentTransform()
                        .GetRelativeTransform(Source->GetActorTransform());
                    FBox Body(ForceInit);
                    double LargestVolume = 0.0;
                    if (const UBodySetup* Collision = Mesh->GetStaticMesh()->GetBodySetup())
                    {
                        // The main hull follows the broad body faces. Smaller hulls
                        // describe conveyor mouths and trim, not a mounting wall.
                        for (const FKConvexElem& Hull : Collision->AggGeom.ConvexElems)
                        {
                            if (!Hull.ElemBox.IsValid) continue;
                            const FBox Bounds = Hull.ElemBox.TransformBy(Hull.GetTransform() * MeshToActor);
                            const double Volume = Bounds.GetVolume();
                            if (Volume > LargestVolume)
                            {
                                LargestVolume = Volume;
                                Body = Bounds;
                            }
                        }
                    }
                    if (Body.IsValid) return Body;
                    // Even without a body hull, exclude hidden ladder interaction boxes.
                    const FBox Bounds = Mesh->CalcBounds(MeshToActor).GetBox();
                    if (Bounds.IsValid && !Bounds.GetExtent().IsNearlyZero()) return Bounds;
                }
            }
            if (Source->IsA<AFGFreightWagon>())
            {
                TInlineComponentArray<UStaticMeshComponent*> Meshes(Source);
                for (const UStaticMeshComponent* Mesh : Meshes)
                {
                    if (Mesh->GetFName() != AFGFreightWagon::CargoMeshComponentName || !Mesh->GetStaticMesh())
                        continue;
                    // Include hidden cargo: unloading must not move the mount out to
                    // the wheels/undercarriage bounds or leave it floating beside the body.
                    const FBox Body = Mesh->CalcBounds(Mesh->GetComponentTransform()
                        .GetRelativeTransform(Source->GetActorTransform())).GetBox();
                    if (Body.IsValid && !Body.GetExtent().IsNearlyZero())
                        return Body;
                }
            }
            const FBox Bounds = Source->CalculateComponentsBoundingBoxInLocalSpace(true);
            if (Bounds.IsValid && !Bounds.GetExtent().IsNearlyZero())
            {
                return Bounds;
            }
        }
        return FBox(FVector(-100.0f, -100.0f, 0.0f), FVector(100.0f, 100.0f, 200.0f));
    }

    bool TryCargoPlatformLayout(const AActor* Target, FInfoScreenLayout& Layout)
    {
        const FTransform& SourceTransform = Target->GetActorTransform();
        FVector NormalSum = FVector::ZeroVector;
        FBox PortBounds(ForceInit);
        const auto AddPort = [&](const FVector& WorldLocation, const FVector& WorldNormal)
        {
            const FVector Normal = SourceTransform.InverseTransformVectorNoScale(WorldNormal).GetSafeNormal2D();
            if (!Normal.IsNearlyZero())
            {
                NormalSum += Normal;
                PortBounds += SourceTransform.InverseTransformPosition(WorldLocation);
            }
        };
        TInlineComponentArray<UFGFactoryConnectionComponent*> Conveyors(Target);
        for (const UFGFactoryConnectionComponent* Port : Conveyors)
            if (Port->GetDirection() != EFactoryConnectionDirection::FCD_SNAP_ONLY)
                AddPort(Port->GetComponentLocation(), Port->GetConnectorNormal());
        TInlineComponentArray<UFGPipeConnectionComponent*> Pipes(Target);
        bool HasPipePorts = false;
        for (const UFGPipeConnectionComponent* Port : Pipes)
            if (Port->GetPipeConnectionType() != EPipeConnectionType::PCT_SNAP_ONLY)
            {
                AddPort(Port->GetComponentLocation(), Port->GetConnectorNormal());
                HasPipePorts = true;
            }
        const FVector Normal = NormalSum.GetSafeNormal2D();
        if (!PortBounds.IsValid || Normal.IsNearlyZero())
            return false;

        // Keep the port-centered height and lateral position while clearing the
        // rendered center support, rather than mounting behind its decoration.
        const float SupportProjection = HasPipePorts
            ? FluidPlatformSupportProjection : CargoPlatformSupportProjection;
        const FVector Position = PortBounds.GetCenter()
            + Normal * (SupportProjection + PanelBackingDepth + 1.0f);
        Layout.Front = FTransform(Normal.Rotation(), Position);
        Layout.bBackVisible = false;
        return true;
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
        // Buildables default to dormancy-driven static spatialization. An attached
        // wagon display must stay dynamic even while nobody is sampling its cargo.
        if (bAutomatic && IsValid(Source) && Source->IsA<AFGFreightWagon>())
            SetNetDormancy(DORM_Awake);
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
    if (IsValid(Target) && Target->IsA<AFGBuildableTrainPlatformCargo>())
    {
        FInfoScreenLayout Result;
        if (TryCargoPlatformLayout(Target, Result))
            return Result;
    }
    const FBox Bounds = SourceBounds(Target);
    const FVector Center = Bounds.GetCenter();
    if (IsValid(Target) && Target->IsA<AFGFreightWagon>())
    {
        // Mount on the cargo body's long sides, not the wider undercarriage.
        const float Scale = FMath::Clamp(static_cast<float>(FMath::Min(
            Bounds.GetSize().X / (PanelWidth + 16.0f),
            Bounds.GetSize().Z / (PanelHeight + 16.0f))), 0.35f, 1.5f);
        const double Height = Center.Z;
        FInfoScreenLayout Result;
        Result.Front = FTransform(FRotator(0.0f, 90.0f, 0.0f),
            FVector(Center.X, Bounds.Max.Y + PanelOffset, Height), FVector(Scale));
        Result.Back = FTransform(FRotator(0.0f, -90.0f, 0.0f),
            FVector(Center.X, Bounds.Min.Y - PanelOffset, Height), FVector(Scale));
        return Result;
    }
    // +X is the front normal of both our frame mesh and UWidgetComponent.
    const float Scale = FMath::Clamp(static_cast<float>(Bounds.GetSize().Y) / (PanelWidth + 16.0f), 0.35f, 1.0f);
    const double MountOffset = IsValid(Target) && Target->IsA<AFGBuildableStorage>()
        ? PanelBackingDepth * Scale : PanelOffset;
    FInfoScreenLayout Result;
    // A cargo subclass without usable I/O geometry still gets only one panel.
    Result.bBackVisible = !IsValid(Target) || !Target->IsA<AFGBuildableTrainPlatformCargo>();
    Result.Front = FTransform(FRotator::ZeroRotator,
        FVector(Bounds.Max.X + MountOffset, Center.Y, Center.Z), FVector(Scale));
    Result.Back = FTransform(FRotator(0.0f, 180.0f, 0.0f),
        FVector(Bounds.Min.X - MountOffset, Center.Y, Center.Z), FVector(Scale));
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
    const bool CargoPlatform = IsValid(Source) && Source->IsA<AFGBuildableTrainPlatformCargo>();
    // Wagons must not gain coupler panels, nor cargo platforms extra non-I/O panels.
    const bool ExtraSides = bAutomatic && bLargeSides && !Freight && !CargoPlatform;
    bool Visible[PanelCount] = {true, bAutomatic && Layout.bBackVisible,
        ExtraSides, ExtraSides};
    if (ExtraSides)
    {
        // Optional storage end panels must still clear the conveyor mouths.
        const FBox Bounds = IsValid(Source) && Source->IsA<AFGBuildableStorage>()
            ? Source->CalculateComponentsBoundingBoxInLocalSpace(true) : SourceBounds(Source);
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
                Widget->SetSnapshot(Snapshot, Layout.FontSize, Layout.HeadingFontSize);
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
