#pragma once

#include "PB59BufferedReadbackFetch.h"
#include "PB59ReadbackQueryPlan.h"
#include <memory>

namespace PB59::Recovery::ReadbackWire
{
struct ReadbackConnectionCloser
{
    void operator()(MYSQL* connection) const noexcept { mysql_close(connection); }
};
using OwnedReadbackConnection = std::unique_ptr<MYSQL, ReadbackConnectionCloser>;

struct BufferedReadbackRows
{
    CompleteReadbackResult<4>::Rows links;
    CompleteReadbackResult<14>::Rows instances;
    CompleteReadbackResult<4>::Rows gifts;
};
namespace QueryExecutionDetail
{
inline bool SameConnection(MYSQL* connection, unsigned long thread) noexcept
{
    return connection && thread && mysql_thread_id(connection) == thread &&
        !mysql_errno(connection) && !mysql_warning_count(connection) && !mysql_more_results(connection);
}
struct ResultCloser
{
    void operator()(MYSQL_RES* result) const noexcept { mysql_free_result(result); }
};

template<std::size_t N>
std::optional<typename CompleteReadbackResult<N>::Rows> Execute(MYSQL* connection, unsigned long thread,
    std::string const& sql, std::array<QueryColumn, N> const& projection, std::size_t rowLimit) noexcept
{
    if (!SameConnection(connection, thread) || mysql_real_query(connection, sql.data(), static_cast<unsigned long>(sql.size())))
        return std::nullopt;
    // Even on refusal the enclosing one-shot owner closes the connection;
    // no unread or failed result can return to a general connection pool.
    if (!SameConnection(connection, thread))
        return std::nullopt;
    std::unique_ptr<MYSQL_RES, ResultCloser> result(mysql_store_result(connection));
    if (!result || !SameConnection(connection, thread))
        return std::nullopt;
    std::array<std::string_view, N> columns{};
    std::array<std::size_t, N> limits{};
    for (std::size_t i = 0; i < N; ++i)
    {
        columns[i] = projection[i].name;
        limits[i] = projection[i].bytes;
    }
    auto rows = ReadBufferedResult(connection, result.get(), columns, limits, rowLimit);
    result.reset();
    if (!SameConnection(connection, thread))
        return std::nullopt;
    return rows;
}
}

// Diagnostic execution only. Caller must exclusively transfer a dedicated,
// already authenticated connection with reconnect disabled, verified schema,
// and established read-only consistent snapshot. Thread ID is an additional
// continuity check, NOT proof of those requirements. No connection returned,
// retry, commit, or hold release. Closing discards any active transaction.
[[nodiscard]] inline std::optional<BufferedReadbackRows> ExecuteReadbackQueries(
    OwnedReadbackConnection connection, unsigned long expectedThread, std::span<std::uint64_t const> items) noexcept
{
    try
    {
        if (!QueryExecutionDetail::SameConnection(connection.get(), expectedThread))
            return std::nullopt;
        auto plan = BuildReadbackQueryPlan(items);
        if (!plan)
            return std::nullopt;
        auto links = QueryExecutionDetail::Execute(connection.get(), expectedThread, plan->links, LinkProjection, plan->rowLimit);
        if (!links)
            return std::nullopt;
        auto instances = QueryExecutionDetail::Execute(connection.get(), expectedThread, plan->instances, InstanceProjection, plan->rowLimit);
        if (!instances)
            return std::nullopt;
        auto gifts = QueryExecutionDetail::Execute(connection.get(), expectedThread, plan->gifts, GiftProjection, plan->rowLimit);
        if (!gifts)
            return std::nullopt;
        return BufferedReadbackRows{std::move(*links), std::move(*instances), std::move(*gifts)};
    }
    catch (...)
    {
        return std::nullopt;
    }
}
}
