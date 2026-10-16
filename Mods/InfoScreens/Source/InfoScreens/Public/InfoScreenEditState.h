#pragma once

#include <cstdint>

/** Orders authoritative layouts without replacing a player's newer unsent draft. */
class FInfoScreenDraftState
{
public:
    void Reset(std::int64_t Revision)
    {
        KnownRevision = Revision;
        DraftRevision = SubmittedRevision = 0;
        Dirty = false;
    }

    void ChangeDraft()
    {
        ++DraftRevision;
        Dirty = true;
    }

    void BeginRequest() { SubmittedRevision = DraftRevision; }
    bool IsDirty() const { return Dirty; }

    bool ObserveLayout(std::int64_t Revision)
    {
        if (Revision < KnownRevision)
            return false;
        KnownRevision = Revision;
        return !Dirty;
    }

    bool AcceptResult(bool Applied, std::int64_t Revision)
    {
        if (!Applied || Revision < KnownRevision)
            return false;
        KnownRevision = Revision;
        if (DraftRevision != SubmittedRevision)
            return false;
        Dirty = false;
        return true;
    }

private:
    std::int64_t KnownRevision = 0;
    std::uint64_t DraftRevision = 0;
    std::uint64_t SubmittedRevision = 0;
    bool Dirty = false;
};

/** Tokens identify the most recent request across editor close/reopen cycles. */
class FInfoScreenRequestGate
{
public:
    std::uint64_t Begin()
    {
        Pending = ++Sequence;
        if (Pending == 0)
            Pending = ++Sequence;
        return Pending;
    }
    void Invalidate() { Pending = 0; }
    bool Matches(std::uint64_t Token) const { return Token != 0 && Token == Pending; }

private:
    std::uint64_t Sequence = 0;
    std::uint64_t Pending = 0;
};
