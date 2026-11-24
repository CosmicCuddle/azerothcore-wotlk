// CS02 Layer 3: REAL PB53 PlayerbotFactory::InitAmmo, DestroyObsoleteAmmo and PlayerbotFactory::DisplacesOffhand
// (extracted verbatim by run_layer3.py) over CS02 engine doubles. Not a worldserver build.
#include <cstdint>
#include <cstdio>
#include <map>
#include <memory>
#include <string>
#include <vector>
using uint8 = std::uint8_t; using uint16 = std::uint16_t; using uint32 = std::uint32_t; using uint64 = std::uint64_t; using int32 = std::int32_t;
// Real values (SharedDefines.h / ItemTemplate.h / Player.h); doubles for everything behavioural.
enum { CLASS_WARRIOR = 1, CLASS_PALADIN = 2, CLASS_HUNTER = 3, CLASS_ROGUE = 4 };
enum { ITEM_CLASS_WEAPON = 2, ITEM_CLASS_PROJECTILE = 6 };
enum { ITEM_SUBCLASS_WEAPON_BOW = 2, ITEM_SUBCLASS_WEAPON_GUN = 3, ITEM_SUBCLASS_WEAPON_POLEARM = 6, ITEM_SUBCLASS_WEAPON_STAFF = 10,
       ITEM_SUBCLASS_WEAPON_CROSSBOW = 18, ITEM_SUBCLASS_WEAPON_FISHING_POLE = 20, ITEM_SUBCLASS_WEAPON_AXE = 0, ITEM_SUBCLASS_WEAPON_AXE2 = 1 };
enum { ITEM_SUBCLASS_ARROW = 2, ITEM_SUBCLASS_BULLET = 3 };
enum { INVTYPE_WEAPON = 13, INVTYPE_SHIELD = 14, INVTYPE_2HWEAPON = 17, INVTYPE_WEAPONMAINHAND = 21, INVTYPE_WEAPONOFFHAND = 22, INVTYPE_HOLDABLE = 23 };
#define INVENTORY_SLOT_BAG_0 255
constexpr uint8 NULL_SLOT = 255;
enum { EQUIPMENT_SLOT_MAINHAND = 15, EQUIPMENT_SLOT_OFFHAND = 16, EQUIPMENT_SLOT_RANGED = 17,
       INVENTORY_SLOT_ITEM_START = 23, INVENTORY_SLOT_ITEM_END = 39, INVENTORY_SLOT_BAG_START = 19, INVENTORY_SLOT_BAG_END = 23 };
