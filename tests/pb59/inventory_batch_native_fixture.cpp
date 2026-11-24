#include "PB59CombatRecoveryState.h"
// Actual executor/admission/effect-journal/item-mod bodies; explicit engine,
// transfer, aura, packet and database doubles. Never an installed server.
#include "PB59InventoryBatchPlan.h"
#include "PB59TransactionAdmission.h"
#include "PB59OriginalCombatEffects.h"
#include "PB59InventoryCustody.h"
#include "PB59OriginalQueueCustody.h"
#include "PB59EffectBoundary.h"
#include "EquipmentCaptureState.h"
#include <algorithm>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <tuple>
#include <vector>
#include <cstdio>
#include <stdexcept>
using uint8 = std::uint8_t;
using uint16 = std::uint16_t;
using uint32 = std::uint32_t;
using uint64 = std::uint64_t;
using int32 = std::int32_t;
constexpr int PLAYER_END = 192, UNIT_FIELD_BYTES_0 = 0;
constexpr int INVENTORY_SLOT_BAG_0 = 255, EQUIPMENT_SLOT_END = 19, INVENTORY_SLOT_BAG_END = 23;
constexpr int EQUIPMENT_SLOT_BODY = 3, EQUIPMENT_SLOT_TABARD = 18, EQUIPMENT_SLOT_MAINHAND = 15;
constexpr int EQUIPMENT_SLOT_OFFHAND = 16, EQUIPMENT_SLOT_RANGED = 17;
constexpr int BIND_WHEN_EQUIPPED = 2, BIND_WHEN_PICKED_UP = 1, BIND_QUEST_ITEM = 4;
constexpr int ITEM_SPELLTRIGGER_ON_NO_DELAY_USE = 4, INVTYPE_2HWEAPON = 17;
constexpr int ITEM_SUBCLASS_WEAPON_POLEARM = 6, ITEM_SUBCLASS_WEAPON_STAFF = 10, ITEM_SUBCLASS_WEAPON_FISHING_POLE = 20;
constexpr int MAX_SPELL_EFFECTS = 3, CR_ARMOR_PENETRATION = 1, EQUIP_ERR_OK = 0;
constexpr int ACHIEVEMENT_CRITERIA_TYPE_EQUIP_ITEM = 1, ACHIEVEMENT_CRITERIA_TYPE_EQUIP_EPIC_ITEM = 2;
enum WeaponAttackType { BASE_ATTACK, OFF_ATTACK, MAX_ATTACK };
enum ItemUpdateState { ITEM_UNCHANGED, ITEM_CHANGED, ITEM_NEW, ITEM_REMOVED };
struct SpellInfo;
struct SpellCooldown { uint32 end = 0; uint16 category = 0; uint32 itemid = 0, maxduration = 0;
    bool sendToSpectator = false, needSendToClient = false; };
using SpellCooldowns = std::map<uint32, SpellCooldown>;
struct GlobalCooldown { uint32 duration = 0, cast_time = 0; };
using GlobalCooldownList = std::map<uint32, GlobalCooldown>;
#include "GlobalCooldownMgr.inc"
struct Guid { uint64 value; uint64 GetRawValue() const { return value; } uint64 GetCounter() const { return value; }
    bool operator<(Guid other) const { return value < other.value; } };
