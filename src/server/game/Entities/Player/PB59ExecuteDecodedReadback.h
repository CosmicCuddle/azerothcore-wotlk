#pragma once

#include "PB59ExecuteReadbackQueries.h"
#include "PB59DecodeCompleteReadback.h"

namespace PB59::Recovery::ReadbackWire
{
namespace QueryExecutionDetail
{
template<std::size_t N>
constexpr bool ProjectionMatches(std::array<QueryColumn, N> const& projection,
    std::array<std::string_view, N> const& columns) noexcept
{
    for (std::size_t i = 0; i < N; ++i)
        if (projection[i].name != columns[i])
            return false;
    return true;
}
static_assert(ProjectionMatches(LinkProjection, LinkColumns));
static_assert(ProjectionMatches(InstanceProjection, InstanceColumns));
static_assert(ProjectionMatches(GiftProjection, GiftColumns));
static_assert(InstanceProjection[13].bytes == MaximumTextBytes);
}

// Diagnostic only: the caller must establish exclusive, authenticated
// connection ownership, schema and a read-only consistent snapshot before
// calling. No authority to release a hold or authenticate a receipt follows.
// Decode the exact owned batch returned by the executor; no external raw rows
// or metadata are supplied to this entry point, and no partial output escapes.
[[nodiscard]] inline std::optional<InventoryReadbackRows> ExecuteDecodedReadback(
    OwnedReadbackConnection connection, unsigned long expectedThread, std::span<std::uint64_t const> items) noexcept
{
    if (items.empty() || items.size() > MaximumOriginals)
        return std::nullopt;
    auto const rowLimit = items.size();
    auto owned = ExecuteReadbackQueries(std::move(connection), expectedThread, items);
    if (!owned)
        return std::nullopt;
    try
    {
        Detail::CapturedViews<4> links(owned->links), gifts(owned->gifts);
        Detail::CapturedViews<14> instances(owned->instances);
        return DecodeRows(links.View(LinkColumns), instances.View(InstanceColumns), gifts.View(GiftColumns), rowLimit);
    }
    catch (...)
    {
        return std::nullopt;
    }
}
}
