#include "Transaction.h"
#include "DatabaseWorker.h"
#include "PB59CharacterSaveObserver.h"
#include "PB59PendingInventoryWrites.h"
#include "PB59PreparedInventoryWrite.h"
#include "PCQueue.h"
#include <iostream>
#include <latch>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <vector>

class CharacterDatabaseConnection
{
public:
    enum class Statements { Unused };
};

struct MySQLConnectionInfo {};

using TestConnection = CharacterDatabaseConnection;
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

struct Player;
struct Session
{
    Player* player;
    std::uint64_t incarnation = 8;
    bool loading = false;
    bool logout = false;

    Player* GetPlayer() const { return player; }
    std::uint64_t GetPB59SessionIncarnation() const { return incarnation; }
    bool PlayerLoading() const { return loading; }
    bool PlayerLogout() const { return logout; }
};

struct Player
{
    using PB59TrackedInventoryWrites = Store;
    struct Guid
    {
        std::uint64_t value;
        std::uint64_t GetRawValue() const { return value; }
    };

    struct Equipment
    {
        bool allowed = true;
        bool InventoryMutationAllowed() const { return allowed; }
    } _pb59Equipment;

    Session session{this};
    Guid guid{123};
    std::uint64_t incarnation = 7;
    bool inWorld = true;
    bool teleport = false;
    bool originalHeld = false;
    bool hasSession = true;
    unsigned builds = 0;
    std::uint64_t item = 456;
    bool empty = false;
    std::function<void()> duringBuild;

    Session* GetSession() { return hasSession ? &session : nullptr; }
    Guid GetGUID() const { return guid; }
    std::uint64_t GetPB59OwnerIncarnation() const { return incarnation; }
    bool IsInWorld() const { return inWorld; }
    bool IsBeingTeleportedFar() const { return teleport; }
    bool HasPB59UnsettledOriginals() const { return originalHeld; }

    void SaveToDBWithEvidence(CharacterDatabaseTransaction transaction, bool create, bool logout,
        PB59::SaveEvidence* evidence)
    {
        if (create || logout)
            throw std::logic_error("unexpected save kind");
        ++builds;
        if (!empty)
            transaction->Append(std::string_view("native standalone save fixture"));
        if (evidence)
        {
            evidence->AddQuest(66001);
            evidence->AddItem(item);
            evidence->AddSkill(164);
        }
        if (duringBuild)
            duringBuild();
    }

    static void DrainPB59CharacterSaveEvidence(bool final = false);
#include "TrackedStandaloneSaveDeclarations.inc"
};

// Compile a caller while only the Player.h declaration is visible.
static_assert(std::is_same_v<decltype(std::declval<Player&>().TryPB59TrackedStandaloneSave(
    std::declval<Store&>(), false, false)), PB59::InventoryWriteSubmission>);

struct SaveConfig
{
    bool enabled = true;

    template<typename Value>
    Value GetOption(std::string_view key, Value fallback) const
    {
        return key == "PB59.CharacterSaveEvidence.Enable" ? enabled : fallback;
    }
};

struct SaveLog
{
    std::string_view format;
    std::vector<std::int64_t> values;
};

static std::vector<SaveLog> SaveLogs;
static SaveConfig Config;
static SaveConfig const* sConfigMgr = &Config;

template<typename... Values>
static void LOG_ERROR(std::string_view, std::string_view format, Values... values)
{
    SaveLogs.push_back({format, {static_cast<std::int64_t>(values)...}});
}

#include "TrackedStandaloneSaveDefinitions.inc"

