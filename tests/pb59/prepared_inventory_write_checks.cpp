#include "Transaction.h"
#include "PB59CharacterSaveObserver.h"
#include "PB59PendingInventoryWrites.h"
#include "PB59PreparedInventoryWrite.h"
#include "PCQueue.h"
#include <iostream>
#include <latch>
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
using Store = PB59::PendingInventoryWrites<2, 2, TestConnection>;
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

static int CheckProtectedCompletion()
{
    using Observer = PB59::CharacterSaveObserver<2>;
    unsigned const before = Checks;
    Pool pool;
    Fence fence;
    Store store(fence, pool);
    Observer observer;
    Observer other;
    auto first = observer.ReserveProtected(123, 7, 8);
    auto second = observer.ReserveProtected(123, 7, 8);
    WRITE_CHECK(first.receipt && second.receipt);
    PreparedTransactionWithOutcome<TestConnection> task(pool.BeginTransaction());
    PB59::SaveEvidence evidence;
    evidence.AddItem(456);
    auto invalid = evidence;
    invalid.itemCount = PB59::SaveEvidence::Limit + 1;
    WRITE_CHECK(!task.TransferOutcome([](auto&&) {}));
    WRITE_CHECK(!other.BindPreparedOutcome(*first.receipt, evidence, task));
    WRITE_CHECK(!observer.BindPreparedOutcome(*first.receipt, invalid, task));
    WRITE_CHECK(observer.BindPreparedOutcome(*first.receipt, evidence, task));
    auto capture = fence.TryReserveCapture(123);
    WRITE_CHECK(capture);
    auto hold = fence.TryPromoteCapture(*capture, 123);
    WRITE_CHECK(hold && hold->MarkPossiblySubmitted());
    auto pending = std::move(*hold);
    capture.reset();
    hold.reset();
    store.CloseAdmission();
    WRITE_CHECK(!fence.TryTrackWrite(123));
    WRITE_CHECK(!observer.BindPreparedOutcome(*second.receipt, evidence, task));
    WRITE_CHECK(!observer.AbandonProtected(*first.receipt) && !observer.ConsumeProtected(*first.receipt));
    WRITE_CHECK(observer.AbandonProtected(*second.receipt) && observer.ConsumeProtected(*second.receipt));
    auto result = pool.AsyncCommitPreparedTransactionWithOutcome(std::move(task));
    WRITE_CHECK(!result.valid() && TestQueue->Empty());
    auto const cancelled = observer.ConsumeProtected(*first.receipt);
    WRITE_CHECK(cancelled && cancelled->result.status == Status::CancelledBeforeExecution);
    WRITE_CHECK(cancelled->evidence.itemCount == 1 && cancelled->evidence.items[0] == 456);
    WRITE_CHECK(!fence.TryTrackWrite(123) && pending.Settle(cancelled->result) == Fence::RecoverySettlement::NoWrite);
    WRITE_CHECK(fence.TryTrackWrite(123));
    WRITE_CHECK(!observer.ConsumeProtected(*first.receipt));
    auto next = observer.ReserveProtected(123, 7, 8);
    PreparedTransactionWithOutcome<TestConnection> fresh(pool.BeginTransaction());
    WRITE_CHECK(next.receipt && !observer.BindPreparedOutcome(*first.receipt, evidence, fresh));
    WRITE_CHECK(observer.BindPreparedOutcome(*next.receipt, evidence, fresh));
    std::cout << Checks - before << " PB59 protected completion handoff checks PASS\n";
    return 0;
}

