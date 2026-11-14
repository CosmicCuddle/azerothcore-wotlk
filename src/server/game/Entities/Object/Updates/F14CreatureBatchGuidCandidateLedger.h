#ifndef COSMIC_F14_CREATURE_BATCH_GUID_CANDIDATE_LEDGER_H
#define COSMIC_F14_CREATURE_BATCH_GUID_CANDIDATE_LEDGER_H

// Portable, value-only candidate list for one real Core UpdateData packet.
// NO claim that a GUID was delivered to a client; duplicate/ambiguous/oversize
// batches fail CLOSED for evidence (ordinary visibility remains unaffected).
#include <cstddef>
#include <cstdint>
#include <vector>

namespace CosmicF14
{
enum class CreatureBatchDirection : std::uint8_t { Create = 1, OutOfRange = 2 };

template<class Guid, std::size_t MaxCandidates = 256>
class CreatureBatchGuidCandidateLedger
{
    static_assert(MaxCandidates > 0, "positive F14 batch candidate capacity required");
public:
    struct Candidate
    {
        Guid guid;
        CreatureBatchDirection direction;
    };

    bool Record(Guid const& guid, CreatureBatchDirection direction)
    {
        if (_poisoned)
            return false;
        if (guid == Guid{} ||
            (direction != CreatureBatchDirection::Create &&
             direction != CreatureBatchDirection::OutOfRange) ||
            _entries.size() >= MaxCandidates)
        {
            Poison();
            return false;
        }

        // The same GUID appearing twice, even with the same direction, makes
        // per-GUID packet attribution ambiguous. Never emit partial evidence.
        for (Candidate const& entry : _entries)
            if (entry.guid == guid)
            {
                Poison();
                return false;
            }

        _entries.push_back({guid, direction});
        return true;
    }

    // Real UpdateData may collect an OUT GUID via the independent legacy
    // VisibleNotifier::SendToSelf sweep, not just BuildOutOfRangeUpdateBlock.
    // Never attribute CREATE if the same GUID is also OUT in that packet;
    // require exactly ONE real OUT entry for an OUT nomination. If any
    // candidate conflicts, suppress ALL packet evidence, never just one.
    bool CompatibleWithRealOutGuids(std::vector<Guid> const& outGuids) const
    {
        if (_poisoned)
            return false;
        for (Candidate const& candidate : _entries)
        {
            std::size_t count = 0;
            for (Guid const& outGuid : outGuids)
                if (outGuid == candidate.guid)
                    ++count;
            if (candidate.direction == CreatureBatchDirection::Create && count != 0)
                return false;
            if (candidate.direction == CreatureBatchDirection::OutOfRange && count != 1)
                return false;
        }
        return true;
    }

    void Poison()
    {
        _poisoned = true;
        _entries.clear();
    }

    void Clear()
    {
        _poisoned = false;
        _entries.clear();
    }

    bool Poisoned() const { return _poisoned; }
    std::vector<Candidate> const& Entries() const { return _entries; }

private:
    bool _poisoned = false;
    std::vector<Candidate> _entries;
};
} // namespace CosmicF14

#endif // COSMIC_F14_CREATURE_BATCH_GUID_CANDIDATE_LEDGER_H