static int CheckStandaloneProducer()
{
    constexpr std::uint64_t guid = 123;
    unsigned const before = Checks;
    {
        Pool pool;
        Fence fence;
        Store store(fence, pool);
        Player player;
        auto submit = [&] { return player.TryPB59TrackedStandaloneSave(store, false, false); };
        WRITE_CHECK(player.TryPB59TrackedStandaloneSave(store, true, false).status == Store::Admission::Declined);
        WRITE_CHECK(player.TryPB59TrackedStandaloneSave(store, false, true).status == Store::Admission::Declined);
        for (bool* guard : {&player.session.loading, &player.session.logout, &player.teleport, &player.originalHeld})
        {
            *guard = true;
            WRITE_CHECK(submit().status == Store::Admission::Declined);
            *guard = false;
        }
        for (bool* guard : {&player.inWorld, &player.hasSession, &player._pb59Equipment.allowed})
        {
            *guard = false;
            WRITE_CHECK(submit().status == Store::Admission::Declined);
            *guard = true;
        }
        player.session.player = nullptr;
        WRITE_CHECK(submit().status == Store::Admission::Declined);
        player.session.player = &player;
        player.session.incarnation = 0;
        WRITE_CHECK(submit().status == Store::Admission::Declined);
        player.session.incarnation = 8;
        {
            auto const hold = fence.TryReserveCapture(guid);
            WRITE_CHECK(hold && submit().status == Store::Admission::WriterUnavailable);
        }
        store.CloseAdmission();
        WRITE_CHECK(submit().status == Store::Admission::Closed);
        WRITE_CHECK(player.builds == 0 && store.Idle() && TestQueue->Empty() && SaveLogs.empty());
    }
    {
        Pool pool;
        Fence fence;
        Store store(fence, pool);
        Store::Submission older{Store::Admission::Declined};
        Store::Submission newer{Store::Admission::Declined};
        {
            Player player;
            older = player.TryPB59TrackedStandaloneSave(store, false, false);
            WRITE_CHECK(older.status == Store::Admission::Tracking && player.builds == 1);
        }
        {
            Player player;
            player.incarnation = 9;
            player.session.incarnation = 10;
            player.item = 789;
            newer = player.TryPB59TrackedStandaloneSave(store, false, false);
            WRITE_CHECK(newer.status == Store::Admission::Tracking && player.builds == 1);
        }
        WRITE_CHECK(TestQueue->Size() == 2 && !fence.TryReserveCapture(guid));
        auto first = TakeTask();
        auto second = TakeTask();
        MySQLConnection executor;
        executor.code = 991;
        second->SetConnection(&executor);
        second->call();
        WRITE_CHECK(executor.size == 1);
        Player::PollPB59TrackedStandaloneSave(store);
        WRITE_CHECK(SaveLogs.size() == 4 && !store.Idle() && !fence.TryReserveCapture(guid));
        auto const newerSequence = static_cast<std::int64_t>(newer.ticket.sequence);
        auto const olderSequence = static_cast<std::int64_t>(older.ticket.sequence);
        using Values = std::vector<std::int64_t>;
        WRITE_CHECK(SaveLogs[0].values == (Values{123, 9, 10, newerSequence, int(Status::FailedUnverified),
            991, 1, 1, 1, 0}));
        WRITE_CHECK(SaveLogs[1].values == (Values{123, newerSequence, 66001}));
        WRITE_CHECK(SaveLogs[2].values == (Values{123, newerSequence, 789}));
        WRITE_CHECK(SaveLogs[3].values == (Values{123, newerSequence, 164}));
        first.reset();
        Player::PollPB59TrackedStandaloneSave(store);
        WRITE_CHECK(SaveLogs.size() == 8 && store.Idle() && !fence.TryReserveCapture(guid));
        WRITE_CHECK(SaveLogs[4].values == (Values{123, 7, 8, olderSequence, int(Status::CancelledBeforeExecution),
            Result::CancelledBeforeExecutionCode, 1, 1, 1, 0}));
        WRITE_CHECK(SaveLogs[6].values == (Values{123, olderSequence, 456}));
        Player::PollPB59TrackedStandaloneSave(store);
        WRITE_CHECK(SaveLogs.size() == 8 && TestQueue->Empty());
        SaveLogs.clear();

        Config.enabled = false;
        Player player;
        player.guid.value = 987; // Reuse pending storage without reusing the uncertain GUID.
        auto const unobserved = player.TryPB59TrackedStandaloneSave(store, false, false);
        Config.enabled = true;
        WRITE_CHECK(unobserved.status == Store::Admission::Tracking && player.builds == 1 && TestQueue->Size() == 1);
        auto task = TakeTask();
        task->SetConnection(&executor);
        task->call();
        WRITE_CHECK(!fence.TryReserveCapture(987));
        Player::PollPB59TrackedStandaloneSave(store);
        WRITE_CHECK(SaveLogs.size() == 1 && store.Idle() && !fence.TryReserveCapture(987));
        WRITE_CHECK(SaveLogs[0].values == (Values{987, std::int64_t(unobserved.ticket.sequence),
            int(Status::FailedUnverified), 991}));
        SaveLogs.clear();
    }
    {
        Pool pool;
        Fence fence;
        Store store(fence, pool);
        Player player;
        player.duringBuild = [] { throw std::runtime_error("native builder failed after dirty state changed"); };
        auto const sent = player.TryPB59TrackedStandaloneSave(store, false, false);
        WRITE_CHECK(sent.status == Store::Admission::Uncertain && sent.ticket.guid == guid && sent.ticket.sequence);
        WRITE_CHECK(player.builds == 1 && store.Idle() && TestQueue->Empty() && !fence.TryReserveCapture(guid));
        Player::PollPB59TrackedStandaloneSave(store);
        WRITE_CHECK(SaveLogs.size() == 1 && SaveLogs[0].format.find("outcome unavailable") != std::string_view::npos);
        WRITE_CHECK(SaveLogs[0].values == (std::vector<std::int64_t>{123, std::int64_t(sent.ticket.sequence)}));
        SaveLogs.clear();
    }
    for (bool observe : {true, false})
    {
        Pool pool;
        Fence fence;
        Store store(fence, pool);
        Player player;
        player.empty = true;
        Config.enabled = observe;
        auto const sent = player.TryPB59TrackedStandaloneSave(store, false, false);
        Config.enabled = true;
        WRITE_CHECK(sent.status == Store::Admission::Tracking && TestQueue->Size() == 1);
        auto task = TakeTask();
        MySQLConnection executor;
        executor.code = Result::EmptyCode;
        task->SetConnection(&executor);
        task->call();
        WRITE_CHECK(Player::PollPB59TrackedStandaloneSave(store));
        WRITE_CHECK(SaveLogs.size() == (observe ? 4u : 1u) && store.Idle() && !fence.TryReserveCapture(guid));
        auto const sequence = std::int64_t(sent.ticket.sequence);
        using Values = std::vector<std::int64_t>;
        auto const expected = observe
            ? Values{123, 7, 8, sequence, int(Status::Empty), Result::EmptyCode, 1, 1, 1, 0}
            : Values{123, sequence, int(Status::Empty), Result::EmptyCode};
        WRITE_CHECK(SaveLogs[0].values == expected && !Player::PollPB59TrackedStandaloneSave(store));
        SaveLogs.clear();
    }
    {
        for (int code : {0, Result::EmptyCode, 991})
        {
            auto& observer = PB59SaveObserver();
            auto const reservation = observer.Reserve(guid, 7, 8);
            std::promise<Result> outcome;
            WRITE_CHECK(observer.Seal(reservation.ticket, {}) && observer.Attach(reservation.ticket,
                outcome.get_future()));
            outcome.set_value(Result::FromExecutionCode(code));
            Player::DrainPB59CharacterSaveEvidence();
            WRITE_CHECK(SaveLogs.size() == (code == 991 ? 1u : 0u));
            SaveLogs.clear();
        }
    }
    {
        Pool pool;
        Fence fence;
        Store store(fence, pool);
        Player player;
        std::latch entered(1), resume(1);
        bool pendingInsideProducer = false;
        player.duringBuild = [&]
        {
            entered.count_down();
            resume.wait();
            // The thread currently building an admitted task cannot synchronously wait for itself.
            pendingInsideProducer = !Player::DrainPB59TrackedStandaloneSavesForShutdown(store);
        };
        Store::Submission sent{Store::Admission::Declined};
        std::thread producer([&] { sent = player.TryPB59TrackedStandaloneSave(store, false, false); });
        entered.wait();
        bool const heldDuringClose = !Player::DrainPB59TrackedStandaloneSavesForShutdown(store) &&
            !store.Idle() && !fence.TryReserveCapture(guid) && TestQueue->Empty();
        Player other;
        other.guid.value = 987;
        auto const refused = other.TryPB59TrackedStandaloneSave(store, false, false);
        resume.count_down();
        producer.join();
        WRITE_CHECK(heldDuringClose && pendingInsideProducer && SaveLogs.empty());
        WRITE_CHECK(refused.status == Store::Admission::Closed && other.builds == 0);
        WRITE_CHECK(sent.status == Store::Admission::Tracking && player.builds == 1 && TestQueue->Size() == 1);
        WRITE_CHECK(!Player::DrainPB59TrackedStandaloneSavesForShutdown(store) && !fence.TryReserveCapture(guid));
        auto task = TakeTask();
        MySQLConnection executor;
        task->SetConnection(&executor);
        task->call();
        WRITE_CHECK(Player::DrainPB59TrackedStandaloneSavesForShutdown(store));
        WRITE_CHECK(executor.size == 1 && SaveLogs.empty() && store.Idle() && fence.TryReserveCapture(guid));
        TestQueue->Shutdown();
    }
    {
        Pool pool;
        Fence fence;
        Store store(fence, pool);
        Player player;
        Config.enabled = false;
        auto const first = player.TryPB59TrackedStandaloneSave(store, false, false);
        auto const second = player.TryPB59TrackedStandaloneSave(store, false, false);
        Config.enabled = true;
        WRITE_CHECK(first.status == Store::Admission::Tracking && second.status == Store::Admission::Tracking);
        TestQueue->Cancel();
        WRITE_CHECK(Player::DrainPB59TrackedStandaloneSavesForShutdown(store));
        WRITE_CHECK(SaveLogs.size() == 2 && !fence.TryReserveCapture(guid));
        using Values = std::vector<std::int64_t>;
        WRITE_CHECK(SaveLogs[0].values == (Values{123, std::int64_t(first.ticket.sequence),
            int(Status::CancelledBeforeExecution), Result::CancelledBeforeExecutionCode}));
        WRITE_CHECK(SaveLogs[1].values == (Values{123, std::int64_t(second.ticket.sequence),
            int(Status::CancelledBeforeExecution), Result::CancelledBeforeExecutionCode}));
        WRITE_CHECK(Player::DrainPB59TrackedStandaloneSavesForShutdown(store) && SaveLogs.size() == 2);
        SaveLogs.clear();
    }
    std::cout << Checks - before << " PB59 standalone producer and diagnostic checks PASS\n";
    return 0;
}

