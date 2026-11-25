#include "Transaction.h"
#include "PB59PreparedInventoryWrite.h"
#include "PCQueue.h"
#include <iostream>
#include <stdexcept>
#include <thread>
#include <type_traits>

struct TestConnection
{
    enum class Statements { Unused };
};

struct MySQLConnectionInfo {};

using Pool = DatabaseWorkerPool<TestConnection>;
using Queue = ProducerConsumerQueue<SQLOperation*>;
using Fence = PB59::InventoryWriteFence<2>;
using Write = PB59::PreparedInventoryWrite<2, TestConnection>;
using Result = DatabaseTransactionResult;
using Status = DatabaseTransactionStatus;

static_assert(std::is_move_constructible_v<Write> && !std::is_copy_constructible_v<Write>);
static_assert(!std::is_default_constructible_v<Write>);

// Pool construction and execution substitute a local queue and result. The
// submission methods and typed task completion/destruction below are native.
static Queue* TestQueue;

template <class T>
DatabaseWorkerPool<T>::DatabaseWorkerPool()
    : _queue(std::make_unique<Queue>()), _async_threads(1), _synch_threads(0), _typedTransactionsReady(true)
{
    TestQueue = _queue.get();
}

template <class T>
DatabaseWorkerPool<T>::~DatabaseWorkerPool()
{
    _queue->Cancel();
}

class MySQLConnection
{
public:
    TransactionBase const* seen = nullptr;
    std::size_t size = 0;
    int code = 0;
    bool failDuringExecution = false;
};

bool TransactionTask::Execute()
{
    return ExecuteWithCode() == 0;
}

int TransactionTask::ExecuteWithCode()
{
    m_conn->seen = m_trans.get();
    m_conn->size = m_trans->GetSize();
    if (m_conn->failDuringExecution)
        throw std::runtime_error("injected interruption after start");
    return m_conn->code;
}

void TransactionBase::Cleanup() {}

#include "PreparedTaskDefinitions.inc"

static std::unique_ptr<SQLOperation> TakeTask()
{
    SQLOperation* operation = nullptr;
    TestQueue->Pop(operation);
    return std::unique_ptr<SQLOperation>(operation);
}

static unsigned Checks = 0;
#define WRITE_CHECK(value) do { \
    ++Checks; \
    if (!(value)) \
    { \
        std::cerr << "prepared inventory write line " << __LINE__ << '\n'; \
        return 1; \
    } \
} while (false)

