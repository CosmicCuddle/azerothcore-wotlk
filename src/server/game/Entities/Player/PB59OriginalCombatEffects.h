#pragma once

#include <cstdint>

namespace PB59
{
// Non-idempotent native effects must never be retried after an ambiguous
// callback. This journal is not a lifetime pin or a durable save proof.
class OriginalCombatEffects
{
public:
    enum class Phase { Fresh, RemovingSet, RemovingMods, Removed,
        RestoringSet, RestoringMods, Restored, Uncertain };
    enum class Result { Rejected, Complete, RecoveryRequired };
    [[nodiscard]] Phase State() const noexcept { return _phase; }
    [[nodiscard]] bool Matches(std::uint64_t ticket, std::uint64_t guid) const noexcept
    {
        return ticket && guid && _ticket == ticket && _guid == guid;
    }
    template<class Verify, class Set, class Mods>
    Result Run(std::uint64_t ticket, std::uint64_t guid, bool apply,
        Verify&& verify, Set&& set, Mods&& mods)
    {
        if (!ticket || !guid || (apply ? _phase != Phase::Removed || !Matches(ticket, guid)
                                      : _phase != Phase::Fresh))
            return Result::Rejected;
        try
        {
            if (!verify())
                return Result::Rejected;
        }
        catch (...)
        {
            return Result::Rejected;
        }
        _ticket = ticket;
        _guid = guid;
        auto uncertain = [&]()
        {
            _phase = Phase::Uncertain;
            return Result::RecoveryRequired;
        };
        try
        {
            _phase = apply ? Phase::RestoringSet : Phase::RemovingSet;
            set(apply);
            if (!verify())
                return uncertain();
            _phase = apply ? Phase::RestoringMods : Phase::RemovingMods;
            mods(apply);
            if (!verify())
                return uncertain();
            _phase = apply ? Phase::Restored : Phase::Removed;
            return Result::Complete;
        }
        catch (...)
        {
            return uncertain();
        }
    }
private:
    Phase _phase = Phase::Fresh;
    std::uint64_t _ticket = 0, _guid = 0;
};
}
