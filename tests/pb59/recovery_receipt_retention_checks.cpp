#include "PB59CharacterSaveObserver.h"
#include <iostream>
#include <stdexcept>
#include <thread>
#include <type_traits>

using Observer = PB59::CharacterSaveObserver<1>;
using Store = Observer::Store;
using Receipt = Observer::ProtectedReceipt;
static_assert(std::is_move_constructible_v<Receipt> && !std::is_copy_constructible_v<Receipt>);
static_assert(!std::is_constructible_v<Receipt, Observer::Ticket> && !std::is_default_constructible_v<Receipt>);
static unsigned Checks = 0;
#define RECEIPT_CHECK(value) do { \
    ++Checks; \
    if (!(value)) \
    { \
        std::cerr << "receipt retention line " << __LINE__ << '\n'; \
        return 1; \
    } \
} while (false)

int main()
{
    for (int code : {0, DatabaseTransactionResult::EmptyCode, DatabaseTransactionResult::AmbiguousCode, 5})
    {
        Observer observer;
        auto first = observer.Reserve(1, 2, 3);
        RECEIPT_CHECK(first.status == Store::Admission::Accepted);
        PB59::SaveEvidence dirty; dirty.AddItem(10);
        RECEIPT_CHECK(observer.Seal(first.ticket, dirty));
        std::promise<DatabaseTransactionResult> delivery;
        RECEIPT_CHECK(observer.Attach(first.ticket, delivery.get_future()));
        delivery.set_value(DatabaseTransactionResult::FromExecutionCode(code));
        // Capacity pressure can consume a ready successful future without
        // invoking Drain's report callback. This is existing diagnostic policy.
        auto next = observer.Reserve(1, 2, 3);
        bool const reclaimable = code == 0 || code == DatabaseTransactionResult::EmptyCode;
        RECEIPT_CHECK(next.status == (reclaimable ? Store::Admission::Accepted : Store::Admission::Full));
        unsigned reports = 0;
        std::optional<Store::Record> record;
        auto sink = [&](auto const& value) { ++reports; record = value; };
        observer.Drain(sink);
        RECEIPT_CHECK(reports == (reclaimable ? 0u : 1u));
        if (reclaimable)
        {
            RECEIPT_CHECK(next.ticket.serial != first.ticket.serial);
            RECEIPT_CHECK(next.ticket.identity.sequence > first.ticket.identity.sequence);
            std::promise<DatabaseTransactionResult> stale;
            RECEIPT_CHECK(!observer.Attach(first.ticket, stale.get_future()));
        }
        else
        {
            RECEIPT_CHECK(record && record->ticket == first.ticket && record->result.executionCode == code);
        }
        auto const count = reports;
        observer.Drain(sink); RECEIPT_CHECK(reports == count);
    }
    for (int code : {0, DatabaseTransactionResult::AmbiguousCode})
    {
        Observer observer;
        auto first = observer.Reserve(1, 2, 3);
        PB59::SaveEvidence dirty; dirty.AddItem(10);
        RECEIPT_CHECK(observer.Seal(first.ticket, dirty));
        std::promise<DatabaseTransactionResult> delivery;
        RECEIPT_CHECK(observer.Attach(first.ticket, delivery.get_future()));
        delivery.set_value(DatabaseTransactionResult::FromExecutionCode(code));
        bool sinkThrew = false;
        try { observer.Drain([](auto const&) { throw std::runtime_error("synthetic sink failure"); }); }
        catch (std::runtime_error const&) { sinkThrew = true; }
        RECEIPT_CHECK(sinkThrew);
        auto next = observer.Reserve(1, 2, 3);
        RECEIPT_CHECK(next.status == (code == 0 ? Store::Admission::Accepted : Store::Admission::Full));
        unsigned oldReports = 0;
        observer.Drain([&](auto const& value) { if (value.ticket == first.ticket) ++oldReports; });
        RECEIPT_CHECK(oldReports == (code == 0 ? 0u : 1u));
        // A throwing sink alone does not protect a successful recovery receipt
        // from the observer's independent reclaim path. Missing still proves nothing.
    }
    {
        Observer observer;
        auto first = observer.Reserve(1, 2, 3);
        PB59::SaveEvidence dirty; dirty.AddItem(10);
        RECEIPT_CHECK(observer.Seal(first.ticket, dirty));
        std::promise<DatabaseTransactionResult> delivery;
        RECEIPT_CHECK(observer.Attach(first.ticket, delivery.get_future()));
        delivery.set_exception(std::make_exception_ptr(std::runtime_error("synthetic delivery failure")));
        auto next = observer.Reserve(1, 2, 3);
        RECEIPT_CHECK(next.status == Store::Admission::Full);
        unsigned reports = 0;
        observer.Drain([&](auto const& value)
        {
            if (value.ticket == first.ticket && value.result.status == DatabaseTransactionStatus::Ambiguous)
                ++reports;
        });
        RECEIPT_CHECK(reports == 1);
    }
    {
        Observer observer;
        auto first = observer.Reserve(1, 2, 3);
        PB59::SaveEvidence dirty; dirty.AddItem(10);
        RECEIPT_CHECK(observer.Seal(first.ticket, dirty));
        std::promise<DatabaseTransactionResult> delivery;
        RECEIPT_CHECK(observer.Attach(first.ticket, delivery.get_future()));
        RECEIPT_CHECK(observer.Reserve(1, 2, 3).status == Store::Admission::Full);
        unsigned reports = 0;
        auto sink = [&](auto const&) { ++reports; };
        observer.Drain(sink); RECEIPT_CHECK(reports == 0);
        delivery.set_value(DatabaseTransactionResult::FromExecutionCode(0));
        observer.Drain(sink); RECEIPT_CHECK(reports == 1);
    }
    PB59::SaveEvidence dirty;
    dirty.AddItem(10);
    for (int code : {0, DatabaseTransactionResult::EmptyCode, DatabaseTransactionResult::NotEnqueuedCode,
             DatabaseTransactionResult::CancelledBeforeExecutionCode, DatabaseTransactionResult::AmbiguousCode, 5})
    {
        Observer observer;
        auto reservation = observer.ReserveProtected(1, 2, 3);
        RECEIPT_CHECK(reservation.status == Store::Admission::Accepted && reservation.receipt);
        auto const& receipt = *reservation.receipt;
        auto full = observer.ReserveProtected(1, 2, 3);
        RECEIPT_CHECK(full.status == Store::Admission::Full && !full.receipt);
        auto rawTicket = Observer::Ticket{0, 1, {1, 2, 3, 1}};
        RECEIPT_CHECK(!observer.Seal(rawTicket, dirty) && !observer.Abandon(rawTicket));
        std::promise<DatabaseTransactionResult> delivery;
        auto future = delivery.get_future();
        RECEIPT_CHECK(!observer.ConsumeProtected(receipt));
        RECEIPT_CHECK(!observer.AttachProtected(receipt, std::move(future)) && future.valid());
        RECEIPT_CHECK(observer.SealProtected(receipt, dirty));
        RECEIPT_CHECK(!observer.SealProtected(receipt, dirty) && !observer.AbandonProtected(receipt));
        RECEIPT_CHECK(!observer.AttachProtected(receipt, {}));
        RECEIPT_CHECK(!observer.Attach(rawTicket, std::promise<DatabaseTransactionResult>().get_future()));
        RECEIPT_CHECK(observer.AttachProtected(receipt, std::move(future)) && !future.valid());
        auto duplicate = std::promise<DatabaseTransactionResult>().get_future();
        RECEIPT_CHECK(!observer.AttachProtected(receipt, std::move(duplicate)) && duplicate.valid());
        RECEIPT_CHECK(!observer.ConsumeProtected(receipt));
        delivery.set_value(DatabaseTransactionResult::FromExecutionCode(code));
        unsigned reports = 0;
        auto sink = [&](auto const&) { ++reports; };
        observer.Drain(sink);
        observer.Drain(sink, true);
        RECEIPT_CHECK(reports == 0);
        RECEIPT_CHECK(observer.Reserve(1, 2, 3).status == Store::Admission::Full);
        auto record = observer.ConsumeProtected(receipt);
        RECEIPT_CHECK(record && record->ticket == rawTicket && record->state == Store::State::Terminal);
        RECEIPT_CHECK(record->result.executionCode == code && record->evidence.items[0] == 10);
        RECEIPT_CHECK(!observer.ConsumeProtected(receipt));
        auto next = observer.ReserveProtected(1, 2, 3);
        RECEIPT_CHECK(next.receipt && !observer.SealProtected(receipt, dirty) && !observer.AbandonProtected(receipt));
        RECEIPT_CHECK(observer.SealProtected(*next.receipt, dirty));
        RECEIPT_CHECK(!observer.AttachProtected(receipt, std::move(duplicate)) && duplicate.valid());
        RECEIPT_CHECK(!observer.ConsumeProtected(receipt));
    }
    {
        std::optional<Observer> observer(std::in_place);
        Observer other;
        auto first = observer->ReserveProtected(1, 2, 3);
        auto foreign = other.ReserveProtected(1, 2, 3);
        RECEIPT_CHECK(first.receipt && foreign.receipt);
        // Both observers issued the same numeric ticket.
        RECEIPT_CHECK(!other.SealProtected(*first.receipt, dirty) && !observer->AbandonProtected(*foreign.receipt));
        observer.reset();
        observer.emplace(); // Same observer storage, fresh instance, same numeric ticket.
        auto replacement = observer->ReserveProtected(1, 2, 3);
        RECEIPT_CHECK(replacement.receipt);
        RECEIPT_CHECK(!observer->SealProtected(*first.receipt, dirty) && !observer->ConsumeProtected(*first.receipt));
        Receipt owner = std::move(*replacement.receipt);
        RECEIPT_CHECK(!observer->AbandonProtected(*replacement.receipt));
        RECEIPT_CHECK(observer->AbandonProtected(owner));
        auto record = observer->ConsumeProtected(owner);
        RECEIPT_CHECK(record && record->evidence.incomplete && !record->result.IsCommitted());
        RECEIPT_CHECK(!observer->ConsumeProtected(owner));
    }
    for (bool brokenPromise : {false, true})
    {
        Observer observer;
        auto reservation = observer.ReserveProtected(1, 2, 3);
        RECEIPT_CHECK(reservation.receipt && observer.SealProtected(*reservation.receipt, dirty));
        {
            std::promise<DatabaseTransactionResult> delivery;
            RECEIPT_CHECK(observer.AttachProtected(*reservation.receipt, delivery.get_future()));
            if (!brokenPromise)
                delivery.set_exception(std::make_exception_ptr(std::runtime_error("synthetic delivery failure")));
        }
        auto record = observer.ConsumeProtected(*reservation.receipt);
        RECEIPT_CHECK(record && record->result.status == DatabaseTransactionStatus::Ambiguous);
    }
    {
        Observer observer;
        auto abandonedOwner = observer.ReserveProtected(1, 2, 3);
        RECEIPT_CHECK(abandonedOwner.receipt);
        abandonedOwner.receipt.reset();
        unsigned reports = 0;
        observer.Drain([&](auto const&) { ++reports; }, true);
        RECEIPT_CHECK(reports == 0 && observer.Reserve(1, 2, 3).status == Store::Admission::Full);
    }
    {
        using MixedObserver = PB59::CharacterSaveObserver<2>;
        using MixedAdmission = MixedObserver::Store::Admission;
        MixedObserver observer;
        auto owner = observer.ReserveProtected(1, 2, 3);
        auto ordinary = observer.Reserve(1, 2, 3);
        RECEIPT_CHECK(owner.receipt && ordinary.status == MixedAdmission::Accepted);
        RECEIPT_CHECK(observer.SealProtected(*owner.receipt, dirty) && observer.Seal(ordinary.ticket, dirty));
        std::promise<DatabaseTransactionResult> protectedDelivery, ordinaryDelivery;
        RECEIPT_CHECK(observer.AttachProtected(*owner.receipt, protectedDelivery.get_future()));
        RECEIPT_CHECK(observer.Attach(ordinary.ticket, ordinaryDelivery.get_future()));
        protectedDelivery.set_value(DatabaseTransactionResult::FromExecutionCode(0));
        ordinaryDelivery.set_value(DatabaseTransactionResult::FromExecutionCode(0));
        auto reclaimed = observer.Reserve(1, 2, 3);
        RECEIPT_CHECK(reclaimed.status == MixedAdmission::Accepted && reclaimed.ticket.slot == ordinary.ticket.slot);
        unsigned ordinaryReports = 0;
        observer.Drain([&](auto const& record)
        {
            if (record.ticket == reclaimed.ticket)
                ++ordinaryReports;
        }, true);
        RECEIPT_CHECK(ordinaryReports == 1);
        std::optional<MixedObserver::Record> first, second;
        std::thread a([&] { first = observer.ConsumeProtected(*owner.receipt); });
        std::thread b([&] { second = observer.ConsumeProtected(*owner.receipt); });
        a.join();
        b.join();
        RECEIPT_CHECK(first.has_value() != second.has_value());
        RECEIPT_CHECK((first ? first : second)->result.IsCommitted());
        auto reused = observer.Reserve(1, 2, 3);
        RECEIPT_CHECK(reused.status == MixedAdmission::Accepted && reused.ticket.slot != ordinary.ticket.slot);
        RECEIPT_CHECK(observer.Seal(reused.ticket, dirty));
    }
    std::cout << Checks << " real-header receipt retention checks PASS (synthetic futures; no DB)\n";
}
