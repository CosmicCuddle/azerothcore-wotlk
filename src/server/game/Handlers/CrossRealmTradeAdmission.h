#pragma once
// Review-only integration candidate; NOT wired into TradeHandler and NOT a
// durable settlement mechanism. Only the serialized native owner context may
// access these ledgers. Never retain Player pointers across async callbacks.
#include "PB59TransactionAdmission.h"
#include "TradeSettlementGate.h"

namespace CosmicTrade {
class DualOwnerAdmission final {
public:
    using Ledger = PB59::TransactionAdmission;
    using Ticket = Ledger::Ticket;
    DualOwnerAdmission(Ledger& left, Ledger& right) noexcept
        : left_(left), right_(right) {
        // No mutation is allowed before BOTH owners have accepted admission.
        if (&left == &right || !left.CanBegin() || !right.CanBegin()) return;
        leftTicket_ = left.Begin();
        if (!leftTicket_) return;
        rightTicket_ = right.Begin();
        if (!rightTicket_) {
            left.RequireRecovery(leftTicket_); // conservative partial admission
            leftTicket_ = 0;
        }
    }
    DualOwnerAdmission(DualOwnerAdmission const&) = delete;
    DualOwnerAdmission& operator=(DualOwnerAdmission const&) = delete;
    ~DualOwnerAdmission() {
        // Never implicitly release a ticket on stack unwind, including
        // a failed DB submission or disconnected peer.
        if (leftTicket_) left_.RequireRecovery(leftTicket_);
        if (rightTicket_) right_.RequireRecovery(rightTicket_);
    }
    bool Admitted() const noexcept {
        return leftTicket_ && rightTicket_ &&
            left_.IsActive(leftTicket_) && right_.IsActive(rightTicket_);
    }
    Ticket LeftTicket() const noexcept { return leftTicket_; }
    Ticket RightTicket() const noexcept { return rightTicket_; }

    // This is a conservative HOLD/QUARANTINE helper only.
    // Do not call Finish from an in-memory receipt alone: real integration
    // requires authenticated persisted readback of BOTH realms and a
    // separately reviewed native owner lifetime / deferred intent adapter.
    Decision Observe(Receipt const& left, Receipt const& right) noexcept {
        if (!Admitted()) return Decision::ManualRecovery;
        Decision const decision = Decide(left, right);
        if (decision == Decision::ManualRecovery) Quarantine();
        return decision;
    }
    void Quarantine() noexcept {
        if (leftTicket_) left_.RequireRecovery(leftTicket_);
        if (rightTicket_) right_.RequireRecovery(rightTicket_);
    }
private:
    Ledger& left_;
    Ledger& right_;
    Ticket leftTicket_ = 0;
    Ticket rightTicket_ = 0;
};
} // namespace CosmicTrade
