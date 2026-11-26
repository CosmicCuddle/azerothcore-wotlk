#include "PB59CompleteReadbackResult.h"
#include <iostream>
#include <string_view>
using namespace PB59::Recovery::ReadbackWire;
using Capture = CompleteReadbackResult<2>;
unsigned checks = 0;
#define CHECK(x) do { ++checks; if (!(x)) { std::cerr << "failed line " << __LINE__ << '\n'; return 1; } } while (false)
int main()
{
    std::string bytes("a\0b X", 5);
    std::array<FetchedCell, 2> row{{{std::span<char const>(bytes), 4, false, false}, {{}, 0, true, false}}};
    {
        Capture c(2, 2, {4, 3});
        CHECK(c.Observe(FetchStatus::Row, row));
        bytes[0] = 'z'; row[1].isNull = false;
        CHECK(c.Observe(FetchStatus::Row, row));
        CHECK(c.Observe(FetchStatus::End));
        auto result = c.Take();
        CHECK(result && result->size() == 2);
        CHECK((*result)[0][0] == std::string("a\0b ", 4) && !(*result)[0][1]);
        CHECK((*result)[1][0] == std::string("z\0b ", 4) && (*result)[1][1] == "");
        CHECK(!c.Take());
        CHECK(!c.Observe(FetchStatus::End));
    }
    {
        Capture c(0, 1, {4, 3});
        CHECK(c.Observe(FetchStatus::End));
        auto got = c.Take(); CHECK(got && got->empty());
    }
    for (auto advertised : {0u, 3u})
    {
        Capture c(advertised, advertised == 0 ? 0 : 2, {4, 3});
        CHECK(!c.Observe(FetchStatus::End)); CHECK(!c.Take());
    }
    for (auto status : {FetchStatus::Error, FetchStatus::Truncated, static_cast<FetchStatus>(99)})
    {
        Capture c(2, 2, {4, 3});
        CHECK(c.Observe(FetchStatus::Row, row));
        CHECK(!c.Observe(status, row)); CHECK(!c.Observe(FetchStatus::End)); CHECK(!c.Take());
    }
    {
        Capture c(2, 2, {4, 3}); CHECK(c.Observe(FetchStatus::Row, row));
        CHECK(!c.Observe(FetchStatus::End)); CHECK(!c.Take());
    }
    {
        Capture c(1, 2, {4, 3}); CHECK(c.Observe(FetchStatus::Row, row));
        CHECK(!c.Observe(FetchStatus::Row, row)); CHECK(!c.Take());
    }
    {
        Capture c(1, 2, {4, 3}); CHECK(!c.Observe(FetchStatus::Row, std::span(row).first(1))); CHECK(!c.Take());
    }
    {
        Capture c(1, 2, {4, 3}); CHECK(c.Observe(FetchStatus::Row, row));
        CHECK(!c.Observe(FetchStatus::End, row)); CHECK(!c.Take());
    }
    {
        Capture c(1, 2, {4, 3}); CHECK(c.Observe(FetchStatus::Row, row));
        CHECK(!c.Take()); CHECK(!c.Observe(FetchStatus::End)); CHECK(!c.Take());
    }
    {
        Capture c(1, 2, {4, 3}); CHECK(c.Observe(FetchStatus::Row, row)); CHECK(c.Observe(FetchStatus::End));
        CHECK(!c.Observe(FetchStatus::End)); CHECK(!c.Take());
    }
    for (unsigned fault = 0; fault < 5; ++fault)
    {
        Capture c(2, 2, {4, 3}); CHECK(c.Observe(FetchStatus::Row, row));
        auto bad = row;
        switch (fault)
        {
            case 0: bad[0].error = true; break;
            case 1: bad[0].reportedLength = 6; break;
            case 2: bad[0].reportedLength = 5; break;
            case 3: bad[0].isNull = true; break;
            case 4: bad[0].buffer = std::span<char const>(bytes).first(1); bad[0].reportedLength = 2; break;
        }
        CHECK(!c.Observe(FetchStatus::Row, bad)); CHECK(!c.Observe(FetchStatus::End)); CHECK(!c.Take());
    }
    std::cout << checks << '/' << checks << " complete readback result checks passed\n";
}
