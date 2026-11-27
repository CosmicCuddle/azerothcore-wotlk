#pragma once

#include "PB59CompleteReadbackResult.h"
#include <mysql.h>
#include <limits>
#include <string_view>

namespace PB59::Recovery::ReadbackWire
{
// Only for a freshly executed, single-result, binary-projected text-protocol
// SELECT, buffered using mysql_store_result on the exclusively owned connection.
// The caller owns both handles, must establish that relationship and must free
// the result even on refusal. No retry, seek, query, reconnect or transaction.
// C API row lengths define the accessible byte extent under its contract;
// there is no separate application-owned bound as with prepared fetch buffers.
// The SQL producer must bound the server result BEFORE mysql_store_result.
// This is not receipt, snapshot or custody authentication.
inline bool BinaryProjection(MYSQL_FIELD const& field) noexcept
{
    if (field.charsetnr != 63) // binary character set, not a text collation
        return false;
    switch (field.type)
    {
        case MYSQL_TYPE_STRING:
        case MYSQL_TYPE_VAR_STRING:
        case MYSQL_TYPE_VARCHAR:
        case MYSQL_TYPE_TINY_BLOB:
        case MYSQL_TYPE_BLOB:
        case MYSQL_TYPE_MEDIUM_BLOB:
        case MYSQL_TYPE_LONG_BLOB:
            return true;
        default:
            return false;
    }
}

template<std::size_t N>
[[nodiscard]] std::optional<typename CompleteReadbackResult<N>::Rows> ReadBufferedResult(
    MYSQL* connection, MYSQL_RES* result, std::array<std::string_view, N> const& columns,
    std::array<std::size_t, N> const& limits, std::size_t rowLimit) noexcept
{
    if (!connection || !result || !rowLimit || mysql_errno(connection) ||
        mysql_warning_count(connection) || mysql_more_results(connection) ||
        mysql_field_count(connection) != N || mysql_num_fields(result) != N)
        return std::nullopt;
    auto count = mysql_num_rows(result);
    if (count > rowLimit || count > std::numeric_limits<std::size_t>::max())
        return std::nullopt;
    auto fields = mysql_fetch_fields(result);
    if (!fields)
        return std::nullopt;
    for (std::size_t i = 0; i < N; ++i)
        if (!fields[i].name || columns[i].empty() || fields[i].name_length != columns[i].size() ||
            std::string_view(fields[i].name, fields[i].name_length) != columns[i] || !BinaryProjection(fields[i]))
            return std::nullopt;

    CompleteReadbackResult<N> capture(static_cast<std::size_t>(count), rowLimit, limits);
    while (auto row = mysql_fetch_row(result))
    {
        if (mysql_errno(connection))
            return std::nullopt;
        auto lengths = mysql_fetch_lengths(result);
        if (!lengths)
            return std::nullopt;
        std::array<FetchedCell, N> cells{};
        for (std::size_t i = 0; i < N; ++i)
        {
            if (lengths[i] > limits[i] || (!row[i] && lengths[i] != 0))
                return std::nullopt;
            auto length = static_cast<std::size_t>(lengths[i]);
            cells[i] = {row[i] ? std::span<char const>(row[i], length) : std::span<char const>{},
                length, !row[i], false};
        }
        if (!capture.Observe(FetchStatus::Row, cells))
            return std::nullopt;
    }
    if (mysql_errno(connection) || mysql_warning_count(connection) || mysql_more_results(connection) ||
        !capture.Observe(FetchStatus::End))
        return std::nullopt;
    return capture.Take();
}
}
