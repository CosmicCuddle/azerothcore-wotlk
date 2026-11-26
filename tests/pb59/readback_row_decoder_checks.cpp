#include "PB59ReadbackRowDecoder.h"
#include <iostream>
using namespace PB59::Recovery::ReadbackWire;
unsigned checks = 0;
#define CHECK(x) do { ++checks; if (!(x)) { std::cerr << "failed line " << __LINE__ << '\n'; return 1; } } while (false)
int main()
{
    std::array<Cell, 4> l{"7", "8", "255", "9"};
    std::array<Cell, 14> i{"9", "10", "7", "11", "12", "13", "-14", "1 ", "15", "2 3 4 ", "-16", "17", "18", std::string_view("a\0b ", 4)};
    std::array<Cell, 4> g{"9", "7", "19", "20"};
    std::array<Row, 1> ls{l}, is{i}, gs{g};
    Table links{LinkColumns, ls}, instances{InstanceColumns, is}, gifts{GiftColumns, gs};
    auto run = [&] { return DecodeRows(links, instances, gifts, 2); };
    auto rows = run();
    CHECK(rows && rows->links.size() == 1 && rows->instances.size() == 1 && rows->gifts.size() == 1);
    auto const& link = rows->links[0];
    CHECK(link.owner == 7 && link.bag == 8 && link.slot == 255 && link.item == 9);
    auto const& item = rows->instances[0];
    CHECK(item.guid == 9 && item.entry == 10 && item.owner == 7 && item.creator == 11 && item.giftCreator == 12 && item.count == 13);
    CHECK(item.duration == -14 && item.charges == "1 " && item.flags == 15 && item.enchantments == "2 3 4 ");
    CHECK(item.randomProperty == -16 && item.durability == 17 && item.playedTime == 18 && item.text == std::string("a\0b ", 4));
    CHECK(rows->gifts[0].item == 9 && rows->gifts[0].owner == 7 && rows->gifts[0].entry == 19 && rows->gifts[0].flags == 20);
    for (auto index : {1, 7, 8, 13})
    {
        auto original = i[index]; i[index].reset();
        auto got = run(); CHECK(got);
        auto const& value = got->instances[0];
        CHECK(index == 1 ? !value.entry : index == 7 ? !value.charges : index == 8 ? !value.flags : !value.text);
        i[index] = original;
    }
    for (auto index : {0, 2, 3, 4, 5, 6, 9, 10, 11, 12})
    {
        auto original = i[index]; i[index].reset(); CHECK(!run()); i[index] = original;
    }
    for (std::size_t index = 0; index < l.size(); ++index)
    {
        auto original = l[index]; l[index].reset(); CHECK(!run()); l[index] = original;
        original = g[index]; g[index].reset(); CHECK(!run()); g[index] = original;
    }
    for (auto [index, bad] : std::array<std::pair<int, std::string_view>, 9>{{
        {0, "4294967296"}, {1, "-1"}, {5, "01"}, {6, "2147483648"}, {6, "-2147483649"},
        {8, ""}, {10, "-32769"}, {10, "32768"}, {11, "65536"}}})
    {
        auto original = i[index]; i[index] = bad; CHECK(!run()); i[index] = original;
    }
    l[2] = "256"; CHECK(!run()); l[2] = "255";
    for (auto [index, max] : std::array<std::pair<int, std::size_t>, 3>{{{7, 255}, {9, 65535}, {13, 512}}})
    {
        auto original = i[index]; std::string bytes(max, 'x'); i[index] = bytes; CHECK(run());
        bytes.push_back('x'); i[index] = bytes; CHECK(!run());
        i[index] = std::string_view("\xc0\x80", 2); CHECK(!run()); i[index] = original;
    }
    i[7] = ""; i[13] = ""; rows = run(); CHECK(rows && rows->instances[0].charges == "" && rows->instances[0].text == "");
    auto names = InstanceColumns; std::swap(names[3], names[4]); instances.columns = names; CHECK(!run()); instances.columns = InstanceColumns;
    ls[0] = Row(l).first(3); CHECK(!run()); ls[0] = l;
    is[0] = Row(i).first(13); CHECK(!run()); is[0] = i;
    gs[0] = Row(g).first(3); CHECK(!run()); gs[0] = g;
    std::array<Row, 3> repeated{l, l, l}; links.rows = repeated; CHECK(!run());
    links.rows = std::span<Row const>(repeated).first(2); rows = run(); CHECK(rows && rows->links.size() == 2);
    links.rows = {}; instances.rows = {}; gifts.rows = {}; rows = run(); CHECK(rows && rows->links.empty() && rows->instances.empty() && rows->gifts.empty());
    gifts.columns = {}; CHECK(!run()); gifts.columns = GiftColumns;
    CHECK(!DecodeRows(links, instances, gifts, 0));
    std::cout << checks << '/' << checks << " row decoder checks passed\n";
}
