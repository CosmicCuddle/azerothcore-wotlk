#include "PreparedStatement.h"
#include "Transaction.h"
#include "PB59InventorySlotFacts.h"
#include "PB59NativeRecoveryCollection.h"
#include "PB59CharacterSaveObserver.h"
#include "PB59TransactionAdmission.h"
#include <algorithm>
#include <array>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <set>
#include <sstream>

#define ASSERT(condition) do { if (!(condition)) std::abort(); } while (false)
#define ABORT() std::abort()
#define LOG_FATAL(...) ((void)0)

enum Statement { CHAR_REP_INVENTORY_ITEM, CHAR_REP_ITEM_INSTANCE, CHAR_UPD_ITEM_INSTANCE, CHAR_UPD_GIFT_OWNER,
    CHAR_DEL_ITEM_INSTANCE, CHAR_DEL_GIFT };
enum ItemUpdateState { ITEM_UNCHANGED, ITEM_CHANGED, ITEM_NEW, ITEM_REMOVED };
enum FieldIndex { ITEM_FIELD_CREATOR, ITEM_FIELD_GIFTCREATOR, ITEM_FIELD_DURATION, ITEM_FIELD_FLAGS,
    ITEM_FIELD_DURABILITY, ITEM_FIELD_CREATE_PLAYED_TIME, ITEM_FIELD_MAXDURABILITY, ITEM_FIELD_CONTAINED };
enum EnchantmentSlot : uint8 { SlotZero };
constexpr uint8 MAX_ITEM_PROTO_SPELLS = 5;
constexpr uint8 MAX_ENCHANTMENT_SLOT = 12;

struct ObjectGuid
{
    using LowType = uint32;
    uint64 value = 0;
    uint64 GetRawValue() const { return value; }
    LowType GetCounter() const { return static_cast<LowType>(value); }
    bool IsEmpty() const { return value == 0; }
    auto operator<=>(ObjectGuid const&) const = default;
};

struct InspectTransaction : Transaction<CharacterDatabaseConnection>
{
    PreparedStatementBase const& StatementAt(std::size_t index) const
    {
        return *std::get<PreparedStatementBase*>(m_queries.at(index).element);
    }
};

class TrackedStatement : public CharacterDatabasePreparedStatement
{
public:
    TrackedStatement(Statement statement, uint8 capacity) : CharacterDatabasePreparedStatement(statement, capacity)
    {
        ++Live;
    }

    ~TrackedStatement() override { --Live; }
    inline static unsigned Live = 0;
};

struct FixtureDatabase
{
    auto BeginTransaction() { return std::make_shared<InspectTransaction>(); }

    CharacterDatabasePreparedStatement* GetPreparedStatement(Statement statement)
    {
        ++preparedCalls;
        if (beforePrepare)
            beforePrepare();
        if (statement == failOn)
            throw std::bad_alloc();
        switch (statement)
        {
            case CHAR_REP_INVENTORY_ITEM:
                return new TrackedStatement(statement, 4);
            case CHAR_REP_ITEM_INSTANCE:
            case CHAR_UPD_ITEM_INSTANCE:
                return new TrackedStatement(statement, 14);
            case CHAR_UPD_GIFT_OWNER:
                return new TrackedStatement(statement, 2);
            case CHAR_DEL_ITEM_INSTANCE:
            case CHAR_DEL_GIFT:
                return new TrackedStatement(statement, 1);
        }
        std::abort();
    }

    void CommitTransaction(CharacterDatabaseTransaction transaction)
    {
        committed = std::static_pointer_cast<InspectTransaction>(std::move(transaction));
    }

    int failOn = -1;
    unsigned preparedCalls = 0;
    std::function<void()> beforePrepare;
    std::shared_ptr<InspectTransaction> committed;
} CharacterDatabase;

class Bag;

class Item
{
public:
    ~Item() { ++Destroyed; }
    bool AppendItemInstanceSave(Transaction<CharacterDatabaseConnection>& transaction) const;
    void SaveToDB(CharacterDatabaseTransaction transaction);

    ObjectGuid GetGUID() const { if (guidReads) ++*guidReads; return guid; }
    ObjectGuid GetOwnerGUID() const { return owner; }
    ObjectGuid GetGuidValue(uint16 field) const
    {
        if (field == ITEM_FIELD_CONTAINED)
            return contained;
        return {field == ITEM_FIELD_CREATOR ? 37u : 53u};
    }
    uint32 GetEntry() const { return 73; }
    uint32 GetCount() const { return 19; }
    uint32 GetUInt32Value(FieldIndex field) const { return fields.at(field); }
    int32 GetSpellCharges(uint8 index) const { return charges.at(index); }
    uint32 GetEnchantmentId(EnchantmentSlot slot) const { return 101 + slot; }
    uint32 GetEnchantmentDuration(EnchantmentSlot slot) const { return 201 + slot; }
    uint32 GetEnchantmentCharges(EnchantmentSlot slot) const { return 301 + slot; }
    int32 GetItemRandomPropertyId() const { return -186; }
    uint32 GetItemSuffixFactor() const { return 0; }
    std::string const& GetText() const { return m_text; }
    ItemUpdateState GetState() const { return uState; }
    bool IsWrapped() const { return wrapped; }
    bool IsBag() const { return bagItem; }
    bool IsInTrade() const { return inTrade; }
    bool IsRefundable() const { return refundable; }
    bool IsBOPTradable() const { return tradable; }
    Item const* GetTemplate() const { return this; }
    Bag* ToBag();
    Bag* GetContainer() { return m_container; }
    uint8 GetSlot() const { return slot; }
    uint8 GetBagSlot() const;
    uint16 GetPos() const { return (uint16(GetBagSlot()) << 8) | GetSlot(); }
    bool PB59InventoryMutationAllowed() const { return mutationAllowed; }
    void SetState(ItemUpdateState state) { uState = state; ++stateChanges; }

