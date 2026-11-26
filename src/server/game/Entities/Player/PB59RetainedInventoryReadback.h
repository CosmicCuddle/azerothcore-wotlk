#pragma once

#include "PB59InventoryReadbackRequest.h"
#include <new>

namespace PB59::Recovery
{
// One instance per retained completion. The enclosing holder must check the
// exact retained identity/settlement and serialize all calls. This does not
// authenticate query results or grant authority to release an inventory hold.
template<std::size_t Capacity, typename Stamp>
class RetainedInventoryReadback final
{
    using Gate = InventoryReadbackRequestGate<Capacity, Stamp>;
    using Store = CharacterSaveEvidence<Capacity>;

public:
    using Identity = typename Gate::Identity;
    using Request = typename Gate::Request;
    using Outcome = typename Gate::Outcome;
    using Observation = typename Gate::Observation;

    [[nodiscard]] std::optional<Request> Begin(Identity const& key, typename Store::Record const& record,
        ExpectedInventoryWrites const& expected)
    {
        if (_attempted)
            return std::nullopt;
        // Refusal, allocation failure, and a lost request never re-arm the
        // retained completion. An explicit future recovery policy is required.
        _attempted = true;
        try
        {
            _gate = Gate::Open(key, record, expected);
            return _gate ? _gate->Begin(key) : std::nullopt;
        }
        catch (std::bad_alloc const&)
        {
            return std::nullopt;
        }
    }

    [[nodiscard]] Observation Finish(Request&& request, std::optional<InventoryReadbackRows> const& rows)
    {
        if (!_gate)
            return {Outcome::Refused, std::nullopt};
        try
        {
            return _gate->Finish(std::move(request), rows);
        }
        catch (std::bad_alloc const&)
        {
            return {Outcome::ReadbackUnavailable, std::nullopt};
        }
    }

private:
    bool _attempted = false;
    std::unique_ptr<Gate> _gate;
};
}
