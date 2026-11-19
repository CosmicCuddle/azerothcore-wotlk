// PB59 source-backed task-queue execution proof, not a database/worldserver test.
// Uses unmodified AzerothCore src/common/Threading/PCQueue.h.  The Core worker
// pops one task then executes it; a later task can finish first with >=2 workers.
#include <type_traits> // dependency transitively supplied by the full Core build
#include "PCQueue.h"
#include <algorithm>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>

namespace
{
struct Task
{
    int phase;
};

struct Recorder
{
    unsigned checks = 0;
    bool passed = true;
    void Check(bool ok, char const* name)
    {
        ++checks;
        if (!ok)
        {
            passed = false;
            std::cerr << "FAIL " << checks << " " << name << '\n';
        }
    }
};

// With exactly ONE worker, even Shutdown after enqueue drains tasks in FIFO.
void SingleWorker(Recorder& t)
{
    ProducerConsumerQueue<Task*> queue;
    std::vector<int> completion;
    for (int i = 0; i < 4; ++i)
        t.Check(queue.TryPush(new Task{i}), "single worker queue accepts task");
    t.Check(queue.Size() == 4, "all four phase tasks queued");
    queue.Shutdown();
    std::thread worker([&] {
        for (;;)
        {
            Task* task = nullptr;
            queue.WaitAndPop(task);
            if (!task)
                break;
            completion.push_back(task->phase);
            delete task;
        }
    });
    worker.join();
    t.Check(completion == std::vector<int>({0, 1, 2, 3}),
        "single worker completes tasks in FIFO order");
    t.Check(queue.Empty(), "shutdown drains all four tasks");
    Task* rejected = new Task{5};
    t.Check(!queue.TryPush(rejected), "shutdown rejects typed new work");
    delete rejected;
    queue.Reset();
    t.Check(queue.TryPush(new Task{6}), "reset accepts new work");
    queue.Cancel();
    t.Check(queue.Empty(), "cancel deletes outstanding pointer task");
    rejected = new Task{7};
    t.Check(!queue.TryPush(rejected), "cancel refuses new work");
    delete rejected;
}

// Two workers both consume *exact* Core FIFO queue. Phase0's worker is held
// after its pop; phase2's independent worker is allowed to complete first.
// This models scheduling only; no statement is run against MySQL/MariaDB.
void MultiWorkerSameRealmOvertake(Recorder& t)
{
    ProducerConsumerQueue<Task*> queue;
    std::mutex mu;
    std::condition_variable cv;
    bool firstTaken = false, releaseFirst = false, laterDone = false;
    std::vector<int> dequeued, completed;

    std::thread first([&] {
        Task* task = nullptr;
        queue.WaitAndPop(task);
        {
            std::lock_guard<std::mutex> g(mu);
            if (task)
            {
                dequeued.push_back(task->phase);
                firstTaken = true;
            }
        }
        cv.notify_all();
        if (task)
        {
            std::unique_lock<std::mutex> g(mu);
            cv.wait(g, [&] { return releaseFirst; });
            completed.push_back(task->phase);
            g.unlock();
            delete task;
        }
    });
    t.Check(queue.TryPush(new Task{0}), "first delete submitted");
    {
        std::unique_lock<std::mutex> g(mu);
        cv.wait(g, [&] { return firstTaken; });
    }
    std::thread second([&] {
        Task* task = nullptr;
        queue.WaitAndPop(task);
        {
            std::lock_guard<std::mutex> g(mu);
            if (task)
            {
                dequeued.push_back(task->phase);
                completed.push_back(task->phase);
                laterDone = true;
            }
        }
        cv.notify_all();
        delete task;
    });
    t.Check(queue.TryPush(new Task{2}), "later same-realm save submitted");
    {
        std::unique_lock<std::mutex> g(mu);
        cv.wait(g, [&] { return laterDone; });
        t.Check(dequeued == std::vector<int>({0, 2}), "FIFO dequeue maintained");
        t.Check(completed == std::vector<int>({2}), "later save can finish before earlier delete");
        releaseFirst = true;
    }
    cv.notify_all();
    first.join();
    second.join();
    t.Check(completed == std::vector<int>({2, 0}), "completion order inverted across workers");
    t.Check(queue.Empty(), "all tasks consumed");
    queue.Shutdown();
}
}

int main()
{
    Recorder t;
    SingleWorker(t);
    MultiWorkerSameRealmOvertake(t);
    std::cout << "checks=" << t.checks << " status=" << (t.passed ? "PASS" : "FAIL") << '\n';
    std::cout << "source=unmodified-Core-PCQueue, single_worker=FIFO, two_workers=same_realm_overtake" << '\n';
    std::cout << "NOT native DB worker invocation, installed worker count, MySQL, trade or gameplay" << '\n';
    return t.passed ? 0 : 1;
}
