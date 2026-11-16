#pragma once

#include "PB59CharacterSaveEvidence.h"
#include <chrono>
#include <future>

namespace PB59
{
// One process-local observer. No callback executes on the database worker.
// All record/future transitions are serialized; the reporting callback must
// not re-enter the observer. Only the caller may submit native SQL.
template<std::size_t Capacity>
class CharacterSaveObserver
{
public:
    using Store = CharacterSaveEvidence<Capacity>;
    using Ticket = typename Store::Ticket;
    using Record = typename Store::Record;
    using Reservation = typename Store::Reservation;

    Reservation Reserve(std::uint64_t guid, std::uint64_t player, std::uint64_t session)
    {
        std::lock_guard lock(_mutex);
        if (_sequence == std::numeric_limits<std::uint64_t>::max())
            return {Store::Admission::Exhausted, {}};
        return _store.Reserve({guid, player, session, ++_sequence});
    }

    bool Seal(Ticket const& ticket, SaveEvidence const& evidence)
    {
        std::lock_guard lock(_mutex);
        return _store.Seal(ticket, evidence);
    }

    bool Attach(Ticket const& ticket, std::future<DatabaseTransactionResult> future)
    {
        std::lock_guard lock(_mutex);
        auto record = _store.Inspect(ticket);
        if (!record || record->state != Store::State::Pending || !future.valid() || _futures[ticket.slot].valid())
            return false;
        _futures[ticket.slot] = std::move(future);
        return true;
    }

    bool Abandon(Ticket const& ticket)
    {
        std::lock_guard lock(_mutex);
        return _store.Abandon(ticket);
    }

    template<class Report>
    void Drain(Report&& report, bool final = false)
    {
        std::lock_guard lock(_mutex);
        for (std::size_t i = 0; i < Capacity; ++i)
        {
            auto record = _store.InspectSlot(i);
            if (!record)
                continue;
            if (record->state == Store::State::Reserved && final)
                _store.Abandon(record->ticket);
            else if (record->state == Store::State::Pending)
            {
                auto& future = _futures[i];
                if (future.valid() && future.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
                {
                    DatabaseTransactionResult result = DatabaseTransactionResult::FromExecutionCode(DatabaseTransactionResult::AmbiguousCode);
                    try { result = future.get(); }
                    catch (std::future_error const&) {} // missing delivery is not a known failure
                    _store.Complete(record->ticket, result);
                }
                else if (final)
                {
                    // DB pool is already closed; never wait indefinitely or replay.
                    future = {};
                    _store.Complete(record->ticket, DatabaseTransactionResult::FromExecutionCode(DatabaseTransactionResult::AmbiguousCode));
                }
            }
            record = _store.InspectSlot(i);
            if (record && record->state == Store::State::Terminal)
            {
                report(*record);
                _store.Acknowledge(record->ticket);
            }
        }
    }

private:
    std::mutex _mutex;
    Store _store;
    std::array<std::future<DatabaseTransactionResult>, Capacity> _futures;
    std::uint64_t _sequence = 0;
};
}
