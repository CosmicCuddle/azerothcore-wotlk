#pragma once

#include "AuctionVisibility.h"
#include <algorithm>
#include <cstddef>

namespace AuctionBrowseSelection
{
struct Context
{
    AuctionVisibility::Mode mode = AuctionVisibility::Mode::Disabled;
    AuctionVisibility::Policy policy;
    AuctionVisibility::Viewer viewer;
};

constexpr bool Visible(Context const& context, AuctionVisibility::Tag const& tag)
{
    return AuctionVisibility::Evaluate(context.mode, context.policy, context.viewer, tag).visible;
}

struct Window { std::size_t begin; std::size_t end; };
constexpr Window PageWindow(std::size_t total, std::size_t offset, std::size_t limit)
{
    auto begin = std::min(total, offset);
    return {begin, begin + std::min(limit, total - begin)};
}

struct Counts { std::size_t returned = 0; std::size_t total = 0; };

// getAll intentionally ignores ordinary browse criteria. Count all permitted
// rows even after reaching the serialization limit in Enforced mode.
// Disabled/Observe keep legacy result/count semantics and the capped scan.
template<class Range, class TagFor, class Emit>
Counts GetAll(Range const& rows, Context const& context, std::size_t cap,
    TagFor tagFor, Emit emit)
{
    Counts result;
    bool const legacy = context.mode == AuctionVisibility::Mode::Disabled ||
        context.mode == AuctionVisibility::Mode::Observe;
    if (legacy)
        result.total = rows.size();
    for (auto const& row : rows)
    {
        if (legacy && result.returned == cap)
            break;
        if (!Visible(context, tagFor(row)))
            continue;
        if (!legacy)
            ++result.total;
        if (result.returned < cap)
        {
            emit(row);
            ++result.returned;
        }
    }
    return result;
}
}