    static unsigned Destroyed;
    std::array<uint32, 7> fields{0, 0, 91, 103, 126, 137, 158};
    std::array<int32, 5> charges{-7, 0, 2, 9, -12};
    std::string m_text{"rune\0text", 9};
    ItemUpdateState uState = ITEM_CHANGED;
    unsigned stateChanges = 0;
    bool wrapped = false;
    bool mutationAllowed = true;
    ObjectGuid guid{0x4000000000000029};
    ObjectGuid owner{0x1234567800000053};
    ObjectGuid contained = owner;
    Bag* m_container = nullptr;
    uint8 slot = 0;
    int32 uQueuePos = -1;
    unsigned* guidReads = nullptr;
    bool bagItem = false, inTrade = false, refundable = false, tradable = false, m_lootGenerated = false;
};

unsigned Item::Destroyed = 0;

constexpr uint8 INVENTORY_SLOT_BAG_0 = 255, INVENTORY_SLOT_BAG_START = 19, INVENTORY_SLOT_BAG_END = 23;
constexpr uint8 INVENTORY_SLOT_ITEM_START = 23, INVENTORY_SLOT_ITEM_END = 39;
constexpr uint8 EQUIPMENT_SLOT_START = 0, EQUIPMENT_SLOT_BODY = 3, EQUIPMENT_SLOT_TABARD = 18, EQUIPMENT_SLOT_END = 19;
constexpr uint8 BANK_SLOT_BAG_START = 67, BANK_SLOT_BAG_END = 74, BUYBACK_SLOT_START = 74, BUYBACK_SLOT_END = 86;
constexpr uint8 KEYRING_SLOT_START = 86, CURRENCYTOKEN_SLOT_END = 150, MAX_BAG_SIZE = 36;
constexpr uint16 PLAYER_FIELD_INV_SLOT_HEAD = 10, CONTAINER_FIELD_SLOT_1 = 10;

class Bag : public Item
{
public:
    Bag() { bagItem = true; guid = {0x4000000000000041}; }
    Item* GetItemByPos(uint8 slot) const;
    uint32 GetBagSize() const { return size; }
    ObjectGuid GetGuidValue(uint16 field) const
    {
        if (field < CONTAINER_FIELD_SLOT_1)
            return Item::GetGuidValue(field);
        return links.at((field - CONTAINER_FIELD_SLOT_1) / 2);
    }
    uint32 size = 4;
    std::array<Item*, MAX_BAG_SIZE> m_bagslot{};
    std::array<ObjectGuid, MAX_BAG_SIZE> links{};
};

Bag* Item::ToBag() { return IsBag() ? static_cast<Bag*>(this) : nullptr; }

class Player;
struct WorldSession
{
    Player* player;
    Player* GetPlayer() const { return player; }
    uint64 GetPB59SessionIncarnation() const { return incarnation; }
    bool PlayerLoading() const { return loading; }
    bool PlayerLogout() const { return logout; }
    uint64 incarnation = 71;
    bool loading = false, logout = false;
};

