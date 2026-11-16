#include "DatabaseTransactionResult.h"
#include "PCQueue.h"
#include <cassert>
#include <chrono>
#include <future>
#include <iostream>
#include <memory>
#include <sstream>
#include <thread>
#include <vector>
using Milliseconds = std::chrono::milliseconds;
using namespace std::chrono_literals;
constexpr Milliseconds DEADLOCK_MAX_RETRY_TIME_MS = 1min;
constexpr int ER_LOCK_DEADLOCK = 1213;
#define AC_DATABASE_API
static Milliseconds GetTimeMS() { return 0ms; }
static Milliseconds GetMSTimeDiffToNow(Milliseconds) { return 2min; }
template<class... T> void Log(T&&...) {}
#define LOG_WARN(...) Log(__VA_ARGS__)
#define LOG_ERROR(...) Log(__VA_ARGS__)
struct TransactionBase { int cleaned = 0; void Cleanup() { ++cleaned; } };
template<class T> using SQLTransaction = std::shared_ptr<TransactionBase>;
struct Connection {
    std::vector<int> codes;
    std::size_t cursor = 0;
    int ExecuteTransaction(std::shared_ptr<TransactionBase> const&) { assert(cursor < codes.size()); return codes[cursor++]; }
};
struct TransactionTask {
    explicit TransactionTask(std::shared_ptr<TransactionBase> t) : m_trans(std::move(t)) {}
    virtual ~TransactionTask() = default;
    virtual bool Execute();
    int ExecuteWithCode();
    int TryExecute();
    void CleanupOnFailure();
    Connection* m_conn = nullptr;
    std::shared_ptr<TransactionBase> m_trans;
    static inline std::mutex _deadlockLock;
};
struct TransactionWithResultTask : TransactionTask {
    using TransactionTask::TransactionTask;
    std::promise<bool> m_result;
    bool Execute() override;
};
#include "task.inc"
#include "native.inc"
template<class T> struct DatabaseWorkerPool {
    std::atomic<bool> _typedTransactionsReady{true};
    std::unique_ptr<ProducerConsumerQueue<TransactionTask*>> _queue = std::make_unique<ProducerConsumerQueue<TransactionTask*>>();
    std::future<DatabaseTransactionResult> AsyncCommitTransactionWithOutcome(SQLTransaction<T> transaction);
};
#include "pool.inc"
int main() {
    using S = DatabaseTransactionStatus;
    auto fresh = [] { return std::make_shared<TransactionBase>(); };
    auto run = [&](std::initializer_list<int> codes, S expected, int expectedCode, int cleaned) {
        DatabaseWorkerPool<Connection> pool;
        auto t = fresh();
        auto f = pool.AsyncCommitTransactionWithOutcome(t);
        assert(f.wait_for(0ms) == std::future_status::timeout);
        TransactionTask* task = nullptr;
        assert(pool._queue->Pop(task));
        Connection conn{codes}; task->m_conn = &conn;
        assert(task->Execute() == (expected == S::Committed)); delete task;
        auto r = f.get(); assert(r.status == expected && r.executionCode == expectedCode);
        assert(conn.cursor == codes.size() && t->cleaned == cleaned);
    };
    run({0}, S::Committed, 0, 0);
    run({-1}, S::Empty, -1, 1);
    run({-3}, S::NotStarted, -3, 1);
    run({-2}, S::Ambiguous, -2, 1);
    run({1213,0}, S::Committed, 0, 0);
    run({1213,-2}, S::Ambiguous, -2, 1);
    run({1213,-3}, S::FailedUnverified, -4, 1);
    run({1213,1213}, S::FailedUnverified, 1213, 1);
    DatabaseWorkerPool<Connection> pool;
    pool._typedTransactionsReady = false;
    assert(pool.AsyncCommitTransactionWithOutcome(fresh()).get().status == S::NotEnqueued);
    pool._typedTransactionsReady = true;
    auto cancelled = pool.AsyncCommitTransactionWithOutcome(fresh());
    pool._queue->Cancel();
    assert(cancelled.get().status == S::CancelledBeforeExecution);
    assert(pool.AsyncCommitTransactionWithOutcome(fresh()).get().status == S::NotEnqueued);
    pool._queue->Reset();
    auto pending = pool.AsyncCommitTransactionWithOutcome(fresh());
    pool._queue->Shutdown();
    assert(pool.AsyncCommitTransactionWithOutcome(fresh()).get().status == S::NotEnqueued);
    TransactionTask* task = nullptr;
    pool._queue->WaitAndPop(task); assert(task);
    Connection conn{{0}}; task->m_conn = &conn; assert(task->Execute()); delete task;
    assert(pending.get().status == S::Committed);
    task = nullptr; pool._queue->WaitAndPop(task); assert(!task);
    // Repeated concurrent shutdown vs admission: accepted work must drain;
    // rejected work must be ready without a consumer.
    for (int i=0; i<200; ++i) {
        DatabaseWorkerPool<Connection> racing;
        std::future<DatabaseTransactionResult> result;
        std::thread producer([&] { result = racing.AsyncCommitTransactionWithOutcome(fresh()); });
        racing._queue->Shutdown(); producer.join();
        TransactionTask* next = nullptr; racing._queue->WaitAndPop(next);
        bool const accepted = next != nullptr;
        if (next) { Connection c{{0}}; next->m_conn = &c; assert(next->Execute()); delete next; }
        auto r = result.get(); assert(r.status == (accepted ? S::Committed : S::NotEnqueued));
    }
    std::cout << "PASS: 8 execution scenarios, 5 admission/cancel/drain checks, 200 shutdown races\n";
}
