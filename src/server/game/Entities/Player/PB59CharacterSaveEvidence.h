#pragma once

#include "DatabaseTransactionResult.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <mutex>
#include <optional>

namespace PB59
{
// Process-local diagnostic evidence only. No Player/Item pointers, SQL,
// re-dirty operation, recovery authorization or automatic retry.
struct SaveIdentity
{
    std::uint64_t guid = 0, player = 0, session = 0, sequence = 0;
    bool Valid() const { return guid && player && session && sequence; }
    bool operator==(SaveIdentity const&) const = default;
};

struct SaveEvidence
{
    static constexpr std::size_t Limit = 64;
    std::array<std::uint32_t, Limit> quests{};
    std::array<std::uint64_t, Limit> items{};
    std::size_t questCount = 0, itemCount = 0;
    bool incomplete = false;

    bool Valid() const
    {
        if (questCount > Limit || itemCount > Limit)
            return false;
        for (std::size_t i = 0; i < questCount; ++i)
            if (!quests[i])
                return false;
        for (std::size_t i = 0; i < itemCount; ++i)
            if (!items[i])
                return false;
        return true;
    }
};

template<std::size_t Capacity>
class CharacterSaveEvidence
{
    static_assert(Capacity > 0);
public:
    struct Ticket
    {
        std::size_t slot = Capacity;
        std::uint64_t serial = 0;
        SaveIdentity identity;
        bool operator==(Ticket const&) const = default;
    };
    enum class State { Free, Reserved, Pending, Terminal };
    struct Record
    {
        Ticket ticket;
        State state = State::Free;
        SaveEvidence evidence;
        DatabaseTransactionResult result{DatabaseTransactionStatus::FailedUnverified, DatabaseTransactionResult::PriorAttemptFailureCode};
    };
    enum class Admission { Accepted, Invalid, Duplicate, Full, Exhausted };
    struct Reservation
    {
        Admission status;
        Ticket ticket;
    };

    // Reserve before consuming dirty state. Saturation is explicit and must
    // be logged by the caller; this primitive never suppresses native saves.
    Reservation Reserve(SaveIdentity id)
    {
        std::lock_guard lock(_mutex);
        if (!id.Valid())
            return {Admission::Invalid, {}};
        std::size_t free = Capacity;
        for (std::size_t i = 0; i < Capacity; ++i)
        {
            if (_records[i].state == State::Free)
            {
                if (free == Capacity)
                    free = i;
            }
            else if (_records[i].ticket.identity == id)
                return {Admission::Duplicate, {}};
        }
        if (free == Capacity)
            return {Admission::Full, {}};
        if (_serial == std::numeric_limits<std::uint64_t>::max())
            return {Admission::Exhausted, {}};
        Ticket ticket{free, ++_serial, id};
        _records[free] = Record{ticket, State::Reserved, {}, {DatabaseTransactionStatus::FailedUnverified, DatabaseTransactionResult::PriorAttemptFailureCode}};
        return {Admission::Accepted, ticket};
    }

    // Capture value evidence once, before handing work to a database worker.
    bool Seal(Ticket const& ticket, SaveEvidence const& evidence)
    {
        std::lock_guard lock(_mutex);
        Record* record = Find(ticket);
        if (!record || record->state != State::Reserved || !evidence.Valid())
            return false;
        record->evidence = evidence;
        record->state = State::Pending;
        return true;
    }

    // An abandoned reservation is not a known database failure. It records
    // missing capture evidence and requires the same diagnostic acknowledgement.
    bool Abandon(Ticket const& ticket)
    {
        std::lock_guard lock(_mutex);
        Record* record = Find(ticket);
        if (!record || record->state != State::Reserved)
            return false;
        record->evidence.incomplete = true;
        record->result = {DatabaseTransactionStatus::FailedUnverified, DatabaseTransactionResult::PriorAttemptFailureCode};
        record->state = State::Terminal;
        return true;
    }

    bool Complete(Ticket const& ticket, DatabaseTransactionResult result)
    {
        std::lock_guard lock(_mutex);
        Record* record = Find(ticket);
        if (!record || record->state != State::Pending)
            return false;
        record->result = result;
        record->state = State::Terminal;
        return true;
    }

    std::optional<Record> Inspect(Ticket const& ticket)
    {
        std::lock_guard lock(_mutex);
        Record* record = Find(ticket);
        return record ? std::optional<Record>(*record) : std::nullopt;
    }

    // Caller must emit/consume diagnostic evidence before acknowledgement.
    // No implicit eviction, and an older ticket cannot release a reused slot.
    bool Acknowledge(Ticket const& ticket)
    {
        std::lock_guard lock(_mutex);
        Record* record = Find(ticket);
        if (!record || record->state != State::Terminal)
            return false;
        *record = Record{};
        return true;
    }

    // Diagnostic drains can enumerate slots even after the original caller
    // disappears. The returned ticket must still match when acknowledging.
    std::optional<Record> InspectSlot(std::size_t slot)
    {
        std::lock_guard lock(_mutex);
        if (slot >= Capacity || _records[slot].state == State::Free)
            return std::nullopt;
        return _records[slot];
    }

    std::array<std::size_t, 4> Counts()
    {
        std::lock_guard lock(_mutex);
        std::array<std::size_t, 4> counts{};
        for (Record const& record : _records)
            ++counts[static_cast<std::size_t>(record.state)];
        return counts;
    }

private:
    Record* Find(Ticket const& ticket)
    {
        if (ticket.slot >= Capacity || !ticket.serial || !ticket.identity.Valid())
            return nullptr;
        Record& record = _records[ticket.slot];
        return record.state != State::Free && record.ticket == ticket ? &record : nullptr;
    }
    std::mutex _mutex;
    std::array<Record, Capacity> _records{};
    std::uint64_t _serial = 0;
};
}
