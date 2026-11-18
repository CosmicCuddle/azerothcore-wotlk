// Real typed Core DB-result header with PB59's intentionally unwired,
// read-only two-realm evidence fence. No native trade or DB connection runs.
#include "PB59TwoRealmCommitEvidence.h"

#include <array>
#include <iostream>
#include <string_view>

using PB59::TradeRealm;
using PB59::TwoRealmCommitEvidence;
using PB59::TwoRealmCommitReview;
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
    auto result = [](Status status) { return Result{status, status == Status::Committed ? 0 : -100}; };
    for (Status first : statuses)
    {
        for (Status second : statuses)
        {
            TwoRealmCommitEvidence evidence;
            check(evidence.Review() == TwoRealmCommitReview::AwaitingResults,
                  "new pair waits for both results");
            check(!evidence.HasBothOutcomes(), "empty pair has no two results");
            check(evidence.Observe(TradeRealm::First, result(first)), "first outcome accepted");
            check(evidence.Review() == (first == Status::Committed
                    ? TwoRealmCommitReview::AwaitingResults
                    : TwoRealmCommitReview::RecoveryReviewRequired),
                  "partial pair never authorizes completion");
            check(evidence.Observe(TradeRealm::Second, result(second)), "second outcome accepted");
            check(evidence.HasBothOutcomes(), "pair tracks both result values");
            check(evidence.Review() == ((first == Status::Committed && second == Status::Committed)
                    ? TwoRealmCommitReview::BothDriverAcknowledgedButReconciliationRequired
                    : TwoRealmCommitReview::RecoveryReviewRequired),
                  "only two acknowledged commits reach reconciliation-required state");
            check(!evidence.Observe(TradeRealm::First, Result::FromExecutionCode(0)),
                  "duplicate first outcome rejected");
            check(evidence.Review() == TwoRealmCommitReview::RecoveryReviewRequired,
                  "duplicate observation poisons pair, never upgrades failure");
        }
    }

    for (Status second : statuses)
    {
        TwoRealmCommitEvidence outOfOrder;
        check(outOfOrder.Observe(TradeRealm::Second, result(second)),
              "second-first callback delivery accepted");
        check(outOfOrder.Review() != TwoRealmCommitReview::BothDriverAcknowledgedButReconciliationRequired,
              "second-only outcome cannot finalize");
        check(outOfOrder.Observe(TradeRealm::First, Result::FromExecutionCode(0)),
              "late first outcome accepted");
        check(outOfOrder.Review() == (second == Status::Committed
                ? TwoRealmCommitReview::BothDriverAcknowledgedButReconciliationRequired
                : TwoRealmCommitReview::RecoveryReviewRequired),
              "out-of-order delivery still requires reconciliation or recovery");
    }

    TwoRealmCommitEvidence invalid;
    check(!invalid.Observe(static_cast<TradeRealm>(2), Result::FromExecutionCode(0)),
          "unknown realm index refused");
    check(invalid.Review() == TwoRealmCommitReview::RecoveryReviewRequired,
          "invalid realm cannot be erased by later acknowledgements");
    check(!invalid.Observe(TradeRealm::First, Result::FromExecutionCode(0)),
          "poisoned evidence rejects later positives");

    std::cout << "checks=" << checks << " status=" << (passed ? "PASS" : "FAIL") << '\n';
    return passed ? 0 : 1;
}
