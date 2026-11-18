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

class TwoRealmCommitEvidence final
{
public:
    // Duplicate or fabricated observations cannot be replaced later to erase
    // an uncertain outcome. Only a trusted, owner-thread delivery may call
    // this method after retrieving the database worker's value-only future.
    bool Observe(TradeRealm realm, DatabaseTransactionResult outcome) noexcept
    {
        auto const index = static_cast<std::size_t>(realm);
        if (index >= _outcomes.size() || _poisoned || _outcomes[index].has_value())
        {
            _poisoned = true;
            return false;
        }
        _outcomes[index] = outcome;
        return true;
    }

    [[nodiscard]] TwoRealmCommitReview Review() const noexcept
    {
        if (_poisoned)
            return TwoRealmCommitReview::RecoveryReviewRequired;
        for (auto const& outcome : _outcomes)
        {
            if (outcome.has_value() && !outcome->IsCommitted())
                return TwoRealmCommitReview::RecoveryReviewRequired;
        }
        if (!_outcomes[0] || !_outcomes[1])
            return TwoRealmCommitReview::AwaitingResults;
        // Even two committed driver acknowledgements cannot establish an
        // atomic two-database transaction, crash durability, or item custody.
        return TwoRealmCommitReview::BothDriverAcknowledgedButReconciliationRequired;
    }

    [[nodiscard]] bool HasBothOutcomes() const noexcept
    {
        return _outcomes[0].has_value() && _outcomes[1].has_value();
    }

private:
    std::array<std::optional<DatabaseTransactionResult>, 2> _outcomes{};
    bool _poisoned = false;
};
}
