#include "PB59CharacterSaveObserver.h"
#include <iostream>
#include <stdexcept>

using Observer = PB59::CharacterSaveObserver<1>;
using Store = Observer::Store;
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
    std::cout << Checks << " real-header receipt retention checks PASS (synthetic futures; no DB)\n";
}
