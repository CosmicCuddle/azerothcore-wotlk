#include "PB59BufferedReadbackFetch.h"
#include <iostream>
#include <type_traits>
using namespace PB59::Recovery::ReadbackWire;

// C API test doubles with declarations from the actual client header. No DB.
namespace
{
MYSQL connection{};
MYSQL_RES result{};
char keyName[] = "guid", textName[] = "text", badName[] = "bad!";
char key[] = "7", bytes[] = {'a', '\0', 'b', ' '}, empty[] = "";
std::array<MYSQL_FIELD, 2> fields{};
std::array<char*, 2> cells{key, bytes};
std::array<unsigned long, 2> lengths{1, 4};
unsigned error = 0, warnings = 0, connColumns = 2, resultColumns = 2;
unsigned calls = 0, physicalRows = 1, failAt = 99;
my_ulonglong advertised = 1;
bool more = false, noFields = false, noLengths = false, eofWarning = false;
void Reset()
{
    fields = {};
    fields[0].name = keyName; fields[0].name_length = 4;
    fields[1].name = textName; fields[1].name_length = 4;
    for (auto& f : fields) { f.charsetnr = 63; f.type = MYSQL_TYPE_VAR_STRING; }
    cells = {key, bytes}; lengths = {1, 4};
    error = warnings = calls = 0; connColumns = resultColumns = 2;
    physicalRows = 1; failAt = 99; advertised = 1;
    more = noFields = noLengths = eofWarning = false;
}
}
extern "C" {
unsigned int STDCALL mysql_errno(MYSQL*) { return error; }
unsigned int STDCALL mysql_warning_count(MYSQL*) { return warnings; }
using MoreResult = decltype(mysql_more_results(static_cast<MYSQL*>(nullptr)));
MoreResult STDCALL mysql_more_results(MYSQL*) { return more; }
unsigned int STDCALL mysql_field_count(MYSQL*) { return connColumns; }
unsigned int STDCALL mysql_num_fields(MYSQL_RES*) { return resultColumns; }
my_ulonglong STDCALL mysql_num_rows(MYSQL_RES*) { return advertised; }
MYSQL_FIELD* STDCALL mysql_fetch_fields(MYSQL_RES*) { return noFields ? nullptr : fields.data(); }
MYSQL_ROW STDCALL mysql_fetch_row(MYSQL_RES*)
{
    if (calls == failAt) { ++calls; error = 1; return nullptr; }
    if (calls++ >= physicalRows) { if (eofWarning) ++warnings; return nullptr; }
    return cells.data();
}
unsigned long* STDCALL mysql_fetch_lengths(MYSQL_RES*) { return noLengths ? nullptr : lengths.data(); }
}
unsigned checks = 0;
#define CHECK(x) do { ++checks; if (!(x)) { std::cerr << "failed line " << __LINE__ << '\n'; return 1; } } while(false)
int main()
{
    constexpr std::array<std::string_view, 2> columns{"guid", "text"};
    constexpr std::array<std::size_t, 2> limits{10, 4};
    auto read = [&] { return ReadBufferedResult(&connection, &result, columns, limits, 2); };
    Reset(); auto rows = read();
    CHECK(rows && rows->size() == 1 && (*rows)[0][0] == "7" && (*rows)[0][1] == std::string("a\0b ",4));
    CHECK(calls == 2);
    bytes[0] = 'x'; CHECK((*rows)[0][1] == std::string("a\0b ",4)); bytes[0] = 'a';
    Reset(); cells[1] = nullptr; lengths[1] = 0; rows = read(); CHECK(rows && !(*rows)[0][1]);
    Reset(); cells[1] = empty; lengths[1] = 0; rows = read(); CHECK(rows && (*rows)[0][1] && (*rows)[0][1]->empty());
    Reset(); advertised = physicalRows = 0; rows = read(); CHECK(rows && rows->empty() && calls == 1);
    Reset(); advertised = physicalRows = 2; rows = read(); CHECK(rows && rows->size() == 2 && calls == 3);
    Reset(); CHECK(!ReadBufferedResult(nullptr, &result, columns, limits, 2)); CHECK(calls == 0);
    Reset(); CHECK(!ReadBufferedResult(&connection, nullptr, columns, limits, 2)); CHECK(calls == 0);
    Reset(); CHECK(!ReadBufferedResult(&connection, &result, columns, limits, 0)); CHECK(calls == 0);
    for (int fault = 0; fault < 12; ++fault)
    {
        Reset();
        switch (fault)
        {
            case 0: error = 1; break;
            case 1: warnings = 1; break;
            case 2: more = true; break;
            case 3: connColumns = 1; break;
            case 4: resultColumns = 3; break;
            case 5: advertised = 3; break;
            case 6: noFields = true; break;
            case 7: fields[1].name = nullptr; break;
            case 8: fields[1].name = badName; break;
            case 9: fields[1].name_length = 3; break;
            case 10: fields[1].charsetnr = 45; break;
            case 11: fields[1].type = MYSQL_TYPE_LONG; break;
        }
        CHECK(!read()); CHECK(calls == 0);
    }
    for (int fault = 0; fault < 7; ++fault)
    {
        Reset();
        switch (fault)
        {
            case 0: noLengths = true; break;
            case 1: lengths[1] = 5; break; // never read past the declared budget
            case 2: cells[1] = nullptr; break;
            case 3: physicalRows = 0; break;
            case 4: physicalRows = 2; break;
            case 5: failAt = 1; break;
            case 6: eofWarning = true; break;
        }
        CHECK(!read()); CHECK(calls > 0);
    }
    std::cout << checks << '/' << checks << " buffered fetch checks passed\n";
}
