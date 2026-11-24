// CS02 / PB59 AutoGear move-rollback harness -- contract under test.
//
// PROPOSED contract, not an agreed native interface. It follows Claude's
// #51 proposal (comment 7118628149) and CS01's result classes (#51
// 7118592939, #52 7118630018): one synchronous owner-context call per command,
// an ordered batch of exact moves, and four truthful outcomes. When Codex
// publishes the bounded Core interface, add a native adapter that satisfies
// `MoveExecutor` and a native `Capture` (see README) -- the oracle and the
// scenario matrix stay unchanged.
#pragma once

#include "SyntheticInventoryCore.h"

#include <concepts>
#include <cstdint>
#include <string>
#include <vector>

namespace CS02MoveRollback
{
using Synthetic::Pos;

enum class Outcome
{
    Refused,          // no effects: nothing moved by the executor
    Applied,          // every move at its exact destination, identity preserved
    Restored,         // failed after effects; every item verified back at `from`
    RecoveryRequired  // failed and not provably restored; lists the GUIDs
};

inline char const* Name(Outcome o)
{
    switch (o)
    {
        case Outcome::Refused: return "Refused";
        case Outcome::Applied: return "Applied";
        case Outcome::Restored: return "Restored";
        case Outcome::RecoveryRequired: return "RecoveryRequired";
    }
    return "?";
}

struct Move
{
    std::uint64_t itemGuid = 0;
    Pos from, to; // `to` is exact, distinct and currently empty; never NULL_SLOT
};

struct Request
{
    std::uint64_t owner = 0, incarnation = 0, custodyStamp = 0;
    std::vector<Move> moves;
};

struct Report
{
    Outcome outcome = Outcome::Refused;
    std::string reason;
    std::vector<std::uint64_t> notWhereExpected; // RecoveryRequired only
};

template<class E>
concept MoveExecutor = requires(E& e, Synthetic::Core& core, Request const& r) {
    { e.Execute(core, r) } -> std::same_as<Report>;
};
}
