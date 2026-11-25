#pragma once

#include "PB59CharacterSaveEvidence.h"
#include "PB59InventoryWriteSubmission.h"
#include "PB59PreparedInventoryWrite.h"
#include <algorithm>
#include <array>
#include <functional>
#include <limits>
#include <mutex>
#include <type_traits>

namespace PB59
{
// The service owning this store must outlive producers and keep the same
// fence and pool available through shutdown. No producer or actor is retained.
template<std::size_t Capacity, std::size_t FenceCapacity, typename Connection>
class PendingInventoryWrites final
{
    static_assert(Capacity > 0);

    using Fence = InventoryWriteFence<FenceCapacity>;
    using Write = PreparedInventoryWrite<FenceCapacity, Connection>;
    using Pool = DatabaseWorkerPool<Connection>;

public:
    using Ticket = InventoryWriteTicket;
    using Admission = InventoryWriteAdmission;
    using Submission = InventoryWriteSubmission;

    struct NativeSaveDiagnostics
    {
        SaveIdentity identity;
        SaveEvidence evidence;
    };

    struct Completion
    {
        Ticket ticket;
        DatabaseTransactionResult result;
        std::optional<NativeSaveDiagnostics> diagnostics;
    };

private:
    struct Slot
    {
        Ticket ticket;
        std::optional<Write> write;
        std::optional<NativeSaveDiagnostics> diagnostics;
    };

    class Reservation final
    {
    public:
        Reservation(PendingInventoryWrites& store, Slot& slot) noexcept : _store(store), _slot(&slot) {}
        Reservation(Reservation const&) = delete;
        Reservation& operator=(Reservation const&) = delete;

        ~Reservation()
        {
            if (_slot)
            {
                std::lock_guard lock(_store._mutex);
                _slot->ticket = {};
            }
        }

        void Keep() noexcept { _slot = nullptr; }

    private:
        PendingInventoryWrites& _store;
        Slot* _slot;
    };

public:
    PendingInventoryWrites(Fence& fence, Pool& database) noexcept : _fence(fence), _database(database) {}

    // Build synchronously outside the store lock. The producer must build an
    // owned payload without consuming dirty state and stop appending on return.
    template<typename Builder>
    [[nodiscard]] Submission TrySubmit(std::uint64_t guid, Builder&& build)
    {
        return Submit<false>(guid, std::forward<Builder>(build));
    }

    // Once a native build is admitted it may have consumed dirty state.
    // CloseAdmission stops new builds. The pool remains available until
    // this accepted work has been handed off and drained.
    template<typename Builder>
        requires std::is_void_v<std::invoke_result_t<Builder, SQLTransaction<Connection> const&>>
    [[nodiscard]] Submission TrySubmitNativeSave(std::uint64_t guid, Builder&& build)
    {
        return Submit<true>(guid, [&](auto const& transaction, std::optional<NativeSaveDiagnostics>&)
        {
            std::invoke(std::forward<Builder>(build), transaction);
        });
    }

    // Store diagnostic membership with the task that executes the save.
    template<typename Builder>
        requires std::is_void_v<std::invoke_result_t<Builder, SQLTransaction<Connection> const&, SaveEvidence&>>
    [[nodiscard]] Submission TrySubmitObservedNativeSave(std::uint64_t guid, std::uint64_t player,
        std::uint64_t session, Builder&& build)
    {
        if (!guid || !player || !session)
            return {Admission::Declined};
        return Submit<true>(guid, [&](auto const& transaction, std::optional<NativeSaveDiagnostics>& diagnostics)
        {
            diagnostics.emplace(NativeSaveDiagnostics{{guid, player, session, 0}, {}});
            std::invoke(std::forward<Builder>(build), transaction, diagnostics->evidence);
            if (!diagnostics->evidence.Valid())
            {
                diagnostics->evidence = {};
                diagnostics->evidence.incomplete = true;
            }
        });
    }

private:
    template<bool NativeSave, typename Builder>
    [[nodiscard]] Submission Submit(std::uint64_t guid, Builder&& build)
    {
        Slot* slot = nullptr;
        Ticket ticket;
        {
            std::lock_guard lock(_mutex);
            if (_closed)
                return {Admission::Closed};
            if (_sequence == std::numeric_limits<std::uint64_t>::max())
                return {Admission::SequenceExhausted};

            auto available = std::ranges::find_if(_slots, [](Slot const& entry) { return !entry.ticket.sequence; });
            if (available == _slots.end())
                return {Admission::Full};
            slot = &*available;
            ticket = {guid, ++_sequence};
            slot->ticket = ticket;
        }
        Reservation reservation(*this, *slot);
        auto write = Write::Prepare(_fence, _database, guid);
        if (!write || !write->GetTransaction())
            return {Admission::WriterUnavailable};
        std::optional<NativeSaveDiagnostics> diagnostics;
        if constexpr (NativeSave)
        {
            {
                std::lock_guard lock(_mutex);
                if (_closed)
                    return {Admission::Closed};
                if (!write->MarkNativeBuildStarted())
                    return {Admission::WriterUnavailable};
            }
            try
            {
                std::invoke(std::forward<Builder>(build), write->GetTransaction(), diagnostics);
            }
            catch (...)
            {
                // No legacy fallback once dirty state may have changed.
                return {Admission::Uncertain, ticket};
            }
        }
        else if (!std::invoke(std::forward<Builder>(build), *write->GetTransaction()))
            return {Admission::Declined};

        std::lock_guard lock(_mutex);
        if constexpr (!NativeSave)
            if (_closed)
                return {Admission::Closed};

        slot->write.emplace(std::move(*write));
        try
        {
            if (slot->write->Dispatch())
            {
                if (diagnostics)
                    diagnostics->identity.sequence = ticket.sequence;
                slot->diagnostics = std::move(diagnostics);
                reservation.Keep();
                return {Admission::Tracking, ticket};
            }
        }
        catch (...)
        {
            // The submitter has no exact future. Drop the marked lease into
            // the fence's uncertain state instead of retrying the write.
        }
        slot->write.reset();
        return {Admission::Uncertain, ticket};
    }

public:
    [[nodiscard]] std::optional<Completion> PollOne()
    {
        std::lock_guard lock(_mutex);
        for (std::size_t count = 0; count < Capacity; ++count)
        {
            auto& slot = _slots[_nextPoll];
            _nextPoll = (_nextPoll + 1) % Capacity;
            if (!slot.write)
                continue;
            auto result = slot.write->Poll();
            if (!result)
                continue;
            Completion completed{slot.ticket, *result, std::move(slot.diagnostics)};
            slot.diagnostics.reset();
            slot.write.reset();
            slot.ticket = {};
            return completed;
        }
        return std::nullopt;
    }

    void CloseAdmission()
    {
        std::lock_guard lock(_mutex);
        _closed = true;
    }

    // A closed idle store has no handles left to drain. Uncertain GUIDs in
    // the fence may still be blocked, and idle supplies no persistence proof.
    [[nodiscard]] bool Idle() const
    {
        std::lock_guard lock(_mutex);
        return std::ranges::none_of(_slots, [](Slot const& entry) { return entry.ticket.sequence; });
    }

private:
    Fence& _fence;
    Pool& _database;
    mutable std::mutex _mutex;
    std::array<Slot, Capacity> _slots{};
    std::uint64_t _sequence = 0;
    std::size_t _nextPoll = 0;
    bool _closed = false;
};
}
