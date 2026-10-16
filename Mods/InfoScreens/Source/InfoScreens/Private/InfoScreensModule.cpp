#include "InfoScreensModule.h"

#include "InfoScreenContent.h"
#include "InfoScreenRemoteCall.h"
#include "InfoScreenSubsystem.h"
#include "Command/CommandSender.h"
#include "Engine/World.h"
#include "FGSchematicManager.h"
#include "FGRecipeManager.h"
#include "String/LexFromString.h"
#include "Modules/ModuleManager.h"

IMPLEMENT_MODULE(FDefaultModuleImpl, InfoScreens)

UInfoScreensInstanceModule::UInfoScreensInstanceModule()
{
    bRootModule = true;
    RemoteCallObjects.Add(UInfoScreenRemoteCall::StaticClass());
}

UInfoScreensWorldModule::UInfoScreensWorldModule()
{
    bRootModule = true;
    ModSubsystems.Add(AInfoScreenSubsystem::StaticClass());
    mSchematics.Add(UInfoScreenSchematic::StaticClass());
    mChatCommands.Add(AInfoScreensModCommand::StaticClass());
    mChatCommands.Add(AInfoScreensTimeCommand::StaticClass());
    mChatCommands.Add(AInfoScreensDistanceCommand::StaticClass());
}

void UInfoScreensWorldModule::DispatchLifecycleEvent(ELifecyclePhase Phase)
{
    Super::DispatchLifecycleEvent(Phase);
    if (Phase == ELifecyclePhase::POST_INITIALIZATION && GetWorld() && GetWorld()->GetNetMode() != NM_Client)
    {
        if (AFGSchematicManager* Manager = AFGSchematicManager::Get(GetWorld()))
        {
            if (!Manager->IsSchematicPurchased(UInfoScreenSchematic::StaticClass()))
                Manager->GiveAccessToSchematic(UInfoScreenSchematic::StaticClass(), nullptr,
                    ESchematicUnlockFlags::SuppressNarrativeMessages);
            // Purchased schematics are not granted again when a later mod version adds a recipe.
            if (Manager->IsSchematicPurchased(UInfoScreenSchematic::StaticClass()))
                if (AFGRecipeManager* Recipes = AFGRecipeManager::Get(GetWorld()))
                    if (!Recipes->IsRecipeAvailable(UInfoBlueprintScreenRecipe::StaticClass()))
                        Recipes->AddAvailableRecipe(UInfoBlueprintScreenRecipe::StaticClass());
        }
    }
}

AInfoScreensModCommand::AInfoScreensModCommand()
{
    CommandName = TEXT("modscreen");
    Usage = NSLOCTEXT("InfoScreens", "ModscreenUsage", "/modscreen ON|OFF|big (shared session setting)");
    MinNumberOfArguments = 1;
}

EExecutionStatus AInfoScreensModCommand::ExecuteCommand_Implementation(UCommandSender* Sender,
    const TArray<FString>& Arguments, const FString& Label)
{
    AInfoScreenSubsystem* Subsystem = AInfoScreenSubsystem::Get(GetWorld());
    if (!Subsystem || !HasAuthority())
        return EExecutionStatus::UNCOMPLETED;
    if (Arguments.Num() != 1)
        return EExecutionStatus::BAD_ARGUMENTS;
    if (Arguments[0].Equals(TEXT("ON"), ESearchCase::IgnoreCase))
    {
        Subsystem->SetModdedContainers(true);
        Sender->SendChatMessage(TEXT("Automatic screens enabled for modded containers."));
    }
    else if (Arguments[0].Equals(TEXT("OFF"), ESearchCase::IgnoreCase))
    {
        Subsystem->SetModdedContainers(false);
        Sender->SendChatMessage(TEXT("Modded-container screens disabled; vanilla screens remain enabled."));
    }
    else if (Arguments[0].Equals(TEXT("big"), ESearchCase::IgnoreCase))
    {
        Subsystem->ToggleLargeSides();
        Sender->SendChatMessage(TEXT("Large side panels toggled for automatic container screens."));
    }
    else
        return EExecutionStatus::BAD_ARGUMENTS;
    return EExecutionStatus::COMPLETED;
}

AInfoScreensTimeCommand::AInfoScreensTimeCommand()
{
    CommandName = TEXT("cstime");
    Usage = NSLOCTEXT("InfoScreens", "TimeUsage", "/cstime <seconds: 0.1-3600> (default 10, shared session setting)");
    MinNumberOfArguments = 1;
}

EExecutionStatus AInfoScreensTimeCommand::ExecuteCommand_Implementation(UCommandSender* Sender,
    const TArray<FString>& Arguments, const FString& Label)
{
    AInfoScreenSubsystem* Subsystem = AInfoScreenSubsystem::Get(GetWorld());
    if (!Subsystem || !HasAuthority())
        return EExecutionStatus::UNCOMPLETED;
    float Seconds = 0.0f;
    if (Arguments.Num() != 1 || !LexTryParseString(Seconds, *Arguments[0]) || !Subsystem->SetUpdateInterval(Seconds))
        return EExecutionStatus::BAD_ARGUMENTS;
    Sender->SendChatMessage(FString::Printf(TEXT("Screen sampling interval set to %g seconds."), Seconds));
    return EExecutionStatus::COMPLETED;
}

AInfoScreensDistanceCommand::AInfoScreensDistanceCommand()
{
    CommandName = TEXT("csdist");
    Usage = NSLOCTEXT("InfoScreens", "DistanceUsage", "/csdist <cm: 100-1000000> (default 10000, shared session setting)");
    MinNumberOfArguments = 1;
}

EExecutionStatus AInfoScreensDistanceCommand::ExecuteCommand_Implementation(UCommandSender* Sender,
    const TArray<FString>& Arguments, const FString& Label)
{
    AInfoScreenSubsystem* Subsystem = AInfoScreenSubsystem::Get(GetWorld());
    if (!Subsystem || !HasAuthority())
        return EExecutionStatus::UNCOMPLETED;
    float Centimeters = 0.0f;
    if (Arguments.Num() != 1 || !LexTryParseString(Centimeters, *Arguments[0]) ||
        !Subsystem->SetActivationDistance(Centimeters))
        return EExecutionStatus::BAD_ARGUMENTS;
    Sender->SendChatMessage(FString::Printf(TEXT("Screen sampling distance set to %g cm."), Centimeters));
    return EExecutionStatus::COMPLETED;
}
