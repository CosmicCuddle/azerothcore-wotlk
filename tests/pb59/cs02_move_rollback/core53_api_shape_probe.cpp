// CS02 / PB59 move-rollback harness -- compile-only probe against REAL Core53 headers.
//
// Pins the synthetic model to the exact public Core53 (79b0b1fe) signatures it
// imitates. If a signature changes, this TU stops compiling and the synthetic
// model must be re-derived. Compile-only: nothing is linked or executed, no
// worldserver is built. Private exact seams (_StoreItem/_EquipItem with
// PB59::ExactStorageReservation, RelocatePB59InventoryLinks) cannot be named
// from outside Player and are deliberately not probed; Codex owns that seam.
#include "Player.h"
#include "ScriptMgr.h"

#include <type_traits>

static_assert(std::is_same_v<decltype(&Player::CanUnequipItem), InventoryResult (Player::*)(uint16, bool) const>);
static_assert(std::is_same_v<decltype(static_cast<InventoryResult (Player::*)(uint8, uint8, ItemPosCountVec&, Item*, bool) const>(
                                 &Player::CanStoreItem)),
    InventoryResult (Player::*)(uint8, uint8, ItemPosCountVec&, Item*, bool) const>);
static_assert(std::is_same_v<decltype(&Player::CanEquipItem), InventoryResult (Player::*)(uint8, uint16&, Item*, bool, bool) const>);
static_assert(std::is_same_v<decltype(&Player::StoreItem), Item* (Player::*)(ItemPosCountVec const&, Item*, bool)>);
static_assert(std::is_same_v<decltype(&Player::EquipItem), Item* (Player::*)(uint16, Item*, bool)>);
static_assert(std::is_same_v<decltype(&Player::RemoveItem), void (Player::*)(uint8, uint8, bool)>);
static_assert(std::is_same_v<decltype(&Player::AutoUnequipOffhandIfNeed), void (Player::*)(bool)>);
static_assert(std::is_same_v<decltype(&ScriptMgr::OnPlayerCanUnequipItem), bool (ScriptMgr::*)(Player*, uint16, bool)>);
// Layout constants the synthetic core copies.
static_assert(INVENTORY_SLOT_BAG_0 == 255 && NULL_BAG == 0 && NULL_SLOT == 255);
static_assert(EQUIPMENT_SLOT_END == 19 && EQUIPMENT_SLOT_MAINHAND == 15 && EQUIPMENT_SLOT_OFFHAND == 16);
static_assert(INVENTORY_SLOT_BAG_START == 19 && INVENTORY_SLOT_BAG_END == 23);
static_assert(INVENTORY_SLOT_ITEM_START == 23 && INVENTORY_SLOT_ITEM_END == 39);
