#include "AuctionResponseFence.h"
#include <cassert>
#include <cstdint>

using namespace AuctionResponseFence;
constexpr Stamp original{1, 42, 100, 1};
static_assert(MayEmit(original, original));
static_assert(!MayEmit(original, {2, 42, 100, 1})); // active policy tightening
static_assert(!MayEmit(original, {1, 42, 101, 1})); // reconnect same GUID
static_assert(!MayEmit(original, {1, 42, 100, 2})); // same-generation regression
static_assert(!MayEmit(original, {1, 43, 100, 1})); // another character
static_assert(!MayEmit(original, {})); // logged out / unknown current
static_assert(!MayEmit({}, {}));
static_assert(!MayEmit({0, 42, 100, 1}, {0, 42, 100, 1}));
static_assert(!MayEmit({1, 0, 100, 1}, {1, 0, 100, 1}));
static_assert(!MayEmit({1, 42, 0, 1}, {1, 42, 0, 1}));
static_assert(!MayEmit({1, 42, 100, 0}, {1, 42, 100, 0}));

int main()
{
    // The completed request and old tags can agree while the active policy
    // has changed. Test each identity dimension independently and together.
    for (std::uint64_t generation = 0; generation < 4; ++generation)
        for (std::uint64_t guid = 41; guid < 44; ++guid)
            for (std::uint64_t session = 99; session < 103; ++session)
                for (std::uint64_t revision = 0; revision < 4; ++revision)
                {
                    Stamp current{generation, guid, session, revision};
                    bool expected = generation == 1 && guid == 42 && session == 100 && revision == 1;
                    assert(MayEmit(original, current) == expected);
                }
}
