#pragma once

#include <cstdint>

namespace AuctionResponseFence
{
// Value-only token for a browse request and its completed response. All fields
// are captured and revalidated by the owner, not by a search worker.
// Generation must be monotonic across policy/mode changes; session is a fresh
// login incarnation; revision changes on every relevant eligibility change.
struct Stamp
{
    std::uint64_t generation = 0;
    std::uint64_t guid = 0;
    std::uint64_t session = 0;
    std::uint64_t revision = 0;
    bool operator==(Stamp const&) const = default;
};

constexpr bool Valid(Stamp const& stamp)
{
    return stamp.generation && stamp.guid && stamp.session && stamp.revision;
}

// 'current' must be read from the actual active policy and live viewer at the
// serialized response boundary. Comparing only request and worker tag is unsafe.
constexpr bool MayEmit(Stamp const& completed, Stamp const& current)
{
    return Valid(completed) && completed == current;
}
}
