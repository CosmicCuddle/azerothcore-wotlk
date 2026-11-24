#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace PB59::Recovery
{
constexpr std::size_t MaximumItemUpdateFields = 256;
constexpr std::size_t MaximumOwnerUpdateFields = 2048;

template<std::size_t Capacity>
struct UpdateMaskFacts
{
    static_assert(Capacity > 0 && Capacity <= MaximumOwnerUpdateFields);
    std::uint32_t fieldCount = 0;
    std::array<std::uint32_t, (Capacity + 31) / 32> words{};
    bool operator==(UpdateMaskFacts const&) const = default;
};

// Value observation only: no serialization, Clear/SetBit, update registration
// or restoration capability. Invalid input leaves the previous output intact.
template<std::size_t Capacity, typename NativeMask>
bool CopyUpdateMask(NativeMask const& mask, std::uint32_t fields, UpdateMaskFacts<Capacity>& output)
{
    if (!fields || fields > Capacity || mask.GetCount() != fields)
        return false;
    UpdateMaskFacts<Capacity> value;
    value.fieldCount = fields;
    for (std::uint32_t index = 0; index < fields; ++index)
        if (mask.GetBit(index))
            value.words[index / 32] |= std::uint32_t(1) << (index % 32);
    output = value;
    return true;
}

template<std::size_t Capacity>
struct ObjectUpdateFacts
{
    UpdateMaskFacts<Capacity> mask;
    bool inWorld = false, objectPending = false, mapRegistered = false;
    bool operator==(ObjectUpdateFacts const&) const = default;
};
using ItemUpdateMask = UpdateMaskFacts<MaximumItemUpdateFields>;
using OwnerUpdateFacts = ObjectUpdateFacts<MaximumOwnerUpdateFields>;
}
