#pragma once

#include "CoreMinimal.h"
#include "FGRemoteCallObject.h"
#include "InfoScreenTypes.h"
#include "InfoScreenEditState.h"
#include "InfoScreenRemoteCall.generated.h"

class AInfoScreen;
class UInfoScreenEditorWidget;

UCLASS()
class INFOSCREENS_API UInfoScreenRemoteCall : public UFGRemoteCallObject
{
    GENERATED_BODY()
public:
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
    void SetEditor(UInfoScreenEditorWidget* InEditor);
    void ClearEditor(UInfoScreenEditorWidget* InEditor);
    uint64 BeginEditRequest(UInfoScreenEditorWidget* InEditor);

    UFUNCTION(Server, Reliable) void ServerApplyLayout(AInfoScreen* Screen, FInfoScreenLayout Layout, uint64 RequestId);
    UFUNCTION(Server, Reliable) void ServerSavePreset(AInfoScreen* Screen, FInfoScreenLayout Layout, uint64 RequestId);
    UFUNCTION(Server, Reliable) void ServerLoadPreset(AInfoScreen* Screen, uint64 RequestId);
    UFUNCTION(Client, Reliable) void ClientEditResult(AInfoScreen* Screen, uint64 RequestId, bool Applied,
        FInfoScreenLayout Layout, int64 Revision, const FText& Message);

private:
    UPROPERTY(Replicated) bool bReplicationAnchor = true;
    UPROPERTY(Transient) TWeakObjectPtr<UInfoScreenEditorWidget> Editor;
    FInfoScreenRequestGate Requests;
    void Reply(AInfoScreen* Screen, uint64 RequestId, bool Applied, const FText& Message);
    bool CanEdit(const AInfoScreen* Screen) const;
};
