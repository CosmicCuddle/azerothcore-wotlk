#pragma once

#include "PB59InventoryBatchPlan.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <type_traits>

namespace PB59::Recovery
{
// Dormant inspection model. No native pointer, IO, reset, replay or release API.
// Native facts must be collected in their serialized context before copying.
constexpr std::size_t MaximumOriginals = 19;
constexpr std::size_t MaximumTextBytes = 512;

struct Operation
{
    std::uint64_t owner = 0, ownerIncarnation = 0, sessionIncarnation = 0, ticket = 0;
    bool operator==(Operation const&) const = default;
    bool OwnerValid() const noexcept { return owner && ownerIncarnation && sessionIncarnation; }
    bool Valid() const noexcept { return OwnerValid() && ticket; }
};

struct ItemFacts
{
    std::uint64_t guid = 0, owner = 0, creator = 0, giftCreator = 0;
    std::uint32_t entry = 0, count = 0, duration = 0, flags = 0;
    std::uint32_t durability = 0, maxDurability = 0, playedTime = 0, suffix = 0;
    std::int32_t randomProperty = 0;
    std::array<std::int32_t, 5> spellCharges{};
    std::array<EnchantFacts, 12> enchants{};
    std::uint16_t textSize = 0;
    std::array<char, MaximumTextBytes> text{};
    bool operator==(ItemFacts const&) const = default;
};

struct SlotFacts
{
    std::uint64_t owner = 0, bag = 0;
    std::uint16_t position = 0;
    bool hasItem = false;
    ItemFacts item;
    bool operator==(SlotFacts const&) const = default;
};

// No truncation: an oversized text field is unavailable evidence. Leave output
// unchanged on failure, so a rejected copy cannot masquerade as an empty slot.
inline bool CopySlot(InventorySlotFacts const& source, SlotFacts& output) noexcept
{
    if (source.item && source.item->text.size() > MaximumTextBytes)
        return false;
    SlotFacts next;
    next.owner = source.owner;
    next.bag = source.bag;
    next.position = source.position;
    next.hasItem = source.item.has_value();
    if (source.item)
    {
        auto const& item = *source.item;
        next.item = {item.guid, item.owner, item.creator, item.giftCreator,
            item.entry, item.count, item.duration, item.flags, item.durability,
            item.maxDurability, item.playedTime, item.suffix, item.randomProperty,
            item.spellCharges, item.enchants, static_cast<std::uint16_t>(item.text.size()), {}};
        std::copy(item.text.begin(), item.text.end(), next.item.text.begin());
    }
    output = next;
    return true;
}

enum class ProofState : std::uint8_t { Unknown, Verified, Uncertain };
enum class InstanceState : std::uint8_t { Unknown, ValueOnly, ExactNativeInstance };

struct Original
{
    SlotFacts before;
    bool observed = false;
    SlotFacts now;
    // A copied claim from native observation, not a portable pointer/lifetime
    // proof. Inspection alone can never turn this claim into write authority.
    InstanceState instance = InstanceState::Unknown;
    bool operator==(Original const&) const = default;
};

struct Evidence
{
    std::uint32_t version = 1;
    Operation operation;
    std::uint64_t revision = 0;
    InventoryBatchOutcome outcome = InventoryBatchOutcome::RecoveryRequired;
    InventoryBatchFailure failure = InventoryBatchFailure::Context;
    std::uint8_t originalCount = 0, reportedCount = 0;
    std::array<Original, MaximumOriginals> originals{};
    std::array<std::uint64_t, MaximumOriginals> reportedGuids{};
    bool ownerPresent = false, sessionPresent = false;
    std::uint64_t observedOwnerIncarnation = 0, observedSessionIncarnation = 0;
    bool pendingSave = false, pendingLogout = false;
    ProofState effects = ProofState::Unknown, queues = ProofState::Unknown;
    bool operator==(Evidence const&) const = default;
};

enum class Issue : std::uint32_t
{
    InvalidRecord = 1, StaleOperation = 2, OwnerUnavailable = 4, SessionUnavailable = 8,
    MissingFacts = 16, CustodyUnproven = 32, EffectsUnproven = 64, QueuesUnproven = 128,
    PendingSave = 256, PendingLogout = 512
};
enum class InspectionStatus
{
    InvalidEvidence, StaleOperation, OwnerUnavailable, SessionUnavailable, NeedsFacts,
    NeedsCustodyProof, NeedsEffectsProof, NeedsQueueProof, HeldForPendingIntents, InMemoryCandidate
};

struct Inspection
{
    InspectionStatus status = InspectionStatus::InvalidEvidence;
    std::uint32_t issues = 0;
    std::array<std::uint64_t, MaximumOriginals> unprovenGuids{};
    std::uint8_t unprovenCount = 0;
    bool Has(Issue issue) const noexcept { return (issues & static_cast<std::uint32_t>(issue)) != 0; }
};

inline bool ValidSlot(SlotFacts const& slot) noexcept
{
    if (!slot.owner || !slot.position)
        return false;
    if (!slot.hasItem)
        return slot.item == ItemFacts{};
    auto const& item = slot.item;
    if (!item.guid || item.owner != slot.owner || !item.entry || !item.count || item.textSize > MaximumTextBytes)
        return false;
    // Require a canonical tail: changing unused bytes is not another spelling
    // of the same evidence and cannot silently alter equality decisions.
    return std::all_of(item.text.begin() + item.textSize, item.text.end(), [](char value) { return value == 0; });
}

inline Inspection Inspect(Evidence const& evidence, Operation const& expected) noexcept
{
    Inspection result;
    auto add = [&](Issue issue) { result.issues |= static_cast<std::uint32_t>(issue); };
    // Scalar context/intent facts remain diagnosable even when the record's
    // structure is malformed. Do not inspect arrays until bounds are checked.
    if (!expected.Valid() || evidence.operation != expected)
        add(Issue::StaleOperation);
    if (!evidence.ownerPresent)
        add(Issue::OwnerUnavailable);
    else if (evidence.observedOwnerIncarnation != evidence.operation.ownerIncarnation)
        add(Issue::StaleOperation);
    if (!evidence.sessionPresent)
        add(Issue::SessionUnavailable);
    else if (evidence.observedSessionIncarnation != evidence.operation.sessionIncarnation)
        add(Issue::StaleOperation);
    if (evidence.pendingSave)
        add(Issue::PendingSave);
    if (evidence.pendingLogout)
        add(Issue::PendingLogout);
    if (evidence.effects != ProofState::Verified)
        add(Issue::EffectsUnproven);
    if (evidence.queues != ProofState::Verified)
        add(Issue::QueuesUnproven);
    if (evidence.version != 1 || !evidence.operation.Valid() || !evidence.revision ||
        evidence.outcome != InventoryBatchOutcome::RecoveryRequired || !evidence.originalCount ||
        evidence.failure <= InventoryBatchFailure::None || evidence.failure > InventoryBatchFailure::Restoration ||
        evidence.originalCount > MaximumOriginals || evidence.reportedCount > MaximumOriginals ||
        evidence.effects > ProofState::Uncertain || evidence.queues > ProofState::Uncertain)
    {
        add(Issue::InvalidRecord);
        return result;
    }
    for (std::size_t index = 0; index < evidence.originalCount; ++index)
    {
        auto const& original = evidence.originals[index];
        if (!ValidSlot(original.before) || !original.before.hasItem ||
            original.before.owner != evidence.operation.owner || original.instance > InstanceState::ExactNativeInstance)
            add(Issue::InvalidRecord);
        for (std::size_t prior = 0; prior < index; ++prior)
        {
            auto const& other = evidence.originals[prior];
            if (original.before.item.guid == other.before.item.guid ||
                original.before.position == other.before.position ||
                (original.observed && other.observed && original.now.hasItem && other.now.hasItem &&
                    (original.now.item.guid == other.now.item.guid || original.now.position == other.now.position)))
                add(Issue::InvalidRecord);
        }
        if (!original.observed)
            add(Issue::MissingFacts);
        else if (!ValidSlot(original.now) || original.now.owner != evidence.operation.owner)
            add(Issue::InvalidRecord);
        if (!original.observed || original.instance != InstanceState::ExactNativeInstance ||
            original.now != original.before)
        {
            add(Issue::CustodyUnproven);
            result.unprovenGuids[result.unprovenCount++] = original.before.item.guid;
        }
    }
    for (std::size_t index = 0; index < evidence.reportedCount; ++index)
    {
        auto const guid = evidence.reportedGuids[index];
        bool known = false;
        for (std::size_t item = 0; item < evidence.originalCount; ++item)
            known = known || evidence.originals[item].before.item.guid == guid;
        if (!guid || !known || std::find(evidence.reportedGuids.begin(), evidence.reportedGuids.begin() + index,
                guid) != evidence.reportedGuids.begin() + index)
            add(Issue::InvalidRecord);
    }
    // Explicit precedence, independent of array order. Keep every issue bit so
    // e.g. a lost owner does not hide the separately retained logout intent.
    if (result.Has(Issue::InvalidRecord))
        result.status = InspectionStatus::InvalidEvidence;
    else if (result.Has(Issue::StaleOperation))
        result.status = InspectionStatus::StaleOperation;
    else if (result.Has(Issue::OwnerUnavailable))
        result.status = InspectionStatus::OwnerUnavailable;
    else if (result.Has(Issue::SessionUnavailable))
        result.status = InspectionStatus::SessionUnavailable;
    else if (result.Has(Issue::MissingFacts))
        result.status = InspectionStatus::NeedsFacts;
    else if (result.Has(Issue::CustodyUnproven))
        result.status = InspectionStatus::NeedsCustodyProof;
    else if (result.Has(Issue::EffectsUnproven))
        result.status = InspectionStatus::NeedsEffectsProof;
    else if (result.Has(Issue::QueuesUnproven))
        result.status = InspectionStatus::NeedsQueueProof;
    else if (result.Has(Issue::PendingSave) || result.Has(Issue::PendingLogout))
        result.status = InspectionStatus::HeldForPendingIntents;
    else
        result.status = InspectionStatus::InMemoryCandidate;
    return result;
}

static_assert(std::is_trivially_copyable_v<Evidence>);
static_assert(sizeof(Evidence) <= 48 * 1024);
}
