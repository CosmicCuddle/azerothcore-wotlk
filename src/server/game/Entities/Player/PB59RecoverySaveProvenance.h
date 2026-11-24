#pragma once

#include "PB59CharacterSaveEvidence.h"
#include "PB59RecoveryEvidence.h"
#include <span>

namespace PB59::Recovery
{
enum class SaveObservation { Unavailable, Invalid, IdentityMismatch, TicketMismatch, Reserved, Pending, Terminal };
struct SaveProvenance
{
    SaveObservation observation = SaveObservation::Unavailable;
    PB59::SaveIdentity identity;
    std::uint64_t observerSerial = 0;
    bool captureComplete = false, allOriginalGuidsListed = false;
    bool executionKnown = false;
    DatabaseTransactionResult execution = DatabaseTransactionResult::FromExecutionCode(
        DatabaseTransactionResult::AmbiguousCode);
    // Existing native save records have neither recovery ticket nor revision,
    // full instance/slot before-images or storage durability acknowledgement.
    // This observation can never stand in for Recovery::PersistenceEvidence.
};

template<std::size_t Capacity>
constexpr SaveProvenance ObserveSave(typename PB59::CharacterSaveEvidence<Capacity>::Record const& record,
    typename PB59::CharacterSaveEvidence<Capacity>::Ticket const& expectedSave,
    Operation const& expected, std::span<std::uint64_t const> originalGuids) noexcept
{
    using Store = PB59::CharacterSaveEvidence<Capacity>;
    SaveProvenance result;
    result.observation = SaveObservation::Invalid;
    result.identity = record.ticket.identity;
    result.observerSerial = record.ticket.serial;
    auto const& id = result.identity;
    if (!expected.Valid() || !id.guid || !id.player || !id.session || !id.sequence ||
        !record.ticket.serial || record.ticket.slot >= Capacity || record.state <= Store::State::Free ||
        record.state > Store::State::Terminal || originalGuids.empty() || originalGuids.size() > MaximumOriginals)
        return result;
    if (id.guid != expected.owner || id.player != expected.ownerIncarnation ||
        id.session != expected.sessionIncarnation)
    {
        result.observation = SaveObservation::IdentityMismatch;
        return result;
    }
    if (record.ticket != expectedSave)
    {
        result.observation = SaveObservation::TicketMismatch;
        return result;
    }
    for (std::size_t index = 0; index < originalGuids.size(); ++index)
        if (!originalGuids[index] || std::find(originalGuids.begin(), originalGuids.begin() + index,
                originalGuids[index]) != originalGuids.begin() + index)
            return result;
    auto const& evidence = record.evidence;
    if (evidence.itemCount > PB59::SaveEvidence::Limit || evidence.questCount > PB59::SaveEvidence::Limit ||
        evidence.skillCount > PB59::SaveEvidence::Limit)
        return result;
    auto valid = [](auto const& values, std::size_t count)
    {
        for (std::size_t index = 0; index < count; ++index)
            if (!values[index] ||
                std::find(values.begin(), values.begin() + index, values[index]) != values.begin() + index)
                return false;
        return true;
    };
    if (!valid(evidence.items, evidence.itemCount) || !valid(evidence.quests, evidence.questCount) ||
        !valid(evidence.skills, evidence.skillCount))
        return result;
    if (record.state == Store::State::Reserved)
    {
        result.observation = SaveObservation::Reserved;
        return result; // not sealed: do not claim capture completeness or membership
    }
    result.captureComplete = !evidence.incomplete;
    result.allOriginalGuidsListed = true;
    for (auto guid : originalGuids)
        if (std::find(evidence.items.begin(), evidence.items.begin() + evidence.itemCount, guid) ==
            evidence.items.begin() + evidence.itemCount)
            result.allOriginalGuidsListed = false;
    if (record.state == Store::State::Pending)
        result.observation = SaveObservation::Pending;
    else
    {
        if (DatabaseTransactionResult::FromExecutionCode(record.result.executionCode).status != record.result.status)
            return result;
        result.observation = SaveObservation::Terminal;
        result.execution = record.result;
        result.executionKnown = true;
    }
    return result;
}

template<std::size_t Capacity>
constexpr SaveProvenance ObserveSave(
    std::optional<typename PB59::CharacterSaveEvidence<Capacity>::Record> const& record,
    typename PB59::CharacterSaveEvidence<Capacity>::Ticket const& expectedSave,
    Operation const& expected, std::span<std::uint64_t const> originalGuids) noexcept
{
    return record ? ObserveSave<Capacity>(*record, expectedSave, expected, originalGuids) : SaveProvenance{};
}
}