class Player
{
public:
    struct PB59InventorySlotLinks { Item const* item; ObjectGuid fieldGuid; ObjectGuid containerGuid; };
    std::optional<PB59InventorySlotLinks> ReadPB59InventorySlotLinks(uint16 pos) const;
    std::optional<PB59InventorySlotLinks> CheckPB59InventoryItemSave(uint16 pos, Item const* expected) const;
    bool AppendPB59InventoryItemSave(uint16 pos, Item const* expected,
        Transaction<CharacterDatabaseConnection>& transaction) const;
    bool AppendPB59InventorySaveBatch(PB59::Stamp const& stamp,
        Transaction<CharacterDatabaseConnection>& transaction, PB59::SaveEvidence* membership = nullptr) const;
    class PB59PreparedRecoverySave;
    static std::unique_ptr<PB59PreparedRecoverySave const> PreparePB59RecoverySavePayload(
        Player const* owner, PB59::Recovery::Operation operation,
        PB59::TransactionAdmission::MutationScope const& scope, PB59::Stamp stamp);
    static int CheckRecoveryPayload();
    std::optional<PB59::InventorySlotFacts> CapturePB59InventorySlot(uint16 pos) const;
    Item* GetItemByPos(uint16 pos) const;
    Item* GetItemByPos(uint8 bag, uint8 slot) const;
    Bag* GetBagByPos(uint8 bag) const;
    ObjectGuid GetGUID() const { if (ownerReads) ++*ownerReads; return owner; }
    uint64 GetPB59OwnerIncarnation() const { return _pb59Transaction.OwnerIncarnation(); }
    ObjectGuid GetLootGUID() const { return loot; }
    ObjectGuid GetGuidValue(uint16 field) const { return fields.at((field - PLAYER_FIELD_INV_SLOT_HEAD) / 2); }
    WorldSession* GetSession() const { return &session; }
    bool IsInWorld() const { return true; }
    bool IsAlive() const { return true; }
    bool IsBeingTeleportedFar() const { return false; }
    bool IsBeingTeleportedNear() const { return false; }
    bool ArmRecovery(PB59::TransactionAdmission::Ticket ticket)
    {
        if (!_pb59Equipment.PrepareInventoryReservation(owner.value, {}))
            return false;
        _pb59Equipment.SetInventoryQuarantined(true);
        return _pb59Transaction.RequireRecovery(ticket);
    }

    struct Timer { Item* item; EnchantmentSlot slot; uint32 leftduration; };
    ObjectGuid owner{0x1234567800000053}, loot{};
    mutable WorldSession session{this};
    bool m_itemUpdateQueueBlocked = false;
    std::vector<Timer> m_enchantDuration;
    std::set<ObjectGuid> m_refundableItems;
    std::vector<Item*> m_itemUpdateQueue;
    PB59::EquipmentCaptureState _pb59Equipment;
    PB59::TransactionAdmission _pb59Transaction;
    unsigned* ownerReads = nullptr;
    std::array<Item*, CURRENCYTOKEN_SLOT_END> m_items{};
    std::array<ObjectGuid, CURRENCYTOKEN_SLOT_END> fields{};
};

struct Inventory
{
    explicit Inventory(bool inBag)
    {
        if (inBag)
        {
            bag.slot = INVENTORY_SLOT_BAG_START;
            player.m_items[bag.slot] = &bag;
            player.fields[bag.slot] = bag.guid;
            item.slot = 1;
            item.m_container = &bag;
            item.contained = bag.guid;
            bag.m_bagslot[item.slot] = &item;
            bag.links[item.slot] = item.guid;
        }
        else
        {
            item.slot = INVENTORY_SLOT_ITEM_START;
            player.m_items[item.slot] = &item;
            player.fields[item.slot] = item.guid;
        }
        position = item.GetPos();
    }
    Player player;
    Bag bag;
    Item item;
    uint16 position;
};

bool TransactionTask::Execute() { std::abort(); }
int TransactionTask::ExecuteWithCode() { std::abort(); }

#include "ItemInstanceSaveDefinitions.inc"

static unsigned Checks = 0;
#define ITEM_CHECK(value) do { \
    ++Checks; \
    if (!(value)) \
    { \
        std::cerr << "item instance save line " << __LINE__ << '\n'; \
        return 1; \
    } \
} while (false)

