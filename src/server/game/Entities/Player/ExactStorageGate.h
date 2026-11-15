#pragma once
#include <cstdint>

namespace PB59
{
// Private insertion seam: the caller must own a live, detached item and the
// player's serialized inventory context. This gate does not establish ownership.
struct ExactStorageReservation
{
    std::uint64_t itemGuid;
    std::uint64_t bagGuid; // zero only for the backpack
    std::uint16_t position;
    std::uint32_t count;
};
struct ExactStorageObservation
{
    std::uint64_t itemGuid;
    std::uint64_t bagGuid;
    std::uint16_t position;
    std::uint32_t count;
    bool detached;
    bool owned;
    bool transferable;
    bool validPosition;
    bool occupied;
    bool clone;
};
constexpr bool AcceptExactStorage(ExactStorageReservation const& expected,
    ExactStorageObservation const& actual)
{
    return expected.itemGuid && expected.count && expected.position &&
        expected.itemGuid == actual.itemGuid && expected.bagGuid == actual.bagGuid &&
        expected.position == actual.position && expected.count == actual.count &&
        actual.detached && actual.owned && actual.transferable && actual.validPosition &&
        !actual.occupied && !actual.clone;
}
// Equipment restoration uses only the base inventory and an explicitly
// validated combat slot. The native caller must preflight CanEquipItem and
// prove detached-item ownership; this gate is not a lifetime proof.
constexpr bool AcceptExactEquip(ExactStorageReservation const& expected,
    ExactStorageObservation const& actual)
{
    return expected.bagGuid == 0 && actual.bagGuid == 0 &&
        AcceptExactStorage(expected, actual);
}
}
