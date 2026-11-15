#pragma once

#include <atomic>
#include <cstdint>
#include <limits>

namespace PB59
{
// Serialized owner-context only. This is an admission/intent ledger, not an
// inventory lock, a lifetime pin, a DB transaction, or a durable recovery log.
// A native adapter must enforce decisions BEFORE the caller's first side effect.
class TransactionAdmission final
{
public:
    enum class Phase { Idle, Active, RecoveryRequired };
    enum class Access { Forward, Recovery };
    enum class Work
    {
        StandaloneSave, CallerOwnedSave, InventoryAndGold, GoldOnly,
        InventoryMutation, Logout, Redirect, Destroy
    };
    enum class Decision { Admit, DeferStandaloneSave, DeferLogout, RejectBeforeMutation, RetainOwner };
    using Ticket = std::uint64_t;
    struct StableProof
    {
        bool ownerContext = false;
        bool inventory = false;
        bool effects = false;
        bool saveQueue = false;
    };

    TransactionAdmission() = default;
    TransactionAdmission(TransactionAdmission const&) = delete;
    TransactionAdmission& operator=(TransactionAdmission const&) = delete;

    [[nodiscard]] Phase State() const noexcept { return _phase; }
    [[nodiscard]] bool OwnerSealed() const noexcept { return _ownerSealed; }
    [[nodiscard]] bool PendingSave() const noexcept { return _pendingSave; }
    [[nodiscard]] bool PendingLogout() const noexcept { return _pendingLogout; }
    [[nodiscard]] bool CanBegin() const noexcept
    {
        return _phase == Phase::Idle && !_ownerSealed && !_externalDepth && !_pendingSave &&
            !_pendingLogout && _lastIssued.load(std::memory_order_relaxed) != std::numeric_limits<Ticket>::max();
    }

    // Only a verified native owner/lifetime adapter may call Begin. Zero is
    // never a ticket. No other transaction can reuse a prior ticket.
    [[nodiscard]] Ticket Begin() noexcept
    {
        if (!CanBegin())
            return 0;
        auto last = _lastIssued.load(std::memory_order_relaxed);
        while (last != std::numeric_limits<Ticket>::max())
        {
            if (_lastIssued.compare_exchange_weak(last, last + 1, std::memory_order_relaxed))
            {
                _last = last + 1;
                _phase = Phase::Active;
                return _last;
            }
        }
        return 0;
    }

    [[nodiscard]] bool IsActive(Ticket ticket) const noexcept
    {
        return CanAccess(ticket, Access::Forward);
    }

    [[nodiscard]] bool CanAccess(Ticket ticket, Access access) const noexcept
    {
        if (!ticket || ticket != _last || _externalDepth || _ownerSealed)
            return false;
        return (access == Access::Forward && _phase == Phase::Active) ||
            (access == Access::Recovery && _phase == Phase::RecoveryRequired);
    }

    // No reset/timeout escape hatch. A failed or abandoned operation retains
    // its ticket and blocks future mutation until explicit reconciliation.
    bool RequireRecovery(Ticket ticket) noexcept
    {
        if (!ticket || ticket != _last || _phase == Phase::Idle)
            return false;
        _phase = Phase::RecoveryRequired;
        return true;
    }

    // Native evidence must be collected independently; this only enforces the
    // conjunction. In-memory stability is NOT durable persistence confirmation.
    bool Finish(Ticket ticket, StableProof proof) noexcept
    {
        if (!ticket || ticket != _last || _phase == Phase::Idle || _externalDepth ||
            !proof.ownerContext || !proof.inventory || !proof.effects || !proof.saveQueue)
            return false;
        _phase = Phase::Idle;
        return true;
    }