static int CheckInventoryLinks()
{
    unsigned const before = Checks;
    for (bool inBag : {false, true})
    {
        for (ItemUpdateState state : {ITEM_NEW, ITEM_CHANGED})
        {
            Inventory view(inBag);
            view.item.uState = state;
            auto facts = view.player.CapturePB59InventorySlot(view.position);
            ITEM_CHECK(facts && facts->item && facts->item->guid == view.item.guid.GetRawValue());
            ITEM_CHECK(facts->bag == (inBag ? view.bag.guid.GetRawValue() : 0));
            view.item.wrapped = state == ITEM_CHANGED;
            InspectTransaction transaction;
            ITEM_CHECK(view.player.AppendPB59InventoryItemSave(view.position, &view.item, transaction));
            ITEM_CHECK(transaction.GetSize() == (view.item.wrapped ? 3u : 2u));
            auto const& link = transaction.StatementAt(0);
            auto const& parameters = link.GetParameters();
            ITEM_CHECK(link.GetIndex() == CHAR_REP_INVENTORY_ITEM);
            ITEM_CHECK(std::get<uint64>(parameters.at(0).data) == view.player.owner.value);
            ITEM_CHECK(std::get<uint32>(parameters.at(1).data) == (inBag ? view.bag.guid.GetCounter() : 0));
            ITEM_CHECK(std::get<uint8>(parameters.at(2).data) == view.item.slot);
            ITEM_CHECK(std::get<uint32>(parameters.at(3).data) == view.item.guid.GetCounter());
            ITEM_CHECK(transaction.StatementAt(1).GetIndex() ==
                (state == ITEM_NEW ? CHAR_REP_ITEM_INSTANCE : CHAR_UPD_ITEM_INSTANCE));
            if (view.item.wrapped)
            {
                ITEM_CHECK(transaction.StatementAt(2).GetIndex() == CHAR_UPD_GIFT_OWNER);
                ITEM_CHECK(!view.player.CapturePB59InventorySlot(view.position));
            }
            ITEM_CHECK(view.item.uState == state && !view.item.stateChanges && !CharacterDatabase.committed);
        }
    }
    {
        Inventory view(false);
        view.player.fields[view.item.slot] = {};
        view.player.m_items[view.item.slot] = nullptr;
        view.item.slot = 0;
        view.player.fields[0] = view.item.guid;
        view.player.m_items[0] = &view.item;
        InspectTransaction transaction;
        ITEM_CHECK(view.player.AppendPB59InventoryItemSave(view.item.GetPos(), &view.item, transaction));
        ITEM_CHECK(transaction.GetSize() == 2);
        ITEM_CHECK(std::get<uint8>(transaction.StatementAt(0).GetParameters().at(2).data) == 0);
    }
    for (unsigned fault = 0; fault < 14; ++fault)
    {
        Inventory view(true);
        Bag wrongContainer;
        wrongContainer.slot = view.bag.slot;
        switch (fault)
        {
            case 0: view.bag.m_bagslot[view.item.slot] = nullptr; break;
            case 1: view.bag.links[view.item.slot] = {}; break;
            case 2: view.item.owner = {777}; break;
            case 3: view.item.contained = view.player.owner; break;
            case 4: view.item.m_container = &wrongContainer; break;
            case 5: ++view.item.slot; break;
            case 6: view.bag.size = MAX_BAG_SIZE + 1; break;
            case 7: view.bag.size = view.item.slot; break;
            case 8: view.bag.owner = {777}; break;
            case 9: view.player.fields[view.bag.slot] = {777}; break;
            case 10: view.bag.m_container = &wrongContainer; break;
            case 11: view.bag.uState = ITEM_REMOVED; break;
            case 12: view.bag.contained = {777}; break;
            case 13:
                view.bag.guid = {0x4000000000000000};
                view.player.fields[view.bag.slot] = view.bag.guid;
                view.item.contained = view.bag.guid;
                break;
        }
        InspectTransaction transaction;
        ITEM_CHECK(!view.player.CapturePB59InventorySlot(view.position));
        ITEM_CHECK(!view.player.AppendPB59InventoryItemSave(view.position, &view.item, transaction));
        ITEM_CHECK(transaction.GetSize() == 0 && !view.item.stateChanges);
    }
    for (unsigned fault = 0; fault < 12; ++fault)
    {
        Inventory view(false);
        Item substitute; // Same GUID and no identity at the requested native slot.
        Item const* expected = &view.item;
        switch (fault)
        {
            case 0: expected = &substitute; break;
            case 1: expected = nullptr; break;
            case 2: view.item.uState = ITEM_UNCHANGED; break;
            case 3: view.item.uState = ITEM_REMOVED; break;
            case 4: view.item.refundable = true; break;
            case 5: view.player.m_refundableItems.insert(view.item.guid); break;
            case 6: view.player.m_enchantDuration.push_back({&view.item, SlotZero, 80}); break;
            case 7: view.item.inTrade = true; break;
            case 8: view.player.loot = view.item.guid; break;
            case 9: view.item.tradable = true; break;
            case 10: view.item.wrapped = true; view.item.uState = ITEM_NEW; break;
            case 11: view.player.m_itemUpdateQueueBlocked = true; break;
        }
        InspectTransaction transaction;
        ITEM_CHECK(!view.player.AppendPB59InventoryItemSave(view.position, expected, transaction));
        ITEM_CHECK(transaction.GetSize() == 0 && !view.item.stateChanges);
        if (fault == 6)
        {
            auto facts = view.player.CapturePB59InventorySlot(view.position);
            ITEM_CHECK(facts && facts->item && facts->item->enchants[0].duration == 80);
        }
    }
    {
        Inventory view(false);
        view.player.fields[view.item.slot] = {};
        view.player.m_items[view.item.slot] = nullptr;
        auto const empty = view.player.CapturePB59InventorySlot(view.position);
        ITEM_CHECK(empty && !empty->item);
        InspectTransaction transaction;
        ITEM_CHECK(!view.player.AppendPB59InventoryItemSave(view.position, &view.item, transaction));
        for (uint8 slot : {EQUIPMENT_SLOT_BODY, EQUIPMENT_SLOT_TABARD, BANK_SLOT_BAG_START, BUYBACK_SLOT_START})
            ITEM_CHECK(!view.player.AppendPB59InventoryItemSave(
                (uint16(INVENTORY_SLOT_BAG_0) << 8) | slot, &view.item, transaction));
        ITEM_CHECK(transaction.GetSize() == 0);
    }
    {
        Inventory view(false);
        CharacterDatabase.failOn = CHAR_UPD_ITEM_INSTANCE;
        bool failed = false;
        try
        {
            InspectTransaction prepared;
            (void)view.player.AppendPB59InventoryItemSave(view.position, &view.item, prepared);
        }
        catch (std::bad_alloc const&)
        {
            failed = true;
        }
        CharacterDatabase.failOn = -1;
        ITEM_CHECK(failed && !CharacterDatabase.committed && TrackedStatement::Live == 0);
        ITEM_CHECK(view.item.uState == ITEM_CHANGED && !view.item.stateChanges);
    }
    std::cout << Checks - before << " PB59 inventory-link save checks PASS\n";
    return 0;
}

