#pragma once

#include "DatabaseWorkerPool.h"
#include "PB59InventoryWriteFence.h"
#include "Transaction.h"
#include <chrono>

namespace PB59
{
// One ordinary writer and its exact typed task. A future native producer must
// keep this handle in an owner-independent pending store until Poll settles it.
// This component does not register a native caller or authorize recovery saves.
template<std::size_t Capacity, typename Connection>
class PreparedInventoryWrite final
{
    using Fence = InventoryWriteFence<Capacity>;
    using Writer = typename Fence::Writer;
    using Pool = DatabaseWorkerPool<Connection>;

public:
    PreparedInventoryWrite(PreparedInventoryWrite&&) noexcept = default;

    [[nodiscard]] static std::optional<PreparedInventoryWrite> Prepare(Fence& fence, Pool& database, std::uint64_t guid)
    {
        auto writer = fence.TryTrackWrite(guid);
        if (!writer)
            return std::nullopt;

        return PreparedInventoryWrite(database, std::move(*writer), database.BeginTransaction());
    }

    // Only the serialized producer may append, and only before Dispatch.
    [[nodiscard]] SQLTransaction<Connection> const& GetTransaction() const noexcept
    {
        return _prepared.GetTransaction();
    }

    // True means we obtained a future. Queue rejection is a typed result, too.
    [[nodiscard]] bool Dispatch()
    {
        if (!_prepared.GetTransaction() || !_writer.MarkPossiblySubmitted())
            return false;

        _outcome = _database.AsyncCommitPreparedTransactionWithOutcome(std::move(_prepared));
        return _outcome.valid();
    }

    [[nodiscard]] std::optional<DatabaseTransactionResult> Poll()
    {
        using namespace std::chrono_literals;
        if (!_outcome.valid())
            return std::nullopt;

        auto result = DatabaseTransactionResult::FromExecutionCode(DatabaseTransactionResult::AmbiguousCode);
        try
        {
            if (_outcome.wait_for(0s) != std::future_status::ready)
                return std::nullopt;
            result = _outcome.get();
        }
        catch (...)
        {
            _outcome = {};
        }
        (void)_writer.Complete(result);
        return result;
    }

private:
    PreparedInventoryWrite(Pool& database, Writer writer, SQLTransaction<Connection> transaction)
        : _database(database), _writer(std::move(writer)), _prepared(std::move(transaction)) {}

    Pool& _database;
    Writer _writer;
    PreparedTransactionWithOutcome<Connection> _prepared;
    std::future<DatabaseTransactionResult> _outcome;
};
}
