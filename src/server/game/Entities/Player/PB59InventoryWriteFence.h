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
            if (!_state || _possiblySubmitted)
                return false;
            _possiblySubmitted = true;
            return true;
        }

        // Reports whether this writer is cleared for capture admission. An
        // uncertain or inconsistent outcome consumes the lease and blocks capture.
        [[nodiscard]] bool Complete(DatabaseTransactionResult result) noexcept requires IsWriter
        {
            if (!_state || !_possiblySubmitted)
                return false;

            bool const expectedStatus = DatabaseTransactionResult::FromExecutionCode(result.executionCode).status ==
                result.status;
            bool const certain = expectedStatus && (result.IsCommitted() ||
                result.status == DatabaseTransactionStatus::Empty ||
                result.status == DatabaseTransactionStatus::NotStarted ||
                result.status == DatabaseTransactionStatus::NotEnqueued ||
                result.status == DatabaseTransactionStatus::CancelledBeforeExecution);
            Release(!certain);
            return certain;
        }

    private:
        friend class InventoryWriteFence;

        Lease(std::shared_ptr<State> state, Entry* entry) noexcept : _state(std::move(state)), _entry(entry) {}

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
            else
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