struct ItemTemplate
{
    uint32 ItemId = 0, Class = 0, SubClass = 0, InventoryType = 0, ItemLevel = 0, Stackable = 1;
    uint32 GetMaxStackSize() const { return Stackable; }
};
struct ItemPosCount { uint16 pos; uint32 count; };
using ItemPosCountVec = std::vector<ItemPosCount>;
class Player;
class Item
{
public:
    ItemTemplate const* proto = nullptr; uint8 bagSlot = INVENTORY_SLOT_BAG_0, slot = 0; uint32 count = 0; bool queued = false;
    ItemTemplate const* GetTemplate() const { return proto; }
    uint8 GetBagSlot() const { return bagSlot; }
    uint8 GetSlot() const { return slot; }
    void AddToUpdateQueueOf(Player*) { queued = true; }
};
class Bag : public Item
{
public:
    std::vector<Item*> items = std::vector<Item*>(4, nullptr);
    uint32 GetBagSize() const { return uint32(items.size()); }
    Item* GetItemByPos(uint8 s) const { return s < items.size() ? items[s] : nullptr; }
};
struct Templates { std::map<uint32, ItemTemplate> t; ItemTemplate const* GetItemTemplate(uint32 e) const { auto i = t.find(e); return i == t.end() ? nullptr : &i->second; } };
static Templates g_templates; static Templates* sObjectMgr = &g_templates;
struct RandomItemMgr { uint32 ammo = 0; uint32 GetAmmo(uint32, uint32) const { return ammo; } };
static RandomItemMgr sRandomItemMgr;
class Player
{
public:
    uint8 cls = CLASS_HUNTER; bool dual = true, titan = false;
    std::vector<std::unique_ptr<Item>> owned;
    Item* items[39]{}; Bag* bags[23]{};
    std::vector<std::pair<uint8, uint8>> destroyed; uint32 ammo = 0; uint32 stored = 0;
    uint8 getClass() const { return cls; }
    bool CanDualWield() const { return dual; }
    bool CanTitanGrip() const { return titan; }
    Item* GetItemByPos(uint8 bag, uint8 slot) const
    { if (bag == INVENTORY_SLOT_BAG_0) return slot < 39 ? items[slot] : nullptr; return bag < 23 && bags[bag] ? bags[bag]->GetItemByPos(slot) : nullptr; }
    Bag* GetBagByPos(uint8 bag) const { return bag < 23 ? bags[bag] : nullptr; }
    uint32 GetItemCount(uint32 entry) const
    {
        uint32 n = 0;
        for (auto const& o : owned) if (o->proto && o->proto->ItemId == entry && Present(o.get())) n += o->count;
        return n;
    }
    bool Present(Item const* item) const
    {
        for (auto* i : items) if (i == item) return true;
        for (auto* b : bags) if (b) for (auto* i : b->items) if (i == item) return true;
        return false;
    }
    void CanStoreNewItem(uint8, uint8, ItemPosCountVec& dest, uint32, uint32 count)
    { for (uint8 s = 23; s < 39; ++s) if (!items[s]) { dest.push_back({uint16(0xFF00 | s), count}); return; } }
    Item* StoreNewItem(ItemPosCountVec const& dest, uint32 entry, bool)
    { Item* i = Make(entry, uint8(dest[0].pos & 255), dest[0].count); ++stored; return i; }
    void SetAmmo(uint32 entry) { ammo = entry; }
    void DestroyItem(uint8 bag, uint8 slot, bool)
    {
        destroyed.push_back({bag, slot});
        if (bag == INVENTORY_SLOT_BAG_0) items[slot] = nullptr; else bags[bag]->items[slot] = nullptr;
    }
    Item* Make(uint32 entry, uint8 slot, uint32 count = 1)
    {
        owned.push_back(std::make_unique<Item>());
        Item* i = owned.back().get(); i->proto = sObjectMgr->GetItemTemplate(entry); i->slot = slot; i->count = count; items[slot] = i;
        return i;
    }
};
// DOUBLE of the PlayerbotFactory members the real bodies touch.
class PlayerbotFactory
{
public:
    Player* bot = nullptr; uint32 level = 80; bool keepOwnedItems = false; uint32 cap = 1000;
    bool AllowsCappedItem(ItemTemplate const* proto) const { return proto->ItemLevel <= cap; }
    void InitAmmo();
    static bool DisplacesOffhand(Player* bot, ItemTemplate const* mainhand);
};
#include "DestroyObsoleteAmmo.inc"   // REAL
#include "InitAmmo.inc"              // REAL
#include "DisplacesOffhand.inc"      // REAL

static int g_checks = 0, g_fail = 0, g_div = 0;
static void Row(std::string const& id, std::string const& expected, std::string const& observed, bool ok)
{
    ++g_checks; g_div += !ok;
    std::printf("SCN %-44s expected=%-28s observed=%-28s %s\n", id.c_str(), expected.c_str(), observed.c_str(), ok ? "as-expected" : "DIVERGENCE");
}

