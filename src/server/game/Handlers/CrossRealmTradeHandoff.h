#pragma once
// Review-only, NOT wired to TradeHandler. No DB settlement or release capability.
#include "PB59TransactionAdmission.h"
#include "TradeSettlementGate.h"
#include <memory>
#include <new>
#include <utility>

namespace CosmicTrade {
class DualOwnerHandoff final {
public:
    using Ledger = PB59::TransactionAdmission;
    using Ticket = Ledger::Ticket;
    static std::unique_ptr<DualOwnerHandoff> Begin(Ledger& left, Ledger& right) noexcept {
        if (&left == &right || !left.CanBegin() || !right.CanBegin()) return {};
        std::unique_ptr<DualOwnerHandoff> result(new (std::nothrow) DualOwnerHandoff());
        if (!result) return {};
        // MutationScope uses the existing lifetime sentinel; no dangling Ledger&.
        result->left_.reset(new (std::nothrow) Ledger::MutationScope(left));
        if (!result->left_ || !result->left_->GetTicket()) return {};
        result->right_.reset(new (std::nothrow) Ledger::MutationScope(right));
        if (!result->right_ || !result->right_->GetTicket()) return {};
        return result;
    }
    DualOwnerHandoff(DualOwnerHandoff const&) = delete;
    DualOwnerHandoff& operator=(DualOwnerHandoff const&) = delete;
    DualOwnerHandoff(DualOwnerHandoff&&) = delete;
    DualOwnerHandoff& operator=(DualOwnerHandoff&&) = delete;
    ~DualOwnerHandoff() = default;
    Ticket LeftTicket() const noexcept { return left_ ? left_->GetTicket() : 0; }
    Ticket RightTicket() const noexcept { return right_ ? right_->GetTicket() : 0; }
    bool Admitted() const noexcept { return LeftTicket() && RightTicket(); }
    // No caller-asserted in-memory receipt may authorize TRADE_COMPLETE.
    Decision Observe(Receipt const& left, Receipt const& right) const noexcept {
        if (!Admitted()) return Decision::ManualRecovery;
        auto result = Decide(left, right);
        return result == Decision::Complete ? Decision::Hold : result;
    }
    // Transfer the unique_ptr to a long-lived coordinator. Abandonment latches
    // recovery for surviving owners. No Finish until verified native DB readback.
private:
    DualOwnerHandoff() = default;
    std::unique_ptr<Ledger::MutationScope> left_;
    std::unique_ptr<Ledger::MutationScope> right_;
};
}
