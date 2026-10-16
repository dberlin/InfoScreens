#include "InfoScreenEditState.h"

#include <cassert>
#include <iostream>

void DirtyDraftSurvivesReplication()
{
    FInfoScreenDraftState State;
    State.Reset(10);
    State.ChangeDraft();
    assert(!State.ObserveLayout(11));
    assert(State.IsDirty());
}

void EditsAfterSubmissionSurviveAcknowledgement()
{
    FInfoScreenDraftState State;
    State.Reset(10);
    State.ChangeDraft();
    State.BeginRequest();
    State.ChangeDraft();
    assert(!State.AcceptResult(true, 11));
    assert(State.IsDirty());
    assert(!State.ObserveLayout(11));
}

void OlderReplicationCannotReplaceAcceptedResult()
{
    FInfoScreenDraftState State;
    State.Reset(10);
    State.ChangeDraft();
    State.BeginRequest();
    assert(State.AcceptResult(true, 11));
    assert(!State.IsDirty());
    assert(!State.ObserveLayout(10));
    assert(State.ObserveLayout(11));
    assert(State.ObserveLayout(12));
}

void ExplicitUnchangedPresetReplacesDraft()
{
    FInfoScreenDraftState State;
    State.Reset(10);
    State.ChangeDraft();
    State.BeginRequest();
    assert(State.AcceptResult(true, 10));
    assert(!State.IsDirty());
}

void RejectionPreservesDraft()
{
    FInfoScreenDraftState State;
    State.Reset(10);
    State.ChangeDraft();
    State.BeginRequest();
    assert(!State.AcceptResult(false, 0));
    assert(State.IsDirty());
}

void NewerAuthorityMakesOlderAcknowledgementObsolete()
{
    FInfoScreenDraftState State;
    State.Reset(10);
    State.ChangeDraft();
    State.BeginRequest();
    assert(!State.ObserveLayout(12));
    assert(!State.AcceptResult(true, 11));
    assert(State.IsDirty());
}

void ClosedEditorCannotReceiveOldReply()
{
    FInfoScreenRequestGate Gate;
    const auto OldRequest = Gate.Begin();
    Gate.Invalidate();
    assert(!Gate.Matches(OldRequest));
    const auto NewRequest = Gate.Begin();
    assert(!Gate.Matches(OldRequest));
    assert(Gate.Matches(NewRequest));
    assert(!Gate.Matches(0));
}

void NewerRequestSupersedesOlderReply()
{
    FInfoScreenRequestGate Gate;
    const auto First = Gate.Begin();
    const auto Second = Gate.Begin();
    assert(!Gate.Matches(First));
    assert(Gate.Matches(Second));
    Gate.Invalidate();
    assert(!Gate.Matches(Second));
}

int main()
{
    DirtyDraftSurvivesReplication();
    EditsAfterSubmissionSurviveAcknowledgement();
    OlderReplicationCannotReplaceAcceptedResult();
    ExplicitUnchangedPresetReplacesDraft();
    RejectionPreservesDraft();
    NewerAuthorityMakesOlderAcknowledgementObsolete();
    ClosedEditorCannotReceiveOldReply();
    NewerRequestSupersedesOlderReply();
    std::cout << "8 multiplayer edit-ordering scenarios passed\n";
}
