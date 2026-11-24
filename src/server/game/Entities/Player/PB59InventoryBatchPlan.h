#pragma once

#include "PB59InventorySlotFacts.h"
#include <span>

namespace PB59
{
enum class InventoryBatchOutcome { Refused, Applied, Restored, RecoveryRequired };
enum class InventoryBatchFailure { None, Context, Plan, UnsupportedState, Policy, Effects, Links, Restoration };
struct InventoryBatchResult
{
    InventoryBatchOutcome outcome = InventoryBatchOutcome::Refused;
    InventoryBatchFailure failure = InventoryBatchFailure::None;
    // GUIDs whose original position/instance could not be re-proven. Effect-
    // only recovery can have an empty list; this is not a DB durability claim.
    std::array<std::uint64_t, 19> recoveryGuids{};
    std::uint8_t recoveryCount = 0;
};

// Owner-context observations only. Validation does not acquire custody, invoke
// policy hooks, authorize mutation or prove that these observations are current.
struct InventoryBatchMove
{
    InventorySlotFacts source;
    InventorySlotFacts destination;
};

enum class InventoryBatchPlanError
{
    None, Empty, TooLarge, Owner, Source, Destination, DuplicateItem, Overlap
};

inline InventoryBatchPlanError ValidateInventoryBatchPlan(
    std::uint64_t owner, std::span<InventoryBatchMove const> moves) noexcept
{
    // The approved initial lane moves combat equipment only, never arbitrary
    // inventory, bags, stacks or a destination vacated by an earlier move.
    constexpr std::size_t maximumMoves = 19;
    if (moves.empty())
        return InventoryBatchPlanError::Empty;
    if (moves.size() > maximumMoves)
        return InventoryBatchPlanError::TooLarge;
    if (!owner)
        return InventoryBatchPlanError::Owner;
    for (std::size_t index = 0; index < moves.size(); ++index)
    {
        auto const& move = moves[index];
        if (move.source.owner != owner || move.destination.owner != owner)
            return InventoryBatchPlanError::Owner;
        if (!move.source.item || !move.source.item->guid || move.source.item->owner != owner ||
            !move.source.item->entry || !move.source.item->count)
            return InventoryBatchPlanError::Source;
        if (move.destination.item)
            return InventoryBatchPlanError::Destination;
        if (move.source.position == move.destination.position)
            return InventoryBatchPlanError::Overlap;
        for (std::size_t other = 0; other < index; ++other)
        {
            auto const& prior = moves[other];
            if (move.source.item->guid == prior.source.item->guid)
                return InventoryBatchPlanError::DuplicateItem;
            if (move.source.position == prior.source.position ||
                move.destination.position == prior.destination.position ||
                move.source.position == prior.destination.position ||
                move.destination.position == prior.source.position)
                return InventoryBatchPlanError::Overlap;
        }
    }
    return InventoryBatchPlanError::None;
}
}
