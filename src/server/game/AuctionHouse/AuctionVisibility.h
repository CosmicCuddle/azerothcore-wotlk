#pragma once

#include <cstdint>
#include <span>

// Generic value-only auction browse contract. No live provider or worker wiring.
namespace AuctionVisibility
{
using Mask = std::uint64_t;
enum class Mode : std::uint8_t { Disabled, Observe, Enforced };
enum class TagState : std::uint8_t { Unknown, Unrestricted, Controlled };
enum class Reason : std::uint8_t { Allowed, Disabled, InvalidPolicy, InvalidTag, InvalidViewer, OrderedRequirement, CapabilityRequirement };
struct Policy
{
    std::uint64_t generation = 0;
    std::uint32_t domain = 0;
    std::uint32_t maxOrdered = 0;
    Mask allocated = 0;
};
struct Viewer
{
    bool valid = false;
    std::uint64_t generation = 0;
    std::uint32_t domain = 0;
    std::uint32_t ordered = 0;
    Mask granted = 0;
};
struct Tag
{
    TagState state = TagState::Unknown;
    std::uint64_t generation = 0;
    std::uint32_t domain = 0;
    std::uint32_t minimum = 0;
    Mask required = 0;
};
struct Allocation { std::uint32_t provider; Mask bits; };
struct Contribution { std::uint32_t provider; bool valid; std::uint32_t ordered; Mask granted; };
struct Decision { bool visible; bool wouldPermit; Reason reason; };

constexpr bool Valid(Policy const& p) { return p.generation && p.domain; }
constexpr bool Valid(Tag const& t, Policy const& p)
{
    if (!Valid(p) || t.generation != p.generation || t.domain != p.domain ||
        t.minimum > p.maxOrdered || (t.required & ~p.allocated))
        return false;
    return t.state == TagState::Controlled ||
        (t.state == TagState::Unrestricted && !t.minimum && !t.required);
}
constexpr bool Valid(Viewer const& v, Policy const& p)
{
    return Valid(p) && v.valid && v.generation == p.generation && v.domain == p.domain &&
        v.ordered <= p.maxOrdered && !(v.granted & ~p.allocated);
}

// Source-side only. Core owns the allocation table and the single ordered
// authority ID. Workers receive only the returned value, not these spans.
constexpr Viewer BuildViewer(Policy const& p, std::uint32_t orderedProvider,
    std::span<Allocation const> allocations, std::span<Contribution const> contributions)
{
    if (!Valid(p) || !orderedProvider)
        return {};
    Mask assigned = 0;
    bool authorityAllocated = false;
    for (std::size_t i = 0; i < allocations.size(); ++i)
    {
        auto const a = allocations[i];
        if (!a.provider || (a.bits & assigned) || (a.bits & ~p.allocated))
            return {};
        for (std::size_t j = 0; j < i; ++j)
            if (allocations[j].provider == a.provider)
                return {};
        assigned |= a.bits;
        authorityAllocated |= a.provider == orderedProvider;
    }
    if (assigned != p.allocated || !authorityAllocated)
        return {};
    Viewer result{false, p.generation, p.domain, 0, 0};
    for (std::size_t i = 0; i < contributions.size(); ++i)
    {
        auto const c = contributions[i];
        for (std::size_t j = 0; j < i; ++j)
            if (contributions[j].provider == c.provider)
                return {};
        Allocation const* owner = nullptr;
        for (auto const& a : allocations)
            if (a.provider == c.provider)
                owner = &a;
        if (!owner)
            return {};
        if (!c.valid)
            continue; // invalid provider grants nothing
        if ((c.granted & ~owner->bits) || (c.provider != orderedProvider && c.ordered))
            return {};
        if (c.provider == orderedProvider)
        {
            if (c.ordered > p.maxOrdered)
                return {};
            result.ordered = c.ordered;
            result.valid = true;
        }
        result.granted |= c.granted;
    }
    return result.valid ? result : Viewer{};
}

// Restriction contributions can only tighten; unknown absorbs. The caller
// must explicitly classify unrestricted for the current policy generation.
constexpr Tag Combine(Tag const& a, Tag const& b, Policy const& p)
{
    if (!Valid(a, p) || !Valid(b, p))
        return {};
    return {a.state == TagState::Controlled || b.state == TagState::Controlled ? TagState::Controlled : TagState::Unrestricted,
        p.generation, p.domain, a.minimum > b.minimum ? a.minimum : b.minimum, a.required | b.required};
}

constexpr Reason Eligibility(Policy const& p, Viewer const& v, Tag const& t)
{
    if (!Valid(p)) return Reason::InvalidPolicy;
    if (!Valid(t, p)) return Reason::InvalidTag;
    if (t.state == TagState::Unrestricted) return Reason::Allowed;
    if (!Valid(v, p)) return Reason::InvalidViewer;
    if (v.ordered < t.minimum) return Reason::OrderedRequirement;
    if ((v.granted & t.required) != t.required) return Reason::CapabilityRequirement;
    return Reason::Allowed;
}

// Browse only. 'p' is the policy context for the request; PR B must fence
// response emission against the actually active generation, including old/old.
constexpr Decision Evaluate(Mode mode, Policy const& p, Viewer const& v, Tag const& t)
{
    if (mode == Mode::Disabled) return {true, true, Reason::Disabled};
    if (mode != Mode::Observe && mode != Mode::Enforced) return {false, false, Reason::InvalidPolicy};
    auto const reason = Eligibility(p, v, t);
    bool const allowed = reason == Reason::Allowed;
    return {mode == Mode::Observe || allowed, allowed, reason};
}
}