struct InventoryBatchFixture
{
    InventoryBatchFixture()
    {
        auto& player = inventory.player;
        inventory.bag.uState = ITEM_UNCHANGED;
        inventory.item.uQueuePos = 2;
        base.guid = {0x4000000000000032};
        base.uState = ITEM_NEW;
        base.slot = INVENTORY_SLOT_ITEM_START;
        base.uQueuePos = 0;
        player.m_items[base.slot] = &base;
        player.fields[base.slot] = base.guid;
        player.m_itemUpdateQueue = {&base, nullptr, &inventory.item};
        player._pb59Equipment.Bind();
        stamp = *player._pb59Equipment.Capture();
    }

    Inventory inventory{true};
    Item base;
    PB59::Stamp stamp;
};

static int CheckInventoryBatch()
{
    unsigned const before = Checks;
    {
        InventoryBatchFixture fixture;
        auto& view = fixture.inventory;
        auto const queue = view.player.m_itemUpdateQueue;
        InspectTransaction transaction;
        view.item.wrapped = true;
        ITEM_CHECK(view.player.AppendPB59InventorySaveBatch(fixture.stamp, transaction));
        ITEM_CHECK(transaction.GetSize() == 5);
        ITEM_CHECK(transaction.StatementAt(0).GetIndex() == CHAR_REP_INVENTORY_ITEM);
        ITEM_CHECK(transaction.StatementAt(1).GetIndex() == CHAR_REP_ITEM_INSTANCE);
        ITEM_CHECK(transaction.StatementAt(2).GetIndex() == CHAR_REP_INVENTORY_ITEM);
        ITEM_CHECK(transaction.StatementAt(3).GetIndex() == CHAR_UPD_ITEM_INSTANCE);
        ITEM_CHECK(transaction.StatementAt(4).GetIndex() == CHAR_UPD_GIFT_OWNER);
        ITEM_CHECK(!view.player.AppendPB59InventorySaveBatch(fixture.stamp, transaction) && transaction.GetSize() == 5);
        ITEM_CHECK(view.player.m_itemUpdateQueue == queue && fixture.base.uQueuePos == 0 && view.item.uQueuePos == 2);
        ITEM_CHECK(!fixture.base.stateChanges && !view.item.stateChanges && !CharacterDatabase.committed);
    }
    for (unsigned fault = 0; fault < 17; ++fault)
    {
        InventoryBatchFixture fixture;
        auto& view = fixture.inventory;
        auto& player = view.player;
        Item detached;
        unsigned orphanReads = 0;
        detached.guidReads = &orphanReads;
        switch (fault)
        {
            case 0: view.bag.links[view.item.slot] = {}; break;
            case 1:
                player.m_itemUpdateQueue[2] = &detached;
                view.item.uState = ITEM_UNCHANGED;
                view.item.uQueuePos = -1;
                break;
            case 2: player.m_itemUpdateQueue.push_back(&view.item); break;
            case 3:
                view.item.guid = {0x4001000000000032}; // different native GUID, same database counter
                view.bag.links[view.item.slot] = view.item.guid;
                break;
            case 4: player.m_items[fixture.base.slot + 1] = &fixture.base; break;
            case 5: view.item.uQueuePos = 1; break;
            case 6: player.m_itemUpdateQueue[2] = nullptr; view.item.uQueuePos = -1; break;
            case 7: player.m_refundableItems.insert(detached.guid); break;
            case 8: player.m_enchantDuration.push_back({&detached, SlotZero, 77}); break;
            case 9: player.m_items[BUYBACK_SLOT_START] = &detached; break;
            case 10: player.m_itemUpdateQueueBlocked = true; break;
            case 11: view.bag.uState = ITEM_CHANGED; break;
            case 12: player.m_itemUpdateQueue.clear(); break;
            case 13: player.m_itemUpdateQueue.assign(PB59::Recovery::MaximumQueueObservations + 1, nullptr); break;
            case 14: ++fixture.stamp.revision; break;
            case 15:
                player.m_itemUpdateQueue.assign(3, nullptr);
                fixture.base.uState = view.item.uState = ITEM_UNCHANGED;
                fixture.base.uQueuePos = view.item.uQueuePos = -1;
                break;
            case 16: view.item.refundable = true; break;
        }
        auto const queue = player.m_itemUpdateQueue;
        auto const prepares = CharacterDatabase.preparedCalls;
        InspectTransaction transaction;
        ITEM_CHECK(!player.AppendPB59InventorySaveBatch(fixture.stamp, transaction));
        ITEM_CHECK(transaction.GetSize() == 0 && CharacterDatabase.preparedCalls == prepares && orphanReads == 0);
        ITEM_CHECK(player.m_itemUpdateQueue == queue && !fixture.base.stateChanges && !view.item.stateChanges);
    }
    for (bool driftStamp : {false, true})
    {
        InventoryBatchFixture fixture;
        auto& player = fixture.inventory.player;
        bool drifted = false;
        CharacterDatabase.beforePrepare = [&]
        {
            if (drifted)
                return;
            drifted = true;
            if (driftStamp)
            {
                PB59::EquipmentCaptureState::Mutation mutate(player._pb59Equipment);
            }
            else
                player.m_itemUpdateQueue.push_back(nullptr);
        };
        {
            InspectTransaction transaction;
            bool const accepted = player.AppendPB59InventorySaveBatch(fixture.stamp, transaction);
            CharacterDatabase.beforePrepare = {};
            ITEM_CHECK(drifted && !accepted && transaction.GetSize() != 0 && !CharacterDatabase.committed);
        }
        ITEM_CHECK(TrackedStatement::Live == 0 && !fixture.base.stateChanges && !fixture.inventory.item.stateChanges);
    }
    std::cout << Checks - before << " PB59 complete inventory queue checks PASS\n";
    return 0;
}