int main()
{
    constexpr std::uint64_t guid = 123;
    constexpr std::uint64_t other = 456;
    {
        Pool pool;
        Fence fence;
        auto reservation = fence.TryReserveCapture(guid);
        WRITE_CHECK(reservation && !Write::Prepare(fence, pool, guid) && TestQueue->Empty());
        reservation.reset();
        {
            auto write = Write::Prepare(fence, pool, guid);
            WRITE_CHECK(write && write->GetTransaction() && !write->Poll());
            WRITE_CHECK(!fence.TryReserveCapture(guid) && TestQueue->Empty());
        }
        WRITE_CHECK(fence.TryReserveCapture(guid));
    }
    {
        Pool pool;
        Fence fence;
        auto prepared = Write::Prepare(fence, pool, guid);
        WRITE_CHECK(prepared);
        auto write = std::move(*prepared);
        WRITE_CHECK(!prepared->Dispatch() && !prepared->Poll() && TestQueue->Empty());
        prepared.reset();
        auto const exact = write.GetTransaction();
        exact->Append(std::string_view("fixture operation"));
        WRITE_CHECK(write.Dispatch() && !write.GetTransaction() && !write.Dispatch());
        WRITE_CHECK(TestQueue->Size() == 1 && !write.Poll() && !fence.TryReserveCapture(guid));
        auto task = TakeTask();
        WRITE_CHECK(task);
        MySQLConnection executor;
        std::thread worker([job = std::move(task), &executor]
        {
            job->SetConnection(&executor);
            job->call();
        });
        worker.join();
        WRITE_CHECK(executor.seen == exact.get() && executor.size == 1);
        WRITE_CHECK(!fence.TryReserveCapture(guid));
        auto const result = write.Poll();
        WRITE_CHECK(result && result->IsCommitted());
        WRITE_CHECK(fence.TryReserveCapture(guid) && !write.Poll());
    }
    {
        Pool pool;
        Fence fence;
        auto older = Write::Prepare(fence, pool, guid);
        auto newer = Write::Prepare(fence, pool, guid);
        WRITE_CHECK(older && newer && older->Dispatch() && newer->Dispatch());
        auto first = TakeTask();
        auto second = TakeTask();
        WRITE_CHECK(first && second);
        MySQLConnection executor;
        executor.code = Result::EmptyCode;
        second->SetConnection(&executor);
        second->call();
        auto const result = newer->Poll();
        WRITE_CHECK(result && result->status == Status::Empty && !older->Poll());
        WRITE_CHECK(!fence.TryReserveCapture(guid) && fence.TryReserveCapture(other));
        first.reset();
        auto const cancelled = older->Poll();
        WRITE_CHECK(cancelled && cancelled->status == Status::CancelledBeforeExecution);
        WRITE_CHECK(fence.TryReserveCapture(guid));
    }
    for (bool reject : {false, true})
    {
        Pool pool;
        Fence fence;
        if (reject)
            TestQueue->Shutdown();
        auto write = Write::Prepare(fence, pool, guid);
        WRITE_CHECK(write && write->Dispatch());
        if (!reject)
            TestQueue->Cancel();
        WRITE_CHECK(!fence.TryReserveCapture(guid) && TestQueue->Empty());
        auto const result = write->Poll();
        WRITE_CHECK(result && result->status == (reject ? Status::NotEnqueued : Status::CancelledBeforeExecution));
        WRITE_CHECK(fence.TryReserveCapture(guid));
    }
    {
        Pool pool;
        Fence fence;
        auto write = Write::Prepare(fence, pool, guid);
        WRITE_CHECK(write && write->Dispatch());
        auto task = TakeTask();
        WRITE_CHECK(task);
        MySQLConnection executor;
        executor.failDuringExecution = true;
        task->SetConnection(&executor);
        bool faultObserved = false;
        try
        {
            task->call();
        }
        catch (std::runtime_error const&)
        {
            faultObserved = true;
        }
        WRITE_CHECK(faultObserved && !write->Poll());
        task.reset();
        auto const result = write->Poll();
        WRITE_CHECK(result && result->status == Status::Ambiguous);
        WRITE_CHECK(!fence.TryReserveCapture(guid));
    }
    {
        Pool pool;
        Fence fence;
        auto write = Write::Prepare(fence, pool, guid);
        WRITE_CHECK(write && write->Dispatch());
        auto pending = std::move(*write);
        write.reset();
        WRITE_CHECK(!fence.TryReserveCapture(guid));
        TestQueue->Cancel();
        WRITE_CHECK(pending.Poll() && fence.TryReserveCapture(guid));
    }
    {
        Pool pool;
        Fence fence;
        auto write = Write::Prepare(fence, pool, guid);
        WRITE_CHECK(write && write->Dispatch());
        write.reset();
        TestQueue->Cancel();
        WRITE_CHECK(!fence.TryReserveCapture(guid));
    }
    {
        Pool pool;
        PreparedTransactionWithOutcome<TestConnection> prepared(pool.BeginTransaction());
        auto result = pool.AsyncCommitPreparedTransactionWithOutcome(std::move(prepared));
        auto replay = pool.AsyncCommitPreparedTransactionWithOutcome(std::move(prepared));
        WRITE_CHECK(result.valid() && !replay.valid() && TestQueue->Size() == 1);
        auto legacy = pool.AsyncCommitTransactionWithOutcome(pool.BeginTransaction());
        WRITE_CHECK(legacy.valid() && TestQueue->Size() == 2);
        TestQueue->Cancel();
        WRITE_CHECK(result.get().status == Status::CancelledBeforeExecution);
        WRITE_CHECK(legacy.get().status == Status::CancelledBeforeExecution);
    }
    std::cout << Checks << " PB59 native prepared-task handoff checks PASS\n";
}
