#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>

namespace PB59
{
// Value copies only. No pointer or foreign-thread access. Time values are
// effective values at a single owner-context observation, not a save proof.
struct EnchantFacts
{
    std::uint32_t id = 0, duration = 0, charges = 0;
    bool operator==(EnchantFacts const&) const = default;
};
struct InventoryInstanceFacts
{
    std::uint64_t guid = 0, owner = 0, creator = 0, giftCreator = 0;
    std::uint32_t entry = 0, count = 0, duration = 0, flags = 0;
    std::uint32_t durability = 0, maxDurability = 0, playedTime = 0, suffix = 0;
    std::int32_t randomProperty = 0;
    std::array<std::int32_t, 5> spellCharges{};
    std::array<EnchantFacts, 12> enchants{};
    std::string text;
    bool operator==(InventoryInstanceFacts const&) const = default;
};
struct InventorySlotFacts
{
    std::uint64_t owner = 0, bag = 0; // bag=0 for the base inventory
    std::uint16_t position = 0;
    std::optional<InventoryInstanceFacts> item;
    bool operator==(InventorySlotFacts const&) const = default;
};

// A null pointer is empty only when the corresponding update field is empty
// as well. A missing/replaced container or inconsistent link is unknown.
inline bool ValidInventorySlot(InventorySlotFacts const& facts,
    std::uint64_t slotFieldGuid, bool validPosition, bool coherentLinks)
{
    if (!facts.owner || !facts.position || !validPosition || !coherentLinks)
        return false;
    if (!facts.item)
        return slotFieldGuid == 0;
    auto const& item = *facts.item;
    return item.guid && item.guid == slotFieldGuid && item.owner == facts.owner &&
        item.entry && item.count;
}

// A slot's container and position may change during a valid move; the full
// copied instance must not. No time may elapse in an exact synchronous move.
inline bool SameInventoryInstance(InventorySlotFacts const& before,
    InventorySlotFacts const& after)
{
    return before.owner && before.owner == after.owner && before.item &&
        after.item && *before.item == *after.item;
}
}