int Player::CheckRecoveryPayload()
{
    using Ledger = PB59::TransactionAdmission;
    unsigned const before = Checks;
    auto operation = [](Player const& owner, Ledger::MutationScope const& scope)
    {
        return PB59::Recovery::Operation{
            owner.owner.value, owner.GetPB59OwnerIncarnation(), owner.session.incarnation, scope.GetTicket()};
    };
    auto& observer = PB59SaveObserver();
    {
        InventoryBatchFixture fixture;
        auto& view = fixture.inventory;
        auto& player = view.player;
        Ledger::MutationScope scope(player._pb59Transaction);
        ITEM_CHECK(player.ArmRecovery(scope.GetTicket()));
        auto const expected = operation(player, scope);
        auto const queue = player.m_itemUpdateQueue;
        auto payload = Player::PreparePB59RecoverySavePayload(&player, expected, scope, fixture.stamp);
        ITEM_CHECK(payload && payload->operation == expected && payload->stamp == fixture.stamp);
        ITEM_CHECK(payload->task.GetTransaction()->GetSize() == 4 && payload->receipt);
        ITEM_CHECK(payload->membership.itemCount == 2 && !payload->membership.incomplete);
        ITEM_CHECK(payload->membership.items[0] == fixture.base.guid.value);
        ITEM_CHECK(payload->membership.items[1] == view.item.guid.value);
        ITEM_CHECK(player.m_itemUpdateQueue == queue && !CharacterDatabase.committed);
        SaveObserver other;
        ITEM_CHECK(!other.AbandonProtected(*payload->receipt) && !observer.ConsumeProtected(*payload->receipt));
        bool reported = false;
        observer.Drain([&](auto const&) { reported = true; }, true);
        ITEM_CHECK(!reported && observer.AbandonProtected(*payload->receipt));
        auto const record = observer.ConsumeProtected(*payload->receipt);
        ITEM_CHECK(record && record->ticket.identity.guid == expected.owner &&
            record->ticket.identity.player == expected.ownerIncarnation &&
            record->ticket.identity.session == expected.sessionIncarnation);
        ITEM_CHECK(!observer.ConsumeProtected(*payload->receipt));
    }
    for (unsigned fault = 0; fault < 12; ++fault)
    {
        InventoryBatchFixture fixture;
        auto& player = fixture.inventory.player;
        Ledger::MutationScope scope(player._pb59Transaction);
        ITEM_CHECK(fault == 0 || player.ArmRecovery(scope.GetTicket()));
        auto expected = operation(player, scope);
        auto* owner = &player;
        unsigned reads = 0;
        player.ownerReads = &reads;
        switch (fault)
        {
            case 1: ++expected.ticket; break;
            case 2: ++expected.owner; break;
            case 3: ++expected.ownerIncarnation; break;
            case 4: ++expected.sessionIncarnation; break;
            case 5: ++fixture.stamp.revision; break;
            case 6: player.session.player = nullptr; break;
            case 7: player.session.loading = true; break;
            case 8: player.session.logout = true; break;
            case 9: (void)player._pb59Transaction.Request(Ledger::Work::Logout); break;
            case 10: owner = nullptr; break;
            case 11: fixture.inventory.bag.links[fixture.inventory.item.slot] = {}; break;
        }
        auto const prepares = CharacterDatabase.preparedCalls;
        ITEM_CHECK(!Player::PreparePB59RecoverySavePayload(owner, expected, scope, fixture.stamp));
        ITEM_CHECK(CharacterDatabase.preparedCalls == prepares && !CharacterDatabase.committed);
        if (fault == 0 || fault == 1 || fault == 3 || fault == 10)
            ITEM_CHECK(reads == 0);
    }
    for (bool failAllocation : {false, true})
    {
        InventoryBatchFixture fixture;
        auto& player = fixture.inventory.player;
        Ledger::MutationScope scope(player._pb59Transaction);
        ITEM_CHECK(player.ArmRecovery(scope.GetTicket()));
        auto expected = operation(player, scope);
        bool replaced = false;
        if (failAllocation)
            CharacterDatabase.failOn = CHAR_UPD_ITEM_INSTANCE;
        else
            CharacterDatabase.beforePrepare = [&]
            {
                if (!replaced)
                {
                    ++player.session.incarnation;
                    ++expected.sessionIncarnation;
                    replaced = true;
                }
            };
        auto payload = Player::PreparePB59RecoverySavePayload(&player, expected, scope, fixture.stamp);
        CharacterDatabase.beforePrepare = {};
        CharacterDatabase.failOn = -1;
        ITEM_CHECK(!payload && (failAllocation || replaced));
        ITEM_CHECK(TrackedStatement::Live == 0 && !CharacterDatabase.committed);
    }
    {
        Player player;
        std::array<Bag, 2> bags;
        std::array<Item, PB59::SaveEvidence::Limit + 1> items;
        for (std::size_t number = 0; number < bags.size(); ++number)
        {
            auto& bag = bags[number];
            bag.guid = {0x4000000000000100 + number};
            bag.uState = ITEM_UNCHANGED;
            bag.size = MAX_BAG_SIZE;
            bag.slot = INVENTORY_SLOT_BAG_START + number;
            player.m_items[bag.slot] = &bag;
            player.fields[bag.slot] = bag.guid;
        }
        for (std::size_t index = 0; index < items.size(); ++index)
        {
            auto& item = items[index];
            auto& bag = bags[index / MAX_BAG_SIZE];
            item.guid = {0x4000000000000200 + index};
            item.m_container = &bag;
            item.contained = bag.guid;
            item.slot = index % MAX_BAG_SIZE;
            item.uQueuePos = index;
            bag.m_bagslot[item.slot] = &item;
            bag.links[item.slot] = item.guid;
            player.m_itemUpdateQueue.push_back(&item);
        }
        player._pb59Equipment.Bind();
        Ledger::MutationScope scope(player._pb59Transaction);
        ITEM_CHECK(player.ArmRecovery(scope.GetTicket()));
        auto const stamp = *player._pb59Equipment.Capture();
        auto const expected = operation(player, scope);
        auto const prepares = CharacterDatabase.preparedCalls;
        ITEM_CHECK(!Player::PreparePB59RecoverySavePayload(&player, expected, scope, stamp));
        ITEM_CHECK(CharacterDatabase.preparedCalls == prepares && !CharacterDatabase.committed);
    }
    {
        InventoryBatchFixture fixture;
        auto& player = fixture.inventory.player;
        Ledger::MutationScope scope(player._pb59Transaction);
        ITEM_CHECK(player.ArmRecovery(scope.GetTicket()));
        auto const expected = operation(player, scope);
        {
            auto unused = Player::PreparePB59RecoverySavePayload(&player, expected, scope, fixture.stamp);
            ITEM_CHECK(unused && unused->receipt);
        }
        std::vector<SaveObserver::ProtectedReceipt> held;
        for (unsigned slot = 0; slot < 256; ++slot)
        {
            auto reservation = observer.ReserveProtected(1000 + slot, 7, 8);
            if (!reservation.receipt)
                break;
            held.emplace_back(std::move(*reservation.receipt));
        }
        ITEM_CHECK(held.size() == 256);
        auto const prepares = CharacterDatabase.preparedCalls;
        ITEM_CHECK(!Player::PreparePB59RecoverySavePayload(&player, expected, scope, fixture.stamp));
        ITEM_CHECK(CharacterDatabase.preparedCalls == prepares);
        bool returned = true;
        for (auto const& receipt : held)
            returned &= observer.AbandonProtected(receipt) && observer.ConsumeProtected(receipt).has_value();
        ITEM_CHECK(returned);
    }
    std::cout << Checks - before << " PB59 protected recovery payload checks PASS\n";
    return 0;
}

