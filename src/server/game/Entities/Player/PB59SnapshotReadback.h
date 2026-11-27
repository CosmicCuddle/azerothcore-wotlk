#pragma once

#include "PB59ExecuteDecodedReadback.h"

namespace PB59::Recovery::ReadbackWire
{
namespace QueryExecutionDetail
{
// Only fixed, non-result transaction-control commands on the dedicated
// connection. Any unexpected result or warning forces the owner to close.
inline bool SnapshotCommand(MYSQL* connection, unsigned long thread, std::string_view sql) noexcept
{
    return SameConnection(connection, thread) &&
        !mysql_real_query(connection, sql.data(), static_cast<unsigned long>(sql.size())) &&
        SameConnection(connection, thread) && mysql_field_count(connection) == 0 &&
        mysql_affected_rows(connection) == 0;
}
}

// Caller must provide a fresh, exclusively owned authenticated connection,
// with no active transaction, verified InnoDB schema and reconnect disabled.
// SET TRANSACTION applies to the next transaction only. A successful START
// does not by itself prove all tables are transactional or writer exclusion.
// No commit or automatic retry; the executor closes the dedicated connection.
[[nodiscard]] inline std::optional<InventoryReadbackRows> ExecuteSnapshotReadback(
    OwnedReadbackConnection connection, unsigned long expectedThread, std::span<std::uint64_t const> items) noexcept
{
    try
    {
        if (!BuildReadbackQueryPlan(items) || items.size() > MaximumOriginals)
            return std::nullopt;
        if (!QueryExecutionDetail::SnapshotCommand(connection.get(), expectedThread,
                "SET TRANSACTION ISOLATION LEVEL REPEATABLE READ") ||
            !QueryExecutionDetail::SnapshotCommand(connection.get(), expectedThread,
                "START TRANSACTION WITH CONSISTENT SNAPSHOT, READ ONLY"))
            return std::nullopt;
        return ExecuteDecodedReadback(std::move(connection), expectedThread, items);
    }
    catch (...)
    {
        return std::nullopt;
    }
}
}
