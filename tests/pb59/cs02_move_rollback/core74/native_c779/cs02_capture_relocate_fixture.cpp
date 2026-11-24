// CS02 independent engine doubles around REAL Core74 bodies (test-only, not a worldserver build).
// REAL (extracted verbatim by run_capture_relocate.py, included below):
//   Player::CapturePB59InventorySlot, Player::RelocatePB59InventoryLinks, Player::GetItemByPos (both),
//   Player::GetBagByPos (PlayerStorage.cpp); Item::GetBagSlot, Item::SetPB59State,
//   Item::AddPB59ToUpdateQueueOf (Item.cpp); Bag::GetItemByPos (Bag.cpp);
//   PB59 headers from the real include path: InventorySlotFacts, InventoryCustody, TransactionAdmission,
//   EquipmentCaptureState, InventoryReservation.
// DOUBLES (this file): ObjectGuid, Object update-field storage and its write/force-update primitives
//   (with fault hooks), WorldSession, Player/Item/Bag data members and trivial inline accessors,
//   ItemTemplate, logging/ASSERT macros, the unreachable Item delete-path helpers.
#include "PB59InventorySlotFacts.h"
#include "PB59InventoryCustody.h"
#include "PB59TransactionAdmission.h"
#include "EquipmentCaptureState.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <list>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

using uint8 = std::uint8_t;
using uint16 = std::uint16_t;
using uint32 = std::uint32_t;
using int32 = std::int32_t;
using uint64 = std::uint64_t;
#define ASSERT(x) do { if (!(x)) { std::fprintf(stderr, "ASSERT %s\n", #x); std::abort(); } } while (0)
#define LOG_DEBUG(...) do { } while (0)

// Real values from Player.h / Bag.h / ItemTemplate.h (constants only).
#define INVENTORY_SLOT_BAG_0 255
#define MAX_BAG_SIZE 36
enum EquipmentSlots { EQUIPMENT_SLOT_BODY = 3, EQUIPMENT_SLOT_TABARD = 18, EQUIPMENT_SLOT_END = 19 };
enum InventorySlots { INVENTORY_SLOT_BAG_START = 19, INVENTORY_SLOT_BAG_END = 23 };
enum InventoryPackSlots { INVENTORY_SLOT_ITEM_START = 23, INVENTORY_SLOT_ITEM_END = 39 };
enum BankBagSlots { BANK_SLOT_BAG_START = 67, BANK_SLOT_BAG_END = 74 };
enum KeyRingSlots { KEYRING_SLOT_START = 86 };
enum CurrencyTokenSlots { CURRENCYTOKEN_SLOT_END = 150 };
enum ItemUpdateState { ITEM_UNCHANGED = 0, ITEM_CHANGED = 1, ITEM_NEW = 2, ITEM_REMOVED = 3 };
enum EnchantmentSlot { MAX_ENCHANTMENT_SLOT = 12 };
#define MAX_ITEM_PROTO_SPELLS 5
enum InventoryType { INVTYPE_WEAPON = 13, INVTYPE_BAG = 18 };
// Update-field indices: doubles (distinct values; GUID fields occupy two indices).
enum : uint16 {
    ITEM_FIELD_OWNER = 6, ITEM_FIELD_CONTAINED = 8, ITEM_FIELD_CREATOR = 10, ITEM_FIELD_GIFTCREATOR = 12,
    ITEM_FIELD_STACK_COUNT = 14, ITEM_FIELD_DURATION = 15, ITEM_FIELD_FLAGS = 21,
    ITEM_FIELD_PROPERTY_SEED = 55, ITEM_FIELD_RANDOM_PROPERTIES_ID = 56, ITEM_FIELD_DURABILITY = 57,
    ITEM_FIELD_MAXDURABILITY = 58, ITEM_FIELD_CREATE_PLAYED_TIME = 59,
    CONTAINER_FIELD_NUM_SLOTS = 64, CONTAINER_FIELD_SLOT_1 = 66, PLAYER_FIELD_INV_SLOT_HEAD = 324 };
enum : uint32 { ITEM_FIELD_FLAG_WRAPPED = 0x8, ITEM_FIELD_FLAG_BOP_TRADEABLE = 0x80, ITEM_FIELD_FLAG_REFUNDABLE = 0x1000 };

