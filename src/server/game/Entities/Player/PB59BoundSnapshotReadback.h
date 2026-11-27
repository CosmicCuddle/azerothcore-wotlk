#pragma once

#include "PB59SnapshotReadback.h"
#include <type_traits>

namespace PB59::Recovery::ReadbackWire
{
// The only producer of this completion executes a read-only snapshot using
// the item set owned by the moved request. No public raw-row constructor or
// result accessor exists. Connection authenticity and competing-writer
// exclusion remain independent prerequisites; never release a hold here.
template<typename Readback>
class BoundSnapshotReadback final
{
    using Request = typename Readback::Request;
    static_assert(std::is_nothrow_move_constructible_v<Request>);
public:
    BoundSnapshotReadback(BoundSnapshotReadback&&) noexcept = default;
    BoundSnapshotReadback(BoundSnapshotReadback const&) = delete;
    BoundSnapshotReadback& operator=(BoundSnapshotReadback const&) = delete;
    BoundSnapshotReadback& operator=(BoundSnapshotReadback&&) = delete;

    [[nodiscard]] static BoundSnapshotReadback Run(Request request,
        OwnedReadbackConnection connection, unsigned long expectedThread) noexcept
    {
        std::optional<InventoryReadbackRows> rows;
        if (request.Key())
            rows = ExecuteSnapshotReadback(std::move(connection), expectedThread, request.Items());
        return BoundSnapshotReadback(std::move(request), std::move(rows));
    }

    // Serialized holder routes by the exact retained identity. Move the
    // capability out before dispatch, including on a refusal or exception.
    // The holder must outlive this synchronous call; no async lifetime proof.
    template<typename Holder>
    [[nodiscard]] typename Readback::Observation DeliverTo(Holder& holder) &&
    {
        if (!_request.Key())
            return {Readback::Outcome::Refused, std::nullopt};
        auto request = std::move(_request);
        auto rows = std::move(_rows);
        return holder.FinishRetainedReadback(std::move(request), rows);
    }

private:
    BoundSnapshotReadback(Request request, std::optional<InventoryReadbackRows> rows) noexcept
        : _request(std::move(request)), _rows(std::move(rows)) {}
    Request _request;
    std::optional<InventoryReadbackRows> _rows;
};
}
