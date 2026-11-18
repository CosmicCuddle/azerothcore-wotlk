#pragma once

#include "PB59TransactionAdmission.h"

#include <memory>
#include <optional>

namespace PB59
{
// Pure pre-first-side-effect admission. This is NOT a Player lifetime pin,
// item-custody guarantee, database transaction, durable recovery record, or
// permission to continue after a native callback has invalidated an owner.
// The native adapter must hold both owners alive and revalidate before every
// irreversible action; crossrealm multi-commit recovery needs separate proof.
class TwoOwnerTradePreflight final
{
public:
    using Ledger = TransactionAdmission;
    using Scope = Ledger::ExternalScope;

    // Serialized native owner context only. Both player-ledger objects must
    // already have independently verified and pinned owner lifetimes.
    TwoOwnerTradePreflight(Ledger& first, Ledger& second) noexcept
    {
        if (std::addressof(first) == std::addressof(second) ||
            !first.OwnerIncarnation() || !second.OwnerIncarnation() ||
            first.OwnerIncarnation() == second.OwnerIncarnation())
            return;

        _first.emplace(first, Ledger::Work::InventoryAndGold);
        if (!_first->Admitted())
        {
            _first.reset();
            return;
        }

        _second.emplace(second, Ledger::Work::InventoryAndGold);
        if (!_second->Admitted())
        {
            // Never leave first-party admission owned after second-party
            // preflight failed. No native inventory/DB side effect occurred.
            _second.reset();
            _first.reset();
            return;
        }
        _bothAdmitted = true;
    }

    TwoOwnerTradePreflight(TwoOwnerTradePreflight const&) = delete;
    TwoOwnerTradePreflight& operator=(TwoOwnerTradePreflight const&) = delete;
    TwoOwnerTradePreflight(TwoOwnerTradePreflight&&) = delete;
    TwoOwnerTradePreflight& operator=(TwoOwnerTradePreflight&&) = delete;

    // This can become false if either ledger was destroyed by a callback.
    // It cannot exclude a native lifetime race *after* this method returns.
    [[nodiscard]] bool Admitted() const noexcept
    {
        return _bothAdmitted && _first && _second &&
            _first->Admitted() && _second->Admitted();
    }

    // A previously granted pair whose ledger vanished is no longer usable.
    [[nodiscard]] bool Revoked() const noexcept
    {
        return _bothAdmitted && !Admitted();
    }

private:
    std::optional<Scope> _first;
    std::optional<Scope> _second;
    bool _bothAdmitted = false;
};
}
