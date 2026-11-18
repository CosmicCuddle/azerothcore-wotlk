// Standalone actual Core typed database-result headers; no Worldserver/Player/DB
// execution. A value-future simulation of Core's asynchronous outcome promise.
#include "PB59TwoRealmCommitFutureBridge.h"

#include <algorithm>
#include <array>
#include <exception>
#include <future>
#include <iostream>
#include <stdexcept>
#include <string_view>

using PB59::TradeRealm;
using PB59::TradeWritePhase;
using PB59::TwoRealmCommitFutureBridge;
using PB59::TwoRealmCommitReview;
using PB59::TwoRealmTradeIdentity;
using Result = DatabaseTransactionResult;

struct Recorder
{
    unsigned checks = 0;
    bool passed = true;
    void Check(bool condition, std::string_view label)
    {
        ++checks;
        if (!condition)
        {
            passed = false;
            std::cerr << "FAIL " << checks << ": " << label << '\n';
        }
    }
};

int main()
{
    Recorder t;
    constexpr TwoRealmTradeIdentity id{9201, 42, 43};
    constexpr std::array<TradeRealm, 4> realms{
        TradeRealm::First, TradeRealm::First, TradeRealm::Second, TradeRealm::Second};
    constexpr std::array<TradeWritePhase, 4> phases{
        TradeWritePhase::SourceDeletion, TradeWritePhase::FinalInventoryAndGold,
        TradeWritePhase::SourceDeletion, TradeWritePhase::FinalInventoryAndGold};

    // Every possible ready-value arrival order, no sleeps or guessed timing.
    std::array<int, 4> order{0, 1, 2, 3};
    unsigned permutations = 0;
    do
    {
        ++permutations;
        TwoRealmCommitFutureBridge bridge(id);
        std::array<std::promise<Result>, 4> promises;
        t.Check(bridge.PollReady() == 0, "no work without attached results");
        for (int i = 0; i < 4; ++i)
        {
            t.Check(bridge.Attach(id, realms[i], phases[i], promises[i].get_future()),
                    "each phase attaches a distinct actual future");
            t.Check(!bridge.HasAllOutcomes(), "attaching future is not a driver acknowledgement");
        }
        t.Check(bridge.PollReady() == 0, "pending future polling is nonblocking");
        for (int i = 0; i < 4; ++i)
        {
            promises[order[i]].set_value(Result::FromExecutionCode(0));
            t.Check(bridge.PollReady() == 1, "exactly one ready value consumed");
            t.Check(bridge.PollReady() == 0, "ready value not processed twice");
            t.Check(bridge.Review() == (i == 3
                    ? TwoRealmCommitReview::BothDriverAcknowledgedButReconciliationRequired
                    : TwoRealmCommitReview::AwaitingResults),
                    "no partial result ordering authorizes two-realm settlement");
        }
        t.Check(bridge.HasAllOutcomes(), "all four distinct result values consumed");
        bridge.SealOutstanding();
        t.Check(bridge.IsSealed(), "shutdown seals future receiver");
        t.Check(bridge.Review() == TwoRealmCommitReview::BothDriverAcknowledgedButReconciliationRequired,
                "all driver success remains only a reconciliation claim");
    } while (std::next_permutation(order.begin(), order.end()));
    t.Check(permutations == 24, "all twenty-four phase arrival orders executed");

    // Every actual DB status family is tested in each of the four write phases.
    constexpr std::array<int, 7> statuses{
        0, Result::EmptyCode, Result::NotStartedCode, Result::AmbiguousCode,
        1213, Result::NotEnqueuedCode, Result::CancelledBeforeExecutionCode};
    for (int failedIndex = 0; failedIndex < 4; ++failedIndex)
    {
        for (int code : statuses)
        {
            TwoRealmCommitFutureBridge bridge(id);
            std::array<std::promise<Result>, 4> promises;
            for (int i = 0; i < 4; ++i)
            {
                t.Check(bridge.Attach(id, realms[i], phases[i], promises[i].get_future()),
                        "typed status fixture future attaches");
                promises[i].set_value(Result::FromExecutionCode(i == failedIndex ? code : 0));
            }
            t.Check(bridge.PollReady() == 4, "all four typed results consumed without waiting");
            t.Check(bridge.HasAllOutcomes(), "typed outcome matrix is complete");
            t.Check(bridge.Review() == (code == 0
                    ? TwoRealmCommitReview::BothDriverAcknowledgedButReconciliationRequired
                    : TwoRealmCommitReview::RecoveryReviewRequired),
                    "one failed/uncertain phase keeps entire trade in recovery");
            t.Check(bridge.PollReady() == 0, "typed outcomes consumed at most once");
        }
    }

    for (int readyIndex = 0; readyIndex < 4; ++readyIndex)
    {
        TwoRealmCommitFutureBridge bridge(id);
        std::array<std::promise<Result>, 4> promises;
        for (int i = 0; i < 4; ++i)
            t.Check(bridge.Attach(id, realms[i], phases[i], promises[i].get_future()),
                    "four futures attached before owner exit");
        promises[readyIndex].set_value(Result::FromExecutionCode(0));
        t.Check(bridge.PollReady() == 1, "exactly one outstanding future completes");
        bridge.SealOutstanding();
        t.Check(bridge.IsSealed(), "explicit owner shutdown sealed incomplete bridge");
        t.Check(bridge.Review() == TwoRealmCommitReview::RecoveryReviewRequired,
                "missing three DB outcomes must require manual reconciliation");
        for (int i = 0; i < 4; ++i)
            if (i != readyIndex)
                promises[i].set_value(Result::FromExecutionCode(0));
        t.Check(bridge.PollReady() == 0, "late DB worker completion not reprocessed");
        t.Check(bridge.Review() == TwoRealmCommitReview::RecoveryReviewRequired,
                "late driver successes never reauthorize sealed trade");
    }

    // Invalid and duplicated submission poisons the bridge before mutation.
    {
        TwoRealmCommitFutureBridge bridge(id);
        std::promise<Result> one, duplicate;
        t.Check(bridge.Attach(id, TradeRealm::First, TradeWritePhase::SourceDeletion,
                              one.get_future()), "first future attached");
        t.Check(!bridge.Attach(id, TradeRealm::First, TradeWritePhase::SourceDeletion,
                               duplicate.get_future()), "duplicate phase future refused");
        t.Check(bridge.IsSealed(), "duplicate attachment permanently seals bridge");
        t.Check(bridge.Review() == TwoRealmCommitReview::RecoveryReviewRequired,
                "duplicate phase not a potential completed trade");
    }
    {
        TwoRealmCommitFutureBridge bridge(id);
        std::promise<Result> one;
        t.Check(!bridge.Attach({9202,42,43}, TradeRealm::First,
                               TradeWritePhase::SourceDeletion, one.get_future()),
                "stale trade-generation result refused");
        t.Check(bridge.Review() == TwoRealmCommitReview::RecoveryReviewRequired,
                "stale future identity requires recovery");
    }
    {
        TwoRealmCommitFutureBridge bridge(id);
        std::future<Result> notValid;
        t.Check(!bridge.Attach(id, TradeRealm::Second, TradeWritePhase::SourceDeletion,
                               std::move(notValid)), "future with no valid shared state rejected");
        t.Check(bridge.IsSealed(), "missing promise is not treated as completion");
    }
    {
        TwoRealmCommitFutureBridge bridge(id);
        std::promise<Result> one;
        t.Check(!bridge.Attach(id, static_cast<TradeRealm>(2), TradeWritePhase::SourceDeletion,
                               one.get_future()), "invalid realm refused");
        t.Check(bridge.Review() == TwoRealmCommitReview::RecoveryReviewRequired,
                "invalid realm poisons future bridge");
    }
    {
        TwoRealmCommitFutureBridge bridge(id);
        std::promise<Result> one;
        t.Check(!bridge.Attach(id, TradeRealm::First, static_cast<TradeWritePhase>(2),
                               one.get_future()), "invalid write phase refused");
        t.Check(bridge.IsSealed(), "invalid write phase seals future bridge");
    }
    {
        TwoRealmCommitFutureBridge bridge(id);
        std::future<Result> abandoned;
        {
            std::promise<Result> producer;
            abandoned = producer.get_future();
            t.Check(bridge.Attach(id, TradeRealm::Second, TradeWritePhase::FinalInventoryAndGold,
                                  std::move(abandoned)), "promise-backed future attached");
        }
        t.Check(bridge.PollReady() == 0, "broken database promise cannot create a driver result");
        t.Check(bridge.IsSealed(), "broken promise seals future bridge");
        t.Check(bridge.Review() == TwoRealmCommitReview::RecoveryReviewRequired,
                "broken promise must not authorize retry or completion");
    }
    {
        TwoRealmCommitFutureBridge bridge(id);
        std::promise<Result> producer;
        auto future = producer.get_future();
        t.Check(bridge.Attach(id, TradeRealm::First, TradeWritePhase::SourceDeletion,
                              std::move(future)), "exceptional promise attached");
        producer.set_exception(std::make_exception_ptr(std::runtime_error("injected task failure")));
        t.Check(bridge.PollReady() == 0, "exceptional promise fails closed");
        t.Check(bridge.Review() == TwoRealmCommitReview::RecoveryReviewRequired,
                "exceptional result is not inferred committed");
    }
    {
        TwoRealmCommitFutureBridge bridge(id);
        auto deferred = std::async(std::launch::deferred, [] {
            return Result::FromExecutionCode(0);
        });
        t.Check(bridge.Attach(id, TradeRealm::Second, TradeWritePhase::SourceDeletion,
                              std::move(deferred)), "deferred test future attached for defensive check");
        t.Check(bridge.PollReady() == 0, "deferred result not executed on owner thread");
        t.Check(bridge.IsSealed(), "unexpected deferred future is rejected");
        t.Check(bridge.Review() == TwoRealmCommitReview::RecoveryReviewRequired,
                "deferred callback cannot upgrade recovery state");
    }
    {
        TwoRealmCommitFutureBridge invalid({0,42,43});
        t.Check(invalid.IsSealed(), "zero trade generation invalid at construction");
        t.Check(invalid.Review() == TwoRealmCommitReview::RecoveryReviewRequired,
                "no default-ready acceptance for invalid trade identity");
    }

    std::cout << "checks=" << t.checks << " status=" << (t.passed ? "PASS" : "FAIL") << '\n';
    return t.passed ? 0 : 1;
}
