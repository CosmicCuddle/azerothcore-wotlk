#pragma once

#include <bit>
#include <cstdint>
#include <limits>

namespace PB59Prototype
{
// Test-only schema for a value captured on the sender-owning execution context.
// Eligibility/weapon weighting rules are inputs, not production defaults.
struct EquipmentCaptureEnvelope
{
    std::uint32_t schema = 1;
    std::uint64_t guid = 0;
    std::uint64_t session = 0;
    std::uint64_t equipmentRevision = 0;
    std::uint64_t policyRevision = 0;
    std::uint32_t level = 0;
    std::uint32_t eligibleMask = 0;
    std::uint64_t rawSum = 0;
    std::uint32_t denominator = 0;
    std::uint32_t average = 0;
    bool capabilitiesKnown = false;
    bool dualWield = false;
    bool titanGrip = false;
    bool unresolvedScaling = false;
    bool known = false;
    bool operator==(EquipmentCaptureEnvelope const&) const = default;
};

// 19 Core equipment slots, excluding cosmetic body (3) and tabard (18).
inline constexpr std::uint32_t CombatSlotMask = ((1u << 19) - 1) & ~((1u << 3) | (1u << 18));

constexpr bool ValidEnvelope(EquipmentCaptureEnvelope const& e)
{
    if (e.schema != 1 || !e.known || !e.guid || !e.session || !e.equipmentRevision ||
        !e.policyRevision || !e.level || !e.capabilitiesKnown || e.unresolvedScaling ||
        !e.eligibleMask || (e.eligibleMask & ~CombatSlotMask) || !e.denominator ||
        e.denominator != static_cast<std::uint32_t>(std::popcount(e.eligibleMask)))
        return false;
    if (e.rawSum > std::uint64_t(std::numeric_limits<std::uint32_t>::max()) * e.denominator)
        return false;
    return e.average && e.rawSum / e.denominator == e.average;
}

// An unchanged numeric average is not a matching authority publication.
constexpr bool SameEnvelope(EquipmentCaptureEnvelope const& expected,
    EquipmentCaptureEnvelope const& current)
{
    return ValidEnvelope(expected) && expected == current;
}
}
