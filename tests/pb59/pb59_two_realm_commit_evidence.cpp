// Real typed Core DB-result header with PB59's intentionally unwired,
// read-only two-realm evidence fence. No native trade or DB connection runs.
#include "PB59TwoRealmCommitEvidence.h"

#include <array>
#include <iostream>
#include <string_view>
#include <type_traits>

using PB59::TradeRealm;
using PB59::TradeWritePhase;
using PB59::TwoRealmCommitEvidence;
using PB59::TwoRealmCommitReview;
using PB59::TwoRealmTradeIdentity;
using Result = DatabaseTransactionResult;
using Status = DatabaseTransactionStatus;

int main()
{
    unsigned checks = 0;
    bool passed = true;
    auto check = [&](bool good, std::string_view label) {
        ++checks;
        if (!good)
        {
            passed = false;
            std::cerr << "FAIL " << checks << " " << label << '\n';
        }
    };

    constexpr std::array<int, 9> codes{
        0, Result::EmptyCode, Result::NotStartedCode, Result::AmbiguousCode,
        1213, Result::NotEnqueuedCode, Result::CancelledBeforeExecutionCode,
        Result::PriorAttemptFailureCode, 99999
    };
    constexpr std::array<Status, 9> expected{
        Status::Committed, Status::Empty, Status::NotStarted, Status::Ambiguous,
        Status::FailedUnverified, Status::NotEnqueued,
        Status::CancelledBeforeExecution, Status::FailedUnverified,
        Status::FailedUnverified
    };
    for (std::size_t i = 0; i < codes.size(); ++i)
    {
        Result const mapped = Result::FromExecutionCode(codes[i]);
        check(mapped.status == expected[i], "exact database result status mapping");
        check(mapped.executionCode == codes[i], "original execution code retained");
        check(mapped.IsCommitted() == (expected[i] == Status::Committed),
              "only driver-acknowledged Commit may report IsCommitted");
    }

    constexpr std::array<Status, 7> statuses{
        Status::Committed, Status::Empty, Status::NotStarted, Status::Ambiguous,
        Status::FailedUnverified, Status::NotEnqueued, Status::CancelledBeforeExecution
    };
    auto result = [](Status status) -> Result {
        switch (status)
        {
            case Status::Committed: return Result::FromExecutionCode(0);
            case Status::Empty: return Result::FromExecutionCode(Result::EmptyCode);
            case Status::NotStarted: return Result::FromExecutionCode(Result::NotStartedCode);
            case Status::Ambiguous: return Result::FromExecutionCode(Result::AmbiguousCode);
            case Status::FailedUnverified: return Result::FromExecutionCode(1213);
            case Status::NotEnqueued: return Result::FromExecutionCode(Result::NotEnqueuedCode);
            case Status::CancelledBeforeExecution: return Result::FromExecutionCode(Result::CancelledBeforeExecutionCode);
        }
        return Result::FromExecutionCode(Result::AmbiguousCode);
    };
    static_assert(!std::is_default_constructible_v<TwoRealmCommitEvidence>);
    TwoRealmTradeIdentity const identity{17, 1207, 1209};
    check(identity.Valid(), "nonzero distinct trade and owner incarnation tokens valid");
    for (Status first : statuses)
    {
        for (Status second : statuses)
        {
            TwoRealmCommitEvidence evidence(identity);
            check(evidence.Review() == TwoRealmCommitReview::AwaitingResults,
                  "new pair waits for both results");
            check(!evidence.HasAllOutcomes(), "empty pair has no two results");
            check(evidence.Observe(identity, TradeRealm::First, TradeWritePhase::SourceDeletion, result(first)), "first outcome accepted");
            check(evidence.Review() == (first == Status::Committed
                    ? TwoRealmCommitReview::AwaitingResults
                    : TwoRealmCommitReview::RecoveryReviewRequired),
                  "partial pair never authorizes completion");
            check(evidence.Observe(identity, TradeRealm::Second, TradeWritePhase::SourceDeletion, result(second)), "second outcome accepted");
            check(!evidence.HasAllOutcomes(), "both source deletions are not all four DB transactions");
            check(evidence.Review() == ((first == Status::Committed && second == Status::Committed)
                    ? TwoRealmCommitReview::AwaitingResults
                    : TwoRealmCommitReview::RecoveryReviewRequired),
                  "source deletions alone never establish four committed outcomes");
            check(evidence.Observe(identity, TradeRealm::First, TradeWritePhase::FinalInventoryAndGold,
                                   Result::FromExecutionCode(0)), "first realm final save accepted");
            check(evidence.Observe(identity, TradeRealm::Second, TradeWritePhase::FinalInventoryAndGold,
                                   Result::FromExecutionCode(0)), "second realm final save accepted");
            check(evidence.HasAllOutcomes(), "all four independent transaction outcomes tracked");
            check(evidence.Review() == ((first == Status::Committed && second == Status::Committed)
                    ? TwoRealmCommitReview::BothDriverAcknowledgedButReconciliationRequired
                    : TwoRealmCommitReview::RecoveryReviewRequired),
                  "even four acknowledgements require reconciliation, never settlement");
            check(!evidence.Observe(identity, TradeRealm::First, TradeWritePhase::SourceDeletion, Result::FromExecutionCode(0)),
                  "duplicate first outcome rejected");
            check(evidence.Review() == TwoRealmCommitReview::RecoveryReviewRequired,
                  "duplicate observation poisons pair, never upgrades failure");
        }
    }

    for (Status second : statuses)
    {
        TwoRealmCommitEvidence outOfOrder(identity);
        check(outOfOrder.Observe(identity, TradeRealm::Second, TradeWritePhase::SourceDeletion, result(second)),
              "second-first callback delivery accepted");
        check(outOfOrder.Review() != TwoRealmCommitReview::BothDriverAcknowledgedButReconciliationRequired,
              "second-only outcome cannot finalize");
        check(outOfOrder.Observe(identity, TradeRealm::First, TradeWritePhase::SourceDeletion, Result::FromExecutionCode(0)),
              "late first outcome accepted");
        check(outOfOrder.Review() == (second == Status::Committed
                ? TwoRealmCommitReview::AwaitingResults
                : TwoRealmCommitReview::RecoveryReviewRequired),
              "out-of-order source deletions still need final-save outcomes");
        check(outOfOrder.Observe(identity, TradeRealm::Second, TradeWritePhase::FinalInventoryAndGold,
                                 Result::FromExecutionCode(0)), "second realm final-save result accepted");
        check(outOfOrder.Observe(identity, TradeRealm::First, TradeWritePhase::FinalInventoryAndGold,
                                 Result::FromExecutionCode(0)), "first realm late final-save result accepted");
        check(outOfOrder.Review() == (second == Status::Committed
                ? TwoRealmCommitReview::BothDriverAcknowledgedButReconciliationRequired
                : TwoRealmCommitReview::RecoveryReviewRequired),
              "out-of-order final results retain conservative decision");
    }

    TwoRealmCommitEvidence invalid(identity);
    check(!invalid.Observe(identity, static_cast<TradeRealm>(2), TradeWritePhase::SourceDeletion, Result::FromExecutionCode(0)),
          "unknown realm index refused");
    check(invalid.Review() == TwoRealmCommitReview::RecoveryReviewRequired,
          "invalid realm cannot be erased by later acknowledgements");
    check(!invalid.Observe(identity, TradeRealm::First, TradeWritePhase::SourceDeletion, Result::FromExecutionCode(0)),
          "poisoned evidence rejects later positives");

    // Negative replay/identity cases must never be recoverable by a late
    // positive database callback. No account, item, or Player pointers exist.
    for (TwoRealmTradeIdentity bad : std::array<TwoRealmTradeIdentity, 6>{{
        {0, 1207, 1209}, {17, 0, 1209}, {17, 1207, 0},
        {17, 1207, 1207}, {0, 0, 0}, {17, 0, 0}}})
    {
        TwoRealmCommitEvidence pair(bad);
        check(!bad.Valid(), "bad trade/owner identity rejected structurally");
        check(!pair.Observe(bad, TradeRealm::First, TradeWritePhase::SourceDeletion, Result::FromExecutionCode(0)),
              "invalid identity cannot commit first realm");
        check(pair.Review() == TwoRealmCommitReview::RecoveryReviewRequired,
              "invalid identity cannot be cleared by any future callback");
    }

    for (TwoRealmTradeIdentity stale : std::array<TwoRealmTradeIdentity, 4>{{
        {18, 1207, 1209}, {17, 1208, 1209}, {17, 1207, 1210}, {17, 1209, 1207}}})
    {
        TwoRealmCommitEvidence pair(identity);
        check(!pair.Observe(stale, TradeRealm::First, TradeWritePhase::SourceDeletion, Result::FromExecutionCode(0)),
              "cross-trade/stale owner callback cannot claim first realm");
        check(!pair.Observe(identity, TradeRealm::Second, TradeWritePhase::SourceDeletion, Result::FromExecutionCode(0)),
              "late valid second realm cannot erase poison from stale first");
        check(pair.Review() == TwoRealmCommitReview::RecoveryReviewRequired,
              "cross-trade result mixing always requires manual recovery");
    }

    for (Result malformed : std::array<Result, 3>{{
        {Status::Committed, 1213}, {Status::Empty, 0}, {Status::CancelledBeforeExecution, Result::NotEnqueuedCode}}})
    {
        TwoRealmCommitEvidence pair(identity);
        check(!pair.Observe(identity, TradeRealm::First, TradeWritePhase::SourceDeletion, malformed),
              "forged/mismatched typed result cannot pass receipt");
        check(pair.Review() == TwoRealmCommitReview::RecoveryReviewRequired,
              "mismatched typed result permanently poisons pair");
    }

    TwoRealmCommitEvidence crossed(identity);
    check(crossed.Observe(identity, TradeRealm::First, TradeWritePhase::SourceDeletion, Result::FromExecutionCode(0)),
          "valid original first realm receives committed driver result");
    check(!crossed.Observe(TwoRealmTradeIdentity{18,1207,1209}, TradeRealm::Second, TradeWritePhase::SourceDeletion,
                           Result::FromExecutionCode(0)),
          "second realm result from another trade cannot finish first trade");
    check(crossed.Review() == TwoRealmCommitReview::RecoveryReviewRequired,
          "cross-identity pair rejects completed appearance");

    // All 49 destination-save outcome combinations with both source deletions
    // confirmed; no failed/ambiguous destination saves may be upgraded.
    for (Status first : statuses)
    {
        for (Status second : statuses)
        {
            TwoRealmCommitEvidence pair(identity);
            check(pair.Observe(identity, TradeRealm::First, TradeWritePhase::SourceDeletion,
                               Result::FromExecutionCode(0)), "source deletion A received");
            check(pair.Observe(identity, TradeRealm::Second, TradeWritePhase::SourceDeletion,
                               Result::FromExecutionCode(0)), "source deletion B received");
            check(pair.Review() == TwoRealmCommitReview::AwaitingResults,
                  "only deletion commits cannot masquerade as final saves");
            check(pair.Observe(identity, TradeRealm::First, TradeWritePhase::FinalInventoryAndGold,
                               result(first)), "final inventory save A received");
            check(pair.Observe(identity, TradeRealm::Second, TradeWritePhase::FinalInventoryAndGold,
                               result(second)), "final inventory save B received");
            check(pair.HasAllOutcomes(), "all two-realm two-phase statuses are stored");
            check(pair.Review() == ((first == Status::Committed && second == Status::Committed)
                    ? TwoRealmCommitReview::BothDriverAcknowledgedButReconciliationRequired
                    : TwoRealmCommitReview::RecoveryReviewRequired),
                  "final-save failure never becomes reconciliation-acknowledged");
        }
    }
    TwoRealmCommitEvidence wrongPhase(identity);
    check(!wrongPhase.Observe(identity, TradeRealm::First, static_cast<TradeWritePhase>(2),
                              Result::FromExecutionCode(0)), "invalid phase fails closed");
    check(wrongPhase.Review() == TwoRealmCommitReview::RecoveryReviewRequired,
          "invalid phase permanently poisons evidence");
    TwoRealmCommitEvidence finalOnly(identity);
    check(finalOnly.Observe(identity, TradeRealm::First, TradeWritePhase::FinalInventoryAndGold,
                            Result::FromExecutionCode(0)), "first final save alone may be recorded");
    check(finalOnly.Observe(identity, TradeRealm::Second, TradeWritePhase::FinalInventoryAndGold,
                            Result::FromExecutionCode(0)), "second final save alone may be recorded");
    check(finalOnly.Review() == TwoRealmCommitReview::AwaitingResults,
          "both final saves with no source deletions never count as full evidence");
    check(!finalOnly.HasAllOutcomes(), "two unobserved source deletion phases remain");
    check(finalOnly.Observe(identity, TradeRealm::First, TradeWritePhase::SourceDeletion,
                            Result::FromExecutionCode(0)), "late source deletion A accepted");
    check(finalOnly.Observe(identity, TradeRealm::Second, TradeWritePhase::SourceDeletion,
                            Result::FromExecutionCode(0)), "late source deletion B accepted");
    check(finalOnly.Review() == TwoRealmCommitReview::BothDriverAcknowledgedButReconciliationRequired,
          "four successful driver results still explicitly require reconciliation");
    TwoRealmCommitEvidence duplicatePhase(identity);
    check(duplicatePhase.Observe(identity, TradeRealm::First, TradeWritePhase::SourceDeletion,
                                 Result::FromExecutionCode(0)), "original phase receipt accepted");
    check(!duplicatePhase.Observe(identity, TradeRealm::First, TradeWritePhase::SourceDeletion,
                                  Result::FromExecutionCode(0)), "duplicate phase result rejected");
    check(duplicatePhase.Review() == TwoRealmCommitReview::RecoveryReviewRequired,
          "duplicate source-deletion result never upgrades the pair");

    // Sealing is a trusted explicit owner-exit/shutdown action, never a
    // spontaneous timer or a DB worker mutating native Player state.
    for (TradeRealm realm : std::array<TradeRealm, 2>{TradeRealm::First, TradeRealm::Second})
    {
        for (TradeWritePhase phase : std::array<TradeWritePhase, 2>{
                 TradeWritePhase::SourceDeletion, TradeWritePhase::FinalInventoryAndGold})
        {
            TwoRealmCommitEvidence dropped(identity);
            check(!dropped.IsSealed(), "new evidence is not sealed");
            check(dropped.Observe(identity, realm, phase, Result::FromExecutionCode(0)),
                  "one of four driver outcomes can arrive");
            check(dropped.Review() == TwoRealmCommitReview::AwaitingResults,
                  "one committed driver result never settles missing phases");
            dropped.SealOutstanding();
            check(dropped.IsSealed(), "owner shutdown seals stale callback context");
            check(dropped.Review() == TwoRealmCommitReview::RecoveryReviewRequired,
                  "any unobserved transaction is a hard recovery hold on seal");
            check(!dropped.Observe(identity, TradeRealm::First,
                                   TradeWritePhase::SourceDeletion, Result::FromExecutionCode(0)),
                  "late DB result cannot re-enter a sealed owner context");
            dropped.SealOutstanding();
            check(dropped.Review() == TwoRealmCommitReview::RecoveryReviewRequired,
                  "repeated seal cannot erase recovery state");
        }
    }

    TwoRealmCommitEvidence noFuture(identity);
    noFuture.SealOutstanding();
    check(noFuture.IsSealed(), "missing-all-outcomes close is observable");
    check(noFuture.Review() == TwoRealmCommitReview::RecoveryReviewRequired,
          "missing-all-outcomes close cannot remain awaiting forever");
    check(!noFuture.Observe(identity, TradeRealm::First, TradeWritePhase::SourceDeletion,
                            Result::FromExecutionCode(0)), "late source result rejected after close");

    TwoRealmCommitEvidence allReceived(identity);
    for (TradeRealm realm : std::array<TradeRealm, 2>{TradeRealm::First, TradeRealm::Second})
    {
        for (TradeWritePhase phase : std::array<TradeWritePhase, 2>{
                 TradeWritePhase::SourceDeletion, TradeWritePhase::FinalInventoryAndGold})
        {
            check(allReceived.Observe(identity, realm, phase, Result::FromExecutionCode(0)),
                  "all four callbacks received before close");
        }
    }
    check(allReceived.Review() == TwoRealmCommitReview::BothDriverAcknowledgedButReconciliationRequired,
          "all driver acknowledgements still do not certify durable trade");
    allReceived.SealOutstanding();
    check(allReceived.IsSealed(), "complete evidence is sealed against further callback processing");
    check(allReceived.Review() == TwoRealmCommitReview::BothDriverAcknowledgedButReconciliationRequired,
          "sealing all-acknowledged evidence does not manufacture durable success");
    check(!allReceived.Observe(identity, TradeRealm::First, TradeWritePhase::SourceDeletion,
                               Result::FromExecutionCode(0)), "sealed complete receipt rejects duplicate");
    check(allReceived.Review() == TwoRealmCommitReview::RecoveryReviewRequired,
          "any post-seal callback is a conservative recovery anomaly");

    std::cout << "checks=" << checks << " status=" << (passed ? "PASS" : "FAIL") << '\n';
    return passed ? 0 : 1;
}
