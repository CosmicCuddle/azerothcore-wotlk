#pragma once
// REVIEW-ONLY native API adapter: never promotes a driver acknowledgement to durable custody.
// Does not modify character items, money, or TradeHandler.
#include "DatabaseTransactionResult.h"
#include <chrono>
#include <cstdint>
#include <future>
#include <utility>

namespace CosmicTrade {
enum class OutcomeDecision : std::uint8_t { Hold, NeedsReconciliation, EligibleForReadback };
struct RealmCommitEvidence {
    std::uint32_t realm = 0;
    bool outcomeReceived = false;
    DatabaseTransactionResult outcome{DatabaseTransactionStatus::NotStarted, DatabaseTransactionResult::NotStartedCode};
};
inline OutcomeDecision EvaluateOutcomes(RealmCommitEvidence const& a, RealmCommitEvidence const& b) noexcept {
    if (!a.realm || !b.realm || a.realm == b.realm) return OutcomeDecision::NeedsReconciliation;
    if (!a.outcomeReceived || !b.outcomeReceived) return OutcomeDecision::Hold;
    if (!a.outcome.IsCommitted() || !b.outcome.IsCommitted()) return OutcomeDecision::NeedsReconciliation;
    // This is NOT TRADE_COMPLETE: the next required step is independent persisted
    // snapshot/receipt readback and item/gold verification on both realms.
    return OutcomeDecision::EligibleForReadback;
}
class NonBlockingOutcome {
public:
    explicit NonBlockingOutcome(std::uint32_t realm, std::future<DatabaseTransactionResult>&& future)
        : realm_(realm), future_(std::move(future)) {}
    NonBlockingOutcome(NonBlockingOutcome&&) = default;
    NonBlockingOutcome& operator=(NonBlockingOutcome&&) = default;
    NonBlockingOutcome(NonBlockingOutcome const&) = delete;
    NonBlockingOutcome& operator=(NonBlockingOutcome const&) = delete;
    RealmCommitEvidence Poll() {
        if (!realm_) return {realm_, true, {DatabaseTransactionStatus::FailedUnverified, DatabaseTransactionResult::NotStartedCode}};
        if (received_) return {realm_, true, result_};
        if (!future_.valid()) return {realm_, true, {DatabaseTransactionStatus::FailedUnverified, DatabaseTransactionResult::NotStartedCode}};
        if (future_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return {realm_, false, result_};
        try { result_ = future_.get(); }
        catch (...) { result_ = {DatabaseTransactionStatus::FailedUnverified, DatabaseTransactionResult::AmbiguousCode}; }
        received_ = true;
        return {realm_, true, result_};
    }
private:
    std::uint32_t realm_;
    std::future<DatabaseTransactionResult> future_;
    bool received_ = false;
    DatabaseTransactionResult result_{DatabaseTransactionStatus::NotStarted, DatabaseTransactionResult::NotStartedCode};
};
}
