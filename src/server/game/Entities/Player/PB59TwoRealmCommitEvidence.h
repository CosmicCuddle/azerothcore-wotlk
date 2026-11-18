#pragma once

#include "DatabaseTransactionResult.h"

#include <array>
#include <cstdint>
#include <optional>

namespace PB59
{
// Evidence-only crossrealm commit aggregation. This class owns no Player*, item,
// database connection, transaction, callback, or recovery journal. Never use its
// result to authorize TRADE_COMPLETE, to destroy original items, or to retry a
// partially executed transaction. It is intentionally UNWIRED.
enum class TwoRealmCommitReview : std::uint8_t
{
    AwaitingResults,
    RecoveryReviewRequired,
    BothDriverAcknowledgedButReconciliationRequired
};

enum class TradeRealm : std::uint8_t
{
    First = 0,
    Second = 1
};

// The current native crossrealm flow writes two distinct transactions per
// realm: early source-item deletions and later inventory/gold saves. A final
// save's driver acknowledgement must never be substituted for deletion proof.
enum class TradeWritePhase : std::uint8_t
{
    SourceDeletion = 0,
    FinalInventoryAndGold = 1
};

// Each pair of callback results must belong to exactly the same proposed
// trade and live owner incarnations. The source of this token must be a
// future trusted world-thread adapter, not a client packet or DB worker.
struct TwoRealmTradeIdentity
{
    std::uint64_t tradeGeneration = 0;
    std::uint64_t firstOwnerIncarnation = 0;
    std::uint64_t secondOwnerIncarnation = 0;

    [[nodiscard]] constexpr bool Valid() const noexcept
    {
        return tradeGeneration != 0 && firstOwnerIncarnation != 0 &&
               secondOwnerIncarnation != 0 && firstOwnerIncarnation != secondOwnerIncarnation;
    }

    constexpr bool operator==(TwoRealmTradeIdentity const&) const noexcept = default;
};

class TwoRealmCommitEvidence final
{
public:
    explicit TwoRealmCommitEvidence(TwoRealmTradeIdentity identity) noexcept :
        _identity(identity), _poisoned(!identity.Valid()) {}
    // Duplicate or fabricated observations cannot be replaced later to erase
    // an uncertain outcome. Only a trusted, owner-thread delivery may call
    // this method after retrieving the database worker's value-only future.
    bool Observe(TwoRealmTradeIdentity identity, TradeRealm realm, TradeWritePhase phase,
                 DatabaseTransactionResult outcome) noexcept
    {
        auto const realmIndex = static_cast<std::size_t>(realm);
        auto const phaseIndex = static_cast<std::size_t>(phase);
        // Do not co-mingle results from a stale trade or a recycled Player
        // incarnation. The typed status must also agree with its real driver
        // code: a caller-fabricated {Committed, errorCode} never counts.
        bool const validResult =
            DatabaseTransactionResult::FromExecutionCode(outcome.executionCode).status == outcome.status;
        if (identity != _identity || !identity.Valid() || !validResult ||
            realmIndex >= _outcomes.size() || phaseIndex >= _outcomes[0].size() ||
            (_poisoned || _sealed) || _outcomes[realmIndex][phaseIndex].has_value())
        {
            _poisoned = true;
            return false;
        }
        _outcomes[realmIndex][phaseIndex] = outcome;
        return true;
    }

    [[nodiscard]] TwoRealmCommitReview Review() const noexcept
    {
        if (_poisoned)
            return TwoRealmCommitReview::RecoveryReviewRequired;
        bool allObserved = true;
        for (auto const& realm : _outcomes)
        {
            for (auto const& outcome : realm)
            {
                if (outcome && !outcome->IsCommitted())
                    return TwoRealmCommitReview::RecoveryReviewRequired;
                if (!outcome)
                    allObserved = false;
            }
        }
        if (!allObserved)
            return TwoRealmCommitReview::AwaitingResults;
        // Four driver acknowledgements across separate transactions are still
        // not an atomic two-database settlement or crash durability proof.
        return TwoRealmCommitReview::BothDriverAcknowledgedButReconciliationRequired;
    }

    [[nodiscard]] bool HasAllOutcomes() const noexcept
    {
        for (auto const& realm : _outcomes)
            for (auto const& outcome : realm)
                if (!outcome)
                    return false;
        return true;
    }

    // Explicit owner-thread close when the future result producer disappears,
    // a player is ending its native lifetime, or shutdown cannot wait for a
    // missing value. No clock, callback, Player* or DB side effect lives here.
    // An incomplete pair closes permanently into recovery; even a fully
    // acknowledged pair still requires external durable reconciliation.
    // Only a future lifetime-safe native adapter may decide when to invoke it.
    void SealOutstanding() noexcept
    {
        _sealed = true;
        if (!HasAllOutcomes())
            _poisoned = true;
    }

    [[nodiscard]] bool IsSealed() const noexcept { return _sealed; }

private:
    TwoRealmTradeIdentity _identity{};
    std::array<std::array<std::optional<DatabaseTransactionResult>, 2>, 2> _outcomes{};
    bool _poisoned = false;
    bool _sealed = false;
};
}
