#include "PB59InventoryReadbackComparison.h"
#include <cstdlib>
#include <iostream>
#include <string_view>

using namespace PB59::Recovery;
using O = InventoryValueOutcome;
static unsigned checks = 0;

template<typename T> PreparedStatementData P(T value)
{
    PreparedStatementData p;
    p.data = std::move(value);
    return p;
}

struct Fixture
{
    ExpectedInventoryWrites expected;
    InventoryReadbackRows observed;
    explicit Fixture(bool gift = false)
    {
        InventoryWriteValues e;
        e.itemGuid = InventoryComparisonDetail::ItemPrefix | 101;
        e.link = {CHAR_REP_INVENTORY_ITEM, {P(uint64(7)), P(uint32(0)), P(uint8(15)), P(uint32(101))}};
        e.instance = {gift ? CHAR_UPD_ITEM_INSTANCE : CHAR_REP_ITEM_INSTANCE,
            {P(uint32(42)), P(uint64(7)), P(uint32(0)), P(uint32(0)), P(uint32(1)),
             P(uint32(0)), P(std::string("0 ")), P(uint32(0)), P(std::string("0 0 0 ")),
             P(int32(-1)), P(uint32(100)), P(uint32(1)), P(std::string("a\0b", 3)), P(uint32(101))}};
        if (gift)
        {
            e.giftOwner = InventorySaveStatement{CHAR_UPD_GIFT_OWNER, {P(uint64(7)), P(uint32(101))}};
            observed.gifts.push_back({101, 7, 99, 1});
        }
        expected.items.push_back(e);
        observed.links.push_back({7, 0, 15, 101});
        observed.instances.push_back({101, 42, 7, 0, 0, 1, 0, "0 ", 0, "0 0 0 ", -1, 100, 1,
            std::string("a\0b", 3)});
    }
    void Check(O outcome, std::string_view column, unsigned line)
    {
        ++checks;
        auto actual = CompareInventoryWriteValues(expected, observed);
        if (actual.outcome != outcome || column != actual.column)
        {
            std::cerr << "inventory comparison line " << line << ": expected "
                      << static_cast<int>(outcome) << " " << column << ", got "
                      << static_cast<int>(actual.outcome) << " " << actual.column << '\n';
            std::exit(1);
        }
    }
};

#define CASE(change, outcome, column) do { Fixture f; change; f.Check(O::outcome, column, __LINE__); } while (false)
#define GIFT(change, outcome, column) do { Fixture f(true); change; f.Check(O::outcome, column, __LINE__); } while (false)

