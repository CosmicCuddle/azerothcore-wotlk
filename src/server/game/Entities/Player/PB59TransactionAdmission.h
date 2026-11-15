#pragma once

#include <atomic>
#include <cstdint>
#include <limits>
#include <memory>
#include <new>
#include <utility>

namespace PB59
{
// Serialized owner-context only. This is an admission/intent ledger, not an
// inventory lock, a lifetime pin, a DB transaction, or a durable recovery log.
// A native adapter must enforce decisions BEFORE the caller's first side effect.
class TransactionAdmission final
{
    enum class RetirementPhase { Reserved, Mutating, Completed, RecoveryRequired, Released };
    // A scope may unwind after its owner was destroyed by a native callback.
    // Keep only the counter alive; never dereference a dead Player ledger.
    struct ExternalState
    {
        TransactionAdmission* owner;
        std::uint64_t depth = 0;
    };
    struct RetirementTag
    {
        std::atomic<RetirementPhase> phase{RetirementPhase::Reserved};
    };
public:
    enum class Phase { Idle, Active, RecoveryRequired };
    enum class Access { Forward, Recovery };
    enum class Work
    {
        StandaloneSave, CallerOwnedSave, InventoryAndGold, GoldOnly,
        InventoryMutation, Logout, Redirect, Destroy, ScheduledSave
    };
    enum class Decision { Admit, DeferStandaloneSave, DeferLogout, RejectBeforeMutation, RetainOwner, DeferScheduledSave };
    using Ticket = std::uint64_t;
    struct StableProof
    {
        bool ownerContext = false;
        bool inventory = false;
        bool effects = false;
        bool saveQueue = false;
    };

    TransactionAdmission() = default;
    ~TransactionAdmission()
    {
        if (_external)
            _external->owner = nullptr;
    }
    TransactionAdmission(TransactionAdmission const&) = delete;
    TransactionAdmission& operator=(TransactionAdmission const&) = delete;

    [[nodiscard]] Phase State() const noexcept { return _phase; }
    [[nodiscard]] bool OwnerSealed() const noexcept { return _ownerSealed; }
    [[nodiscard]] bool PendingSave() const noexcept { return _pendingSave; }
    [[nodiscard]] bool PendingLogout() const noexcept { return _pendingLogout; }
    // The producer lease owns only a tiny token, never Player/AI/session.
    // Its destruction is safe even after the native logout deletes Player.
    // It excludes new PB59 work during the producer's synchronous retirement;
    // it is NOT a thread lock or a proof that every deletion path is guarded.
    class RetirementLease final
    {
    public:
        RetirementLease() = default;
        ~RetirementLease() { Release(); }
        RetirementLease(RetirementLease&&) noexcept = default;
        RetirementLease& operator=(RetirementLease&& other) noexcept
        {
            if (this != &other)
            {
                Release();
                _token = std::move(other._token);
            }
            return *this;
        }
        RetirementLease(RetirementLease const&) = delete;
        RetirementLease& operator=(RetirementLease const&) = delete;
        explicit operator bool() const noexcept { return bool(_token); }
        // Mark BEFORE the first producer side effect, including a callback.
        [[nodiscard]] bool MarkMutation() noexcept
        {
            auto expected = RetirementPhase::Reserved;
            return _token && _token->phase.compare_exchange_strong(expected,
                RetirementPhase::Mutating, std::memory_order_acq_rel);
        }
        // Completion is a producer result, NOT a durable save acknowledgement.
        // The token remains held until the scope exits, even after Complete.
        [[nodiscard]] bool Complete() noexcept
        {
            auto expected = RetirementPhase::Mutating;
            return _token && _token->phase.compare_exchange_strong(expected,
                RetirementPhase::Completed, std::memory_order_acq_rel);
        }
    private:
        friend class TransactionAdmission;
        explicit RetirementLease(std::shared_ptr<RetirementTag> token) noexcept : _token(std::move(token)) { }
        void Release() noexcept
        {
            if (!_token)
                return;
            auto phase = _token->phase.load(std::memory_order_acquire);
            // Only the unique lease writes this phase. Keep partial retirement
            // latched in the owner's ledger when the producer unwinds.
            _token->phase.store(phase == RetirementPhase::Mutating ?
                RetirementPhase::RecoveryRequired : RetirementPhase::Released, std::memory_order_release);
            _token.reset();
        }
        std::shared_ptr<RetirementTag> _token;
    };

    [[nodiscard]] bool RetirementInProgress() const noexcept
    {
        auto token = _retirement.load(std::memory_order_acquire);
        return token && token->phase.load(std::memory_order_acquire) != RetirementPhase::Released;
    }
    [[nodiscard]] bool RetirementRecoveryRequired() const noexcept
    {
        auto token = _retirement.load(std::memory_order_acquire);
        return token && token->phase.load(std::memory_order_acquire) == RetirementPhase::RecoveryRequired;
    }
    // Compare the live owner's token after a callback. This does not pin
    // the Player or authorize foreign-thread lookup/dereference.
    [[nodiscard]] bool OwnsRetirement(RetirementLease const& lease) const noexcept
    {
        return lease._token && _retirement.load(std::memory_order_acquire) == lease._token &&
            lease._token->phase.load(std::memory_order_acquire) == RetirementPhase::Mutating;
    }
    [[nodiscard]] bool CanRetire() const noexcept
    {
        return _phase == Phase::Idle && !_ownerSealed && !ExternalDepth() &&
            !_pendingSave && !_pendingLogout && !RetirementInProgress();
    }