int main()
{
    std::printf("REAL: PlayerbotFactory::InitAmmo, DestroyObsoleteAmmo, PlayerbotFactory::DisplacesOffhand (PB53). DOUBLES: Player/Item/Bag, "
                "templates, RandomItemMgr, factory members.\n");
    auto& t = g_templates.t;
    t[10] = {10, ITEM_CLASS_WEAPON, ITEM_SUBCLASS_WEAPON_BOW, 15, 200, 1};
    t[20] = {20, ITEM_CLASS_PROJECTILE, ITEM_SUBCLASS_ARROW, 24, 150, 1000}; // old, lower arrows the bot owns
    t[21] = {21, ITEM_CLASS_PROJECTILE, ITEM_SUBCLASS_ARROW, 24, 200, 1000}; // stocked kind
    t[22] = {22, ITEM_CLASS_PROJECTILE, ITEM_SUBCLASS_BULLET, 24, 100, 1000}; // wrong subclass
    for (int keep = 1; keep >= 0; --keep)
    {
        Player p; PlayerbotFactory f; f.bot = &p; f.keepOwnedItems = keep; sRandomItemMgr.ammo = 21;
        p.Make(10, EQUIPMENT_SLOT_RANGED);
        p.Make(20, 23, 200); p.Make(22, 24, 200);
        f.InitAmmo();
        bool const kept = p.items[23] && p.items[24] && p.destroyed.empty();
        if (keep)
            Row("F1 keepOwned=1 (capped AutoGear finishers)", "old ammo kept; new stocked", std::string(kept ? "kept" : "destroyed") + " destroyed=" +
                std::to_string(p.destroyed.size()) + " stored=" + std::to_string(p.stored) + " ammo=" + std::to_string(p.ammo),
                kept && p.stored == 1 && p.ammo == 21);
        else
            Row("F2 keepOwned=0 control (legacy factory)", "both obsolete stacks destroyed", "destroyed=" + std::to_string(p.destroyed.size()),
                p.destroyed.size() == 2);
    }
    // DisplacesOffhand vs CS02's independent WotLK oracle (cases with an unambiguous expectation only).
    struct Case { char const* name; bool off; uint32 offType; uint32 mainSub; uint32 mainType; bool dual, titan; bool expect; };
    Case cases[] = {
        {"no off-hand", false, INVTYPE_SHIELD, ITEM_SUBCLASS_WEAPON_AXE2, INVTYPE_2HWEAPON, true, false, false},
        {"2H axe, shield, no Titan's Grip", true, INVTYPE_SHIELD, ITEM_SUBCLASS_WEAPON_AXE2, INVTYPE_2HWEAPON, true, false, true},
        {"1H axe, shield", true, INVTYPE_SHIELD, ITEM_SUBCLASS_WEAPON_AXE, INVTYPE_WEAPON, false, false, false},
        {"1H axe, off-hand weapon, no dual wield", true, INVTYPE_WEAPONOFFHAND, ITEM_SUBCLASS_WEAPON_AXE, INVTYPE_WEAPON, false, false, true},
        {"1H axe, off-hand weapon, dual wield", true, INVTYPE_WEAPONOFFHAND, ITEM_SUBCLASS_WEAPON_AXE, INVTYPE_WEAPON, true, false, false},
        {"staff, holdable, Titan's Grip", true, INVTYPE_HOLDABLE, ITEM_SUBCLASS_WEAPON_STAFF, INVTYPE_2HWEAPON, true, true, true},
        {"polearm, off-hand 2H, Titan's Grip", true, INVTYPE_2HWEAPON, ITEM_SUBCLASS_WEAPON_POLEARM, INVTYPE_2HWEAPON, true, true, true},
        {"2H axe, off-hand 2H axe, Titan's Grip", true, INVTYPE_2HWEAPON, ITEM_SUBCLASS_WEAPON_AXE2, INVTYPE_2HWEAPON, true, true, false},
        {"1H axe, off-hand 2H, no Titan's Grip", true, INVTYPE_2HWEAPON, ITEM_SUBCLASS_WEAPON_AXE, INVTYPE_WEAPON, true, false, true},
    };
    for (auto const& c : cases)
    {
        Player p; p.dual = c.dual; p.titan = c.titan;
        t[30] = {30, ITEM_CLASS_WEAPON, ITEM_SUBCLASS_WEAPON_AXE, c.offType, 100, 1};
        if (c.off) p.Make(30, EQUIPMENT_SLOT_OFFHAND);
        ItemTemplate main{31, ITEM_CLASS_WEAPON, c.mainSub, c.mainType, 100, 1};
        bool const got = PlayerbotFactory::DisplacesOffhand(&p, &main);
        Row(std::string("F3 DisplacesOffhand: ") + c.name, c.expect ? "displaces" : "keeps", got ? "displaces" : "keeps", got == c.expect);
    }
    std::printf("CS02 layer3 factory: %d checks, %d property failures, %d divergences from pre-registered expectations\n", g_checks, g_fail, g_div);
    return g_fail ? 1 : 0;
}
