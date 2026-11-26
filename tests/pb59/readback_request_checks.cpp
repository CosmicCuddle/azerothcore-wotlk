#include "PB59InventoryReadbackRequest.h"
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
    static_assert(!std::is_copy_constructible_v<Gate::Request>);
    static_assert(!std::is_default_constructible_v<Gate::Request>);
    Fixture f;
    auto g = f.Open(); CHECK(g);
    auto other = f.Open(); CHECK(other);
    auto k = f.key; k.operation.ticket++; CHECK(!g->Begin(k));
    k = f.key; k.operation.ownerIncarnation++; CHECK(!g->Begin(k));
    k = f.key; k.operation.sessionIncarnation++; CHECK(!g->Begin(k));
    k = f.key; k.stamp.revision++; CHECK(!g->Begin(k));
    k = f.key; k.stamp.generation++; CHECK(!g->Begin(k));
    k = f.key; k.ticket.serial++; CHECK(!g->Begin(k));
    k = f.key; k.ticket.slot++; CHECK(!g->Begin(k));
    k = f.key; k.ticket.identity.sequence++; CHECK(!g->Begin(k));
    auto a = g->Begin(f.key); CHECK(a);
    CHECK(a->Key() && *a->Key() == f.key);
    CHECK(a->Items().size() == 1 && a->Items()[0] == f.expected.items[0].itemGuid);
    CHECK(!g->Begin(f.key));
    auto b = other->Begin(f.key); CHECK(b);
    CHECK(other->Finish(std::move(*a), f.rows).outcome == O::Refused);
    CHECK(a->Key());
    auto moved = std::move(*a); CHECK(!a->Key() && a->Items().empty());
    CHECK(g->Finish(std::move(*a), f.rows).outcome == O::Refused);
    f.expected.items[0].instance.parameters[12] = P(std::string("changed"));
    auto result = g->Finish(std::move(moved), f.rows);
    CHECK(result.outcome == O::ComparedValues && result.comparison && result.comparison->outcome == InventoryValueOutcome::Match);
    CHECK(!moved.Key()); CHECK(!g->Begin(f.key));
    CHECK(g->Finish(std::move(moved), f.rows).outcome == O::Refused);
    auto unavailable = other->Finish(std::move(*b), std::nullopt);
    CHECK(unavailable.outcome == O::ReadbackUnavailable && !unavailable.comparison);
    CHECK(!other->Begin(f.key));
    Fixture bad; bad.record.state = Store::State::Pending; CHECK(!bad.Open());
    bad = Fixture{}; bad.record.result.status = DatabaseTransactionStatus::Ambiguous; CHECK(!bad.Open());
    bad = Fixture{}; bad.record.result.executionCode = -2; CHECK(!bad.Open());
    bad = Fixture{}; bad.key.operation.owner++; CHECK(!bad.Open());
    bad = Fixture{}; bad.key.stamp.revision = 0; CHECK(!bad.Open());
    bad = Fixture{}; bad.key.ticket.serial++; CHECK(!bad.Open());
    bad = Fixture{}; bad.key.ticket.slot = 8; bad.record.ticket = bad.key.ticket; CHECK(!bad.Open());
    bad = Fixture{}; bad.record.evidence.incomplete = true; CHECK(!bad.Open());
    bad = Fixture{}; bad.record.evidence.itemCount = 65; CHECK(!bad.Open());
    bad = Fixture{}; bad.record.evidence.items[0]++; CHECK(!bad.Open());
    bad = Fixture{}; bad.record.evidence.questCount = 1; bad.record.evidence.quests[0] = 42; CHECK(!bad.Open());
    bad = Fixture{}; bad.expected.items.clear(); CHECK(!bad.Open());
    bad = Fixture{}; bad.expected.items[0].link.parameters[0] = P(uint64(77)); bad.expected.items[0].instance.parameters[1] = P(uint64(77)); CHECK(!bad.Open());
    bad = Fixture{}; bad.expected.items[0].instance.parameters.pop_back(); CHECK(!bad.Open());
    bad = Fixture{}; auto mismatch = bad.Open(); CHECK(mismatch); auto req = mismatch->Begin(bad.key); CHECK(req);
    bad.rows.instances[0].text = "wrong"; result = mismatch->Finish(std::move(*req), bad.rows);
    CHECK(result.outcome == O::ComparedValues && result.comparison && result.comparison->outcome == InventoryValueOutcome::Mismatch);
    CHECK(!mismatch->Begin(bad.key));
    std::cout << checks << '/' << checks << " request ownership checks passed\n";
}
