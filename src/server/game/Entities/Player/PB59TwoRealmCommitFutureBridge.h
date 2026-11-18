#pragma once

#include "PB59TwoRealmCommitEvidence.h"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <future>
#include <utility>

namespace PB59
{
// Owner-thread-only, intentionally UNWIRED adapter for the Core's existing
// AsyncCommitTransactionWithOutcome value futures. No Player*, item, session,
// database worker callback, retry, journal, or lifetime pin is retained.
// It must NEVER be used to send TRADE_COMPLETE or discard original items.
// The future producer must be the Core DB outcome promise API, not std::async
// (destroying an outstanding std::async future can block on its worker).
class TwoRealmCommitFutureBridge final
{
public:
    explicit TwoRealmCommitFutureBridge(TwoRealmTradeIdentity identity) noexcept :
        _identity(identity), _evidence(identity), _closed(!identity.Valid()) {}

    TwoRealmCommitFutureBridge(TwoRealmCommitFutureBridge const&) = delete;
    TwoRealmCommitFutureBridge& operator=(TwoRealmCommitFutureBridge const&) = delete;

    // Attach exactly one promise-backed database result future for every
    // native [realm][write phase] transaction. Neither identity nor phase may
    // come from a client packet. Failure permanently seals incomplete evidence.
    bool Attach(TwoRealmTradeIdentity identity, TradeRealm realm, TradeWritePhase phase,
                std::future<DatabaseTransactionResult>&& future) noexcept
    {
        auto const ri = static_cast<std::size_t>(realm);
        auto const pi = static_cast<std::size_t>(phase);
        if (_closed || identity != _identity || !identity.Valid() ||
            ri >= _futures.size() || pi >= _futures[0].size() ||
            _submitted[ri][pi] || !future.valid())
        {
            SealOutstanding();
            return false;
        }
        _futures[ri][pi] = std::move(future);
        _submitted[ri][pi] = true;
        return true;
    }

    // Call only on a serialized owning thread, with native lifetime authority
    // established separately. wait_for(0) never blocks the world thread. An
    // exception or deferred future is unknown execution, NEVER safe to retry.
    // Returns the number of ready values consumed this tick (0-4).
    std::size_t PollReady() noexcept
    {
        if (_closed)
            return 0;
        std::size_t count = 0;
        for (std::size_t ri = 0; ri < _futures.size(); ++ri)
        {
            for (std::size_t pi = 0; pi < _futures[ri].size(); ++pi)
            {
                if (!_submitted[ri][pi] || _observed[ri][pi])
                    continue;
                auto& future = _futures[ri][pi];
                if (!future.valid())
                {
                    SealOutstanding();
                    return count;
                }
                try
                {
                    auto const ready = future.wait_for(std::chrono::seconds{0});
                    if (ready == std::future_status::timeout)
                        continue;
                    if (ready != std::future_status::ready)
                    {
                        SealOutstanding();
                        return count;
                    }
                    auto const result = future.get();
                    if (!_evidence.Observe(_identity, static_cast<TradeRealm>(ri),
                                           static_cast<TradeWritePhase>(pi), result))
                    {
                        SealOutstanding();
                        return count;
                    }
                    _observed[ri][pi] = true;
                    ++count;
                }
                catch (...)
                {
                    // Broken promise, exceptional task or unknown failure:
                    // no worker re-entry and no guessed commit status.
                    SealOutstanding();
                    return count;
                }
            }
        }
        return count;
    }

    // Native owner exit, abandoned producer or shutdown MUST explicitly seal.
    // This makes missing outcome values a hard recovery requirement, and
    // prevents a late database completion from resurrecting an old trade.
    void SealOutstanding() noexcept
    {
        _closed = true;
        _evidence.SealOutstanding();
    }

    [[nodiscard]] bool IsSealed() const noexcept { return _closed; }
    [[nodiscard]] TwoRealmCommitReview Review() const noexcept { return _evidence.Review(); }
    [[nodiscard]] bool HasAllOutcomes() const noexcept { return _evidence.HasAllOutcomes(); }

private:
    TwoRealmTradeIdentity const _identity;
    TwoRealmCommitEvidence _evidence;
    std::array<std::array<std::future<DatabaseTransactionResult>, 2>, 2> _futures{};
    std::array<std::array<bool, 2>, 2> _submitted{};
    std::array<std::array<bool, 2>, 2> _observed{};
    bool _closed = false;
};
}
