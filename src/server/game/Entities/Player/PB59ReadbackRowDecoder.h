#pragma once

#include "PB59InventoryReadbackComparison.h"
#include "PB59ReadbackWireValues.h"
#include <algorithm>
#include <array>
#include <span>
#include <utility>

namespace PB59::Recovery::ReadbackWire
{
// Column names describe the required projection (not the physical table order).
// The future producer must authenticate metadata, complete bytes and snapshot.
// A decoded result alone grants no authority to release an inventory hold.
using Row = std::span<Cell const>;
struct Table
{
    std::span<std::string_view const> columns;
    std::span<Row const> rows;
};
inline constexpr std::array<std::string_view, 4> LinkColumns{"guid", "bag", "slot", "item"};
inline constexpr std::array<std::string_view, 14> InstanceColumns{"guid", "itemEntry", "owner_guid",
    "creatorGuid", "giftCreatorGuid", "count", "duration", "charges", "flags", "enchantments",
    "randomPropertyId", "durability", "playedTime", "text"};
inline constexpr std::array<std::string_view, 4> GiftColumns{"item_guid", "guid", "entry", "flags"};

namespace Detail
{
template<std::integral T>
bool NullableInteger(Cell const& cell, std::optional<T>& output)
{
    if (!cell)
    {
        output.reset();
        return true;
    }
    T value{};
    if (!Integer(cell, value))
        return false;
    output = value;
    return true;
}
inline bool Utf8Text(Cell const& cell, std::size_t limit, bool nullable, std::optional<std::string>& output)
{
    return Text(cell, limit, nullable, output) && (!output || InventoryComparisonDetail::Utf8(*output));
}
inline std::optional<InventoryReadbackLink> Link(Row row)
{
    InventoryReadbackLink out{};
    if (row.size() != LinkColumns.size() || !Integer(row[0], out.owner) || !Integer(row[1], out.bag) ||
        !Integer(row[2], out.slot) || !Integer(row[3], out.item))
        return std::nullopt;
    return out;
}
inline std::optional<InventoryReadbackInstance> Instance(Row row)
{
    InventoryReadbackInstance out{};
    std::optional<std::string> enchants;
    if (row.size() != InstanceColumns.size() || !Integer(row[0], out.guid) ||
        !NullableInteger(row[1], out.entry) || !Integer(row[2], out.owner) || !Integer(row[3], out.creator) ||
        !Integer(row[4], out.giftCreator) || !Integer(row[5], out.count) || !Integer(row[6], out.duration) ||
        !Utf8Text(row[7], 255, true, out.charges) || !NullableInteger(row[8], out.flags) ||
        !Utf8Text(row[9], 65535, false, enchants) || !Integer(row[10], out.randomProperty) ||
        !Integer(row[11], out.durability) || !Integer(row[12], out.playedTime) ||
        !Utf8Text(row[13], MaximumTextBytes, true, out.text))
        return std::nullopt;
    out.enchantments = std::move(*enchants);
    return out;
}
inline std::optional<InventoryReadbackGift> Gift(Row row)
{
    InventoryReadbackGift out{};
    if (row.size() != GiftColumns.size() || !Integer(row[0], out.item) || !Integer(row[1], out.owner) ||
        !Integer(row[2], out.entry) || !Integer(row[3], out.flags))
        return std::nullopt;
    return out;
}

template<typename T, typename Decode, std::size_t N>
bool Append(Table table, std::array<std::string_view, N> const& columns, std::size_t rowLimit,
    std::vector<T>& output, Decode decode)
{
    if (!std::ranges::equal(table.columns, columns) || table.rows.size() > rowLimit)
        return false;
    output.reserve(table.rows.size());
    for (Row row : table.rows)
    {
        auto decoded = decode(row);
        if (!decoded)
            return false;
        output.push_back(std::move(*decoded));
    }
    return true;
}
}

// All or nothing. Do not discard duplicates, unexpected keys or missing rows;
// the comparator must see the actual multiplicity. A valid empty projection
// is distinct from unavailable input. The caller must certify completeness.
[[nodiscard]] inline std::optional<InventoryReadbackRows> DecodeRows(Table links, Table instances,
    Table gifts, std::size_t rowLimitPerTable)
{
    InventoryReadbackRows out;
    if (!rowLimitPerTable || !Detail::Append(links, LinkColumns, rowLimitPerTable, out.links, Detail::Link) ||
        !Detail::Append(instances, InstanceColumns, rowLimitPerTable, out.instances, Detail::Instance) ||
        !Detail::Append(gifts, GiftColumns, rowLimitPerTable, out.gifts, Detail::Gift))
        return std::nullopt;
    return out;
}
}