int main()
{
    CASE((void)0, Match, "");
    GIFT((void)0, Match, "");
    CASE(f.expected.items.clear(), Refused, "empty expected set");
    CASE(f.observed.links.clear(), Missing, "item");
    CASE(f.observed.instances.clear(), Missing, "guid");
    GIFT(f.observed.gifts.clear(), Missing, "item_guid");
    CASE(f.observed.gifts.push_back((InventoryReadbackGift{101, 7, 99, 1})), Unexpected, "item_guid");
    CASE(f.observed.links.push_back(f.observed.links.front()), Duplicate, "item");
    CASE(f.observed.instances.push_back(f.observed.instances.front()), Duplicate, "guid");
    GIFT(f.observed.gifts.push_back(f.observed.gifts.front()), Duplicate, "item_guid");
    CASE(auto x = f.observed.links.front(); x.item++; f.observed.links.push_back(x), Duplicate, "owner/bag/slot");
    CASE(auto x = f.observed.links.front(); x.item++; x.slot++; f.observed.links.push_back(x), Unexpected, "item");
    CASE(auto x = f.observed.instances.front(); x.guid++; f.observed.instances.push_back(x), Unexpected, "guid");
    CASE(f.expected.items.push_back(f.expected.items.front()), Refused, "duplicate expected link");
    CASE(f.observed.instances[0].entry.reset(), Mismatch, "itemEntry");
    CASE(f.observed.instances[0].flags.reset(), Mismatch, "flags");
    CASE(f.observed.instances[0].charges.reset(), Mismatch, "charges");
    CASE(f.observed.instances[0].text.reset(), Mismatch, "text");
    CASE(f.observed.instances[0].text = "a", Mismatch, "text");
    CASE(f.observed.instances[0].charges = "0", Mismatch, "charges");
    CASE(f.observed.instances[0].enchantments = "0 0 0", Mismatch, "enchantments");
    CASE(f.observed.links[0].owner++, Mismatch, "guid");
    CASE(f.observed.links[0].bag++, Mismatch, "bag");
    CASE(f.observed.links[0].slot++, Mismatch, "slot");
    CASE(f.observed.instances[0].owner++, Mismatch, "owner_guid");
    CASE(f.observed.instances[0].creator++, Mismatch, "creatorGuid");
    CASE(f.observed.instances[0].giftCreator++, Mismatch, "giftCreatorGuid");
    CASE(f.observed.instances[0].count++, Mismatch, "count");
    CASE(f.observed.instances[0].duration++, Mismatch, "duration");
    CASE(f.observed.instances[0].randomProperty++, Mismatch, "randomPropertyId");
    CASE(f.observed.instances[0].durability++, Mismatch, "durability");
    CASE(f.observed.instances[0].playedTime++, Mismatch, "playedTime");
    GIFT(f.observed.gifts[0].owner++, Mismatch, "guid");
    GIFT(f.observed.gifts[0].entry++; f.observed.gifts[0].flags++, Match, "");
    CASE(f.expected.items[0].itemGuid ^= uint64(1) << 32, Unrepresentable, "identity");
    CASE(f.expected.items[0].itemGuid = 101, Unrepresentable, "identity");
    CASE(f.expected.items[0].link.parameters[0] = P(uint64(1) << 32), Unrepresentable, "identity");
    CASE(f.expected.items[0].link.parameters[2] = P(uint32(15)), Refused, "binding type");
    CASE(f.expected.items[0].instance.parameters[1] = P(uint64(8)), Refused, "identity/count");
    CASE(f.expected.items[0].instance.parameters[13] = P(uint32(102)), Refused, "identity/count");
    CASE(f.expected.items[0].instance.parameters[4] = P(uint32(0)), Refused, "identity/count");
    CASE(f.expected.items[0].instance.parameters[5] = P(uint32(0x80000000)), Unrepresentable, "duration");
    CASE(f.expected.items[0].instance.parameters[9] = P(int32(32768)), Unrepresentable, "randomPropertyId");
    CASE(f.expected.items[0].instance.parameters[9] = P(int32(-32769)), Unrepresentable, "randomPropertyId");
    CASE(f.expected.items[0].instance.parameters[9] = P(int32(-32768)); f.observed.instances[0].randomProperty = -32768, Match, "");
    CASE(f.expected.items[0].instance.parameters[10] = P(uint32(65536)), Unrepresentable, "durability");
    CASE(f.expected.items[0].instance.parameters[10] = P(uint32(65535)); f.observed.instances[0].durability = 65535, Match, "");
    CASE(f.expected.items[0].instance.parameters[6] = P(std::string(256, 'a')), Unrepresentable, "charges");
    CASE(f.expected.items[0].instance.parameters[6] = P(std::string(255, 'a')); f.observed.instances[0].charges = std::string(255, 'a'), Match, "");
    CASE(f.expected.items[0].instance.parameters[12] = P(std::string(MaximumTextBytes + 1, 'a')), Unrepresentable, "text");
    CASE(f.expected.items[0].instance.parameters[12] = P(std::string("\xc0\x80")), Unrepresentable, "text");
    CASE(f.expected.items[0].instance.parameters[12] = P(std::string("\xed\xa0\x80")), Unrepresentable, "text");
    CASE(f.expected.items[0].instance.parameters[12] = P(std::string("\xf4\x90\x80\x80")), Unrepresentable, "text");
    CASE(f.expected.items[0].instance.parameters[12] = P(std::string("\xf0\x9f\x98\x80")); f.observed.instances[0].text = "\xf0\x9f\x98\x80", Match, "");
    CASE(f.expected.items[0].link.index = CHAR_UPD_ITEM_INSTANCE, Refused, "statement");
    CASE(f.expected.items[0].instance.parameters.pop_back(), Refused, "statement");
    CASE(f.expected.items[0].instance.parameters[12] = P(uint32(0)), Refused, "binding type");
    GIFT(f.expected.items[0].giftOwner->parameters[1] = P(uint32(102)), Refused, "identity");
    GIFT(f.expected.items[0].instance.index = CHAR_REP_ITEM_INSTANCE, Refused, "statement");
    // Independent CS02/Grok mutation-review gaps (Coordination #52, 7191850277).
    CASE(f.expected.items[0].instance.parameters[8] = P(std::string(65535, 'a')); f.observed.instances[0].enchantments = std::string(65535, 'a'), Match, "");
    CASE(f.expected.items[0].instance.parameters[8] = P(std::string(65536, 'a')); f.observed.instances[0].enchantments = std::string(65536, 'a'), Unrepresentable, "enchantments");
    CASE(f.expected.items[0].instance.parameters[6] = P(std::string()); f.observed.instances[0].charges.reset(), Mismatch, "charges");
    CASE(f.expected.items[0].instance.parameters[0] = P(uint32(0)); f.observed.instances[0].entry = 0, Refused, "identity/count");
    CASE(f.expected.items[0].instance.parameters[12] = P(std::string("\xe0\x80\x80")); f.observed.instances[0].text = "\xe0\x80\x80", Unrepresentable, "text");
    CASE(f.expected.items[0].instance.parameters[12] = P(std::string("\xf0\x80\x80\x80")); f.observed.instances[0].text = "\xf0\x80\x80\x80", Unrepresentable, "text");
    CASE(auto e = f.expected.items.front(); e.link.parameters[2] = P(uint8(16)); f.expected.items.push_back(e), Refused, "duplicate expected link");
    CASE(auto e = f.expected.items.front(); e.itemGuid = InventoryComparisonDetail::ItemPrefix | 102; e.link.parameters[0] = P(uint64(8)); e.link.parameters[3] = P(uint32(102)); e.instance.parameters[1] = P(uint64(8)); e.instance.parameters[13] = P(uint32(102)); f.expected.items.push_back(e); auto l = f.observed.links.front(); l.owner = 8; l.item = 102; f.observed.links.push_back(l); auto v = f.observed.instances.front(); v.guid = 102; v.owner = 8; f.observed.instances.push_back(v), Refused, "mixed owners");
    GIFT(f.expected.items[0].giftOwner->parameters.push_back(P(uint32(0))), Refused, "statement");
    std::cout << checks << "/" << checks << " inventory value comparison checks passed\n";
}
