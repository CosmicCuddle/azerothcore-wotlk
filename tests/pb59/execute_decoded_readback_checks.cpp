#include "PB59ExecuteDecodedReadback.h"
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
void Reset(unsigned kind = 0)
{
    queries = frees = closes = cursor = width = 0;
    fault = kind; rows = 1; openResult = badCleanup = false;
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
unsigned long STDCALL mysql_thread_id(MYSQL*) { return 41; }
unsigned int STDCALL mysql_errno(MYSQL*) { return 0; }
unsigned int STDCALL mysql_warning_count(MYSQL*) { return 0; }
using MoreResult = decltype(mysql_more_results(static_cast<MYSQL*>(nullptr)));
MoreResult STDCALL mysql_more_results(MYSQL*) { return false; }
int STDCALL mysql_real_query(MYSQL*, char const*, unsigned long)
{
    if (openResult) badCleanup = true;
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
int main()
{
    constexpr std::uint64_t prefix = 0x4000ULL << 48;
    std::array<std::uint64_t, 2> ids{prefix | 7, prefix | 8};
    auto run = [&](unsigned count = 1) {
        return ExecuteDecodedReadback(OwnedReadbackConnection(&connection), 41, std::span(ids).first(count));
    };
    Reset(); auto got = run();
    CHECK(got && got->links.size() == 1 && got->instances.size() == 1 && got->gifts.size() == 1);
    CHECK(got->links[0].owner == 17 && got->links[0].bag == 0 && got->links[0].slot == 23 && got->links[0].item == 7);
    auto const& i = got->instances[0];
    CHECK(i.guid == 7 && i.entry == 100 && i.owner == 17 && i.creator == 19 && i.giftCreator == 20 && i.count == 2);
    CHECK(i.duration == -5 && !i.charges && i.flags == 0 && i.enchantments == "1 2 3 ");
    CHECK(i.randomProperty == -186 && i.durability == 126 && i.playedTime == 41 && i.text == std::string("a\0b ", 4));
    CHECK(got->gifts[0].item == 7 && got->gifts[0].owner == 17 && got->gifts[0].entry == 200 && got->gifts[0].flags == 9);
    CHECK(queries == 3 && frees == 3 && closes == 1 && !badCleanup);
    for (unsigned kind : {1u, 2u, 5u, 6u, 7u, 9u, 10u, 11u, 13u})
    {
        Reset(kind); CHECK(!run()); CHECK(closes == 1 && !badCleanup);
        CHECK(queries == ((kind == 7 || kind == 9 || kind == 10) ? 2u : 3u));
        CHECK(frees == queries - (kind == 10 ? 1u : 0u));
    }
    Reset(3); got = run(); CHECK(got && got->links[0].owner == 18);
    Reset(4); CHECK(!run()); CHECK(queries == 1 && frees == 1 && closes == 1);
    Reset(4); got = run(2); CHECK(got && got->links.size() == 2 && got->links[0].item == got->links[1].item);
    Reset(8); got = run(); CHECK(got && got->instances[0].charges && got->instances[0].charges->empty());
    Reset(12); got = run(); CHECK(got && got->links.size() == 1 && got->instances.size() == 1 && got->gifts.empty());
    Reset(14); got = run(); CHECK(got && got->links.empty() && got->instances.empty() && got->gifts.empty());
    CHECK(queries == 3 && frees == 3 && closes == 1 && !badCleanup);
    Reset(); CHECK(!run(0)); CHECK(queries == 0 && closes == 1);
    Reset(); ids[0] = 7; CHECK(!run()); CHECK(queries == 0 && closes == 1);
    std::cout << checks << '/' << checks << " execute/decode checks passed\n";
}
