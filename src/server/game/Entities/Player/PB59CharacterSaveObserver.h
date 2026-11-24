#pragma once

#include "PB59CharacterSaveEvidence.h"
#include <chrono>
#include <future>
#include <memory>
#include <utility>

namespace PB59
{
// One process-local observer. No callback executes on the database worker.
// All record/future transitions are serialized; the reporting callback must
// not re-enter the observer. Only the caller may submit native SQL.
template<std::size_t Capacity>
class CharacterSaveObserver
{
    struct ReceiptOwner {};

public:
    using Store = CharacterSaveEvidence<Capacity>;
    using Ticket = typename Store::Ticket;
    using Record = typename Store::Record;
    using Reservation = typename Store::Reservation;

    class ProtectedReceipt
    {
    public:
        ProtectedReceipt(ProtectedReceipt&&) noexcept = default;

    private:
        friend class CharacterSaveObserver;
        ProtectedReceipt(std::shared_ptr<ReceiptOwner const> owner, Ticket ticket) noexcept
            : _owner(std::move(owner)), _ticket(ticket) {}

        std::shared_ptr<ReceiptOwner const> _owner;
        Ticket _ticket;
    };

    struct ProtectedReservation
    {
        typename Store::Admission status;
        std::optional<ProtectedReceipt> receipt;
    };

    Reservation Reserve(std::uint64_t guid, std::uint64_t player, std::uint64_t session)
    {
        std::lock_guard lock(_mutex);
        return ReserveLocked(guid, player, session);
    }

    // Admit before capture or submission. The handle owns access to this
    // observer's exact slot generation. Losing it leaves the slot held.
    [[nodiscard]] ProtectedReservation ReserveProtected(std::uint64_t guid, std::uint64_t player, std::uint64_t session)
    {
        std::lock_guard lock(_mutex);
        if (!_receiptOwner)
            _receiptOwner = std::make_shared<ReceiptOwner>();
        auto reservation = ReserveLocked(guid, player, session);
        if (reservation.status != Store::Admission::Accepted)
            return {reservation.status, std::nullopt};
        _protected[reservation.ticket.slot] = true;
        return {reservation.status, ProtectedReceipt(_receiptOwner, reservation.ticket)};
    }

    bool Seal(Ticket const& ticket, SaveEvidence const& evidence)
    {
        std::lock_guard lock(_mutex);
        return !IsProtected(ticket) && _store.Seal(ticket, evidence);
    }

    bool Attach(Ticket const& ticket, std::future<DatabaseTransactionResult> future)
    {
        std::lock_guard lock(_mutex);
        return !IsProtected(ticket) && AttachLocked(ticket, std::move(future));
    }

    bool Abandon(Ticket const& ticket)
    {
        std::lock_guard lock(_mutex);
        return !IsProtected(ticket) && _store.Abandon(ticket);
    }

    bool SealProtected(ProtectedReceipt const& receipt, SaveEvidence const& evidence)
    {
        std::lock_guard lock(_mutex);
        return Owns(receipt) && _store.Seal(receipt._ticket, evidence);
    }

    // A refused attachment leaves the supplied future with the caller.
    bool AttachProtected(ProtectedReceipt const& receipt, std::future<DatabaseTransactionResult>&& future)
    {
        std::lock_guard lock(_mutex);
        return Owns(receipt) && AttachLocked(receipt._ticket, std::move(future));
    }

    // Only reserved, unsealed captures can be abandoned. The exact consumer
    // still receives the terminal record with incomplete evidence.
    bool AbandonProtected(ProtectedReceipt const& receipt)
    {
        std::lock_guard lock(_mutex);
        return Owns(receipt) && _store.Abandon(receipt._ticket);
    }

