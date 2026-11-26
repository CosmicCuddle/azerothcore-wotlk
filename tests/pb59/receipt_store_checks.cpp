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
    Fixture f;
    Store store;
    auto reserved = store.Reserve(f.key.ticket.identity);
    CHECK(reserved.status == Store::Admission::Accepted);
    f.key.ticket = reserved.ticket;
    auto rec = store.Inspect(reserved.ticket); CHECK(rec);
    CHECK(!Gate::Open(f.key, *rec, f.expected));
    CHECK(store.Seal(reserved.ticket, f.record.evidence));
    rec = store.Inspect(reserved.ticket); CHECK(rec);
    CHECK(!Gate::Open(f.key, *rec, f.expected));
    CHECK(store.Complete(reserved.ticket, {DatabaseTransactionStatus::Committed, 0}));
    rec = store.Inspect(reserved.ticket); CHECK(rec);
    auto first = Gate::Open(f.key, *rec, f.expected); CHECK(first);
    auto req = first->Begin(f.key); CHECK(req);
    CHECK(store.Acknowledge(reserved.ticket));
    CHECK(!store.Inspect(reserved.ticket));
    auto next = store.Reserve(f.key.ticket.identity); CHECK(next.status == Store::Admission::Accepted);
    CHECK(next.ticket.slot == reserved.ticket.slot && next.ticket.serial != reserved.ticket.serial);
    CHECK(store.Seal(next.ticket, f.record.evidence));
    CHECK(store.Complete(next.ticket, {DatabaseTransactionStatus::Committed, 0}));
    auto fresh = store.Inspect(next.ticket); CHECK(fresh);
    auto nextKey = f.key; nextKey.ticket = next.ticket;
    CHECK(!Gate::Open(f.key, *fresh, f.expected));
    CHECK(!Gate::Open(nextKey, *rec, f.expected));
    auto second = Gate::Open(nextKey, *fresh, f.expected); CHECK(second);
    CHECK(!second->Begin(f.key));
    auto req2 = second->Begin(nextKey); CHECK(req2);
    CHECK(second->Finish(std::move(*req), f.rows).outcome == O::Refused);
    CHECK(first->Finish(std::move(*req2), f.rows).outcome == O::Refused);
    CHECK(req->Key() && req2->Key());
    auto observation = first->Finish(std::move(*req), f.rows);
    CHECK(observation.comparison && observation.comparison->outcome == InventoryValueOutcome::Match);
    CHECK(second->Finish(std::move(*req2), std::nullopt).outcome == O::ReadbackUnavailable);
    CHECK(!second->Begin(nextKey));
    CHECK(!first->Begin(f.key));
    // Documented limitation: an externally retained record can open a new gate.
    // Holder must enforce uniqueness; matching values do not prove DB custody.
    auto reopened = Gate::Open(f.key, *rec, f.expected); CHECK(reopened);
    CHECK(reopened->Begin(f.key));
    for (auto status : {DatabaseTransactionStatus::Empty, DatabaseTransactionStatus::NotStarted,
                       DatabaseTransactionStatus::Ambiguous, DatabaseTransactionStatus::FailedUnverified,
                       DatabaseTransactionStatus::NotEnqueued, DatabaseTransactionStatus::CancelledBeforeExecution})
    {
        auto altered = *fresh; altered.result = {status, 0};
        CHECK(!Gate::Open(nextKey, altered, f.expected));
    }
    auto altered = *fresh; altered.evidence.AddSkill(42); CHECK(!Gate::Open(nextKey, altered, f.expected));
    altered = *fresh; altered.evidence.items[0] = 0; CHECK(!Gate::Open(nextKey, altered, f.expected));
    auto e = f.expected.items[0]; e.itemGuid = InventoryComparisonDetail::ItemPrefix | 102;
    e.link.parameters[2] = P(uint8(16)); e.link.parameters[3] = P(uint32(102)); e.instance.parameters[13] = P(uint32(102));
    f.expected.items.push_back(e);
    altered = *fresh; altered.evidence.items[1] = altered.evidence.items[0]; altered.evidence.items[0] = e.itemGuid; altered.evidence.itemCount = 2;
    auto l = f.rows.links[0]; l.item = 102; l.slot = 16; f.rows.links.push_back(l);
    auto v = f.rows.instances[0]; v.guid = 102; f.rows.instances.push_back(v);
    auto multi = Gate::Open(nextKey, altered, f.expected); CHECK(multi);
    auto request = multi->Begin(nextKey); CHECK(request && request->Items().size() == 2);
    observation = multi->Finish(std::move(*request), f.rows);
    CHECK(observation.comparison && observation.comparison->outcome == InventoryValueOutcome::Match);
    altered.evidence.items[1] = e.itemGuid; CHECK(!Gate::Open(nextKey, altered, f.expected));
    std::cout << checks << '/' << checks << " receipt-store boundary checks passed\n";
}
