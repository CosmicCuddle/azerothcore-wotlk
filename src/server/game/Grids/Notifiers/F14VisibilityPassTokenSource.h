#ifndef COSMIC_F14_VISIBILITY_PASS_TOKEN_SOURCE_H
#define COSMIC_F14_VISIBILITY_PASS_TOKEN_SOURCE_H

// Core8 experimental source primitive. No player/packet/creature dependencies.
// This only assigns monotonically unique NONZERO pass IDs on one producer.
// Never equate a batch send-call with per-creature delivery or client ACK.
#include <atomic>
#include <cstdint>
#include <limits>

namespace CosmicF14
{
class VisibilityPassTokenSource
{
public:
    std::uint64_t Issue()
    {
        std::uint64_t candidate = _next.load(std::memory_order_relaxed);
        while (candidate)
        {
            std::uint64_t const replacement =
                candidate == std::numeric_limits<std::uint64_t>::max()
                ? 0 : candidate + 1;
            if (_next.compare_exchange_weak(
                    candidate, replacement, std::memory_order_relaxed,
                    std::memory_order_relaxed))
                return candidate;
        }
        return 0; // exhausted: never recycle or wrap a pass identifier.
    }

    std::uint64_t PeekNextForTestOnly() const
    {
        return _next.load(std::memory_order_relaxed);
    }

    // Only standalone tests should choose a near-overflow starting point.
    explicit VisibilityPassTokenSource(std::uint64_t first = 1) : _next(first) {}

private:
    std::atomic<std::uint64_t> _next;
};
} // namespace CosmicF14

#endif // COSMIC_F14_VISIBILITY_PASS_TOKEN_SOURCE_H