struct ObjectGuid
{
    uint64 value = 0;
    uint64 GetRawValue() const { return value; }
    std::string ToString() const { return std::to_string(value); }
    bool operator==(ObjectGuid const&) const = default;
    static const ObjectGuid Empty;
};
const ObjectGuid ObjectGuid::Empty{};

struct ItemTemplate { uint32 InventoryType = INVTYPE_WEAPON; };

// Fault/observation hooks on the double write primitives only.
struct Faults
{
    int writes = 0, forced = 0, throwAt = 0, hookAt = 0;
    bool persistent = false, hookAfter = false;
    std::function<void()> hook;
    void Write(auto fn)
    {
        ++writes;
        if (hookAt && writes == hookAt && hook && !hookAfter) hook();
        if (throwAt && (writes == throwAt || (persistent && writes >= throwAt)))
            throw std::runtime_error("injected write failure");
        fn();
        if (hookAt && writes == hookAt && hook && hookAfter) hook();
    }
};

class Player;
class Bag;
struct Object
{
    Faults* faults = nullptr;
    std::map<uint16, uint64> values;
    uint64 Raw(uint16 f) const { auto i = values.find(f); return i == values.end() ? 0 : i->second; }
    ObjectGuid GetGuidValue(uint16 f) const { return {Raw(f)}; }
    uint32 GetUInt32Value(uint16 f) const { return uint32(Raw(f)); }
    int32 GetInt32Value(uint16 f) const { return int32(uint32(Raw(f))); }
    bool HasFlag(uint16 f, uint32 flag) const { return (GetUInt32Value(f) & flag) != 0; }
    void SetGuidValue(uint16 f, ObjectGuid v)
    {
        if (GetGuidValue(f) == v) return; // real Object::SetGuidValue skips an unchanged value
        faults->Write([&] { values[f] = v.value; });
    }
    void ForceValuesUpdateAtIndex(uint32) { faults->Write([&] { ++faults->forced; }); }
};

class Item : public Object
{
public:
    virtual ~Item() = default;
    ObjectGuid guid;
    ItemTemplate const* proto = nullptr;
    uint8 m_slot = 0;
    Bag* m_container = nullptr;
    ItemUpdateState uState = ITEM_UNCHANGED;
    int32 uQueuePos = -1;
    bool mb_in_trade = false;
    bool m_lootGenerated = false;
    std::string m_text;
    std::array<int32, 5> charges{};
    std::array<PB59::EnchantFacts, 12> enchants{};

    ObjectGuid GetGUID() const { return guid; }
    ObjectGuid GetOwnerGUID() const { return GetGuidValue(ITEM_FIELD_OWNER); }
    ItemTemplate const* GetTemplate() const { return proto; }
    uint32 GetEntry() const { return 1000 + uint32(guid.value % 97); }
    uint32 GetCount() const { return GetUInt32Value(ITEM_FIELD_STACK_COUNT); }
    bool IsBag() const { return GetTemplate()->InventoryType == INVTYPE_BAG; }
    bool IsInTrade() const { return mb_in_trade; }
    bool IsRefundable() const { return HasFlag(ITEM_FIELD_FLAGS, ITEM_FIELD_FLAG_REFUNDABLE); }
    bool IsBOPTradable() const { return HasFlag(ITEM_FIELD_FLAGS, ITEM_FIELD_FLAG_BOP_TRADEABLE); }
    bool IsWrapped() const { return HasFlag(ITEM_FIELD_FLAGS, ITEM_FIELD_FLAG_WRAPPED); }
    int32 GetItemRandomPropertyId() const { return GetInt32Value(ITEM_FIELD_RANDOM_PROPERTIES_ID); }
    uint32 GetItemSuffixFactor() const { return GetUInt32Value(ITEM_FIELD_PROPERTY_SEED); }
    std::string const& GetText() const { return m_text; }
    int32 GetSpellCharges(uint8 i) const { return charges[i]; }
    uint32 GetEnchantmentId(EnchantmentSlot s) const { return enchants[s].id; }
    uint32 GetEnchantmentDuration(EnchantmentSlot s) const { return enchants[s].duration; }
    uint32 GetEnchantmentCharges(EnchantmentSlot s) const { return enchants[s].charges; }
    uint8 GetSlot() const { return m_slot; }
    Bag* GetContainer() { return m_container; }
    uint8 GetBagSlot() const;                                                   // REAL (Item.cpp)
    uint16 GetPos() const { return uint16(GetBagSlot()) << 8 | GetSlot(); }     // same as Item.h:313
    ItemUpdateState GetState() const { return uState; }
    bool IsInUpdateQueue() const { return uQueuePos != -1; }                    // same as Item.h:368
    uint32 GetQueuePos() const { return uQueuePos; }                            // same as Item.h:369
    void SetPB59State(ItemUpdateState state, Player* forplayer);                // REAL (Item.cpp)
    void AddPB59ToUpdateQueueOf(Player* player);                                // REAL (Item.cpp)
    void SetPB59Slot(uint8 slot) { m_slot = slot; }                             // same as Item.h:426
    void SetPB59Container(Bag* container) { m_container = container; }          // same as Item.h:427
    void RemoveFromUpdateQueueOf(Player*) { ASSERT(!"unreachable delete path"); }
    Bag* ToBag() { if (IsBag()) return reinterpret_cast<Bag*>(this); else return nullptr; }
};

