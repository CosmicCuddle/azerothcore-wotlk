/*
 * This file is part of the AzerothCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef _TRANSACTION_H
#define _TRANSACTION_H

#include "DatabaseEnvFwd.h"
#include "DatabaseTransactionResult.h"
#include "Define.h"
#include "SQLOperation.h"
#include "StringFormat.h"
#include <functional>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

/*! Transactions, high level class. */
class AC_DATABASE_API TransactionBase
{
    friend class TransactionTask;
    friend class MySQLConnection;
    friend class ModuleDatabasePool;

    template <typename T>
    friend class DatabaseWorkerPool;

public:
    TransactionBase()  = default;
    virtual ~TransactionBase() { Cleanup(); }

    void Append(std::string_view sql);

    template<typename... Args>
    void Append(std::string_view sql, Args&&... args)
    {
        Append(Acore::StringFormat(sql, std::forward<Args>(args)...));
    }

    [[nodiscard]] std::size_t GetSize() const { return m_queries.size(); }

protected:
    void AppendPreparedStatement(PreparedStatementBase* statement);
    void Cleanup();
    std::vector<SQLElementData> m_queries;

private:
    bool _cleanedUp{false};
};

template<typename T>
class Transaction : public TransactionBase
{
public:
    using TransactionBase::Append;

    void Append(PreparedStatement<T>* statement)
    {
        AppendPreparedStatement(statement);
    }
};

/*! Low level class*/
class AC_DATABASE_API TransactionTask : public SQLOperation
{
    template <class T>
    friend class DatabaseWorkerPool;

    friend class DatabaseWorker;
    friend class TransactionCallback;

public:
    TransactionTask(std::shared_ptr<TransactionBase> trans) : m_trans(std::move(trans)) { }
    ~TransactionTask() override = default;

protected:
    bool Execute() override;
    int TryExecute();
    int ExecuteWithCode();
    void CleanupOnFailure();

    std::shared_ptr<TransactionBase> m_trans;
    static std::mutex _deadlockLock;
};

class AC_DATABASE_API TransactionWithResultTask : public TransactionTask
{
public:
    TransactionWithResultTask(std::shared_ptr<TransactionBase> trans) : TransactionTask(trans) { }

    TransactionFuture GetFuture() { return m_result.get_future(); }

protected:
    bool Execute() override;

    TransactionPromise m_result;
};

// Value-only future. No callback or Player pointer is invoked on the DB worker.
// A queued task destroyed by Cancel() publishes a distinct cancellation result.
class AC_DATABASE_API TransactionWithOutcomeTask : public TransactionTask
{
public:
    explicit TransactionWithOutcomeTask(std::shared_ptr<TransactionBase> trans) : TransactionTask(std::move(trans)) {}
    ~TransactionWithOutcomeTask() override;
    std::future<DatabaseTransactionResult> GetFuture() { return _result.get_future(); }
    void Reject();

protected:
    bool Execute() override;

private:
    void Complete(int code);
    std::promise<DatabaseTransactionResult> _result;
    bool _started = false;
    bool _completed = false;
};

// Allocate the exact native task and its result before building the transaction.
// Only the pool may take the task and future out of this single-use handle.
template<typename T>
class PreparedTransactionWithOutcome final
{
public:
    explicit PreparedTransactionWithOutcome(SQLTransaction<T> transaction)
        : _transaction(std::move(transaction)),
          _task(_transaction ? std::make_unique<TransactionWithOutcomeTask>(_transaction) : nullptr),
          _result(_task ? _task->GetFuture() : std::future<DatabaseTransactionResult>{}) {}

    PreparedTransactionWithOutcome(PreparedTransactionWithOutcome&&) noexcept = default;

    [[nodiscard]] SQLTransaction<T> const& GetTransaction() const noexcept { return _transaction; }

    // The receiver must leave the future untouched on refusal. Once it takes
    // ownership, the ordinary submission API can no longer enqueue this task.
    template<typename Receiver>
    [[nodiscard]] bool TransferOutcome(Receiver&& receiver)
    {
        if (!_task || !_result.valid())
            return false;
        std::invoke(std::forward<Receiver>(receiver), std::move(_result));
        return !_result.valid();
    }

private:
    friend class DatabaseWorkerPool<T>;

    SQLTransaction<T> _transaction;
    std::unique_ptr<TransactionWithOutcomeTask> _task;
    std::future<DatabaseTransactionResult> _result;
};

class AC_DATABASE_API TransactionCallback
{
public:
    TransactionCallback(TransactionFuture&& future) : m_future(std::move(future)) { }
    TransactionCallback(TransactionCallback&&) = default;

    TransactionCallback& operator=(TransactionCallback&&) = default;

    void AfterComplete(std::function<void(bool)> callback) &
    {
        m_callback = std::move(callback);
    }

    bool InvokeIfReady();

    TransactionFuture m_future;
    std::function<void(bool)> m_callback;
};

#endif