static int CheckPendingStore()
{
    constexpr std::uint64_t guid = 123;
    constexpr std::uint64_t other = 456;
    auto accept = [](auto const&) { return true; };
    unsigned const before = Checks;
    {
        using SmallStore = PB59::PendingInventoryWrites<1, 2, TestConnection>;
        using Admission = SmallStore::Admission;
        Pool pool;
        Fence fence;
        SmallStore store(fence, pool);
        WRITE_CHECK(store.Idle());
        SmallStore::Submission sent{Admission::Declined};
        bool builderRan = false;
        {
            std::string const producerPayload = "owned operation";
            sent = store.TrySubmit(guid, [&](auto& transaction)
            {
                transaction.Append(std::string_view(producerPayload));
                auto nested = store.TrySubmit(other, [&](auto const&) { builderRan = true; return true; });
                return nested.status == Admission::Full && !store.Idle() && !store.PollOne();
            });
        } // producer state has gone away
        WRITE_CHECK(sent.status == Admission::Tracking && sent.ticket.guid == guid && sent.ticket.sequence);
        WRITE_CHECK(!builderRan && !store.Idle() && !fence.TryReserveCapture(guid));
        WRITE_CHECK(store.TrySubmit(other, accept).status == Admission::Full && TestQueue->Size() == 1);
        auto task = TakeTask();
        WRITE_CHECK(task);
        MySQLConnection executor;
        task->SetConnection(&executor);
        task->call();
        WRITE_CHECK(executor.size == 1 && !fence.TryReserveCapture(guid));
        auto const done = store.PollOne();
        WRITE_CHECK(done && done->ticket == sent.ticket && done->result.IsCommitted());
        WRITE_CHECK(store.Idle() && !store.PollOne() && fence.TryReserveCapture(guid));
        auto const again = store.TrySubmit(guid, accept);
        WRITE_CHECK(again.status == Admission::Tracking && again.ticket.sequence > sent.ticket.sequence);
        TestQueue->Cancel();
        auto const cancelled = store.PollOne();
        WRITE_CHECK(cancelled && cancelled->ticket == again.ticket &&
            cancelled->result.status == Status::CancelledBeforeExecution);
    }
    {
        Pool pool;
        Fence fence;
        Store store(fence, pool);
        bool builderRan = false;
        {
            auto capture = fence.TryReserveCapture(guid);
            WRITE_CHECK(capture && store.TrySubmit(guid, [&](auto const&)
            {
                builderRan = true;
                return true;
            }).status == Store::Admission::WriterUnavailable);
        }
        WRITE_CHECK(!builderRan && store.Idle() && TestQueue->Empty());
        auto const declined = store.TrySubmit(guid, [](auto const&) { return false; });
        WRITE_CHECK(declined.status == Store::Admission::Declined && !declined.ticket.sequence);
        WRITE_CHECK(store.Idle() && fence.TryReserveCapture(guid));
        bool threw = false;
        try
        {
            (void)store.TrySubmit(guid, [](auto const&) -> bool { throw std::runtime_error("builder stopped"); });
        }
        catch (std::runtime_error const&)
        {
            threw = true;
        }
        WRITE_CHECK(threw && store.Idle() && TestQueue->Empty() && fence.TryReserveCapture(guid));
    }
    {
        Pool pool;
        Fence fence;
        Store store(fence, pool);
        auto const older = store.TrySubmit(guid, accept);
        auto const newer = store.TrySubmit(guid, accept);
        WRITE_CHECK(older.status == Store::Admission::Tracking && newer.status == Store::Admission::Tracking);
        auto first = TakeTask();
        auto second = TakeTask();
        WRITE_CHECK(first && second);
        MySQLConnection executor;
        second->SetConnection(&executor);
        second->call();
        auto const done = store.PollOne();
        WRITE_CHECK(done && done->ticket == newer.ticket && done->result.IsCommitted());
        WRITE_CHECK(!store.PollOne() && !store.Idle() && !fence.TryReserveCapture(guid));
        first.reset();
        auto const cancelled = store.PollOne();
        WRITE_CHECK(cancelled && cancelled->ticket == older.ticket &&
            cancelled->result.status == Status::CancelledBeforeExecution);
        WRITE_CHECK(store.Idle() && fence.TryReserveCapture(guid));
    }
    {
        Pool pool;
        Fence fence;
        Store store(fence, pool);
        auto const first = store.TrySubmit(guid, accept);
        auto const second = store.TrySubmit(other, accept);
        WRITE_CHECK(first.status == Store::Admission::Tracking && second.status == Store::Admission::Tracking);
        TestQueue->Cancel();
        auto const done = store.PollOne();
        WRITE_CHECK(done && done->ticket == first.ticket);
        auto const later = store.TrySubmit(guid, accept); // slot zero reused and immediately rejected by the queue
        WRITE_CHECK(later.status == Store::Admission::Tracking);
        auto const after = store.PollOne();
        WRITE_CHECK(after && after->ticket == second.ticket);
        auto const last = store.PollOne();
        WRITE_CHECK(last && last->ticket == later.ticket && last->result.status == Status::NotEnqueued);
        WRITE_CHECK(store.Idle());
    }
    {
        Pool pool;
        Fence fence;
        Store store(fence, pool);
        std::latch started(1);
        std::latch finish(1);
        Store::Submission submission{Store::Admission::Declined};
        std::thread producer([&]
        {
            submission = store.TrySubmit(guid, [&](auto const&)
            {
                started.count_down();
                finish.wait();
                return true;
            });
        });
        started.wait();
        auto const busyDuringBuild = !store.Idle() && !store.PollOne() && !fence.TryReserveCapture(guid);
        store.CloseAdmission();
        auto const refused = store.TrySubmit(other, accept);
        finish.count_down();
        producer.join();
        WRITE_CHECK(busyDuringBuild && refused.status == Store::Admission::Closed);
        WRITE_CHECK(submission.status == Store::Admission::Closed && !submission.ticket.sequence);
        WRITE_CHECK(store.Idle() && TestQueue->Empty() && fence.TryReserveCapture(guid));
    }
    for (bool reject : {false, true})
    {
        Pool pool;
        Fence fence;
        Store store(fence, pool);
        if (reject)
            TestQueue->Shutdown();
        auto const sent = store.TrySubmit(guid, accept);
        WRITE_CHECK(sent.status == Store::Admission::Tracking && !store.Idle());
        store.CloseAdmission();
        WRITE_CHECK(store.TrySubmit(other, accept).status == Store::Admission::Closed);
        WRITE_CHECK(!fence.TryReserveCapture(guid));
        if (!reject)
            TestQueue->Cancel();
        auto const done = store.PollOne();
        WRITE_CHECK(done && done->ticket == sent.ticket &&
            done->result.status == (reject ? Status::NotEnqueued : Status::CancelledBeforeExecution));
        WRITE_CHECK(store.Idle() && !store.PollOne() && fence.TryReserveCapture(guid));
    }
    {
        Pool pool;
        Fence fence;
        Store store(fence, pool);
        auto const sent = store.TrySubmit(guid, accept);
        WRITE_CHECK(sent.status == Store::Admission::Tracking);
        auto task = TakeTask();
        WRITE_CHECK(task);
        MySQLConnection executor;
        executor.code = Result::AmbiguousCode;
        task->SetConnection(&executor);
        task->call();
        auto const outcome = store.PollOne();
        WRITE_CHECK(outcome && outcome->result.status == Status::Ambiguous);
        WRITE_CHECK(store.Idle() && !store.PollOne() && !fence.TryReserveCapture(guid) && TestQueue->Empty());
    }
    {
        Pool pool;
        Fence fence;
        {
            Store store(fence, pool);
            WRITE_CHECK(store.TrySubmit(guid, accept).status == Store::Admission::Tracking);
        }
        TestQueue->Cancel();
        WRITE_CHECK(!fence.TryReserveCapture(guid) && fence.TryReserveCapture(other));
    }
    std::cout << Checks - before << " PB59 pending inventory write store checks PASS\n";
    return 0;
}

#ifdef PB59_TEST_LOST_DISPATCH_RESULT
static int CheckLostDispatchResult()
{
    constexpr std::uint64_t guid = 123;
    Pool pool;
    Fence fence;
    Store store(fence, pool);
    auto const sent = store.TrySubmit(guid, [](auto const&) { return true; });
    WRITE_CHECK(sent.status == Store::Admission::Uncertain && sent.ticket.sequence);
    WRITE_CHECK(store.Idle() && !store.PollOne() && !fence.TryReserveCapture(guid));
    WRITE_CHECK(TestQueue->Size() == 1);
    auto task = TakeTask();
    WRITE_CHECK(task);
    MySQLConnection executor;
    task->SetConnection(&executor);
    task->call();
    WRITE_CHECK(TestQueue->Empty() && !store.PollOne() && !fence.TryReserveCapture(guid));
    std::cout << Checks << " PB59 lost dispatch result checks PASS\n";
    return 0;
}
#endif

int main()
{
#ifdef PB59_TEST_LOST_DISPATCH_RESULT
    return CheckLostDispatchResult();
#endif
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
    if (auto const status = CheckPendingStore())
        return status;
    return CheckProtectedCompletion();
}
