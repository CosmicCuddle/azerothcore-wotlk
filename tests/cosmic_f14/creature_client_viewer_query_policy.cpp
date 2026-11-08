// CosmicCuddle F14 pure negative safety test — no game server needed.
// A true result is ONLY permission to consult a default-FALSE script hook;
// it is NOT permission to conceal or remove any creature.
#include "../../src/server/game/Entities/Player/F14CreatureClientVisibilityQuery.h"
#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>

namespace P = CosmicF14CreatureClientVisibilityQuery;
constexpr P::Candidate cOrdinaryNpc{true, false, false, false, false, false, false};
static_assert(P::MayAskScript(cOrdinaryNpc), "ordinary NPC may be queried, not hidden");
static_assert(!P::MayAskScript({}), "noncreature source is never queried");
static_assert(!P::MayAskScript({false, false, false, false, false, false, false}),
              "player target must never be hidden by NPC seam");
static_assert(!P::MayAskScript({true, true, false, false, false, false, false}),
              "GM-on observer must remain unaffected");
static_assert(!P::MayAskScript({true, false, true, false, false, false, false}),
              "owned/charmed creature must remain unaffected");
static_assert(!P::MayAskScript({true, false, false, true, false, false, false}),
              "pet must remain unaffected");
static_assert(!P::MayAskScript({true, false, false, false, true, false, false}),
              "summon must remain unaffected");
static_assert(!P::MayAskScript({true, false, false, false, false, true, false}),
              "guardian must remain unaffected");
static_assert(!P::MayAskScript({true, false, false, false, false, false, true}),
              "totem must remain unaffected");

int main()
{
    unsigned checked = 0;
    // Exhaust all 128 combinations including noncreature, GM and ownership.
    for (unsigned bits = 0; bits != 128; ++bits)
    {
        P::Candidate const current {
            bool(bits & 1),
            bool(bits & 2),
            bool(bits & 4),
            bool(bits & 8),
            bool(bits & 16),
            bool(bits & 32),
            bool(bits & 64)
        };
        bool const expected = bits == 1;
        assert(P::MayAskScript(current) == expected);
        ++checked;
    }
    std::printf("F14 CLIENT VIEWER POLICY 128/128 SAFE\n");
    return checked == 128 ? 0 : 1;
}
