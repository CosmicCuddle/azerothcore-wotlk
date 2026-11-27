#include "PB59BoundSnapshotReadback.h"
#include "PB59RetainedInventoryReadback.h"
#include <iostream>
using namespace PB59::Recovery::ReadbackWire;
namespace
{
MYSQL connection{};
MYSQL_RES result{};
std::array<MYSQL_FIELD, 14> fields{};
std::array<std::string, 14> names{};
std::array<std::optional<std::string>, 14> values{};
std::array<char*, 14> cells{};
std::array<unsigned long, 14> lengths{};
unsigned queries, frees, closes, cursor, width, rows, fault;
bool openResult, badCleanup;
unsigned controls, controlFault, faultStep, warning, error;
unsigned long threadId;
my_ulonglong affected;
bool extra;
std::array<std::string, 2> controlSql;
std::array<std::string, 3> selectSql;
void Reset(unsigned kind = 0)
{
    queries = frees = closes = cursor = width = 0;
    fault = kind; rows = 1; openResult = badCleanup = false;
    controls = controlFault = faultStep = warning = error = 0;
    threadId = 41; affected = 0; extra = false; controlSql = {}; selectSql = {};
}
template<std::size_t N>
void Fields(std::array<QueryColumn, N> const& projection, std::array<Cell, N> const& source)
{
    width = N; fields = {}; values = {};
    for (std::size_t i = 0; i < N; ++i)
    {
        names[i] = projection[i].name;
        fields[i].name = names[i].data(); fields[i].name_length = names[i].size();
        fields[i].type = MYSQL_TYPE_VAR_STRING; fields[i].charsetnr = 63;
        if (source[i]) values[i] = std::string(*source[i]);
    }
}
}
extern "C" {
unsigned long STDCALL mysql_thread_id(MYSQL*) { return threadId; }
unsigned int STDCALL mysql_errno(MYSQL*) { return error; }
unsigned int STDCALL mysql_warning_count(MYSQL*) { return warning; }
using MoreResult = decltype(mysql_more_results(static_cast<MYSQL*>(nullptr)));
MoreResult STDCALL mysql_more_results(MYSQL*) { return extra; }
my_ulonglong STDCALL mysql_affected_rows(MYSQL*) { return affected; }
int STDCALL mysql_real_query(MYSQL*, char const* sql, unsigned long size)
{
    std::string_view command(sql, size);
    if (command.starts_with("SET ") || command.starts_with("START "))
    {
        if (queries || controls >= 2 || openResult) { badCleanup = true; return 1; }
        controlSql[controls++] = command;
        width = 0;
        if (controls == faultStep)
        {
            switch (controlFault)
            {
                case 1: return 1;
                case 2: error = 1; break;
                case 3: warning = 1; break;
                case 4: ++threadId; break;
                case 5: extra = true; break;
                case 6: width = 1; break;
                case 7: affected = 1; break;
            }
        }
        return 0;
    }
    if (controls != 2) { badCleanup = true; return 1; }
    if (openResult) badCleanup = true;
    if (queries < selectSql.size()) selectSql[queries] = command;
    ++queries; cursor = 0; rows = 1;
    if (queries == 1)
    {
        Fields(LinkProjection, std::array<Cell, 4>{"17", "0", "23", "7"});
        if (fault == 1) values[2] = "256";
        if (fault == 2) values[0].reset();
        if (fault == 3) values[0] = "18";
        if (fault == 4) rows = 2;
    }
    else if (queries == 2)
    {
        Fields(InstanceProjection, std::array<Cell, 14>{"7", "100", "17", "19", "20", "2", "5",
            "0 ", "0", "1 2 3 ", "-186", "126", "41", std::string_view("a\0b ", 4)});
        if (fault == 5) values[13] = std::string("\xc0\x80", 2);
        if (fault == 6) values[10] = "32768";
        if (fault == 7) values[13] = std::string(513, 'x');
        if (fault == 8) values[7] = "";
        if (fault == 9) fields[4].name = names[3].data();
        if (fault == 10) return 1;
    }
    else if (queries == 3)
    {
        Fields(GiftProjection, std::array<Cell, 4>{"7", "17", "200", "9"});
        if (fault == 11) values[3] = "01";
        if (fault == 12) rows = 0;
        if (fault == 13) rows = 2;
    }
    else return 1;
    if (fault == 14) rows = 0;
    for (std::size_t i = 0; i < width; ++i)
    {
        cells[i] = values[i] ? values[i]->data() : nullptr;
        lengths[i] = values[i] ? values[i]->size() : 0;
    }
    return 0;
}
MYSQL_RES* STDCALL mysql_store_result(MYSQL*) { openResult = true; return &result; }
void STDCALL mysql_free_result(MYSQL_RES*)
{
    if (!openResult) badCleanup = true;
    openResult = false; ++frees;
    // Make borrowed views into result bytes observably invalid after cleanup.
    for (auto& v : values) if (v) std::fill(v->begin(), v->end(), 'x');
}
void STDCALL mysql_close(MYSQL*) { if (openResult) badCleanup = true; ++closes; }
unsigned int STDCALL mysql_field_count(MYSQL*) { return width; }
unsigned int STDCALL mysql_num_fields(MYSQL_RES*) { return width; }
my_ulonglong STDCALL mysql_num_rows(MYSQL_RES*) { return rows; }
MYSQL_FIELD* STDCALL mysql_fetch_fields(MYSQL_RES*) { return fields.data(); }
MYSQL_ROW STDCALL mysql_fetch_row(MYSQL_RES*) { return cursor++ < rows ? cells.data() : nullptr; }
unsigned long* STDCALL mysql_fetch_lengths(MYSQL_RES*) { return lengths.data(); }
}
unsigned checks = 0;
#define CHECK(x) do { ++checks; if (!(x)) { std::cerr << "failed line " << __LINE__ << '\n'; return 1; } } while (false)
using namespace PB59::Recovery;
struct TestStamp { uint64 revision, generation; bool operator==(TestStamp const&) const = default; };
using Readback = RetainedInventoryReadback<8, TestStamp>;
using Store = PB59::CharacterSaveEvidence<8>;
using Job = BoundSnapshotReadback<Readback>;
using O = Readback::Outcome;
using V = InventoryValueOutcome;
template<typename T> PreparedStatementData P(T value) { PreparedStatementData p; p.data = std::move(value); return p; }
// The real retained gate and comparator, with an isolated routing adapter.
struct Holder
{
    Readback retained;
    Readback::Identity key{{17, 8, 9, 10}, {11, 12}, {0, 13, {17, 8, 9, 14}}};
    Store::Record record;
    ExpectedInventoryWrites expected;
    unsigned deliveries = 0;
    Holder()
    {
        record.ticket = key.ticket;
        record.state = Store::State::Terminal;
        record.result = {DatabaseTransactionStatus::Committed, 0};
        InventoryWriteValues e;
        e.itemGuid = InventoryComparisonDetail::ItemPrefix | 7;
        e.link = {CHAR_REP_INVENTORY_ITEM, {P(uint64(17)), P(uint32(0)), P(uint8(23)), P(uint32(7))}};
        e.instance = {CHAR_UPD_ITEM_INSTANCE, {P(uint32(100)), P(uint64(17)), P(uint32(19)), P(uint32(20)),
            P(uint32(2)), P(uint32(5)), P(std::string("0 ")), P(uint32(0)), P(std::string("1 2 3 ")),
            P(int32(-186)), P(uint32(126)), P(uint32(41)), P(std::string("a\0b ", 4)), P(uint32(7))}};
        e.giftOwner = InventorySaveStatement{CHAR_UPD_GIFT_OWNER, {P(uint64(17)), P(uint32(7))}};
        expected.items.push_back(e);
        record.evidence.itemCount = 1;
        record.evidence.items[0] = e.itemGuid;
    }
    auto Begin() { return retained.Begin(key, record, expected); }
    Readback::Observation FinishRetainedReadback(Readback::Request&& request, std::optional<InventoryReadbackRows> const& rows)
    {
        ++deliveries;
        return retained.Finish(std::move(request), rows);
    }
};
int main()
{
    static_assert(!std::is_copy_constructible_v<Job>);
    for (unsigned kind : {0u, 1u, 3u, 7u, 8u, 10u, 12u, 14u})
    {
        Holder h; auto request = h.Begin(); CHECK(request);
        CHECK(!h.Begin());
        Reset(kind); auto job = Job::Run(std::move(*request), OwnedReadbackConnection(&connection), 41);
        CHECK(!request->Key() && closes == 1 && !badCleanup);
        h.expected.items[0].instance.parameters[12] = P(std::string("changed after capture"));
        auto result = std::move(job).DeliverTo(h);
        if (kind == 1 || kind == 7 || kind == 10)
            CHECK(result.outcome == O::ReadbackUnavailable && !result.comparison);
        else
        {
            CHECK(result.outcome == O::ComparedValues && result.comparison);
            V want = kind == 0 ? V::Match : (kind == 12 || kind == 14) ? V::Missing : V::Mismatch;
            CHECK(result.comparison->outcome == want);
        }
        CHECK(h.deliveries == 1 && !h.Begin());
        CHECK(std::move(job).DeliverTo(h).outcome == O::Refused && h.deliveries == 1);
    }
    {
        Holder a, b; auto ra = a.Begin(); auto rb = b.Begin(); CHECK(ra && rb);
        Reset(); auto job = Job::Run(std::move(*ra), OwnedReadbackConnection(&connection), 41);
        CHECK(std::move(job).DeliverTo(b).outcome == O::Refused);
        CHECK(std::move(job).DeliverTo(a).outcome == O::Refused && a.deliveries == 0);
        Reset(); auto other = Job::Run(std::move(*rb), OwnedReadbackConnection(&connection), 41);
        auto result = std::move(other).DeliverTo(b);
        CHECK(result.outcome == O::ComparedValues && result.comparison && result.comparison->outcome == V::Match);
    }
    {
        Holder h; h.record.result.status = DatabaseTransactionStatus::Ambiguous; CHECK(!h.Begin());
        h.record.result.status = DatabaseTransactionStatus::Committed; CHECK(!h.Begin());
    }
    {
        Holder h; h.record.evidence.items[0]++; CHECK(!h.Begin());
        h.record.evidence.items[0]--; CHECK(!h.Begin());
    }
    std::cout << checks << '/' << checks << " real gate snapshot checks passed\n";
}