    // Explicit busy/retry, not a promise to replay a specific caller's intent.
    // Do not consume a deferred logout: its flags belong to the native adapter.
    [[nodiscard]] RetirementLease TryAcquireRetirement() noexcept
    {
        if (!CanRetire())
            return {};
        try
        {
            auto token = std::make_shared<RetirementTag>();
            auto expected = _retirement.load(std::memory_order_acquire);
            if ((expected && expected->phase.load(std::memory_order_acquire) != RetirementPhase::Released) ||
                !_retirement.compare_exchange_strong(expected, token, std::memory_order_acq_rel))
                return {};
            return RetirementLease(std::move(token));
        }
        catch (std::bad_alloc const&)
        {
            return {};
        }
    }

    [[nodiscard]] bool CanBegin() const noexcept
    {
        return CanRetire() && _lastIssued.load(std::memory_order_relaxed) != std::numeric_limits<Ticket>::max();
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
        if (!ticket || ticket != _last || ExternalDepth() || _ownerSealed)
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
        if (!ticket || ticket != _last || _phase == Phase::Idle || ExternalDepth() ||
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
            return work == Work::Destroy && !ExternalDepth() ? Decision::Admit : Decision::RejectBeforeMutation;
        // An independent retirement request cannot enter an admitted producer.
        // Ordinary save scopes still use their existing idle rules. No new
        // external economic mutation while retirement is in progress.
        // The admitted producer's native handoff must be distinguished by its
        // own adapter; this generic Request is not that handoff.
        if (RetirementInProgress())
        {
            if (work == Work::Destroy)
                return Decision::RetainOwner;
            if (work == Work::Logout || work == Work::Redirect || work == Work::InventoryMutation)
                return Decision::RejectBeforeMutation;
        }
        if (_phase == Phase::Idle)
        {
            switch (work)
            {
                case Work::ScheduledSave:
                case Work::StandaloneSave:
                case Work::CallerOwnedSave:
                case Work::InventoryAndGold:
                case Work::GoldOnly:
                case Work::InventoryMutation: return Decision::Admit;
                case Work::Logout:
                    if (ExternalDepth() || _pendingSave)
                    {
                        _pendingLogout = true;
                        return Decision::DeferLogout;
                    }
                    return Decision::Admit;
                case Work::Redirect:
                    return (ExternalDepth() || _pendingSave || _pendingLogout) ?
                        Decision::RejectBeforeMutation : Decision::Admit;
                case Work::Destroy:
                    return (ExternalDepth() || _pendingSave || _pendingLogout) ?
                        Decision::RetainOwner : Decision::Admit;
            }
            return Decision::RejectBeforeMutation;
        }
        switch (work)
        {
            // Timer and mask remain owned by the scheduled producer. No
            // separate full-save intent is manufactured for a deferred tick.
            case Work::ScheduledSave: return Decision::DeferScheduledSave;
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

    // Owner-context only. The counter outlives the ledger if a callback
    // destroys it, but this is NOT permission to continue native Player work.
    class ExternalScope final
    {
    public:
        ExternalScope(TransactionAdmission& state, Work work) noexcept
            : _work(work), _decision(state.Request(work))
        {
            if (_decision != Decision::Admit)
                return;
            try
            {
                if (!state._external)
                    state._external = std::make_shared<ExternalState>(ExternalState{&state});
                if (state._external->depth == std::numeric_limits<std::uint64_t>::max())
                {
                    _decision = Decision::RejectBeforeMutation;
                    return;
                }
                _counter = state._external;
                ++_counter->depth;
            }
            catch (std::bad_alloc const&)
            {
                _decision = Decision::RejectBeforeMutation;
            }
        }
        ~ExternalScope() { if (_counter) --_counter->depth; }
        ExternalScope(ExternalScope const&) = delete;
        ExternalScope& operator=(ExternalScope const&) = delete;
        [[nodiscard]] Decision Result() const noexcept
        {
            return _decision == Decision::Admit && !Admitted() ?
                Decision::RejectBeforeMutation : _decision;
        }
        [[nodiscard]] bool Admitted() const noexcept
        {
            return _decision == Decision::Admit && _counter && _counter->owner;
        }
        // The outer admitted replay scope alone can acknowledge its own intent.
        bool CompleteDeferred() noexcept
        {
            if (!Admitted() || _counter->depth != 1)
                return false;
            auto& state = *_counter->owner;
            if (state._phase != Phase::Idle || _work != Work::StandaloneSave || !state._pendingSave)
                return false;
            state._pendingSave = false;
            return true;
        }
        // Final lifecycle handoff, only after all required save work.
        bool SealOwner() noexcept
        {
            if (!Admitted() || _counter->depth != 1)
                return false;
            auto& state = *_counter->owner;
            if (state._phase != Phase::Idle || state._pendingSave || state._ownerSealed ||
                (_work != Work::Logout && _work != Work::Redirect && _work != Work::Destroy) ||
                (state._pendingLogout && _work != Work::Logout))
                return false;
            state._pendingLogout = false;
            state._ownerSealed = true;
            return true;
        }
    private:
        Work _work;
        Decision _decision;
        std::shared_ptr<ExternalState> _counter;
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
    std::shared_ptr<ExternalState> _external;
    [[nodiscard]] std::uint64_t ExternalDepth() const noexcept
    {
        return _external ? _external->depth : 0;
    }
    bool _ownerSealed = false;
    bool _pendingSave = false;
    bool _pendingLogout = false;
    // Atomic publication rejects competing retirement producers even if their
    // requests overlap. The rest of this ledger and Player access still
    // require the serialized native owner context.
    // Keep a partial-retirement token even after its lease has unwound.
    // No automatic reset; an exact owner recovery adapter is still required.
    std::atomic<std::shared_ptr<RetirementTag>> _retirement;
};
}
