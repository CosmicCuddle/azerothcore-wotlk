#pragma once
#include <cstdint>

// Pure admission gate only: does not write a journal, commit databases, or grant items.
// Receipts must be verified against persisted trade-id, realm, owner, and payload hash.
namespace CosmicTrade
{
enum class SideState : std::uint8_t { None, Prepared, Committed, Refunded, Conflict };
enum class Decision : std::uint8_t { Hold, Complete, ManualRecovery };
struct Receipt
{
    std::uint64_t tradeId = 0;
    std::uint32_t realm = 0;
    std::uint64_t owner = 0;
    std::uint64_t payloadHash = 0;
    SideState state = SideState::None;
    bool durable = false;
};

inline Decision Decide(Receipt const& left, Receipt const& right) noexcept
{
    if (!left.tradeId || left.tradeId != right.tradeId || !left.realm || !right.realm ||
        left.realm == right.realm || !left.owner || !right.owner ||
        !left.payloadHash || !right.payloadHash)
        return Decision::ManualRecovery;
    if (left.state == SideState::Conflict || right.state == SideState::Conflict ||
        left.state == SideState::Refunded || right.state == SideState::Refunded)
        return Decision::ManualRecovery;
    if (!left.durable || !right.durable)
        return Decision::Hold;
    if (left.state == SideState::Committed && right.state == SideState::Committed)
        return Decision::Complete;
    return Decision::Hold;
}
}
