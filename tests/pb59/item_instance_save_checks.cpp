#include "PreparedStatement.h"
#include "Transaction.h"
#include <array>
#include <cstdlib>
#include <iostream>
#include <sstream>

#define ASSERT(condition) do { if (!(condition)) std::abort(); } while (false)
#define ABORT() std::abort()
#define LOG_FATAL(...) ((void)0)

enum Statement { CHAR_REP_ITEM_INSTANCE, CHAR_UPD_ITEM_INSTANCE, CHAR_UPD_GIFT_OWNER,
    CHAR_DEL_ITEM_INSTANCE, CHAR_DEL_GIFT };
enum ItemUpdateState { ITEM_UNCHANGED, ITEM_CHANGED, ITEM_NEW, ITEM_REMOVED };
enum FieldIndex { ITEM_FIELD_CREATOR, ITEM_FIELD_GIFTCREATOR, ITEM_FIELD_DURATION, ITEM_FIELD_FLAGS,
    ITEM_FIELD_DURABILITY, ITEM_FIELD_CREATE_PLAYED_TIME };
enum EnchantmentSlot : uint8 { SlotZero };
constexpr uint8 MAX_ITEM_PROTO_SPELLS = 5;
constexpr uint8 MAX_ENCHANTMENT_SLOT = 12;

struct ObjectGuid
{
    using LowType = uint32;
    uint64 value;
    uint64 GetRawValue() const { return value; }
    LowType GetCounter() const { return static_cast<LowType>(value); }
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
        if (statement == failOn)
            throw std::bad_alloc();
        switch (statement)
        {
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
    std::shared_ptr<InspectTransaction> committed;
} CharacterDatabase;

class Item
{
public:
    ~Item() { ++Destroyed; }
    bool AppendItemInstanceSave(Transaction<CharacterDatabaseConnection>& transaction) const;
    void SaveToDB(CharacterDatabaseTransaction transaction);

    ObjectGuid GetGUID() const { return {0x4000000000000029}; }
    ObjectGuid GetOwnerGUID() const { return {0x1234567800000053}; }
    ObjectGuid GetGuidValue(FieldIndex field) const { return {field == ITEM_FIELD_CREATOR ? 37u : 53u}; }
    uint32 GetEntry() const { return 73; }
    uint32 GetCount() const { return 19; }
    uint32 GetUInt32Value(FieldIndex field) const { return fields.at(field); }
    int32 GetSpellCharges(uint8 index) const { return charges.at(index); }
    uint32 GetEnchantmentId(EnchantmentSlot slot) const { return 101 + slot; }
    uint32 GetEnchantmentDuration(EnchantmentSlot slot) const { return 201 + slot; }
    uint32 GetEnchantmentCharges(EnchantmentSlot slot) const { return 301 + slot; }
    int32 GetItemRandomPropertyId() const { return -186; }
    ItemUpdateState GetState() const { return uState; }
    bool IsWrapped() const { return wrapped; }
    bool PB59InventoryMutationAllowed() const { return mutationAllowed; }
    void SetState(ItemUpdateState state) { uState = state; ++stateChanges; }

    static unsigned Destroyed;
    std::array<uint32, 6> fields{0, 0, 91, 103, 126, 137};
    std::array<int32, 5> charges{-7, 0, 2, 9, -12};
    std::string m_text{"rune\0text", 9};
    ItemUpdateState uState = ITEM_CHANGED;
    unsigned stateChanges = 0;
    bool wrapped = false;
    bool mutationAllowed = true;
};

unsigned Item::Destroyed = 0;

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
}
