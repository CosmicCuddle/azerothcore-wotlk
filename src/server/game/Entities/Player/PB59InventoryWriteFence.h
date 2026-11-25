#pragma once

#include "DatabaseTransactionResult.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <utility>

namespace PB59
{
// Dormant, process-local coordination for registered writers. Before using a capture
// reservation, the caller must establish that all competing writers use the same fence.
template<std::size_t Capacity>
class InventoryWriteFence final
{
    static_assert(Capacity > 0);

    static bool MatchesExecutionCode(DatabaseTransactionResult result) noexcept
    {
        return DatabaseTransactionResult::FromExecutionCode(result.executionCode).status == result.status;
    }

    static bool DefinitivelyDidNotWrite(DatabaseTransactionStatus status) noexcept
    {
        return status == DatabaseTransactionStatus::Empty || status == DatabaseTransactionStatus::NotStarted ||
            status == DatabaseTransactionStatus::NotEnqueued ||
            status == DatabaseTransactionStatus::CancelledBeforeExecution;
    }

public:
    class RecoveryWrite;

private:
    struct Entry
    {
        std::uint64_t guid = 0;
        std::size_t writers = 0;
        bool capture = false;
        bool uncertain = false;
    };

    struct State
    {
        std::mutex mutex;
        std::array<Entry, Capacity> entries{};
    };

    template<bool IsWriter>
    class Lease final
    {
    public:
        Lease(Lease&&) noexcept = default;
        ~Lease() { Release(_possiblySubmitted); }

        // Call before handing the transaction to any path that could execute it.
        // Transfer the lease to completion handling and supply that exact task's result.
        [[nodiscard]] bool MarkPossiblySubmitted() noexcept requires IsWriter
        {
            return MarkSubmitted();
        }

        // Reports whether this writer is cleared for capture admission. An
        // uncertain or inconsistent outcome consumes the lease and blocks capture.
        [[nodiscard]] bool Complete(DatabaseTransactionResult result) noexcept requires IsWriter
        {
            if (!_state || !_possiblySubmitted)
                return false;

            bool const certain = MatchesExecutionCode(result) &&
                (result.IsCommitted() || DefinitivelyDidNotWrite(result.status));
            Release(!certain);
            return certain;
        }

    private:
        friend class InventoryWriteFence;
        friend class RecoveryWrite;

        Lease(std::shared_ptr<State> state, Entry* entry) noexcept : _state(std::move(state)), _entry(entry) {}

        [[nodiscard]] bool MarkSubmitted() noexcept
        {
            if (!_state || _possiblySubmitted)
                return false;
            _possiblySubmitted = true;
            return true;
        }

        void Release(bool uncertain) noexcept
        {
            auto state = std::move(_state);
            if (!state)
                return;

            std::lock_guard lock(state->mutex);
            if constexpr (IsWriter)
            {
                --_entry->writers;
                _entry->uncertain |= uncertain;
            }
            else if (!uncertain)
                _entry->capture = false;

            if (!_entry->writers && !_entry->capture && !_entry->uncertain)
                *_entry = {};
        }

        std::shared_ptr<State> _state;
        Entry* _entry;
        bool _possiblySubmitted = false;
    };

public:
    using Writer = Lease<true>;
    using Capture = Lease<false>;

    enum class RecoverySettlement { NotAwaitingResult, NoWrite, NeedsVerification, Uncertain };

    class RecoveryWrite final
    {
    public:
        RecoveryWrite(RecoveryWrite&&) noexcept = default;

        [[nodiscard]] bool MarkPossiblySubmitted() noexcept { return _capture.MarkSubmitted(); }

        // Only the trusted consumer of the exact task may settle this lease.
        // A commit still holds the GUID for persisted-state verification.
        [[nodiscard]] RecoverySettlement Settle(DatabaseTransactionResult result) noexcept
        {
            if (!_capture._state || !_capture._possiblySubmitted || _settled)
                return RecoverySettlement::NotAwaitingResult;
            _settled = true;

            if (!MatchesExecutionCode(result))
                return RecoverySettlement::Uncertain;
            if (DefinitivelyDidNotWrite(result.status))
            {
                _capture.Release(false);
                return RecoverySettlement::NoWrite;
            }
            if (result.IsCommitted())
                return RecoverySettlement::NeedsVerification;
            return RecoverySettlement::Uncertain;
        }

    private:
        friend class InventoryWriteFence;

        explicit RecoveryWrite(Capture&& capture) noexcept : _capture(std::move(capture)) {}

        Capture _capture;
        bool _settled = false;
    };

    InventoryWriteFence() = default;
    InventoryWriteFence(InventoryWriteFence const&) = delete;
    InventoryWriteFence& operator=(InventoryWriteFence const&) = delete;

    [[nodiscard]] std::optional<Writer> TryTrackWrite(std::uint64_t guid)
    {
        return TryEnter<true>(guid);
    }

    // Holds out registered writers only during read-only capture. No recovery
    // submission or durable-state authority is granted by this reservation.
    [[nodiscard]] std::optional<Capture> TryReserveCapture(std::uint64_t guid)
    {
        return TryEnter<false>(guid);
    }

    // Consumes the exact capture for this fence and GUID. The exclusive entry
    // remains set throughout the move. A refusal leaves the capture with its owner.
    [[nodiscard]] std::optional<RecoveryWrite> TryPromoteCapture(Capture& capture, std::uint64_t guid)
    {
        if (capture._state != _state)
            return std::nullopt;

        std::lock_guard lock(_state->mutex);
        if (capture._entry->guid != guid || !capture._entry->capture)
            return std::nullopt;
        return RecoveryWrite(std::move(capture));
    }

private:
    template<bool IsWriter>
    std::optional<Lease<IsWriter>> TryEnter(std::uint64_t guid)
    {
        if (!guid)
            return std::nullopt;

        std::lock_guard lock(_state->mutex);
        Entry* entry = nullptr;
        Entry* unused = nullptr;
        for (auto& candidate : _state->entries)
        {
            if (candidate.guid == guid)
            {
                entry = &candidate;
                break;
            }
            if (!candidate.guid && !unused)
                unused = &candidate;
        }
        if (!entry)
            entry = unused;
        if (!entry || entry->capture)
            return std::nullopt;

        if constexpr (IsWriter)
        {
            if (entry->writers == std::numeric_limits<std::size_t>::max())
                return std::nullopt;
            ++entry->writers;
        }
        else
        {
            if (entry->writers || entry->uncertain)
                return std::nullopt;
            entry->capture = true;
        }
        entry->guid = guid;
        return Lease<IsWriter>(_state, entry);
    }

    std::shared_ptr<State> const _state = std::make_shared<State>();
};
}
