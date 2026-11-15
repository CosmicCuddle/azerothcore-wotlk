#pragma once

#include "PB59InventorySlotFacts.h"

namespace PB59
{
struct VisibleEquipmentFacts
{
    std::uint32_t entry = 0;
    std::uint16_t permanent = 0, temporary = 0;
    bool operator==(VisibleEquipmentFacts const&) const = default;
};
inline VisibleEquipmentFacts VisibleEquipment(InventorySlotFacts const& slot)
{
    if (!slot.item)
        return {};
    return {slot.item->entry, static_cast<std::uint16_t>(slot.item->enchants[0].id),
        static_cast<std::uint16_t>(slot.item->enchants[1].id)};
}
// Only a single equip/unequip of the same combat slot is covered. The caller
// must independently prove the exact original and current inventory states.
inline bool VisibleTransitionValid(InventorySlotFacts const& before, InventorySlotFacts const& after)
{
    return before.owner && before.owner == after.owner && before.position &&
        before.position == after.position && !before.bag && !after.bag &&
        before.item.has_value() != after.item.has_value();
}
// Each field can be the old or new stock value after an interrupted update.
// Custom/third-party display values must not be overwritten on a guess.
inline bool CanReconcileVisible(VisibleEquipmentFacts const& before,
    VisibleEquipmentFacts const& after, VisibleEquipmentFacts const& current)
{
    return (current.entry == before.entry || current.entry == after.entry) &&
        (current.permanent == before.permanent || current.permanent == after.permanent) &&
        (current.temporary == before.temporary || current.temporary == after.temporary);
}
}
