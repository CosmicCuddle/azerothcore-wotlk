#include "AuctionBrowseSelection.h"
#include <cassert>
#include <limits>
#include <vector>

using namespace AuctionVisibility;
using namespace AuctionBrowseSelection;

struct Row { unsigned id; Tag tag; bool ordinary; };

int main()
{
    Context context{Mode::Enforced, {5, 1, 8, 1}, {true, 5, 1, 3, 0}};
    std::vector<Row> rows;
    for (unsigned i = 0; i < 120; ++i)
    {
        Tag tag = i < 35 ? Tag{TagState::Unrestricted, 5, 1, 0, 0} :
            i < 70 ? Tag{TagState::Controlled, 5, 1, 3, 0} :
            i < 90 ? Tag{TagState::Controlled, 5, 1, 4, 0} :
            i < 100 ? Tag{TagState::Controlled, 5, 1, 0, 1} :
            i < 110 ? Tag{} : Tag{TagState::Unrestricted, 4, 1, 0, 0};
        rows.push_back({i, tag, i % 2 == 0});
    }
    std::vector<unsigned> normal, detailed, all;
    for (auto const& row : rows)
        if (Visible(context, row.tag))
        {
            normal.push_back(row.id);
            if (row.ordinary)
                detailed.push_back(row.id);
        }
    assert(normal.size() == 70 && detailed.size() == 35);
    auto first = PageWindow(normal.size(), 0, 50);
    auto second = PageWindow(normal.size(), 50, 50);
    auto empty = PageWindow(normal.size(), 70, 50);
    auto beyond = PageWindow(normal.size(), 999, 50);
    assert(first.end - first.begin == 50 && second.end - second.begin == 20);
    assert(empty.begin == empty.end && beyond.begin == beyond.end);
    auto max = std::numeric_limits<std::size_t>::max();
    assert(PageWindow(max, max - 1, max).end == max);
    auto tagFor = [](Row const& row) { return row.tag; };
    auto emit = [&](Row const& row) { all.push_back(row.id); };
    auto counts = GetAll(rows, context, 50, tagFor, emit);
    assert(counts.returned == 50 && counts.total == 70 && all.size() == 50);
    assert(all.front() == 0 && all.back() == 49); // includes non-ordinary rows
    all.clear();
    counts = GetAll(rows, context, 0, tagFor, emit);
    assert(counts.returned == 0 && counts.total == 70 && all.empty());
    for (auto mode : {Mode::Disabled, Mode::Observe})
    {
        all.clear();
        context.mode = mode;
        counts = GetAll(rows, context, 50, tagFor, emit);
        assert(counts.returned == 50 && counts.total == 120 && all.size() == 50);
        for (auto const& row : rows)
            assert(Visible(context, row.tag));
    }
    Context noProvider;
    all.clear();
    counts = GetAll(rows, noProvider, 200, tagFor, emit);
    assert(counts.returned == 120 && counts.total == 120);
}