struct Map { std::set<void const*> updates; bool PB59HasUpdateObject(void const* pointer) const { return updates.contains(pointer); } };
struct AuraEffect {
    int32 GetAmount() const { return 1; } int32 GetAmplitude() const { return 0; }
    int32 GetPeriodicTimer() const { return 0; } uint32 GetTickNumber() const { return 0; }
    auto PB59RecoveryState() const { return std::tuple{1}; }
};
struct Aura {
    struct Application { Aura* base = nullptr; Aura const* GetBase() const { return base; }
        auto PB59RecoveryState() const { return std::tuple{base}; } };
    using ApplicationMap = std::map<Guid, Application*>;
    ApplicationMap applications;
    uint32 id = 1; int32 duration = -1;
    uint32 GetId() const { return id; } Guid GetCasterGUID() const { return {1}; }
    Guid GetCastItemGUID() const { return {10}; }
    int32 GetDuration() const { return duration; } int32 GetMaxDuration() const { return -1; }
    uint8 GetCharges() const { return 0; } uint8 GetStackAmount() const { return 1; }
    AuraEffect const* GetEffect(uint8) const { return nullptr; }
    auto PB59RecoveryState() const { return std::tuple{1}; }
    auto const& GetApplicationMap() const { return applications; }
};
class Player;
struct Session {
    Player* player = nullptr; uint64 incarnation = 7;
    bool logout = false, loading = false, headless = true, closed = true;
    Player* GetPlayer() const { return player; }
    uint64 GetPB59SessionIncarnation() const { return incarnation; }
    bool PlayerLoading() const { return loading; } bool PlayerLogout() const { return logout; }
    bool IsHeadless() const { return headless; } bool IsSocketClosed() const { return closed; }
};
struct ItemTemplate {
    uint32 ItemSet = 0, Bonding = 0, InventoryType = 0, SubClass = 0, stat = 5;
    struct Spell { int32 SpellId = 0; uint8 SpellTrigger = 0; };
    std::array<Spell, 5> Spells{};
};
class Item {
public:
    PB59::InventoryInstanceFacts facts;
    ItemTemplate prototype;
    int32 uQueuePos = -1; ItemUpdateState uState = ITEM_UNCHANGED;
    bool world = true, updated = false;
    Map* map;
    explicit Item(uint64 guid, Map* ownerMap) : map(ownerMap) {
        facts.guid = guid; facts.owner = 1; facts.entry = uint32(guid + 100); facts.count = 1;
        facts.durability = facts.maxDurability = 100;
    }
    Guid GetGUID() const { return {facts.guid}; } Guid GetOwnerGUID() const { return {facts.owner}; }
    uint32 GetCount() const { return facts.count; } uint32 GetEntry() const { return facts.entry; }
    ItemTemplate const* GetTemplate() const { return &prototype; }
    ItemUpdateState GetState() const { return uState; }
    bool IsSoulBound() const { return true; } bool IsInWorld() const { return world; }
    bool IsBroken() const { return false; } bool HasSocket() const { return false; }
    bool PB59HasObjectUpdatePending() const { return updated; }
    void ClearUpdateMask(bool) { updated = false; map->updates.erase(this); }
};
class Bag : public Item { public: using Item::Item; };
struct ItemSetEntry { uint32 required_skill_id = 0, required_skill_value = 0; };
struct SetStore { ItemSetEntry entry; ItemSetEntry const* LookupEntry(uint32) const { return &entry; } } sets;
auto& sItemSetStore = sets;
struct ItemSetEffect { uint32 setid = 0, item_count = 0; void const* spells[8]{}; };
struct ItemPosCount { uint16 pos; uint32 count; };
using ItemPosCountVec = std::vector<ItemPosCount>;
#define LOG_DEBUG(...) do { } while (false)
struct ScriptMgr {
    std::function<void()> equip;
    void OnPlayerEquip(Player*, Item*, int, int, bool) {
        if (equip) { PB59::InventoryReservation::NoteCallback(); equip(); }
    }
} scripts;
auto sScriptMgr = &scripts;
class Player {
public:
    Session session;
    Map map;
    std::array<uint32, PLAYER_END> fields{};
    std::map<uint16, Item*> slots;
    std::map<uint8, Bag*> bags;
    std::vector<std::unique_ptr<Item>> ownership;
    std::vector<Item*> m_itemUpdateQueue, m_itemDuration;
    struct Timer { Item* item; };
    std::vector<Timer> m_enchantDuration;
    std::vector<ItemSetEffect*> ItemSetEff;
    float m_auraFlatModifiersGroup[2][2]{}, m_auraPctModifiersGroup[2][2]{}, m_weaponDamage[2][2][2]{};
    float m_auraBaseFlatMod[2]{}, m_auraBasePctMod[2]{}, m_modAttackSpeedPct[3]{}, m_speed_rate[3]{};
    int32 m_baseRatingValue[3]{};
    uint32 m_baseSpellPower = 0, m_baseSpellDamage = 0, m_baseSpellHealing = 0, m_baseFeralAP = 0;
    uint32 m_baseManaRegen = 0, m_baseHealthRegen = 0, m_weaponChangeTimer = 0;
    int32 m_spellPenetrationItemMod = 0, m_baseSpellCritChance = 0;
    float m_modMeleeHitChance = 0, m_modRangedHitChance = 0, m_modSpellHitChance = 0, m_realDodge = 0, m_realParry = 0;
    bool m_canParry = false, m_canBlock = false, m_canTitanGrip = false, m_canModifyStats = true;
    int _dualWieldMode = 0;
    float m_ammoDPS = 0;
    uint32 m_interruptMask = 0, m_removedAurasCount = 0, m_extraAttacks = 0;
    void const* m_currentSpells[3]{};
    std::map<uint32, Aura*> auras;
    PB59::EquipmentCaptureState _pb59Equipment;
    PB59::TransactionAdmission _pb59Transaction;
    std::array<PB59::OriginalCombatEffects, EQUIPMENT_SLOT_END> _pb59OriginalCombatEffects{};
    SpellCooldowns m_spellCooldowns;
    GlobalCooldownMgr m_GlobalCooldownMgr;
    bool m_objectUpdated = false, inWorld = true, alive = true, combat = false, far = false, near = false;
    bool canTitan = false;
    unsigned policyCalls = 0, transfers = 0, modsCalls = 0;
    unsigned rejectPolicy = 0, rejectTransfer = 0, recoveryTransfer = 0, lieTransfer = 0;
    unsigned throwMods = 0, denyUndo = 0;
    std::vector<uint64> transferOrder;
    std::function<void()> policyHook, modsHook, visibleHook;
    Player() {
        session.player = this; _pb59Equipment.Bind();
        for (uint16 slot = 0; slot < 39; ++slot) slots.emplace(uint16(65280 + slot), nullptr);
        for (uint8 bag = 19; bag < 23; ++bag)
            for (uint16 slot = 0; slot < 4; ++slot) slots.emplace(uint16((bag << 8) | slot), nullptr);
    }
    ~Player() { for (auto* entry : ItemSetEff) delete entry; }
    Item* Add(uint16 pos, uint64 guid) {
        ownership.emplace_back(new Item(guid, &map)); Item* item = ownership.back().get();
        slots.at(pos) = item; fields[16 + (pos & 255) * 2] = uint32(guid);
        if ((pos >> 8) == 255 && (pos & 255) < 19) {
            fields[96 + (pos & 255) * 2] = item->GetEntry(); fields[1] += item->prototype.stat;
        }
        return item;
    }
    Guid GetGUID() const { return {1}; } uint32 GetLevel() const { return 60; }
    uint32 GetSkillValue(uint32) const { return 450; }
    Session* GetSession() const { return const_cast<Session*>(&session); }
    Map* GetMap() const { return const_cast<Map*>(&map); }
    bool IsInWorld() const { return inWorld; } bool IsAlive() const { return alive; }
    bool IsInCombat() const { return combat; } bool IsBeingTeleportedFar() const { return far; }
    bool IsBeingTeleportedNear() const { return near; } bool CanTitanGrip() const { return canTitan; }
    uint16 GetValuesCount() const { return PLAYER_END; }
    uint32 GetUInt32Value(uint16 field) const { return fields.at(field); }
    auto const& GetOwnedAuras() const { return auras; }
    std::map<uint32, Aura::Application*> appliedAuras;
    auto const& GetAppliedAuras() const { return appliedAuras; }
    bool HasPB59UnsettledOriginals() const {
        if (_pb59Transaction.State() != PB59::TransactionAdmission::Phase::Idle) return true;
        for (auto const& journal : _pb59OriginalCombatEffects)
            if (journal.State() != PB59::OriginalCombatEffects::Phase::Fresh) return true;
        return false;
    }
    Item* GetItemByPos(uint16 pos) const { auto it = slots.find(pos); return it == slots.end() ? nullptr : it->second; }
    Item* GetItemByPos(uint8 bag, uint8 slot) const { return GetItemByPos(uint16((bag << 8) | slot)); }
    Bag* GetBagByPos(uint8 bag) const { auto it = bags.find(bag); return it == bags.end() ? nullptr : it->second; }
    std::optional<PB59::InventorySlotFacts> CapturePB59InventorySlot(uint16 pos) const {
        if (!inWorld || !alive || far || near || session.logout || session.loading || session.player != this) return {};
        auto found = slots.find(pos); if (found == slots.end()) return {};
        uint64 bag = 0;
        if ((pos >> 8) != 255) { auto* container = GetBagByPos(pos >> 8); if (!container) return {}; bag = container->GetGUID().value; }
        auto const* item = found->second;
        return PB59::InventorySlotFacts{1, bag, pos, item ? std::optional{item->facts} : std::nullopt};
    }
    bool CheckPB59InventoryBatchPlan(uint64, uint64, uint64, PB59::Stamp const&,
        std::span<PB59::InventoryBatchMove const>) const;
    std::optional<PB59::Stamp> CapturePB59InventoryStamp() const;
    PB59::InventoryBatchResult ExecutePB59InventoryBatchPlan(uint64, uint64, uint64, PB59::Stamp const&,
        std::span<PB59::InventoryBatchMove const>);
    int CanUnequipItem(uint16, bool) {
        ++policyCalls; if (policyHook) policyHook(); return policyCalls == rejectPolicy ? 1 : 0;
    }
    int CanEquipItem(uint8 slot, uint16& destination, Item*, bool) {
        ++policyCalls; if (policyHook) policyHook(); destination = uint16(65280 + slot);
        return policyCalls == rejectPolicy ? 1 : 0;
    }
    int CanStoreItem(uint8 bag, uint8 slot, ItemPosCountVec& destinations, Item*, bool) {
        ++policyCalls; if (policyHook) policyHook(); destinations.push_back({uint16((bag << 8) | slot), 1});
        return policyCalls == rejectPolicy ? 1 : 0;
    }
    void Mark(Item* item) {
        if (item->uQueuePos < 0) { item->uQueuePos = int32(m_itemUpdateQueue.size()); m_itemUpdateQueue.push_back(item); }
        if (item->uState != ITEM_NEW) item->uState = ITEM_CHANGED;
        item->updated = true; map.updates.insert(item);
    }
    PB59::LinkTransferResult RelocatePB59InventoryLinks(PB59::InventorySlotFacts const& from,
        PB59::InventorySlotFacts const& to, PB59::TransactionAdmission::Ticket ticket,
        PB59::TransactionAdmission::Access access) {
        ++transfers;
        if (!_pb59Transaction.CanAccess(ticket, access)) return PB59::LinkTransferResult::Rejected;
        if (transfers == rejectTransfer || (denyUndo && access == PB59::TransactionAdmission::Access::Recovery))
            return PB59::LinkTransferResult::Rejected;
        if (transfers == lieTransfer) return PB59::LinkTransferResult::Linked;
        auto a = CapturePB59InventorySlot(from.position), b = CapturePB59InventorySlot(to.position);
        if (!a || !b || *a != from || *b != to) return PB59::LinkTransferResult::Rejected;
        Item*& source = slots.at(from.position); Item*& destination = slots.at(to.position);
        Item* item = source;
        if (!PB59::RelocateInventoryPointer(source, destination, item)) return PB59::LinkTransferResult::Rejected;
        transferOrder.push_back(item->GetGUID().value); Mark(item);
        if ((from.position >> 8) == 255) fields[16 + (from.position & 255) * 2] = 0;
        else Mark(GetBagByPos(from.position >> 8));
        if ((to.position >> 8) == 255) fields[16 + (to.position & 255) * 2] = uint32(item->GetGUID().value);
        else Mark(GetBagByPos(to.position >> 8));
        m_objectUpdated = true; map.updates.insert(this);
        return transfers == recoveryTransfer ? PB59::LinkTransferResult::RecoveryRequired : PB59::LinkTransferResult::Linked;
    }
    PB59::OriginalCombatEffects::Result ReconcilePB59OriginalCombatEffects(
        PB59::InventorySlotFacts const&, PB59::TransactionAdmission::Ticket, bool);
    void _ApplyItemMods(Item*, uint8, bool);
    void CorrectMetaGemEnchants(uint8, bool) { }
    void _ApplyItemBonuses(ItemTemplate const* proto, uint8, bool apply) {
        ++modsCalls; if (modsHook) modsHook();
        if (modsCalls == throwMods) throw std::runtime_error("fixture effect failure");
        fields[1] += apply ? proto->stat : -proto->stat;
    }
    void _ApplyAmmoBonuses() { }
    void ApplyItemEquipSpell(Item*, bool) { }
    void ApplyItemDependentAuras(Item*, bool) { }
    static WeaponAttackType GetAttackBySlot(uint8) { return MAX_ATTACK; }
    void UpdateWeaponDependentAuras(WeaponAttackType) { }
    void ApplyEnchantment(Item*, bool) { }
    void UpdateExpertise(WeaponAttackType) { }
    void RecalculateRating(int) { }
    void UpdateTitansGrip() { }
    void SetVisibleItemSlot(uint8 slot, Item* item) {
        fields[96 + slot * 2] = item ? item->GetEntry() : 0;
        if (visibleHook) { PB59::InventoryReservation::NoteCallback(); visibleHook(); }
        m_objectUpdated = true; map.updates.insert(this);
    }
    void ApplyEquipCooldown(Item*) { }
    void UpdateAchievementCriteria(int, uint32, uint32 = 0) { }
    void UpdateForQuestWorldObjects() { }
    void ClearUpdateMask(bool) { m_objectUpdated = false; map.updates.erase(this); }
    // Public producer double uses the same guard as native Remove/Destroy.
    void PublicInventoryMutation() { if (_pb59Equipment.InventoryMutationAllowed()) fields[4] = 99; }
};
void AddItemsSetItem(Player*, Item*) { }
void RemoveItemsSetItem(Player*, ItemTemplate const*) { }
#include "Admission.inc"
#include "InventoryStamp.inc"
#include "ItemMods.inc"
#include "CombatEffects.inc"
#include "Executor.inc"
#include "Checks.inc"
