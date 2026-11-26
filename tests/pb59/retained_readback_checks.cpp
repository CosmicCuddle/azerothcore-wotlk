#include "PB59RetainedInventoryReadback.h"
#include <cstdlib>
#include <iostream>
using namespace PB59::Recovery;
struct TestStamp { uint64 revision, generation; bool operator==(TestStamp const&) const = default; };
using Gate = InventoryReadbackRequestGate<8, TestStamp>;
using Store = PB59::CharacterSaveEvidence<8>;
using O = Gate::Outcome;
unsigned checks = 0;
#define CHECK(x) do { ++checks; if (!(x)) { std::cerr << "failed line " << __LINE__ << '\n'; return 1; } } while (false)
template<typename T> PreparedStatementData P(T value) { PreparedStatementData p; p.data = std::move(value); return p; }
struct Fixture
{
    Gate::Identity key{{7, 8, 9, 10}, {11, 12}, {0, 13, {7, 8, 9, 14}}};
    Store::Record record;
    ExpectedInventoryWrites expected;
    InventoryReadbackRows rows;
    Fixture()
    {
        record.ticket = key.ticket;
        record.state = Store::State::Terminal;
        record.result = {DatabaseTransactionStatus::Committed, 0};
        InventoryWriteValues e;
        e.itemGuid = InventoryComparisonDetail::ItemPrefix | 101;
        e.link = {CHAR_REP_INVENTORY_ITEM, {P(uint64(7)), P(uint32(0)), P(uint8(15)), P(uint32(101))}};
        e.instance = {CHAR_REP_ITEM_INSTANCE, {P(uint32(42)), P(uint64(7)), P(uint32(0)), P(uint32(0)), P(uint32(1)), P(uint32(0)), P(std::string("0 ")), P(uint32(0)), P(std::string("0 0 0 ")), P(int32(-1)), P(uint32(100)), P(uint32(1)), P(std::string("a")), P(uint32(101))}};
        expected.items.push_back(e);
        record.evidence.itemCount = 1;
        record.evidence.items[0] = e.itemGuid;
        rows.links.push_back({7, 0, 15, 101});
        rows.instances.push_back({101, 42, 7, 0, 0, 1, 0, "0 ", 0, "0 0 0 ", -1, 100, 1, "a"});
    }
    auto Open() { return Gate::Open(key, record, expected); }
};
int main()
{
    using Holder = RetainedInventoryReadback<8, TestStamp>;
    Fixture f;
    Holder first, second, refused, lost;
    auto a = first.Begin(f.key, f.record, f.expected); CHECK(a);
    CHECK(!first.Begin(f.key, f.record, f.expected));
    auto b = second.Begin(f.key, f.record, f.expected); CHECK(b);
    CHECK(first.Finish(std::move(*b), f.rows).outcome == O::Refused);
    CHECK(b->Key());
    CHECK(second.Finish(std::move(*a), f.rows).outcome == O::Refused);
    CHECK(a->Key());
    auto observation = first.Finish(std::move(*a), f.rows);
    CHECK(observation.comparison && observation.comparison->outcome == InventoryValueOutcome::Match);
    CHECK(!a->Key());
    CHECK(!first.Begin(f.key, f.record, f.expected));
    CHECK(first.Finish(std::move(*a), f.rows).outcome == O::Refused);
    CHECK(second.Finish(std::move(*b), std::nullopt).outcome == O::ReadbackUnavailable);
    CHECK(!second.Begin(f.key, f.record, f.expected));
    auto bad = f.record; bad.state = Store::State::Pending;
    CHECK(!refused.Begin(f.key, bad, f.expected));
    CHECK(!refused.Begin(f.key, f.record, f.expected));
    auto abandoned = lost.Begin(f.key, f.record, f.expected); CHECK(abandoned);
    abandoned.reset();
    CHECK(!lost.Begin(f.key, f.record, f.expected));
    Holder mismatch;
    auto req = mismatch.Begin(f.key, f.record, f.expected); CHECK(req);
    f.rows.instances[0].text = "wrong";
    observation = mismatch.Finish(std::move(*req), f.rows);
    CHECK(observation.comparison && observation.comparison->outcome == InventoryValueOutcome::Mismatch);
    CHECK(!mismatch.Begin(f.key, f.record, f.expected));
    std::cout << checks << '/' << checks << " retained readback checks passed\n";
}
