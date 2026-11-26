#pragma once

#include "PB59CharacterSaveEvidence.h"
#include "PB59InventoryReadbackComparison.h"
#include <memory>
#include <span>
#include <utility>

namespace PB59::Recovery
{
// Dormant request ownership primitive. A future retained-hold owner must
// create one gate from its own exact committed record under serialization.
// No trusted DB producer, snapshot or release authority is supplied here.
template<std::size_t Capacity, typename Stamp>
class InventoryReadbackRequestGate final
{
    using Store = CharacterSaveEvidence<Capacity>;

public:
    struct Identity
    {
        Operation operation;
        Stamp stamp;
        typename Store::Ticket ticket;
        bool operator==(Identity const&) const = default;
    };

private:
    struct State
    {
        Identity identity;
        ExpectedInventoryWrites expected;
        std::vector<uint64> items;
    };

public:
    class Request final
    {
    public:
        Request(Request&&) noexcept = default;
        Request(Request const&) = delete;
        Request& operator=(Request const&) = delete;
        Request& operator=(Request&&) = delete;

        [[nodiscard]] Identity const* Key() const noexcept
        {
            return _state ? &_state->identity : nullptr;
        }

        [[nodiscard]] std::span<uint64 const> Items() const noexcept
        {
            return _state ? std::span<uint64 const>(_state->items) : std::span<uint64 const>();
        }

    private:
        friend class InventoryReadbackRequestGate;
        explicit Request(std::shared_ptr<State const> state) noexcept : _state(std::move(state)) {}
        std::shared_ptr<State const> _state;
    };

    enum class Outcome { Refused, ReadbackUnavailable, ComparedValues };
    struct Observation
    {
        Outcome outcome;
        std::optional<InventoryValueComparison> comparison;
    };

    InventoryReadbackRequestGate(InventoryReadbackRequestGate const&) = delete;
    InventoryReadbackRequestGate& operator=(InventoryReadbackRequestGate const&) = delete;

    // The caller MUST supply its retained record, not an externally assembled
    // diagnostic copy. This checks consistency, not native record authenticity.
    // A successful commit is required; ambiguous outcomes remain held.
    [[nodiscard]] static std::unique_ptr<InventoryReadbackRequestGate> Open(
        Identity const& key, typename Store::Record const& record,
        ExpectedInventoryWrites const& expected)
    {
        auto const& op = key.operation;
        auto const& id = key.ticket.identity;
        auto const& membership = record.evidence;
        if (!op.Valid() || !key.stamp.revision || !key.ticket.serial ||
            key.ticket.slot >= Capacity || !id.Valid() ||
            id.guid != op.owner || id.player != op.ownerIncarnation ||
            id.session != op.sessionIncarnation || record.ticket != key.ticket ||
            record.state != Store::State::Terminal || !record.result.IsCommitted() ||
            record.result.executionCode != 0 || !membership.Valid() || membership.incomplete ||
            membership.questCount || membership.skillCount || expected.items.empty() ||
            expected.items.size() != membership.itemCount)
            return {};

        // Validate the entire bound write set. With no observed rows, a valid
        // nonempty set reaches Missing; malformed values must not start a query.
        if (CompareInventoryWriteValues(expected, {}).outcome != InventoryValueOutcome::Missing)
            return {};

        std::set<uint64> captured(membership.items.begin(), membership.items.begin() + membership.itemCount);
        if (captured.size() != membership.itemCount)
            return {};
        std::vector<uint64> items;
        items.reserve(expected.items.size());
        for (auto const& item : expected.items)
        {
            auto const* owner = InventoryComparisonDetail::Parameter<uint64>(item.link, 0);
            if (!owner || *owner != op.owner || captured.erase(item.itemGuid) != 1)
                return {};
            items.push_back(item.itemGuid);
        }
        if (!captured.empty())
            return {};

        // Own a value copy, not a mutable alias into the caller's capture.
        return std::unique_ptr<InventoryReadbackRequestGate>(new InventoryReadbackRequestGate(
            std::make_shared<State const>(State{key, expected, std::move(items)})));
    }

    // Serialized owner context only. At most one request per gate; losing a
    // request does not clear the hold or permit automatic retry.
    [[nodiscard]] std::optional<Request> Begin(Identity const& key)
    {
        if (_issued || key != _state->identity)
            return std::nullopt;
        _issued = true;
        return Request(_state);
    }

    // The future query adapter must own both the request and its actual result.
    // A correctly tagged arbitrary row set is NOT proof of snapshot identity.
    // Consume the exact capability even if readback fails or mismatches.
    [[nodiscard]] Observation Finish(Request&& request, std::optional<InventoryReadbackRows> const& rows)
    {
        if (!_issued || _consumed || request._state != _state)
            return {Outcome::Refused, std::nullopt};
        _consumed = true;
        request._state.reset();
        if (!rows)
            return {Outcome::ReadbackUnavailable, std::nullopt};
        return {Outcome::ComparedValues, CompareInventoryWriteValues(_state->expected, *rows)};
    }

private:
    explicit InventoryReadbackRequestGate(std::shared_ptr<State const> state) noexcept
        : _state(std::move(state)) {}

    std::shared_ptr<State const> const _state;
    bool _issued = false;
    bool _consumed = false;
};
}