int main()
{
    using Value = decltype(PreparedStatementData::data);
    std::array<Value, 14> const expected{
        uint32{73}, uint64{0x1234567800000053}, uint32{37}, uint32{53}, uint32{19}, uint32{91},
        std::string("-7 0 2 9 -12 "), uint32{103},
        std::string("101 201 301 102 202 302 103 203 303 104 204 304 105 205 305 106 206 306 "
            "107 207 307 108 208 308 109 209 309 110 210 310 111 211 311 112 212 312 "),
        int32{-186}, uint32{126}, uint32{137}, std::string("rune\0text", 9), uint32{41}
    };
    for (ItemUpdateState state : {ITEM_NEW, ITEM_CHANGED})
    {
        for (bool wrapped : {false, true})
        {
            InspectTransaction transaction;
            {
                Item item;
                item.uState = state;
                item.wrapped = wrapped;
                item.mutationAllowed = false;
                Item const& source = item;
                ITEM_CHECK(source.AppendItemInstanceSave(transaction));
                ITEM_CHECK(item.uState == state && item.stateChanges == 0 &&
                    item.m_text == std::get<std::string>(expected[12]));
                item.m_text.assign(1024, '?');
                item.charges.fill(0);
            }
            bool const gift = state == ITEM_CHANGED && wrapped;
            ITEM_CHECK(transaction.GetSize() == (gift ? 2u : 1u));
            auto const& instance = transaction.StatementAt(0);
            ITEM_CHECK(instance.GetIndex() == (state == ITEM_NEW ? CHAR_REP_ITEM_INSTANCE : CHAR_UPD_ITEM_INSTANCE));
            auto const& fields = instance.GetParameters();
            ITEM_CHECK(fields.size() == expected.size());
            for (std::size_t index = 0; index < fields.size(); ++index)
                ITEM_CHECK(fields[index].data == expected[index]);
            if (gift)
            {
                auto const& owner = transaction.StatementAt(1);
                ITEM_CHECK(owner.GetIndex() == CHAR_UPD_GIFT_OWNER);
                ITEM_CHECK(owner.GetParameters().at(0).data == expected[1]);
                ITEM_CHECK(owner.GetParameters().at(1).data == expected[13]);
            }
            ITEM_CHECK(!CharacterDatabase.committed);
        }
    }
    ITEM_CHECK(TrackedStatement::Live == 0);
    for (ItemUpdateState state : {ITEM_UNCHANGED, ITEM_REMOVED})
    {
        Item item;
        item.uState = state;
        InspectTransaction transaction;
        ITEM_CHECK(!item.AppendItemInstanceSave(transaction) && transaction.GetSize() == 0);
        ITEM_CHECK(item.uState == state && !item.stateChanges);
    }
    {
        Item item;
        item.wrapped = true;
        InspectTransaction transaction;
        CharacterDatabase.failOn = CHAR_UPD_GIFT_OWNER;
        bool failed = false;
        try
        {
            (void)item.AppendItemInstanceSave(transaction);
        }
        catch (std::bad_alloc const&)
        {
            failed = true;
        }
        CharacterDatabase.failOn = -1;
        ITEM_CHECK(failed && transaction.GetSize() == 0 && TrackedStatement::Live == 0);
        ITEM_CHECK(item.uState == ITEM_CHANGED && !item.stateChanges && !CharacterDatabase.committed);
    }
    {
        Item item;
        item.wrapped = true;
        auto transaction = CharacterDatabase.BeginTransaction();
        item.mutationAllowed = false;
        item.SaveToDB(transaction);
        ITEM_CHECK(transaction->GetSize() == 0 && item.uState == ITEM_CHANGED && !item.stateChanges);
        item.mutationAllowed = true;
        item.SaveToDB(transaction);
        ITEM_CHECK(transaction->GetSize() == 2 && item.uState == ITEM_UNCHANGED && item.stateChanges == 1);
        ITEM_CHECK(!CharacterDatabase.committed);
        item.SaveToDB(transaction);
        ITEM_CHECK(transaction->GetSize() == 2 && item.stateChanges == 2);
    }
    {
        Item item;
        item.uState = ITEM_NEW;
        item.SaveToDB(nullptr);
        ITEM_CHECK(item.stateChanges == 1 && item.uState == ITEM_UNCHANGED);
        ITEM_CHECK(CharacterDatabase.committed && CharacterDatabase.committed->GetSize() == 1);
        ITEM_CHECK(CharacterDatabase.committed->StatementAt(0).GetIndex() == CHAR_REP_ITEM_INSTANCE);
        CharacterDatabase.committed.reset();
    }
    {
        auto* item = new Item;
        item->uState = ITEM_REMOVED;
        item->wrapped = true;
        unsigned const before = Item::Destroyed;
        item->SaveToDB(nullptr);
        ITEM_CHECK(Item::Destroyed == before + 1);
        ITEM_CHECK(CharacterDatabase.committed && CharacterDatabase.committed->GetSize() == 2);
        ITEM_CHECK(CharacterDatabase.committed->StatementAt(0).GetIndex() == CHAR_DEL_ITEM_INSTANCE);
        ITEM_CHECK(CharacterDatabase.committed->StatementAt(1).GetIndex() == CHAR_DEL_GIFT);
        ITEM_CHECK(CharacterDatabase.committed->StatementAt(0).GetParameters().at(0).data == expected[13]);
        CharacterDatabase.committed.reset();
    }
    ITEM_CHECK(TrackedStatement::Live == 0);
    std::cout << Checks << " PB59 item-instance save checks PASS\n";
    if (auto const status = CheckInventoryLinks())
        return status;
    if (auto const status = CheckInventoryBatch())
        return status;
    return Player::CheckRecoveryPayload();
}
