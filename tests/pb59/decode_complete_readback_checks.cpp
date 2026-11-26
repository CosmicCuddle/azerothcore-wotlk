#include "PB59DecodeCompleteReadback.h"
#include <iostream>
using namespace PB59::Recovery;
using namespace PB59::Recovery::ReadbackWire;
unsigned checks = 0;
#define CHECK(x) do { ++checks; if (!(x)) { std::cerr << "failed line " << __LINE__ << '\n'; return 1; } } while (false)

template<std::size_t N>
std::array<std::size_t, N> Limits()
{
    std::array<std::size_t, N> limits{};
    limits.fill(65535);
    return limits;
}
template<std::size_t N>
bool Add(CompleteReadbackResult<N>& capture, std::array<Cell, N> const& cells)
{
    std::array<FetchedCell, N> fetched{};
    for (std::size_t n = 0; n < N; ++n)
        if (cells[n])
            fetched[n] = {std::span<char const>(cells[n]->data(), cells[n]->size()), cells[n]->size(), false, false};
        else
            fetched[n] = {{}, 0, true, false};
    return capture.Observe(FetchStatus::Row, fetched);
}
struct Batch
{
    CompleteReadbackResult<4> links, gifts;
    CompleteReadbackResult<14> instances;
    Batch(std::size_t l = 1, std::size_t i = 1, std::size_t g = 1)
        : links(l, 20, Limits<4>()), gifts(g, 20, Limits<4>()), instances(i, 20, Limits<14>()) {}
    auto Decode(std::span<std::string_view const> lc = LinkColumns,
        std::span<std::string_view const> ic = InstanceColumns, std::span<std::string_view const> gc = GiftColumns)
    {
        return DecodeCompleteReadback(links, lc, instances, ic, gifts, gc);
    }
};
int main()
{
    std::array<Cell, 4> l{"7", "0", "23", "9"}, g{"9", "7", "13", "14"};
    std::string text("a\0b ", 4);
    std::array<Cell, 14> i{"9", "10", "7", "11", "12", "1", "-2", std::nullopt, "0", "1 2 3 ", "-4", "5", "6", text};
    {
        Batch b;
        CHECK(Add(b.links, l) && Add(b.instances, i) && Add(b.gifts, g));
        text[0] = 'z';
        CHECK(b.links.Observe(FetchStatus::End) && b.instances.Observe(FetchStatus::End) && b.gifts.Observe(FetchStatus::End));
        auto rows = b.Decode();
        CHECK(rows && rows->links.size() == 1 && rows->instances.size() == 1 && rows->gifts.size() == 1);
        CHECK(rows->links[0].owner == 7 && rows->links[0].item == 9 && rows->links[0].slot == 23);
        CHECK(rows->instances[0].guid == 9 && rows->instances[0].text == std::string("a\0b ", 4));
        CHECK(!rows->instances[0].charges && rows->instances[0].flags == 0 && rows->instances[0].randomProperty == -4);
        CHECK(rows->gifts[0].entry == 13 && rows->gifts[0].flags == 14);
        CHECK(!b.Decode());
    }
    for (int missing = 0; missing < 3; ++missing)
    {
        Batch b;
        CHECK(Add(b.links, l) && Add(b.instances, i) && Add(b.gifts, g));
        if (missing != 0) CHECK(b.links.Observe(FetchStatus::End));
        if (missing != 1) CHECK(b.instances.Observe(FetchStatus::End));
        if (missing != 2) CHECK(b.gifts.Observe(FetchStatus::End));
        CHECK(!b.Decode());
        CHECK(!b.links.Take() && !b.instances.Take() && !b.gifts.Take());
    }
    {
        Batch b(0, 0, 0);
        CHECK(b.links.Observe(FetchStatus::End) && b.instances.Observe(FetchStatus::End) && b.gifts.Observe(FetchStatus::End));
        auto rows = b.Decode(); CHECK(rows && rows->links.empty() && rows->instances.empty() && rows->gifts.empty());
    }
    {
        Batch b(2, 1, 0);
        CHECK(Add(b.links, l) && Add(b.links, l) && Add(b.instances, i));
        CHECK(b.links.Observe(FetchStatus::End) && b.instances.Observe(FetchStatus::End) && b.gifts.Observe(FetchStatus::End));
        auto rows = b.Decode(); CHECK(rows && rows->links.size() == 2 && rows->gifts.empty());
    }
    for (int wrong = 0; wrong < 3; ++wrong)
    {
        Batch b;
        CHECK(Add(b.links, l) && Add(b.instances, i) && Add(b.gifts, g));
        CHECK(b.links.Observe(FetchStatus::End) && b.instances.Observe(FetchStatus::End) && b.gifts.Observe(FetchStatus::End));
        CHECK(!b.Decode(wrong == 0 ? std::span<std::string_view const>(GiftColumns) : LinkColumns,
            wrong == 1 ? std::span<std::string_view const>(InstanceColumns).first(13) : InstanceColumns,
            wrong == 2 ? std::span<std::string_view const>(LinkColumns) : GiftColumns));
        CHECK(!b.links.Take() && !b.instances.Take() && !b.gifts.Take());
    }
    {
        Batch b;
        auto invalid = i; invalid[10] = "32768";
        CHECK(Add(b.links, l) && Add(b.instances, invalid) && Add(b.gifts, g));
        CHECK(b.links.Observe(FetchStatus::End) && b.instances.Observe(FetchStatus::End) && b.gifts.Observe(FetchStatus::End));
        CHECK(!b.Decode()); CHECK(!b.links.Take() && !b.instances.Take() && !b.gifts.Take());
    }
    {
        Batch b(20, 0, 0);
        for (int j = 0; j < 20; ++j) CHECK(Add(b.links, l));
        CHECK(b.links.Observe(FetchStatus::End) && b.instances.Observe(FetchStatus::End) && b.gifts.Observe(FetchStatus::End));
        CHECK(!b.Decode());
        CHECK(!b.links.Take() && !b.instances.Take() && !b.gifts.Take());
    }
    {
        Batch b(1, 0, 0);
        CHECK(Add(b.links, l)); CHECK(!b.links.Observe(FetchStatus::Truncated));
        CHECK(b.instances.Observe(FetchStatus::End) && b.gifts.Observe(FetchStatus::End)); CHECK(!b.Decode());
    }
    std::cout << checks << '/' << checks << " complete decode checks passed\n";
}
