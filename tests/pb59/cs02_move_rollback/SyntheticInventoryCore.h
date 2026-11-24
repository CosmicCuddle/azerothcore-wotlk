// CS02 / PB59 AutoGear move-rollback harness -- SYNTHETIC inventory core.
//
// *** SYNTHETIC. This is NOT the native Player. ***
// It is a small value model of the Core53 (79b0b1fe) PlayerStorage paths that
// the AutoGear strip/restore uses, written from the exact source:
//   - Player::CanUnequipItem runs sScriptMgr->OnPlayerCanUnequipItem FIRST
//     (PlayerStorage.cpp:2653), so any held Item* may be stale afterwards;
//   - Player::StoreItem/_StoreItem return the stored item, which "can differ
//     from pItem" after a stack merge, and pItem "can be deleted" (3283-3420);
//   - Player::RemoveItem clears the slot, item mods, visible field and
//     enchant/item durations but keeps the owner field (3724-3800);
//   - Player::AutoUnequipOffhandIfNeed moves the off-hand to bags or, with
//     full bags, removes it from the inventory and mails it (Player.cpp:12785).
// Results produced against this model are SYNTHETIC and are never evidence of
// native success, lifetime safety or durable DB atomicity.
//
// Items are never freed while a scenario runs: a "destroyed" item becomes a
// tombstone, and any later use through a held pointer is RECORDED as a stale
// pointer use instead of being undefined behaviour. That keeps the hazard
// observable and the harness clean under ASan/UBSan.
#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace CS02MoveRollback::Synthetic
{
// Same numeric layout as Core53 (Player.h / ItemDefines.h) for the base inventory.
constexpr std::uint8_t BAG0 = 255;             // INVENTORY_SLOT_BAG_0
constexpr std::uint8_t NULL_BAG = 0;           // NULL_BAG
constexpr std::uint8_t NULL_SLOT = 255;        // NULL_SLOT
constexpr std::uint8_t EQUIP_END = 19;         // EQUIPMENT_SLOT_END
constexpr std::uint8_t MAINHAND = 15, OFFHAND = 16;
constexpr std::uint8_t BAG_START = 19, BAG_END = 23;       // INVENTORY_SLOT_BAG_START/END
constexpr std::uint8_t PACK_START = 23, PACK_END = 39;     // INVENTORY_SLOT_ITEM_START/END
constexpr std::uint8_t BASE_SLOTS = 39;

enum class InventoryResult { Ok, CantDoRightNow, ItemNotFound, NotInCombat, CantEquip,
    BagFull, CantStack, OnlyEmptyBags, AlreadyLooted, InTrade, NotOwner, InvalidPosition };

struct Pos
{
    std::uint8_t bag = BAG0, slot = NULL_SLOT;
    constexpr std::uint16_t Packed() const { return std::uint16_t((bag << 8) | slot); }
    constexpr bool operator==(Pos const&) const = default;
    constexpr auto operator<=>(Pos const&) const = default;
    bool IsEquipment() const { return bag == BAG0 && slot < EQUIP_END; }
};

struct Item
{
    std::uint64_t guid = 0, owner = 0;
    std::uint32_t entry = 0, count = 1, maxStack = 1;
    std::uint32_t durability = 0, enchantId = 0;
    bool twoHanded = false, isBag = false, inTrade = false, lootGenerated = false;
    std::uint8_t bagSize = 0;
    // link state as the native Item carries it
    Pos pos{BAG0, NULL_SLOT};
    std::uint64_t containedIn = 0; // ITEM_FIELD_CONTAINED: owner or bag guid, 0 when detached
    bool tombstone = false;        // destroyed; memory kept on purpose
};

enum class Actor { Executor, Hook, Core };
enum class EventKind { Remove, Insert, Merge, Destroy, Mail, Clone, StaleUse, MutationWhileClosed,
    HookCall,
    PendingIntent,  // a callback registered a deferred logout/save intent (recorded by the test)
    UseAfterUnlink  // SYNTHETIC in-call effect helper ran after its item was unlinked
};
struct Event
{
    Actor actor;
    EventKind kind;
    std::uint64_t guid;
    Pos pos;
    int phase; // 0 preflight, 1 forward, 2 reverse, -1 unknown
};

// Fault injection on the n-th native insertion/removal the core performs.
enum class InsertFault { None, Refuse, Throw, InsertThenThrow, WrongPlace, ReplacePointer };
enum class RemoveFault { None, Noop, Throw };

struct InjectedFailure : std::runtime_error
{
    using std::runtime_error::runtime_error;
};

class Core
{
public:
    std::uint64_t ownerGuid = 0x100;
    std::uint64_t incarnation = 7;
    std::uint64_t custodyStamp = 42;
    bool loggingOut = false, retiring = false, inCombat = false;
    int transactionDepth = 0; // re-entry fence the executor is expected to honour
    int phase = -1;           // set by executors for ledger attribution only

    std::function<bool(Core&, std::uint16_t pos, bool swap)> onPlayerCanUnequipItem;
    // SYNTHETIC analogue of the in-call boundary Codex reported (#51 7131425394):
    // _ApplyItemMods -> _ApplyItemBonuses -> OnPlayerCustomScalingStatValueBefore,
    // then three effect helpers still receive the original item. Fires after an
    // insertion into an equipment slot. Not a claim about any installed module.
    std::function<void(Core&, Item*)> onApplyItemMods;
    int effectHelpersAfterCallback = 3;
    void RecordPendingIntent() { Record(Actor::Hook, EventKind::PendingIntent, 0, Pos{}); }
    std::map<int, InsertFault> insertFaults; // by insertion ordinal (0-based)
    std::map<int, RemoveFault> removeFaults; // by removal ordinal (0-based)

    std::vector<Event> ledger;
    std::array<int, EQUIP_END> appliedMods{};      // +1 at equip, -1 at unequip
    std::array<std::uint64_t, EQUIP_END> visible{}; // PLAYER_VISIBLE_ITEM_n_ENTRYID analogue (guid)
    std::array<std::uint64_t, BASE_SLOTS> slotField{}; // PLAYER_FIELD_INV_SLOT_HEAD analogue
    std::multiset<std::uint64_t> durations;           // enchant/item duration registrations
    std::set<std::uint64_t> updateQueue;              // ITEM_CHANGED queue (normal save marking)
    std::vector<std::uint64_t> mailed;                // AutoUnequipOffhandIfNeed full-bag path

    Core() = default;
    Core(Core const&) = delete;
    Core& operator=(Core const&) = delete;

    // ----- fixture construction (not native calls) -----
    Item* Create(std::uint32_t entry, std::uint64_t guid, Pos at, bool twoHanded = false)
    {
        auto& it = Alloc(guid, entry);
        it.twoHanded = twoHanded;
        Link(&it, at);
        return &it;
    }
    Item* CreateBag(std::uint64_t guid, std::uint8_t bagSlot, std::uint8_t size)
    {
        auto& it = Alloc(guid, 9000 + size);
        it.isBag = true;
        it.bagSize = size;
        bags[bagSlot].assign(size, nullptr);
        Link(&it, Pos{BAG0, bagSlot});
        return &it;
    }
    Item* CreateDetached(std::uint32_t entry, std::uint64_t guid)
    {
        return &Alloc(guid, entry);
    }

    // ----- reads -----
    Item* GetItemByPos(std::uint8_t bag, std::uint8_t slot) const
    {
        if (bag == BAG0)
            return slot < BASE_SLOTS ? base[slot] : nullptr;
        auto b = bags.find(bag);
        if (b == bags.end() || !BagAt(bag) || slot >= b->second.size())
            return nullptr;
        return b->second[slot];
    }
    Item* GetItemByPos(Pos p) const { return GetItemByPos(p.bag, p.slot); }
    Item* BagAt(std::uint8_t bagSlot) const
    {
        Item* b = (bagSlot >= BAG_START && bagSlot < BAG_END) ? base[bagSlot] : nullptr;
        return b && b->isBag ? b : nullptr;
    }
    bool ValidPosition(Pos p) const
    {
        if (p.bag == BAG0)
            return p.slot < BASE_SLOTS;
        Item* b = BagAt(p.bag);
        return b && p.slot < b->bagSize;
    }
    std::uint64_t BagGuid(Pos p) const
    {
        if (p.bag == BAG0)
            return 0;
        Item* b = BagAt(p.bag);
        return b ? b->guid : 0;
    }
    std::vector<Item*> AllArena() const
    {
        std::vector<Item*> out;
        for (auto const& p : arena)
            out.push_back(p.get());
        return out;
    }
    Item* FindArena(std::uint64_t guid) const
    {
        for (auto const& p : arena)
            if (p->guid == guid && !p->tombstone)
                return p.get();
        return nullptr;
    }
    int insertions = 0, removals = 0;

    // ----- native-shaped API -----
    // Core53: hook FIRST, then the stock checks.
    InventoryResult CanUnequipItem(std::uint16_t pos, bool swap)
    {
        Record(Actor::Core, EventKind::HookCall, 0, Unpack(pos));
        if (onPlayerCanUnequipItem)
        {
            Actor saved = actor;
            actor = Actor::Hook;
            bool ok = onPlayerCanUnequipItem(*this, pos, swap);
            actor = saved;
            if (!ok)
                return InventoryResult::CantDoRightNow;
        }
        Item* it = GetItemByPos(Unpack(pos));
        if (!it)
            return InventoryResult::Ok;
        if (it->lootGenerated)
            return InventoryResult::AlreadyLooted;
        if (inCombat)
            return InventoryResult::NotInCombat;
        if (!swap && it->isBag && BagNotEmpty(it))
            return InventoryResult::OnlyEmptyBags;
        return InventoryResult::Ok;
    }

    // bag/slot NULL_BAG/NULL_SLOT = "anywhere" (native search, may merge).
    InventoryResult CanStoreItem(std::uint8_t bag, std::uint8_t slot, std::vector<Pos>& dest, Item* item) const
    {
        dest.clear();
        if (!Live(item))
            return InventoryResult::ItemNotFound;
        if (item->inTrade)
            return InventoryResult::InTrade;
        if (bag != NULL_BAG || slot != NULL_SLOT)
        {
            Pos p{bag, slot};
            if (!ValidPosition(p) || p.IsEquipment() || (p.bag == BAG0 && p.slot < PACK_START))
                return InventoryResult::InvalidPosition;
            Item* there = GetItemByPos(p);
            if (there && there != item)
            {
                if (there->entry != item->entry || there->count + item->count > there->maxStack)
                    return InventoryResult::CantStack;
            }
            dest.push_back(p);
            return InventoryResult::Ok;
        }
        // merge first (native behaviour), then first free general slot
        for (Pos p : StoragePositions())
        {
            Item* there = GetItemByPos(p);
            if (there && there != item && there->entry == item->entry && item->maxStack > 1 &&
                there->count + item->count <= there->maxStack)
            {
                dest.push_back(p);
                return InventoryResult::Ok;
            }
        }
        for (Pos p : StoragePositions())
            if (!GetItemByPos(p))
            {
                dest.push_back(p);
                return InventoryResult::Ok;
            }
        return InventoryResult::BagFull;
    }

    InventoryResult CanEquipItem(std::uint8_t slot, Item* item) const
    {
        if (!Live(item))
            return InventoryResult::ItemNotFound;
        if (slot >= EQUIP_END)
            return InventoryResult::InvalidPosition;
        if (GetItemByPos(BAG0, slot) && GetItemByPos(BAG0, slot) != item)
            return InventoryResult::CantEquip;
        if (slot == OFFHAND)
            if (Item* mh = GetItemByPos(BAG0, MAINHAND); mh && mh != item && mh->twoHanded)
                return InventoryResult::CantEquip;
        if (inCombat)
            return InventoryResult::NotInCombat;
        return InventoryResult::Ok;
    }

    void RemoveItem(std::uint8_t bag, std::uint8_t slot)
    {
        int ord = removals++;
        RemoveFault f = Fault(removeFaults, ord, RemoveFault::None);
        if (f == RemoveFault::Throw)
            throw InjectedFailure("injected RemoveItem throw");
        if (f == RemoveFault::Noop)
            return;
        Item* it = GetItemByPos(bag, slot);
        if (!it)
            return;
        Unlink(it);
    }

    // Returns the stored item; after a merge it differs from `item` and
    // `item` is destroyed (Core53 comment at _StoreItem).
    Item* StoreItem(std::vector<Pos> const& dest, Item* item)
    {
        if (!Use(item) || dest.empty())
            return nullptr;
        return Insert(dest.back(), item, /*allowMerge*/ true);
    }
    Item* EquipItem(std::uint16_t pos, Item* item)
    {
        if (!Use(item))
            return nullptr;
        return Insert(Unpack(pos), item, /*allowMerge*/ false);
    }
    // Exact insertion analogue: refuses occupied destination; never merges.
    Item* StoreExact(Pos p, Item* item)
    {
        if (!Use(item))
            return nullptr;
        if (GetItemByPos(p))
            return nullptr;
        return Insert(p, item, false);
    }

    // Player.cpp:12785 analogue: a two-hander in the main hand forces the off-hand out.
    void AutoUnequipOffhandIfNeed()
    {
        Item* mh = GetItemByPos(BAG0, MAINHAND);
        Item* oh = GetItemByPos(BAG0, OFFHAND);
        if (!mh || !oh || !mh->twoHanded)
            return;
        std::vector<Pos> dest;
        Actor saved = actor;
        actor = Actor::Core;
        if (CanStoreItem(NULL_BAG, NULL_SLOT, dest, oh) == InventoryResult::Ok)
        {
            Unlink(oh);
            Insert(dest.back(), oh, true);
        }
        else
        {
            Unlink(oh);
            Record(Actor::Core, EventKind::Mail, oh->guid, oh->pos);
            mailed.push_back(oh->guid);
            oh->owner = 0; // left the inventory
        }
        actor = saved;
    }

    // Used by hooks to model callback side effects.
    void HookMergeAway(Item* victim)
    {
        if (!Live(victim))
            return;
        Unlink(victim);
        Destroy(victim);
    }
    void HookRelocate(Item* it, Pos to)
    {
        Unlink(it);
        Link(it, to);
        Record(actor, EventKind::Insert, it->guid, to);
    }

    std::vector<Pos> StoragePositions() const
    {
        std::vector<Pos> out;
        for (std::uint8_t s = PACK_START; s < PACK_END; ++s)
            out.push_back(Pos{BAG0, s});
        for (std::uint8_t b = BAG_START; b < BAG_END; ++b)
            if (Item* bag = BagAt(b))
                for (std::uint8_t s = 0; s < bag->bagSize; ++s)
                    out.push_back(Pos{b, s});
        return out;
    }
    std::vector<Pos> AllPositions() const
    {
        std::vector<Pos> out;
        for (std::uint8_t s = 0; s < BASE_SLOTS; ++s)
            out.push_back(Pos{BAG0, s});
        for (std::uint8_t b = BAG_START; b < BAG_END; ++b)
            if (Item* bag = BagAt(b))
                for (std::uint8_t s = 0; s < bag->bagSize; ++s)
                    out.push_back(Pos{b, s});
        return out;
    }
    static Pos Unpack(std::uint16_t p) { return Pos{std::uint8_t(p >> 8), std::uint8_t(p & 0xFF)}; }
    bool Live(Item const* it) const { return it && !it->tombstone; }
    Actor actor = Actor::Executor;

private:
    std::vector<std::unique_ptr<Item>> arena;
    std::array<Item*, BASE_SLOTS> base{};
    std::map<std::uint8_t, std::vector<Item*>> bags;
    std::uint64_t nextCloneGuid = 0xC000;

    Item& Alloc(std::uint64_t guid, std::uint32_t entry)
    {
        arena.push_back(std::make_unique<Item>());
        Item& it = *arena.back();
        it.guid = guid;
        it.entry = entry;
        it.owner = ownerGuid;
        it.durability = 50 + std::uint32_t(guid % 17);
        it.enchantId = std::uint32_t(guid % 5);
        return it;
    }
    template<class F>
    static F Fault(std::map<int, F>& m, int ord, F none)
    {
        auto f = m.find(ord);
        return f == m.end() ? none : f->second;
    }
    bool BagNotEmpty(Item const* bag) const
    {
        for (std::uint8_t b = BAG_START; b < BAG_END; ++b)
            if (base[b] == bag)
            {
                auto it = bags.find(b);
                return it != bags.end() &&
                    std::any_of(it->second.begin(), it->second.end(), [](Item* i) { return i; });
            }
        return false;
    }
    bool Use(Item* it)
    {
        if (!it)
            return false;
        if (it->tombstone)
        {
            Record(actor, EventKind::StaleUse, it->guid, it->pos);
            return false;
        }
        return true;
    }
    void Record(Actor a, EventKind k, std::uint64_t guid, Pos p)
    {
        ledger.push_back(Event{a, k, guid, p, phase});
        if (a == Actor::Executor && (k == EventKind::Remove || k == EventKind::Insert) &&
            (loggingOut || retiring))
            ledger.push_back(Event{a, EventKind::MutationWhileClosed, guid, p, phase});
    }
    void Link(Item* it, Pos p)
    {
        if (p.bag == BAG0)
        {
            base[p.slot] = it;
            slotField[p.slot] = it->guid;
            it->containedIn = ownerGuid;
        }
        else
        {
            bags[p.bag][p.slot] = it;
            it->containedIn = BagAt(p.bag)->guid;
        }
        it->pos = p;
        it->owner = ownerGuid;
        if (p.IsEquipment())
        {
            appliedMods[p.slot] += 1;
            visible[p.slot] = it->guid;
        }
        durations.insert(it->guid);
        updateQueue.insert(it->guid);
    }
    void Unlink(Item* it)
    {
        Pos p = it->pos;
        if (p.slot == NULL_SLOT)
            return;
        if (p.bag == BAG0)
        {
            base[p.slot] = nullptr;
            slotField[p.slot] = 0;
        }
        else
            bags[p.bag][p.slot] = nullptr;
        if (p.IsEquipment())
        {
            appliedMods[p.slot] -= 1;
            visible[p.slot] = 0;
        }
        if (auto d = durations.find(it->guid); d != durations.end())
            durations.erase(d);
        it->containedIn = 0;
        it->pos = Pos{BAG0, NULL_SLOT};
        Record(actor, EventKind::Remove, it->guid, p);
    }
    void Destroy(Item* it)
    {
        it->tombstone = true;
        updateQueue.erase(it->guid);
        Record(actor, EventKind::Destroy, it->guid, it->pos);
    }
    Item* Insert(Pos p, Item* item, bool allowMerge)
    {
        int ord = insertions++;
        InsertFault f = Fault(insertFaults, ord, InsertFault::None);
        if (f == InsertFault::Throw)
            throw InjectedFailure("injected insertion throw");
        if (f == InsertFault::Refuse || !ValidPosition(p))
            return nullptr;
        if (f == InsertFault::WrongPlace)
        {
            for (Pos q : StoragePositions())
                if (!GetItemByPos(q) && q != p)
                {
                    p = q;
                    break;
                }
        }
        Item* there = GetItemByPos(p);
        // Core53 _StoreItem: any occupant takes the merge branch, including
        // the item itself (count += count, then the source is deleted).
        if (there)
        {
            if (!allowMerge || there->entry != item->entry)
                return nullptr;
            there->count += item->count;
            Record(actor, EventKind::Merge, item->guid, p);
            Destroy(item);
            return there;
        }
        if (f == InsertFault::ReplacePointer)
        {
            // pointer replacement: a new object carries the stack, original deleted
            Item& clone = Alloc(nextCloneGuid++, item->entry);
            clone.count = item->count;
            Record(actor, EventKind::Clone, clone.guid, p);
            Destroy(item);
            Link(&clone, p);
            Record(actor, EventKind::Insert, clone.guid, p);
            return &clone;
        }
        Link(item, p);
        Record(actor, EventKind::Insert, item->guid, p);
        if (p.IsEquipment() && onApplyItemMods)
        {
            Actor saved = actor;
            actor = Actor::Hook;
            onApplyItemMods(*this, item);
            actor = saved;
            for (int h = 0; h < effectHelpersAfterCallback; ++h)
                if (!Live(item) || item->pos != p)
                    Record(Actor::Core, EventKind::UseAfterUnlink, item->guid, p);
        }
        if (f == InsertFault::InsertThenThrow)
            throw InjectedFailure("injected throw after insertion");
        return item;
    }
};
}
