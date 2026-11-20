// PB59 test-only: actual Core queue and outcome type, modeled typed task.
// Mirrors TransactionWithOutcomeTask's result publication/destructor rules;
// does NOT build or call the Core database worker or execute SQL.
#include <type_traits>  // Required by this pinned Core PCQueue.h
#include "PCQueue.h"
#include "DatabaseTransactionResult.h"
#include <chrono>
#include <condition_variable>
#include <future>
#include <iostream>
#include <mutex>
#include <thread>

namespace
{
using namespace std::chrono_literals;
struct Checks
{
    unsigned n = 0;
    bool ok = true;
    void Require(bool pass, char const* desc)
    {
        ++n;
        if (!pass)
        {
            std::cerr << "FAIL " << n << ": " << desc << '\n';
            ok = false;
        }
    }
};

// Only the lightweight task lifecycle below is modeled; it uses the EXACT
// enum/code mapping and the EXACT queue type from the pinned Core sources.
class ModelOutcomeTask
{
public:
    ModelOutcomeTask() = default;
    ~ModelOutcomeTask()
    {
        if (!_completed)
            Publish(_started ? DatabaseTransactionResult::AmbiguousCode
                             : DatabaseTransactionResult::CancelledBeforeExecutionCode);
    }
    std::future<DatabaseTransactionResult> GetFuture() { return _promise.get_future(); }
    void Reject() { Publish(DatabaseTransactionResult::NotEnqueuedCode); }
    void Start() { _started = true; }
    void Finish(int code) { _started = true; Publish(code); }
private:
    void Publish(int code)
    {
        _promise.set_value(DatabaseTransactionResult::FromExecutionCode(code));
        _completed = true;
    }
    std::promise<DatabaseTransactionResult> _promise;
    bool _started = false;
    bool _completed = false;
};

bool Ready(std::future<DatabaseTransactionResult>& f)
{
    return f.valid() && f.wait_for(0s) == std::future_status::ready;
}

void CancelQueued(Checks& t)
{
    ProducerConsumerQueue<ModelOutcomeTask*> queue;
    auto* a = new ModelOutcomeTask();
    auto* b = new ModelOutcomeTask();
    auto fa = a->GetFuture(), fb = b->GetFuture();
    t.Require(queue.TryPush(a), "first task admitted");
    t.Require(queue.TryPush(b), "second task admitted");
    t.Require(!Ready(fa) && !Ready(fb), "neither queued task completed");
    queue.Cancel();  // Real queue deletes both pointers while locked.
    t.Require(queue.Empty(), "Cancel removes queued tasks");
    t.Require(Ready(fa) && Ready(fb), "destructors publish ready results");
    t.Require(fa.get().status == DatabaseTransactionStatus::CancelledBeforeExecution,
        "first queued future cancelled before execution");
    t.Require(fb.get().status == DatabaseTransactionStatus::CancelledBeforeExecution,
        "second queued future cancelled before execution");
    auto* rejected = new ModelOutcomeTask();
    auto fr = rejected->GetFuture();
    t.Require(!queue.TryPush(rejected), "typed task rejected after cancel");
    rejected->Reject();
    delete rejected;
    t.Require(Ready(fr) && fr.get().status == DatabaseTransactionStatus::NotEnqueued,
        "explicit rejection publishes NotEnqueued, not cancellation");
}

void ShutdownDrains(Checks& t)
{
    ProducerConsumerQueue<ModelOutcomeTask*> queue;
    auto* work = new ModelOutcomeTask();
    auto f = work->GetFuture();
    t.Require(queue.TryPush(work), "task admitted before graceful shutdown");
    queue.Shutdown();
    auto* rejected = new ModelOutcomeTask();
    auto rejectFuture = rejected->GetFuture();
    t.Require(!queue.TryPush(rejected), "new typed work refused after Shutdown");
    rejected->Reject();
    delete rejected;
    std::thread worker([&] {
        ModelOutcomeTask* task = nullptr;
        queue.WaitAndPop(task);
        if (task)
        {
            task->Finish(0);
            delete task;
        }
    });
    worker.join();
    t.Require(Ready(f) && f.get().status == DatabaseTransactionStatus::Committed,
        "already queued task drained under graceful shutdown");
    t.Require(Ready(rejectFuture) && rejectFuture.get().status == DatabaseTransactionStatus::NotEnqueued,
        "post-shutdown rejected task reports NotEnqueued");
    t.Require(queue.Empty(), "graceful queue is empty");
}

void CancelWhileClaimed(Checks& t)
{
    ProducerConsumerQueue<ModelOutcomeTask*> queue;
    std::mutex mu;
    std::condition_variable cv;
    bool claimed = false, release = false;
    auto* active = new ModelOutcomeTask();
    auto* queued = new ModelOutcomeTask();
    auto fa = active->GetFuture(), fq = queued->GetFuture();
    t.Require(queue.TryPush(active), "claimed task queued");
    std::thread worker([&] {
        ModelOutcomeTask* task = nullptr;
        queue.WaitAndPop(task);
        {
            std::lock_guard<std::mutex> lock(mu);
            claimed = task != nullptr;
        }
        cv.notify_all();
        if (task)
        {
            task->Start();
            std::unique_lock<std::mutex> lock(mu);
            cv.wait(lock, [&] { return release; });
            lock.unlock();
            task->Finish(0);
            delete task;
        }
    });
    {
        std::unique_lock<std::mutex> lock(mu);
        cv.wait(lock, [&] { return claimed; });
    }
    t.Require(queue.TryPush(queued), "second task queued while first in-flight");
    queue.Cancel();
    t.Require(Ready(fq) && fq.get().status == DatabaseTransactionStatus::CancelledBeforeExecution,
        "cancel only clears second still-queued task");
    t.Require(!Ready(fa), "in-flight task not magically cancelled or committed");
    {
        std::lock_guard<std::mutex> lock(mu);
        release = true;
    }
    cv.notify_all();
    worker.join();
    t.Require(Ready(fa) && fa.get().status == DatabaseTransactionStatus::Committed,
        "in-flight task may complete after queue cancellation");
    t.Require(queue.Empty(), "cancelled queue ends empty");
}

void OutcomeSemantics(Checks& t)
{
    auto abandoned = new ModelOutcomeTask();
    auto f = abandoned->GetFuture();
    abandoned->Start();
    delete abandoned;
    t.Require(Ready(f) && f.get().status == DatabaseTransactionStatus::Ambiguous,
        "started task destroyed without completion becomes ambiguous");
    for (auto code : {DatabaseTransactionResult::NotStartedCode,
                      DatabaseTransactionResult::PriorAttemptFailureCode,
                      DatabaseTransactionResult::AmbiguousCode,
                      DatabaseTransactionResult::CancelledBeforeExecutionCode,
                      DatabaseTransactionResult::NotEnqueuedCode})
    {
        auto result = DatabaseTransactionResult::FromExecutionCode(code);
        t.Require(!result.IsCommitted(), "every failed/uncertain code denies committed state");
    }
    t.Require(DatabaseTransactionResult::FromExecutionCode(0).IsCommitted(),
        "only acknowledged 0 status is Committed (not durability)");
}
}

int main()
{
    Checks t;
    CancelQueued(t);
    ShutdownDrains(t);
    CancelWhileClaimed(t);
    OutcomeSemantics(t);
    std::cout << "checks=" << t.n << " status=" << (t.ok ? "PASS" : "FAIL") << '\n';
    std::cout << "SCOPE real PCQueue.h + DatabaseTransactionResult.h; MODELED task lifetime, no DB" << '\n';
    return t.ok ? 0 : 1;
}
