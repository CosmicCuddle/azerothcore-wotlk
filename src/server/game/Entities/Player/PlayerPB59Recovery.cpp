/*
 * This file is part of the AzerothCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "Player.h"
#include "Map.h"

std::unique_ptr<PB59::Recovery::NativeCollection> Player::PreparePB59RecoveryCollection(
    Player const& owner, std::span<PB59::InventoryBatchMove const> moves)
{
    using namespace PB59::Recovery;
    // Caller already owns the serialized native context. Prepare before scope
    // admission: refusal/allocation failure cannot strand a native mutation.
    try
    {
        auto const stamp = owner.CapturePB59InventoryStamp();
        auto const* session = owner.GetSession();
        if (!stamp || !stamp->revision || !session || owner.m_itemUpdateQueue.size() > MaximumQueueObservations)
            return {};
        auto const guid = owner.GetGUID().GetRawValue();
        auto const incarnation = owner.GetPB59OwnerIncarnation();
        auto const sessionIncarnation = session->GetPB59SessionIncarnation();
        if (!owner.CheckPB59InventoryBatchPlan(guid, incarnation, sessionIncarnation, *stamp, moves))
            return {};
        auto buffer = std::unique_ptr<NativeCollection>(new NativeCollection);
        buffer->_thread = std::this_thread::get_id();
        buffer->_owner = &owner;
        buffer->_map = owner.GetMap();
        if (!buffer->_map)
            return {};
        buffer->_preparedStamp = *stamp;
        auto& values = buffer->_values;
        values.evidence.operation = {guid, incarnation, sessionIncarnation, 0}; // ticket only comes from Bind
        values.evidence.revision = stamp->revision;
        values.evidence.originalCount = static_cast<std::uint8_t>(moves.size());
        values.summary.preparedQueueEntries = owner.m_itemUpdateQueue.size();
        for (std::size_t index = 0; index < values.summary.preparedQueueEntries; ++index)
        {
            auto& entry = buffer->_queueBefore[index];
            entry.item = owner.m_itemUpdateQueue[index];
            if (!entry.item)
                continue; // a native queue tombstone is a position, not a missing capture
            entry.value = {entry.item->GetGUID().GetRawValue(), entry.item->GetOwnerGUID().GetRawValue(),
                entry.item->uQueuePos, true};
            if (!entry.value.guid || entry.value.owner != guid ||
                entry.value.position != static_cast<std::int32_t>(index))
                return {};
            for (std::size_t previous = 0; previous < index; ++previous)
                if (buffer->_queueBefore[previous].value.present &&
                    buffer->_queueBefore[previous].value.guid == entry.value.guid)
                    return {};
        }
        values.summary.queueBeforeObserved = true;
        for (std::size_t index = 0; index < moves.size(); ++index)
        {
            if (!CopySlot(moves[index].source, values.evidence.originals[index].before) ||
                !CopySlot(moves[index].destination, values.destinations[index].before))
                return {};
            buffer->_originals[index] = owner.GetItemByPos(moves[index].source.position);
            if (!buffer->_originals[index])
                return {};
        }
        return buffer;
    }
    catch (...)
    {
        return {};
    }
}

PB59::Recovery::CollectionStatus Player::BindPB59RecoveryCollection(PB59::Recovery::NativeCollection& buffer,
    PB59::TransactionAdmission::MutationScope const& scope)
{
    using namespace PB59::Recovery;
    using Access = PB59::TransactionAdmission::Access;
    if (std::this_thread::get_id() != buffer._thread)
        return CollectionStatus::WrongThread; // no scope/actor access or shared-buffer mutation on a foreign thread
    if (buffer._phase != NativeCollection::Phase::Prepared)
        return CollectionStatus::WrongPhase;
    auto& output = buffer._values;
    auto& evidence = output.evidence;
    buffer._phase = NativeCollection::Phase::Terminal; // failed binding is not reusable
    auto alive = [&] { return scope.CanAccess(evidence.operation.ownerIncarnation, Access::Forward); };
    if (!alive())
        return output.summary.status = CollectionStatus::WrongScope;
    // This real admitted scope is the only ticket source. No PB identifier,
    // save sequence or caller-supplied integer can substitute for it.
    evidence.operation.ticket = scope.GetTicket();
    output.summary.ticketBound = true;
    auto const* owner = buffer._owner;
    auto read = [&](auto&& getter)
    {
        if (!alive())
            return false;
        getter();
        return alive();
    };
    try
    {
        std::uint64_t guid = 0;
        WorldSession const* session = nullptr;
        Player const* bound = nullptr;
        std::uint64_t incarnation = 0;
        bool quarantined = false, loading = true, logout = true, inWorld = false;
        Map const* map = nullptr;
        std::optional<PB59::Stamp> stamp;
        if (!read([&] { guid = owner->GetGUID().GetRawValue(); }) || guid != evidence.operation.owner ||
            !read([&] { session = owner->GetSession(); }) || !session ||
            !read([&] { bound = session->GetPlayer(); }) || bound != owner ||
            !read([&] { incarnation = session->GetPB59SessionIncarnation(); }) ||
            incarnation != evidence.operation.sessionIncarnation ||
            !read([&] { loading = session->PlayerLoading(); }) || loading ||
            !read([&] { logout = session->PlayerLogout(); }) || logout ||
            !read([&] { inWorld = owner->IsInWorld(); }) || !inWorld ||
            !read([&] { map = owner->GetMap(); }) || map != buffer._map ||
            !read([&] { quarantined = owner->_pb59Equipment.InventoryQuarantined(); }) || !quarantined ||
            !PB59::InventoryQuarantine::Contains(nullptr, evidence.operation.owner) ||
            !read([&] { stamp = owner->_pb59Equipment.Capture(); }) || !stamp ||
            stamp->session != buffer._preparedStamp.session || stamp->revision < buffer._preparedStamp.revision)
            return output.summary.status = alive() ? CollectionStatus::ContextUnavailable :
                CollectionStatus::OwnerUnavailable;
        for (std::size_t index = 0; index < evidence.originalCount; ++index)
        {
            std::optional<PB59::InventorySlotFacts> native;
            SlotFacts observed;
            Item const* item = nullptr;
            if (!read([&] { native = owner->CapturePB59InventorySlot(evidence.originals[index].before.position); }) ||
                !native || !CopySlot(*native, observed) || observed != evidence.originals[index].before ||
                !read([&] { item = owner->GetItemByPos(observed.position); }) || item != buffer._originals[index] ||
                !read([&] { native = owner->CapturePB59InventorySlot(output.destinations[index].before.position); }) ||
                !native || !CopySlot(*native, observed) || observed != output.destinations[index].before)
                return output.summary.status = alive() ? CollectionStatus::ContextUnavailable :
                    CollectionStatus::OwnerUnavailable;
        }
        // The prepared image must still be exact at admission. Do not bind a
        // stale image to a new native ticket, even if the planned slots match.
        std::size_t queueSize = 0;
        if (!read([&] { queueSize = owner->m_itemUpdateQueue.size(); }) ||
            queueSize != output.summary.preparedQueueEntries)
            return output.summary.status = alive() ? CollectionStatus::ContextUnavailable :
                CollectionStatus::OwnerUnavailable;
        for (std::size_t index = 0; index < queueSize; ++index)
        {
            Item const* item = nullptr;
            QueueEntry entry;
            if (!read([&] { item = owner->m_itemUpdateQueue[index]; }) || item != buffer._queueBefore[index].item)
                return output.summary.status = alive() ? CollectionStatus::ContextUnavailable :
                    CollectionStatus::OwnerUnavailable;
            entry.present = item != nullptr;
            if (item && (!read([&] { entry.guid = item->GetGUID().GetRawValue(); }) ||
                !read([&] { entry.owner = item->GetOwnerGUID().GetRawValue(); }) ||
                !read([&] { entry.position = item->uQueuePos; })))
                return output.summary.status = CollectionStatus::OwnerUnavailable;
            if (entry != buffer._queueBefore[index].value)
                return output.summary.status = CollectionStatus::ContextUnavailable;
        }
        auto const boundStamp = *stamp;
        if (!read([&] { stamp = owner->_pb59Equipment.Capture(); }) || !stamp || *stamp != boundStamp)
            return output.summary.status = alive() ? CollectionStatus::ContextUnavailable :
                CollectionStatus::OwnerUnavailable;
        buffer._boundStamp = *stamp;
        evidence.revision = stamp->revision;
        output.summary.revisionObserved = true;
        buffer._phase = NativeCollection::Phase::Bound;
        return output.summary.status = CollectionStatus::Bound;
    }
    catch (...)
    {
        return output.summary.status = alive() ? CollectionStatus::ContextUnavailable :
            CollectionStatus::OwnerUnavailable;
    }
}

PB59::Recovery::CollectionStatus Player::CollectPB59RecoveryEvidence(PB59::Recovery::NativeCollection& buffer,
    PB59::TransactionAdmission::MutationScope const& scope, PB59::InventoryBatchResult const& failure)
{
    using namespace PB59::Recovery;
    using Access = PB59::TransactionAdmission::Access;
    if (std::this_thread::get_id() != buffer._thread)
        return CollectionStatus::WrongThread;
    if (buffer._phase != NativeCollection::Phase::Bound)
        return CollectionStatus::WrongPhase;
    buffer._phase = NativeCollection::Phase::Terminal; // preserve first observation, never silently refresh it
    auto& output = buffer._values;
    auto& evidence = output.evidence;
    output.summary.revisionObserved = false;
    evidence.revision = 0;
    evidence.outcome = failure.outcome;
    evidence.failure = failure.failure;
    evidence.reportedCount = failure.recoveryCount;
    if (failure.recoveryCount <= MaximumOriginals)
        std::copy_n(failure.recoveryGuids.begin(), failure.recoveryCount, evidence.reportedGuids.begin());
    if (failure.outcome != PB59::InventoryBatchOutcome::RecoveryRequired ||
        failure.failure <= PB59::InventoryBatchFailure::None ||
        failure.failure > PB59::InventoryBatchFailure::Restoration || failure.recoveryCount > MaximumOriginals)
        return output.summary.status = CollectionStatus::InvalidInput;
    auto alive = [&]
    {
        return scope.GetTicket() == evidence.operation.ticket &&
            (scope.CanAccess(evidence.operation.ownerIncarnation, Access::Forward) ||
                scope.CanAccess(evidence.operation.ownerIncarnation, Access::Recovery));
    };
    // Static entry: no call through a possibly retired Player is needed to
    // discover a dead sentinel, and no native pointer is resolved on failure.
    auto const* owner = buffer._owner;
    auto read = [&](auto&& getter)
    {
        if (!alive())
            return false;
        getter();
        return alive();
    };
    auto unavailable = [&]
    {
        evidence.ownerPresent = evidence.sessionPresent = false;
        return output.summary.status = CollectionStatus::OwnerUnavailable;
    };
    if (!alive())
        return unavailable();
    try
    {
        // Pending intent is observed before ordinary context refusal, but never
        // by dereferencing a ledger whose retained scope is no longer live.
        if (!read([&] { evidence.pendingSave = owner->_pb59Transaction.PendingSave(); }) ||
            !read([&] { evidence.pendingLogout = owner->_pb59Transaction.PendingLogout(); }))
            return unavailable();
        output.summary.intentsObserved = true;
        if (!read([&] { evidence.observedOwnerIncarnation = owner->GetPB59OwnerIncarnation(); }))
            return unavailable();
        std::uint64_t guid = 0;
        if (!read([&] { guid = owner->GetGUID().GetRawValue(); }))
            return unavailable();
        if (guid != evidence.operation.owner)
            return output.summary.status = CollectionStatus::ContextUnavailable;
        evidence.ownerPresent = true;
        WorldSession const* session = nullptr;
        Player const* bound = nullptr;
        if (!read([&] { session = owner->GetSession(); }))
            return unavailable();
        if (!session)
            return output.summary.status = CollectionStatus::ContextUnavailable;
        if (!read([&] { evidence.observedSessionIncarnation = session->GetPB59SessionIncarnation(); }) ||
            !read([&] { bound = session->GetPlayer(); }))
            return unavailable();
        evidence.sessionPresent = bound == owner;
        if (!evidence.sessionPresent || evidence.observedSessionIncarnation != evidence.operation.sessionIncarnation)
            return output.summary.status = CollectionStatus::ContextUnavailable;
        bool loading = true, logout = true, inWorld = false, quarantined = false;
        Map const* map = nullptr;
        std::optional<PB59::Stamp> stamp;
        if (!read([&] { loading = session->PlayerLoading(); }) ||
            !read([&] { logout = session->PlayerLogout(); }) ||
            !read([&] { inWorld = owner->IsInWorld(); }) ||
            !read([&] { map = owner->GetMap(); }) ||
            !read([&] { quarantined = owner->_pb59Equipment.InventoryQuarantined(); }) ||
            !read([&] { stamp = owner->_pb59Equipment.Capture(); }))
            return unavailable();
        if (!evidence.sessionPresent || loading || logout || !inWorld || map != buffer._map || !quarantined ||
            !PB59::InventoryQuarantine::Contains(nullptr, evidence.operation.owner) ||
            evidence.observedOwnerIncarnation != evidence.operation.ownerIncarnation ||
            evidence.observedSessionIncarnation != evidence.operation.sessionIncarnation || !stamp ||
            stamp->session != buffer._boundStamp.session || stamp->revision < buffer._boundStamp.revision)
        {
            evidence.revision = 0; // current native revision is unavailable/stale, not an invented generation
            return output.summary.status = CollectionStatus::ContextUnavailable;
        }
        evidence.revision = stamp->revision;
        output.summary.revisionObserved = true;
        bool complete = true;
        for (std::size_t index = 0; index < evidence.originalCount; ++index)
        {
            auto& original = evidence.originals[index];
            std::optional<PB59::InventorySlotFacts> observed;
            if (!read([&] { observed = owner->CapturePB59InventorySlot(original.before.position); }))
                return unavailable();
            if (observed && CopySlot(*observed, original.now))
            {
                original.observed = true;
                if (original.now.hasItem && original.now.item.guid == original.before.item.guid)
                {
                    Item const* item = nullptr;
                    if (!read([&] { item = owner->GetItemByPos(original.before.position); }))
                        return unavailable();
                    original.instance = item == buffer._originals[index] ? InstanceState::ExactNativeInstance :
                        InstanceState::ValueOnly;
                }
            }
            else
                complete = false;
            auto& destination = output.destinations[index];
            if (!read([&] { observed = owner->CapturePB59InventorySlot(destination.before.position); }))
                return unavailable();
            if (observed && CopySlot(*observed, destination.now))
                destination.observed = true;
            else
                complete = false;
        }
        static_assert(EQUIPMENT_SLOT_END <= 32);
        for (std::size_t slot = 0; slot < EQUIPMENT_SLOT_END; ++slot)
        {
            PB59::OriginalCombatEffects::Phase phase;
            if (!read([&] { phase = owner->_pb59OriginalCombatEffects[slot].State(); }))
                return unavailable();
            if (phase != PB59::OriginalCombatEffects::Phase::Fresh)
                output.summary.nonFreshJournalMask |= std::uint32_t(1) << slot;
            if (phase == PB59::OriginalCombatEffects::Phase::Uncertain)
                output.summary.uncertainJournalMask |= std::uint32_t(1) << slot;
        }
        output.summary.journalsObserved = true;
        evidence.effects = output.summary.nonFreshJournalMask ? ProofState::Uncertain : ProofState::Unknown;
        if (!read([&] { output.summary.observedQueueEntries = owner->m_itemUpdateQueue.size(); }))
            return unavailable();
        output.summary.queueObserved = true;
        output.summary.queueConsistent = output.summary.observedQueueEntries <= MaximumQueueObservations;
        if (output.summary.queueConsistent)
        {
            auto const count = std::max(output.summary.preparedQueueEntries, output.summary.observedQueueEntries);
            for (std::size_t index = 0; index < count; ++index)
            {
                Item const* item = nullptr;
                QueueEntry now;
                if (index < output.summary.observedQueueEntries &&
                    !read([&] { item = owner->m_itemUpdateQueue[index]; }))
                    return unavailable();
                now.present = item != nullptr;
                if (item)
                {
                    if (!read([&] { now.guid = item->GetGUID().GetRawValue(); }) ||
                        !read([&] { now.owner = item->GetOwnerGUID().GetRawValue(); }) ||
                        !read([&] { now.position = item->uQueuePos; }))
                        return unavailable();
                    if (!now.guid || now.position != static_cast<std::int32_t>(index) ||
                        now.owner != evidence.operation.owner)
                        output.summary.queueConsistent = false;
                }
                auto const& before = buffer._queueBefore[index];
                // Length changes matter even when only trailing tombstones changed.
                if (index >= output.summary.preparedQueueEntries || index >= output.summary.observedQueueEntries ||
                    before.item != item || before.value != now)
                {
                    ++output.summary.changedQueueEntries;
                    if (output.summary.queueDifferenceCount < MaximumOriginals)
                        output.queueDifferences[output.summary.queueDifferenceCount++] =
                            {index, before.value, now, before.item == item};
                }
            }
            output.summary.queueCompared = true;
            output.summary.queueUnchanged = output.summary.changedQueueEntries == 0;
        }
        // Queue shape and journal phases are observations, not original full
        // queue/update-mask or combat/aura before-images. Never promote them.
        evidence.queues = output.summary.queueCompared && output.summary.queueConsistent &&
            output.summary.queueUnchanged ? ProofState::Unknown : ProofState::Uncertain;
        bool pendingSave = false, pendingLogout = false;
        if (!read([&] { pendingSave = owner->_pb59Transaction.PendingSave(); }) ||
            !read([&] { pendingLogout = owner->_pb59Transaction.PendingLogout(); }))
            return unavailable();
        evidence.pendingSave = evidence.pendingSave || pendingSave;
        evidence.pendingLogout = evidence.pendingLogout || pendingLogout;
        return output.summary.status = complete && output.summary.queueConsistent ? CollectionStatus::Collected :
            CollectionStatus::Incomplete;
    }
    catch (...)
    {
        if (!alive())
            return unavailable();
        return output.summary.status = CollectionStatus::Incomplete;
    }
}