    // A caller-owned transaction can never be silently deferred or partially
    // appended. The caller must reject BEFORE any of its own side effects.
    [[nodiscard]] Decision Request(Work work) noexcept
    {
        if (_ownerSealed)
            return work == Work::Destroy && !_externalDepth ? Decision::Admit : Decision::RejectBeforeMutation;
        if (_phase == Phase::Idle)
        {
            switch (work)
            {
                case Work::StandaloneSave:
                case Work::CallerOwnedSave:
                case Work::InventoryAndGold:
                case Work::GoldOnly:
                case Work::InventoryMutation: return Decision::Admit;
                case Work::Logout:
                    if (_externalDepth || _pendingSave)
                    {
                        _pendingLogout = true;
                        return Decision::DeferLogout;
                    }
                    return Decision::Admit;
                case Work::Redirect:
                    return (_externalDepth || _pendingSave || _pendingLogout) ?
                        Decision::RejectBeforeMutation : Decision::Admit;
                case Work::Destroy:
                    return (_externalDepth || _pendingSave || _pendingLogout) ?
                        Decision::RetainOwner : Decision::Admit;
            }
            return Decision::RejectBeforeMutation;
        }
        switch (work)
        {
            case Work::StandaloneSave: _pendingSave = true; return Decision::DeferStandaloneSave;
            case Work::Logout: _pendingLogout = true; return Decision::DeferLogout;
            case Work::Destroy: return Decision::RetainOwner;
            case Work::CallerOwnedSave:
            case Work::InventoryAndGold:
            case Work::GoldOnly:
            case Work::InventoryMutation:
            case Work::Redirect: return Decision::RejectBeforeMutation;
        }
        return Decision::RejectBeforeMutation;
    }

    // External scopes exclude a new PB59 transaction during save/economy
    // callbacks. Nested ordinary work is supported. The state must outlive
    // the scope; destroying a Player with live scopes is NOT permitted.
    class ExternalScope final
    {
    public:
        ExternalScope(TransactionAdmission& state, Work work) noexcept
            : _state(state), _work(work), _decision(state.Request(work))
        {
            if (_decision == Decision::Admit)
            {
                if (_state._externalDepth == std::numeric_limits<std::uint64_t>::max())
                    _decision = Decision::RejectBeforeMutation;
                else
                    ++_state._externalDepth;
            }
        }
        ~ExternalScope() { if (Admitted()) --_state._externalDepth; }
        ExternalScope(ExternalScope const&) = delete;
        ExternalScope& operator=(ExternalScope const&) = delete;
        [[nodiscard]] Decision Result() const noexcept { return _decision; }
        [[nodiscard]] bool Admitted() const noexcept { return _decision == Decision::Admit; }
        // The outer admitted replay scope alone can acknowledge its own intent.
        // The adapter calls this only after the real operation accepts it.
        bool CompleteDeferred() noexcept
        {
            if (!Admitted() || _state._phase != Phase::Idle || _state._externalDepth != 1)
                return false;
            if (_work != Work::StandaloneSave || !_state._pendingSave)
                return false;
            _state._pendingSave = false;
            return true;
        }
        // Final lifecycle handoff only, after all required save work and
        // native logout/redirect preparation. No new gear work may start in
        // the gap between release of this scope and destruction of its owner.
        // The adapter must keep this state alive until the scope has exited.
        bool SealOwner() noexcept
        {
            if (!Admitted() || _state._phase != Phase::Idle || _state._externalDepth != 1 ||
                _state._pendingSave || _state._ownerSealed ||
                (_work != Work::Logout && _work != Work::Redirect && _work != Work::Destroy) ||
                (_state._pendingLogout && _work != Work::Logout))
                return false;
            _state._pendingLogout = false;
            _state._ownerSealed = true;
            return true;
        }
    private:
        TransactionAdmission& _state;
        Work _work;
        Decision _decision;
    };

    // Unwinding a native operation without an explicit stable Finish retains
    // quarantine. It must not release the owner or replay pending work itself.
    class MutationScope final
    {
    public:
        explicit MutationScope(TransactionAdmission& state) noexcept : _state(state), _ticket(state.Begin()) { }
        ~MutationScope() { if (_ticket) _state.RequireRecovery(_ticket); }
        MutationScope(MutationScope const&) = delete;
        MutationScope& operator=(MutationScope const&) = delete;
        [[nodiscard]] Ticket GetTicket() const noexcept { return _ticket; }
        bool Finish(StableProof proof) noexcept
        {
            if (!_state.Finish(_ticket, proof))
                return false;
            _ticket = 0;
            return true;
        }
    private:
        TransactionAdmission& _state;
        Ticket _ticket;
    };

    // Pending intent is coalesced, not a retained caller transaction or a
    // durability acknowledgement. The native adapter must preserve the actual
    // logout flags, replay ownership, bounded retry and shutdown policy.
private:
    inline static std::atomic<Ticket> _lastIssued{0};
    Phase _phase = Phase::Idle;
    Ticket _last = 0;
    std::uint64_t _externalDepth = 0;
    bool _ownerSealed = false;
    bool _pendingSave = false;
    bool _pendingLogout = false;
};
}