static int CheckTrackedNativeSave()
{
    constexpr std::uint64_t guid = 123;
    constexpr std::uint64_t other = 456;
    unsigned const before = Checks;
    {
        Pool pool;
        Fence fence;
        Store store(fence, pool);
        bool dirty = true, heldBeforeBuild = false;
        TransactionBase const* written = nullptr;
        auto const sent = store.TrySubmitNativeSave(guid, [&](auto const& transaction)
        {
            heldBeforeBuild = !fence.TryReserveCapture(guid) && !store.Idle();
            store.CloseAdmission();
            written = transaction.get();
            dirty = false;
            transaction->Append(std::string_view("native save fixture"));
        });
        WRITE_CHECK(sent.status == Store::Admission::Tracking && heldBeforeBuild && !dirty);
        bool ranAfterClose = false;
        auto const refused = store.TrySubmitNativeSave(other, [&](auto const&) { ranAfterClose = true; });
        WRITE_CHECK(refused.status == Store::Admission::Closed && !ranAfterClose);
        WRITE_CHECK(!store.Idle() && !fence.TryReserveCapture(guid));
        auto task = TakeTask();
        WRITE_CHECK(task);
        MySQLConnection executor;
        task->SetConnection(&executor);
        task->call();
        WRITE_CHECK(executor.seen == written && executor.size == 1 && !fence.TryReserveCapture(guid));
        auto const result = store.PollOne();
        WRITE_CHECK(result && result->ticket == sent.ticket && result->result.IsCommitted());
        WRITE_CHECK(store.Idle() && fence.TryReserveCapture(guid));
    }
    for (bool reject : {false, true})
    {
        Pool pool;
        Fence fence;
        Store store(fence, pool);
        bool dirty = true;
        if (reject)
            TestQueue->Shutdown();
        auto const sent = store.TrySubmitNativeSave(guid, [&](auto const&) { dirty = false; });
        WRITE_CHECK(sent.status == Store::Admission::Tracking && !dirty);
        if (!reject)
            TestQueue->Cancel();
        auto const result = store.PollOne();
        WRITE_CHECK(result && result->ticket == sent.ticket);
        WRITE_CHECK(result->result.status == (reject ? Status::NotEnqueued : Status::CancelledBeforeExecution));
        WRITE_CHECK(store.Idle() && TestQueue->Empty() && !fence.TryReserveCapture(guid));
        WRITE_CHECK(fence.TryReserveCapture(other));
    }
    {
        Pool pool;
        Fence fence;
        Store store(fence, pool);
        bool dirty = true;
        auto const sent = store.TrySubmitNativeSave(guid, [&](auto const&)
        {
            dirty = false;
            throw std::runtime_error("injected native builder fault after dirty consumption");
        });
        WRITE_CHECK(sent.status == Store::Admission::Uncertain && sent.ticket.guid == guid && !dirty);
        WRITE_CHECK(store.Idle() && TestQueue->Empty() && !fence.TryReserveCapture(guid));
        WRITE_CHECK(!store.PollOne() && fence.TryReserveCapture(other));
    }
    {
        Pool pool;
        Fence fence;
        Store store(fence, pool);
        auto capture = fence.TryReserveCapture(guid);
        bool ran = false;
        auto const refused = store.TrySubmitNativeSave(guid, [&](auto const&) { ran = true; });
        WRITE_CHECK(capture && refused.status == Store::Admission::WriterUnavailable && !ran);
        WRITE_CHECK(store.Idle() && TestQueue->Empty());
    }
    std::cout << Checks - before << " PB59 tracked native save checks PASS\n";
    return 0;
}

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
    Player player;
    auto const sent = player.TryPB59TrackedStandaloneSave(store, false, false);
    WRITE_CHECK(sent.status == Store::Admission::Uncertain && sent.ticket.sequence);
    WRITE_CHECK(store.Idle() && !store.PollOne() && !fence.TryReserveCapture(guid));
    WRITE_CHECK(player.builds == 1 && SaveLogs.size() == 1);
    WRITE_CHECK(SaveLogs[0].values == (std::vector<std::int64_t>{123, std::int64_t(sent.ticket.sequence)}));
    WRITE_CHECK(Player::DrainPB59TrackedStandaloneSavesForShutdown(store));
    WRITE_CHECK(TestQueue->Size() == 1); // The tracker is drained while the database still owns the task.
    MySQLConnection executor;
    {
        DatabaseWorker worker(TestQueue, &executor);
        TestQueue->Shutdown();
    }
    WRITE_CHECK(Player::DrainPB59TrackedStandaloneSavesForShutdown(store));
    WRITE_CHECK(TestQueue->Empty() && !store.PollOne() && !fence.TryReserveCapture(guid));
    WRITE_CHECK(executor.size == 1 && SaveLogs.size() == 1);
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
    if (auto const status = CheckTrackedNativeSave())
        return status;
    if (auto const status = CheckStandaloneProducer())
        return status;
    return CheckProtectedCompletion();
}