class Bag : public Item
{
public:
    Item* m_bagslot[MAX_BAG_SIZE]{};
    uint32 GetBagSize() const { return GetUInt32Value(CONTAINER_FIELD_NUM_SLOTS); }
    Item* GetItemByPos(uint8 slot) const;                                       // REAL (Bag.cpp)
};

class WorldSession
{
public:
    Player* player = nullptr;
    bool loading = false, logout = false;
    Player* GetPlayer() const { return player; }
    bool PlayerLoading() const { return loading; }
    bool PlayerLogout() const { return logout; }
};

struct EnchantDuration { Item* item = nullptr; EnchantmentSlot slot = MAX_ENCHANTMENT_SLOT; uint32 leftduration = 0; };

class Player : public Object
{
public:
    ObjectGuid guid{42};
    WorldSession* session = nullptr;
    bool inWorld = true, alive = true;
    int mSemaphoreTeleport_Far = 0, mSemaphoreTeleport_Near = 0;
    bool m_itemUpdateQueueBlocked = false;
    ObjectGuid m_lootGuid;
    std::list<EnchantDuration> m_enchantDuration;
    Item* m_items[150]{};
    std::vector<Item*> m_itemUpdateQueue;
    PB59::EquipmentCaptureState _pb59Equipment;
    PB59::TransactionAdmission _pb59Transaction;

    ObjectGuid GetGUID() const { return guid; }
    WorldSession* GetSession() const { return session; }
    bool IsInWorld() const { return inWorld; }
    bool IsAlive() const { return alive; }
    bool IsBeingTeleportedFar() const { return mSemaphoreTeleport_Far != 0; }
    bool IsBeingTeleportedNear() const { return mSemaphoreTeleport_Near != 0; }
    ObjectGuid GetLootGUID() const { return m_lootGuid; }
    void DeleteRefundReference(ObjectGuid) { ASSERT(!"unreachable delete path"); }

    Item* GetItemByPos(uint16 pos) const;                                       // REAL
    Item* GetItemByPos(uint8 bag, uint8 slot) const;                            // REAL
    Bag* GetBagByPos(uint8 bag) const;                                          // REAL
    std::optional<PB59::InventorySlotFacts> CapturePB59InventorySlot(uint16 pos) const;      // REAL
    PB59::LinkTransferResult RelocatePB59InventoryLinks(PB59::InventorySlotFacts const& source,
        PB59::InventorySlotFacts const& destination, PB59::TransactionAdmission::Ticket ticket,
        PB59::TransactionAdmission::Access access);                             // REAL
};

#include "ItemGetBagSlot.inc"
#include "ItemSetPB59State.inc"
#include "ItemAddPB59ToUpdateQueueOf.inc"
#include "BagGetItemByPos.inc"
#include "PlayerGetItemByPos16.inc"
#include "PlayerGetItemByPos.inc"
#include "PlayerGetBagByPos.inc"
#include "CapturePB59InventorySlot.inc"
#include "RelocatePB59InventoryLinks.inc"

#include "cs02_capture_relocate_checks.inc"
