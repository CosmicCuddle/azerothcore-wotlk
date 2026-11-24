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
    // A native adapter may establish this baseline only for an exact item
    // observed in storage and an empty equipment destination, before effects.
    bool BeginUnequipped(std::uint64_t ticket, std::uint64_t guid) noexcept
    {
        if (_phase != Phase::Fresh || !ticket || !guid)
            return false;
        _ticket = ticket;
        _guid = guid;
        _phase = Phase::Removed;
        return true;
    }
    template<class Verify>
    bool RearmAfterVerifiedMove(std::uint64_t ticket, std::uint64_t guid, Verify&& verify) noexcept
    {
        if ((_phase != Phase::Removed && _phase != Phase::Restored) || !Matches(ticket, guid))
            return false;
        try
        {
            if (!verify())
                return false;
        }
        catch (...)
        {
            return false;
        }
        _phase = Phase::Fresh;
        _ticket = 0;
        _guid = 0;
        return true;
    }
    [[nodiscard]] bool Matches(std::uint64_t ticket, std::uint64_t guid) const noexcept
    {
        return ticket && guid && _ticket == ticket && _guid == guid;
    }
    // C1: normal Restored state is not permanent. Only a native owner
    // after transaction Finish and independent equipment/effect/save-queue
    // validation may re-arm a settled slot. Never reset Uncertain or Removed.
    // No runtime caller is wired until that stronger native proof exists.
    template<class Verify>
    bool RearmAfterVerifiedRestore(std::uint64_t ticket, std::uint64_t guid,
        Verify&& verifiedStable) noexcept
    {
        if (_phase != Phase::Restored || !Matches(ticket, guid))
            return false;
        try
        {
            if (!verifiedStable())
                return false;
        }
        catch (...)
        {
            return false;
        }
        _phase = Phase::Fresh;
        _ticket = 0;
        _guid = 0;
        return true;
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