    // Poll the future without waiting for completion. Copy before freeing the slot.
    // Processing that value is the caller's responsibility, outside this lock.
    [[nodiscard]] std::optional<Record> ConsumeProtected(ProtectedReceipt const& receipt)
    {
        std::lock_guard lock(_mutex);
        if (!Owns(receipt))
            return std::nullopt;
        auto const& ticket = receipt._ticket;
        auto record = _store.Inspect(ticket);
        if (record && record->state == Store::State::Pending && CompleteReady(ticket))
            record = _store.Inspect(ticket);
        if (!record || record->state != Store::State::Terminal || !_store.Acknowledge(ticket))
            return std::nullopt;
        _protected[ticket.slot] = false;
        return record;
    }

    template<class Report>
    void Drain(Report&& report, bool final = false)
    {
        std::lock_guard lock(_mutex);
        for (std::size_t i = 0; i < Capacity; ++i)
        {
            if (_protected[i])
                continue;
            auto record = _store.InspectSlot(i);
            if (!record)
                continue;
            if (record->state == Store::State::Reserved && final)
                _store.Abandon(record->ticket);
            else if (record->state == Store::State::Pending && !CompleteReady(record->ticket) && final)
            {
                // DB pool is already closed; never wait indefinitely or replay.
                _futures[i] = {};
                _store.Complete(record->ticket,
                    DatabaseTransactionResult::FromExecutionCode(DatabaseTransactionResult::AmbiguousCode));
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
    Reservation ReserveLocked(std::uint64_t guid, std::uint64_t player, std::uint64_t session)
    {
        if (_sequence == std::numeric_limits<std::uint64_t>::max())
            return {Store::Admission::Exhausted, {}};
        SaveIdentity const identity{guid, player, session, ++_sequence};
        auto reservation = _store.Reserve(identity);
        if (reservation.status == Store::Admission::Full)
        {
            // The production report sink deliberately ignores successful/empty
            // saves. Reclaim only ordinary successful records when full.
            ReclaimSuccessful();
            reservation = _store.Reserve(identity);
        }
        return reservation;
    }

    bool IsProtected(Ticket const& ticket) const
    {
        return ticket.slot < Capacity && _protected[ticket.slot];
    }

    bool Owns(ProtectedReceipt const& receipt) const
    {
        return receipt._owner && receipt._owner == _receiptOwner && IsProtected(receipt._ticket);
    }

    bool AttachLocked(Ticket const& ticket, std::future<DatabaseTransactionResult>&& future)
    {
        auto record = _store.Inspect(ticket);
        if (!record || record->state != Store::State::Pending || !future.valid() || _futures[ticket.slot].valid())
            return false;
        _futures[ticket.slot] = std::move(future);
        return true;
    }

    bool CompleteReady(Ticket const& ticket)
    {
        auto& future = _futures[ticket.slot];
        if (!future.valid() || future.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
            return false;
        auto result = DatabaseTransactionResult::FromExecutionCode(DatabaseTransactionResult::AmbiguousCode);
        try { result = future.get(); }
        catch (...) {} // failed delivery is uncertain, never a success or permission to replay
        return _store.Complete(ticket, result);
    }

    void ReclaimSuccessful()
    {
        // Caller holds _mutex. Bounded, nonblocking, no reporting callback.
        for (std::size_t i = 0; i < Capacity; ++i)
        {
            if (_protected[i])
                continue;
            auto record = _store.InspectSlot(i);
            if (!record)
                continue;
            if (record->state == Store::State::Pending && CompleteReady(record->ticket))
                record = _store.InspectSlot(i);
            if (record && record->state == Store::State::Terminal &&
                (record->result.IsCommitted() || record->result.status == DatabaseTransactionStatus::Empty))
                _store.Acknowledge(record->ticket);
        }
    }

    std::mutex _mutex;
    Store _store;
    std::array<std::future<DatabaseTransactionResult>, Capacity> _futures;
    std::array<bool, Capacity> _protected{};
    std::shared_ptr<ReceiptOwner const> _receiptOwner;
    std::uint64_t _sequence = 0;
};
}
