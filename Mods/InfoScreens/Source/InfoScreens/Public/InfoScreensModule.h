#pragma once

#include "CoreMinimal.h"
#include "Module/GameInstanceModule.h"
#include "Module/GameWorldModule.h"
#include "Command/ChatCommandInstance.h"
#include "InfoScreensModule.generated.h"

UCLASS()
class INFOSCREENS_API UInfoScreensInstanceModule : public UGameInstanceModule
{
    GENERATED_BODY()
public:
    UInfoScreensInstanceModule();
};

UCLASS()
class INFOSCREENS_API UInfoScreensWorldModule : public UGameWorldModule
{
    GENERATED_BODY()
public:
    UInfoScreensWorldModule();
    virtual void DispatchLifecycleEvent(ELifecyclePhase Phase) override;
};

UCLASS()
class INFOSCREENS_API AInfoScreensModCommand : public AChatCommandInstance
{
    GENERATED_BODY()
public:
    AInfoScreensModCommand();
    virtual EExecutionStatus ExecuteCommand_Implementation(UCommandSender* Sender,
        const TArray<FString>& Arguments, const FString& Label) override;
};

UCLASS()
class INFOSCREENS_API AInfoScreensTimeCommand : public AChatCommandInstance
{
    GENERATED_BODY()
public:
    AInfoScreensTimeCommand();
    virtual EExecutionStatus ExecuteCommand_Implementation(UCommandSender* Sender,
        const TArray<FString>& Arguments, const FString& Label) override;
};

UCLASS()
class INFOSCREENS_API AInfoScreensDistanceCommand : public AChatCommandInstance
{
    GENERATED_BODY()
public:
    AInfoScreensDistanceCommand();
    virtual EExecutionStatus ExecuteCommand_Implementation(UCommandSender* Sender,
        const TArray<FString>& Arguments, const FString& Label) override;
};
