#pragma once

#include <array>
#include <charconv>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace PB59::Recovery::ReadbackWire
{
// Diagnostic SQL only. Requires independently verified integer/text schema,
// an exclusive connection and consistent snapshot. No execution or authority.
// The extra byte and extra row are overflow sentinels, never accepted data.
struct QueryColumn
{
    std::string_view name;
    std::size_t bytes;
};
inline constexpr std::array<QueryColumn, 4> LinkProjection{{{"guid", 10}, {"bag", 10}, {"slot", 3}, {"item", 10}}};
inline constexpr std::array<QueryColumn, 14> InstanceProjection{{{"guid", 10}, {"itemEntry", 10}, {"owner_guid", 10},
    {"creatorGuid", 10}, {"giftCreatorGuid", 10}, {"count", 10}, {"duration", 11}, {"charges", 255},
    {"flags", 10}, {"enchantments", 65535}, {"randomPropertyId", 6}, {"durability", 5}, {"playedTime", 10}, {"text", 512}}};
inline constexpr std::array<QueryColumn, 4> GiftProjection{{{"item_guid", 10}, {"guid", 10}, {"entry", 10}, {"flags", 10}}};

struct ReadbackQueryPlan
{
    std::string links, instances, gifts;
    std::size_t rowLimit;
};
namespace QueryPlanDetail
{
inline void Decimal(std::string& out, std::uint64_t value)
{
    char buffer[20];
    auto converted = std::to_chars(buffer, buffer + sizeof(buffer), value);
    out.append(buffer, converted.ptr);
}

template<std::size_t N>
std::string Select(std::array<QueryColumn, N> const& columns, std::string_view table,
    std::string_view key, std::string_view keys, std::size_t rows)
{
    std::string sql = "SELECT ";
    for (std::size_t i = 0; i < N; ++i)
    {
        if (i) sql += ',';
        sql += "SUBSTRING(CAST(`";
        sql += columns[i].name;
        sql += "` AS BINARY),1,";
        Decimal(sql, columns[i].bytes + 1);
        sql += ") AS `";
        sql += columns[i].name;
        sql += '`';
    }
    sql += " FROM `"; sql += table; sql += "` WHERE `"; sql += key;
    sql += "` IN ("; sql += keys; sql += ") LIMIT "; Decimal(sql, rows + 1);
    return sql;
}
}

[[nodiscard]] inline std::optional<ReadbackQueryPlan> BuildReadbackQueryPlan(std::span<std::uint64_t const> items)
{
    // Only canonical single-realm item GUIDs, not arbitrary strings or low IDs.
    constexpr std::uint64_t prefix = std::uint64_t(0x4000) << 48;
    constexpr std::uint64_t lowMask = 0xffffffffULL;
    constexpr std::size_t maximumItems = 19;
    if (items.empty() || items.size() > maximumItems)
        return std::nullopt;
    std::string keys;
    for (std::size_t i = 0; i < items.size(); ++i)
    {
        auto const low = items[i] & lowMask;
        if (!low || items[i] != (prefix | low))
            return std::nullopt;
        for (std::size_t j = 0; j < i; ++j)
            if (items[i] == items[j])
                return std::nullopt;
        if (i) keys += ',';
        QueryPlanDetail::Decimal(keys, low);
    }
    // Query by item key only, never filter by expected owner: wrong-owner rows
    // must reach the comparator. No joins or DISTINCT that hide missing rows.
    return ReadbackQueryPlan{
        QueryPlanDetail::Select(LinkProjection, "character_inventory", "item", keys, items.size()),
        QueryPlanDetail::Select(InstanceProjection, "item_instance", "guid", keys, items.size()),
        QueryPlanDetail::Select(GiftProjection, "character_gifts", "item_guid", keys, items.size()), items.size()};
}
}
