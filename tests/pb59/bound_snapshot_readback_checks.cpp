#include "PB59BoundSnapshotReadback.h"
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
        Fields(InstanceProjection, std::array<Cell, 14>{"7", "100", "17", "19", "20", "2", "-5",
            std::nullopt, "0", "1 2 3 ", "-186", "126", "41", std::string_view("a\0b ", 4)});
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
// Synthetic retained holder with the same move-only request/Finish contract.
// The actual gate's identity/receipt checks remain separate tests.
struct Readback
{
    struct Identity { unsigned serial; };
    struct State { Identity key; std::array<std::uint64_t, 1> items; };
    class Request
    {
        friend struct Readback;
        explicit Request(std::shared_ptr<State const> state) noexcept : state(std::move(state)) {}
        std::shared_ptr<State const> state;
    public:
        Request(Request&&) noexcept = default;
        Request(Request const&) = delete;
        Identity const* Key() const noexcept { return state ? &state->key : nullptr; }
        std::span<std::uint64_t const> Items() const noexcept { return state ? std::span<std::uint64_t const>(state->items) : std::span<std::uint64_t const>{}; }
    };
    enum class Outcome { Refused, ReadbackUnavailable, ComparedValues };
    struct Observation { Outcome outcome; std::optional<unsigned> item; };
    std::shared_ptr<State const> state;
    bool issued = false;
    unsigned delivered = 0;
    explicit Readback(unsigned serial, std::uint64_t item) : state(std::make_shared<State const>(State{{serial}, {item}})) {}
    std::optional<Request> Begin()
    {
        if (issued) return {};
        issued = true;
        return Request(state);
    }
    Observation FinishRetainedReadback(Request&& request, std::optional<PB59::Recovery::InventoryReadbackRows> const& rows)
    {
        ++delivered;
        if (request.state != state) return {Outcome::Refused, std::nullopt};
        request.state.reset();
        if (!rows) return {Outcome::ReadbackUnavailable, std::nullopt};
        return {Outcome::ComparedValues, rows->links.empty() ? 0u : rows->links[0].item};
    }
};
using Job = BoundSnapshotReadback<Readback>;
using O = Readback::Outcome;
int main()
{
    constexpr std::uint64_t prefix = 0x4000ULL << 48;
    static_assert(!std::is_default_constructible_v<Job> && !std::is_copy_constructible_v<Job>);
    static_assert(!std::is_constructible_v<Job, Readback::Request, std::optional<PB59::Recovery::InventoryReadbackRows>>);
    {
        Readback holder(1, prefix | 7); auto req = holder.Begin(); CHECK(req);
        Reset(); auto job = Job::Run(std::move(*req), OwnedReadbackConnection(&connection), 41);
        CHECK(!req->Key() && controls == 2 && queries == 3 && frees == 3 && closes == 1 && !badCleanup);
        for (auto const& sql : selectSql) CHECK(sql.ends_with(" IN (7) LIMIT 2"));
        auto moved = std::move(job);
        CHECK(std::move(job).DeliverTo(holder).outcome == O::Refused && holder.delivered == 0);
        auto result = std::move(moved).DeliverTo(holder);
        CHECK(result.outcome == O::ComparedValues && result.item == 7 && holder.delivered == 1);
        CHECK(std::move(moved).DeliverTo(holder).outcome == O::Refused && holder.delivered == 1);
        CHECK(!holder.Begin());
        Reset(); auto empty = Job::Run(std::move(*req), OwnedReadbackConnection(&connection), 41);
        CHECK(controls == 0 && queries == 0 && closes == 1);
        CHECK(std::move(empty).DeliverTo(holder).outcome == O::Refused && holder.delivered == 1);
    }
    {
        Readback a(2, prefix | 19), b(2, prefix | 19); auto req = a.Begin(); CHECK(req);
        Reset(); auto job = Job::Run(std::move(*req), OwnedReadbackConnection(&connection), 41);
        for (auto const& sql : selectSql) CHECK(sql.ends_with(" IN (19) LIMIT 2"));
        CHECK(std::move(job).DeliverTo(b).outcome == O::Refused && b.delivered == 1);
        CHECK(std::move(job).DeliverTo(a).outcome == O::Refused && a.delivered == 0);
        CHECK(!a.Begin());
    }
    for (unsigned kind : {1u, 7u, 10u, 11u})
    {
        Readback holder(3, prefix | 7); auto req = holder.Begin(); CHECK(req);
        Reset(kind); auto job = Job::Run(std::move(*req), OwnedReadbackConnection(&connection), 41);
        CHECK(closes == 1 && !badCleanup);
        auto result = std::move(job).DeliverTo(holder);
        CHECK(result.outcome == O::ReadbackUnavailable && !result.item && holder.delivered == 1);
        CHECK(std::move(job).DeliverTo(holder).outcome == O::Refused && !holder.Begin());
    }
    {
        Readback holder(4, 7); auto req = holder.Begin(); CHECK(req);
        Reset(); auto job = Job::Run(std::move(*req), OwnedReadbackConnection(&connection), 41);
        CHECK(controls == 0 && queries == 0 && closes == 1);
        CHECK(std::move(job).DeliverTo(holder).outcome == O::ReadbackUnavailable);
    }
    {
        Readback holder(5, prefix | 7); auto req = holder.Begin(); CHECK(req);
        struct Throws { Readback::Observation FinishRetainedReadback(Readback::Request&&, std::optional<PB59::Recovery::InventoryReadbackRows> const&) { throw 1; } } throws;
        Reset(); auto job = Job::Run(std::move(*req), OwnedReadbackConnection(&connection), 41);
        bool threw = false;
        try { (void)std::move(job).DeliverTo(throws); } catch (int) { threw = true; }
        CHECK(threw && std::move(job).DeliverTo(holder).outcome == O::Refused && holder.delivered == 0);
    }
    std::cout << checks << '/' << checks << " bound snapshot checks passed\n";
}
