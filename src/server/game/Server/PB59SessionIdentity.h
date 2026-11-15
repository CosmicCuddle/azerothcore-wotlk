#pragma once

#include <atomic>
#include <cstdint>
#include <limits>

namespace PB59
{
// Process-local, non-recycling identity. Separate namespace from Player
// incarnation; zero means exhausted and cannot authorize recovery.
class SessionIncarnationSequence final
{
public:
    explicit constexpr SessionIncarnationSequence(std::uint64_t initial = 0) noexcept : _last(initial) { }
    [[nodiscard]] std::uint64_t Issue() noexcept
    {
        auto value = _last.load(std::memory_order_relaxed);
        while (value != std::numeric_limits<std::uint64_t>::max())
            if (_last.compare_exchange_weak(value, value + 1, std::memory_order_relaxed))
                return value + 1;
        return 0;
    }
private:
    std::atomic<std::uint64_t> _last;
};

class SessionIdentity final
{
public:
    SessionIdentity() noexcept : _id(_sequence.Issue()) { }
    SessionIdentity(SessionIdentity const&) = delete;
    SessionIdentity& operator=(SessionIdentity const&) = delete;
    [[nodiscard]] std::uint64_t Get() const noexcept { return _id; }
private:
    inline static constinit SessionIncarnationSequence _sequence{0};
    std::uint64_t const _id;
};
}
