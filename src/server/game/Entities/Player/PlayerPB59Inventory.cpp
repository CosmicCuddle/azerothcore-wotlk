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
#include "Bag.h"
#include "Item.h"
#include "Map.h"
#include "PB59EffectBoundary.h"
#include "PB59CombatRecoveryState.h"
#include "ScriptMgr.h"
#include "SpellAuraEffects.h"
#include "SpellAuras.h"
#include "WorldSession.h"
#include <algorithm>
#include <array>
#include <tuple>
#include <utility>
#include <vector>

PB59::InventoryBatchResult Player::ExecutePB59InventoryBatchPlan(uint64 owner,
    uint64 ownerIncarnation, uint64 sessionIncarnation, PB59::Stamp const& stamp,
    std::span<PB59::InventoryBatchMove const> request)
{
    using Outcome = PB59::InventoryBatchOutcome;
    using Failure = PB59::InventoryBatchFailure;
    using Access = PB59::TransactionAdmission::Access;
    using Journal = PB59::OriginalCombatEffects;
    PB59::InventoryBatchResult result;
    result.failure = Failure::Context;
    try
    {
        if (PB59::ValidateInventoryBatchPlan(owner, request) != PB59::InventoryBatchPlanError::None)
        {
            result.failure = Failure::Plan;
            return result;
        }
        if (PB59::InventoryReservation::ActiveForState(&_pb59Equipment) || _pb59Equipment.InventoryQuarantined() ||
            !CheckPB59InventoryBatchPlan(owner, ownerIncarnation, sessionIncarnation, stamp, request))
            return result;
    }
    catch (...)
    {
        return result;
    }

    // Every coordinator allocation/copy is made before opening native custody.
    // Native observation and engine helpers remain fallible; failure is not a
    // promise that a partially applied non-idempotent effect can be replayed.
    struct Step
    {
        PB59::InventoryBatchMove move;
        Item* item = nullptr;
        bool fromEquipment = false;
        bool linked = false;
        bool effects = false;
    };
    struct QueueState
    {
        Item* item;
        int32 index;
        ItemUpdateState state;
        bool inWorld;
        bool objectUpdated;
    };
    struct AuraFacts
    {
        void const* identity = nullptr;
        uint32 spell = 0;
        uint64 caster = 0, item = 0;
        int32 duration = 0, maximum = 0;
        uint8 charges = 0, stacks = 0;
        std::array<int32, MAX_SPELL_EFFECTS * 4> effects{};
        decltype(std::declval<Aura const&>().PB59RecoveryState()) internal{};
        std::array<decltype(std::declval<AuraEffect const&>().PB59RecoveryState()), MAX_SPELL_EFFECTS> effectState{};
        using ApplicationFacts = decltype(std::declval<Aura::ApplicationMap::mapped_type>()->PB59RecoveryState());
        std::array<std::tuple<uint64, ApplicationFacts>, 128> applications{};
        std::size_t applicationCount = 0;
        bool operator==(AuraFacts const&) const = default;
    };
    auto nativeCombat = [&]()
    {
        return std::tuple{PB59::CopyCombatArray(m_auraFlatModifiersGroup),
            PB59::CopyCombatArray(m_auraPctModifiersGroup), PB59::CopyCombatArray(m_weaponDamage),
            PB59::CopyCombatArray(m_auraBaseFlatMod), PB59::CopyCombatArray(m_auraBasePctMod),
            PB59::CopyCombatArray(m_baseRatingValue), PB59::CopyCombatArray(m_modAttackSpeedPct),
            PB59::CopyCombatArray(m_speed_rate), PB59::CopyCombatArray(m_currentSpells),
            std::tuple{m_baseSpellPower, m_baseSpellDamage, m_baseSpellHealing, m_baseFeralAP,
                m_baseManaRegen, m_baseHealthRegen, m_spellPenetrationItemMod, m_modMeleeHitChance,
                m_modRangedHitChance, m_modSpellHitChance, m_baseSpellCritChance, m_realDodge, m_realParry,
                m_canParry, m_canBlock, m_canTitanGrip, m_canModifyStats, _dualWieldMode, m_weaponChangeTimer,
                m_ammoDPS, m_interruptMask, m_removedAurasCount, m_extraAttacks}};
    };
    struct SetFacts
    {
        uint32 id = 0, count = 0;
        std::array<void const*, 8> spells{};
        bool operator==(SetFacts const&) const = default;
    };
    struct Effects
    {
        std::array<uint32, PLAYER_END> fields{};
        decltype(nativeCombat()) combat{};
        std::array<SetFacts, 128> sets{};
        std::size_t setCount = 0;
        std::array<AuraFacts, 512> auras{};
        std::size_t count = 0;
        bool operator==(Effects const&) const = default;
    };
    std::array<Step, 19> steps{};
    std::vector<Item*> queueBefore;
    std::vector<QueueState> queueStates;
    std::vector<void const*> protectedItems;
    std::vector<std::uint64_t> protectedCounters;
    std::unique_ptr<Effects[]> snapshots;
    try
    {
        snapshots = std::make_unique<Effects[]>(2);
    }
    catch (...)
    {
        result.failure = Failure::UnsupportedState;
        return result;
    }
    auto& before = snapshots[0];
    auto& after = snapshots[1];
    SpellCooldowns cooldownsBefore;
    GlobalCooldownMgr globalCooldownsBefore;
    auto* originalMap = GetMap();
    auto* originalSession = GetSession();
    auto const originalLevel = GetLevel();
    auto const originalIdentity = GetUInt32Value(UNIT_FIELD_BYTES_0);
    auto const originalObjectUpdated = m_objectUpdated;
    bool const originalSocketClosed = originalSession->IsSocketClosed();
    auto const count = request.size();
    auto combat = [](uint16 pos)
    {
        auto const slot = pos & 255;
        return (pos >> 8) == INVENTORY_SLOT_BAG_0 && slot < EQUIPMENT_SLOT_END &&
            slot != EQUIPMENT_SLOT_BODY && slot != EQUIPMENT_SLOT_TABARD;
    };
    auto captureEffects = [&](Effects& output)
    {
        if (GetValuesCount() != PLAYER_END || GetOwnedAuras().size() > output.auras.size())
            return false;
        output.fields.fill(0);
        output.auras.fill({});
        output.count = 0;
        output.combat = nativeCombat();
        output.sets.fill({});
        output.setCount = 0;
        for (auto const* effect : ItemSetEff)
            if (effect)
            {
                if (output.setCount == output.sets.size())
                    return false;
                auto& facts = output.sets[output.setCount++];
                facts.id = effect->setid;
                facts.count = effect->item_count;
                std::copy(std::begin(effect->spells), std::end(effect->spells), facts.spells.begin());
            }
        std::sort(output.sets.begin(), output.sets.begin() + output.setCount,
            [](SetFacts const& left, SetFacts const& right) { return left.id < right.id; });
        for (uint16 field = 0; field < PLAYER_END; ++field)
            output.fields[field] = GetUInt32Value(field);
        auto captureAura = [&](Aura const* aura)
        {
            if (!aura)
                return false;
            for (std::size_t index = 0; index < output.count; ++index)
                if (output.auras[index].identity == aura)
                    return true;
            if (output.count == output.auras.size())
                return false;
            auto& facts = output.auras[output.count++];
            facts.identity = aura;
            facts.spell = aura->GetId();
            facts.caster = aura->GetCasterGUID().GetRawValue();
            facts.item = aura->GetCastItemGUID().GetRawValue();
            facts.duration = aura->GetDuration();
            facts.maximum = aura->GetMaxDuration();
            facts.charges = aura->GetCharges();
            facts.stacks = aura->GetStackAmount();
            facts.internal = aura->PB59RecoveryState();
            if (aura->GetApplicationMap().size() > facts.applications.size())
                return false;
            for (auto const& [guid, application] : aura->GetApplicationMap())
            {
                if (!application)
                    return false;
                facts.applications[facts.applicationCount++] = {guid.GetRawValue(), application->PB59RecoveryState()};
            }
            for (uint8 index = 0; index < MAX_SPELL_EFFECTS; ++index)
                if (auto const* effect = aura->GetEffect(index))
                {
                    facts.effectState[index] = effect->PB59RecoveryState();
                    facts.effects[index * 4] = effect->GetAmount();
                    facts.effects[index * 4 + 1] = effect->GetAmplitude();
                    facts.effects[index * 4 + 2] = effect->GetPeriodicTimer();
                    facts.effects[index * 4 + 3] = int32(effect->GetTickNumber());
                }
            return true;
        };
        for (auto const& entry : GetOwnedAuras())
            if (!captureAura(entry.second))
                return false;
        for (auto const& entry : GetAppliedAuras())
            if (!entry.second || !captureAura(entry.second->GetBase()))
                return false;
        std::sort(output.auras.begin(), output.auras.begin() + output.count,
            [](AuraFacts const& left, AuraFacts const& right)
            {
                return std::tie(left.spell, left.caster, left.item, left.duration, left.effects) <
                    std::tie(right.spell, right.caster, right.item, right.duration, right.effects);
            });
        return true;
    };
    auto watch = [&](Item* item)
    {
        if (!item || item->GetOwnerGUID().GetRawValue() != owner)
            return false;
        if (std::find(protectedItems.begin(), protectedItems.end(), item) != protectedItems.end())
            return true;
        if (!PB59::ExactOriginalQueue(m_itemUpdateQueue, item, item->uQueuePos, item->uQueuePos >= 0) ||
            item->PB59HasObjectUpdatePending() != originalMap->PB59HasUpdateObject(item))
            return false;
        queueStates.push_back({item, item->uQueuePos, item->GetState(), item->IsInWorld(),
            item->PB59HasObjectUpdatePending()});
        protectedItems.push_back(item);
        protectedCounters.push_back(item->GetGUID().GetCounter());
        return true;
    };
    try
    {
        result.failure = Failure::UnsupportedState;
        if (!originalMap || !captureEffects(before) || m_itemUpdateQueue.size() > 4096 ||
            originalObjectUpdated != originalMap->PB59HasUpdateObject(this))
            return result;
        queueBefore = m_itemUpdateQueue;
        cooldownsBefore = m_spellCooldowns;
        globalCooldownsBefore = m_GlobalCooldownMgr;
        queueStates.reserve(queueBefore.size() + count * 3);
        protectedItems.reserve(queueBefore.size() + count * 3);
        protectedCounters.reserve(queueBefore.size() + count * 3);
        for (auto* item : queueBefore)
            if (item && !watch(item))
                return result;
        for (std::size_t index = 0; index < count; ++index)
        {
            auto& step = steps[index];
            step.move = request[index];
            step.item = GetItemByPos(step.move.source.position);
            step.fromEquipment = combat(step.move.source.position);
            if (!step.item || !watch(step.item) || !step.item->IsInWorld() || step.item->GetCount() != 1)
                return result;
            auto const* proto = step.item->GetTemplate();
            if (!proto || (!step.fromEquipment && !step.item->IsSoulBound() &&
                (proto->Bonding == BIND_WHEN_EQUIPPED || proto->Bonding == BIND_WHEN_PICKED_UP ||
                    proto->Bonding == BIND_QUEST_ITEM)))
                return result;
            // Timers and on-obtain spells require separate custody/effect
            // handling. Refuse before policy callbacks or any native writes.
            if (std::find(m_itemDuration.begin(), m_itemDuration.end(), step.item) != m_itemDuration.end())
                return result;
            for (auto const& timer : m_enchantDuration)
                if (timer.item == step.item)
                    return result;
            for (auto const& spell : proto->Spells)
                if (spell.SpellId && spell.SpellTrigger == ITEM_SPELLTRIGGER_ON_NO_DELAY_USE)
                    return result;
            for (auto const* slot : {&step.move.source, &step.move.destination})
                if (slot->bag && !watch(GetBagByPos(slot->position >> 8)))
                    return result;
        }
    }
    catch (...)
    {
        return result;
    }

    if (!_pb59Equipment.PrepareInventoryReservation(owner, protectedItems, protectedCounters))
        return result;
    result.failure = Failure::Context;
    PB59::TransactionAdmission::MutationScope scope(_pb59Transaction);
    auto const ticket = scope.GetTicket();
    if (!ticket)
        return result;
    PB59::InventoryReservation reservation(&_pb59Equipment, owner, protectedItems, protectedCounters);
    _pb59Equipment.SetInventoryQuarantined(true);
    _pb59Equipment.Invalidate();
    bool uncertain = false;
    bool attempted = false;
    std::size_t current = 0;
    auto live = [&]()
    {
        // Test the retained ledger sentinel BEFORE any native owner access.
        return scope.CanAccess(ownerIncarnation, Access::Forward) ||
            scope.CanAccess(ownerIncarnation, Access::Recovery);
    };
    auto context = [&](bool recovery)
    {
        if (!live())
            return false;
        return GetSession() == originalSession && originalSession->GetPlayer() == this &&
            originalSession->GetPB59SessionIncarnation() == sessionIncarnation &&
            !originalSession->PlayerLoading() && !originalSession->PlayerLogout() &&
            (originalSession->IsHeadless() || originalSession->IsSocketClosed() == originalSocketClosed) &&
            IsInWorld() && IsAlive() && !IsInCombat() && GetMap() == originalMap &&
            GetLevel() == originalLevel && GetUInt32Value(UNIT_FIELD_BYTES_0) == originalIdentity &&
            !IsBeingTeleportedFar() && !IsBeingTeleportedNear() &&
            (recovery || (!_pb59Transaction.PendingSave() && !_pb59Transaction.PendingLogout()));
    };
    auto observations = [&](bool original)
    {
        if (!context(true))
            return false;
        for (std::size_t index = 0; index < count; ++index)
        {
            auto const& step = steps[index];
            auto const source = CapturePB59InventorySlot(step.move.source.position);
            auto const destination = CapturePB59InventorySlot(step.move.destination.position);
            if (!source || !destination)
                return false;
            if (!original && step.linked)
            {
                auto expected = step.move.destination;
                expected.item = step.move.source.item;
                if (source->item || source->owner != owner || source->bag != step.move.source.bag ||
                    *destination != expected || GetItemByPos(expected.position) != step.item)
                    return false;
            }
            else if (*source != step.move.source || *destination != step.move.destination ||
                GetItemByPos(source->position) != step.item)
                return false;
        }
        return true;
    };
    auto recoveryResult = [&]()
    {
        result.outcome = Outcome::RecoveryRequired;
        result.recoveryCount = 0;
        if (live())
        {
            _pb59Transaction.RequireRecovery(ticket);
            for (std::size_t index = 0; index < count; ++index)
            {
                bool restored = false;
                try
                {
                    auto const now = CapturePB59InventorySlot(steps[index].move.source.position);
                    restored = now && *now == steps[index].move.source &&
                        GetItemByPos(now->position) == steps[index].item;
                }
                catch (...)
                {
                }
                if (!restored)
                    result.recoveryGuids[result.recoveryCount++] = steps[index].move.source.item->guid;
            }
        }
        else
            for (std::size_t index = 0; index < count; ++index)
                result.recoveryGuids[result.recoveryCount++] = steps[index].move.source.item->guid;
        return result;
    };
    auto cooldownsSame = [&]()
    {
        if (!m_GlobalCooldownMgr.PB59Equivalent(globalCooldownsBefore) ||
            m_spellCooldowns.size() != cooldownsBefore.size())
            return false;
        for (auto const& [spell, old] : cooldownsBefore)
        {
            auto const found = m_spellCooldowns.find(spell);
            if (found == m_spellCooldowns.end())
                return false;
            auto const& now = found->second;
            if (now.end != old.end || now.category != old.category || now.itemid != old.itemid ||
                now.maxduration != old.maxduration || now.sendToSpectator != old.sendToSpectator ||
                now.needSendToClient != old.needSendToClient)
                return false;
        }
        return true;
    };
    auto policy = [&](Step const& step, bool inverse)
    {
        if (!context(inverse) || !observations(false))
            return false;
        auto const from = inverse ? step.move.destination.position : step.move.source.position;
        auto const to = inverse ? step.move.source.position : step.move.destination.position;
        if (combat(from) && CanUnequipItem(from, false) != EQUIP_ERR_OK)
            return false;
        if (!context(inverse) || !observations(false))
            return false;
        if (combat(to))
        {
            auto const* proto = step.item->GetTemplate();
            bool const needsEmptyOffhand = (to & 255) == EQUIPMENT_SLOT_MAINHAND &&
                ((proto->InventoryType == INVTYPE_2HWEAPON && !CanTitanGrip()) ||
                    proto->SubClass == ITEM_SUBCLASS_WEAPON_POLEARM || proto->SubClass == ITEM_SUBCLASS_WEAPON_STAFF ||
                    proto->SubClass == ITEM_SUBCLASS_WEAPON_FISHING_POLE);
            if (needsEmptyOffhand && GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_OFFHAND))
                return false;
            uint16 destination = 0;
            if (CanEquipItem(to & 255, destination, GetItemByPos(from), false) != EQUIP_ERR_OK || destination != to)
                return false;
            if (needsEmptyOffhand && GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_OFFHAND))
                return false;
        }
        else
        {
            ItemPosCountVec destinations;
            if (CanStoreItem(to >> 8, to & 255, destinations, GetItemByPos(from), false) != EQUIP_ERR_OK ||
                destinations.size() != 1 || destinations[0].pos != to || destinations[0].count != 1)
                return false;
        }
        return context(inverse) && observations(false);
    };
    auto secondary = [&](uint8 slot, Item* visible)
    {
        if (slot == EQUIPMENT_SLOT_MAINHAND)
            UpdateExpertise(BASE_ATTACK);
        else if (slot == EQUIPMENT_SLOT_OFFHAND)
            UpdateExpertise(OFF_ATTACK);
        if (slot == EQUIPMENT_SLOT_MAINHAND || slot == EQUIPMENT_SLOT_OFFHAND || slot == EQUIPMENT_SLOT_RANGED)
            RecalculateRating(CR_ARMOR_PENETRATION);
        UpdateTitansGrip();
        SetVisibleItemSlot(slot, visible); // preserves the existing custom display hook
    };
    auto transfer = [&](Step& step, bool inverse)
    {
        auto from = inverse ? step.move.destination : step.move.source;
        auto to = inverse ? step.move.source : step.move.destination;
        from.item = step.move.source.item;
        to.item.reset();
        auto const access = inverse ? Access::Recovery : Access::Forward;
        auto const moved = RelocatePB59InventoryLinks(from, to, ticket, access);
        if (moved == PB59::LinkTransferResult::RecoveryRequired)
            uncertain = true;
        if (moved != PB59::LinkTransferResult::Linked)
            return false;
        step.linked = !inverse;
        return observations(false);
    };
    auto resetJournal = [&](Step const& step)
    {
        auto const slot = (step.fromEquipment ? step.move.source.position : step.move.destination.position) & 255;
        auto& journal = _pb59OriginalCombatEffects[slot];
        return journal.State() == Journal::Phase::Fresh ||
            journal.RearmAfterVerifiedMove(ticket, step.move.source.item->guid, [&] { return observations(false); });
    };
    auto effects = [&](Step& step, bool apply)
    {
        auto facts = step.fromEquipment ? step.move.source : step.move.destination;
        facts.item = step.move.source.item;
        auto const value = ReconcilePB59OriginalCombatEffects(facts, ticket, apply);
        if (value == Journal::Result::RecoveryRequired)
            uncertain = true;
        if (value != Journal::Result::Complete)
            return false;
        auto const slot = facts.position & 255;
        auto const& journal = _pb59OriginalCombatEffects[slot];
        if (!journal.Matches(ticket, step.move.source.item->guid) ||
            journal.State() != (apply ? Journal::Phase::Restored : Journal::Phase::Removed))
        {
            uncertain = true;
            return false;
        }
        step.effects = !step.effects;
        return observations(false);
    };
    auto queuesValid = [&]()
    {
        if (!context(true))
            return false;
        for (auto const& saved : queueStates)
            if (!PB59::ExactOriginalQueue(m_itemUpdateQueue, saved.item, saved.item->uQueuePos,
                    saved.item->uQueuePos >= 0) || saved.item->IsInWorld() != saved.inWorld ||
                saved.item->PB59HasObjectUpdatePending() != originalMap->PB59HasUpdateObject(saved.item))
                return false;
        for (auto* item : m_itemUpdateQueue)
            if (item && std::find(protectedItems.begin(), protectedItems.end(), item) == protectedItems.end())
                return false;
        return true;
    };
    auto finish = [&](Outcome outcome)
    {
        if (!context(outcome == Outcome::Restored) || !observations(outcome != Outcome::Applied) || !queuesValid())
            return false;
        for (std::size_t index = 0; index < count; ++index)
            if (!resetJournal(steps[index]))
                return false;
        if (!scope.Finish({true, true, true, true}))
            return false;
        _pb59Equipment.SetInventoryQuarantined(false);
        result.outcome = outcome;
        return true;
    };

    struct VerifyContext
    {
        decltype(context)* current;
        decltype(observations)* observed;
        decltype(live)* lifetime;
        PB59::InventoryReservation* reservation;
        unsigned violations;
        bool recovery;
    } verification{&context, &observations, &live, &reservation, reservation.Violations(), false};
    auto alive = [](void* pointer)
    {
        return (*static_cast<VerifyContext*>(pointer)->lifetime)();
    };
    auto verify = [](void* pointer)
    {
        auto const& value = *static_cast<VerifyContext*>(pointer);
        return (*value.current)(value.recovery) && (*value.observed)(false) &&
            value.reservation->Violations() == value.violations;
    };
    try
    {
        result.failure = Failure::Policy;
        // Strip destinations are independently empty: run their complete
        // native admission before the first effect. Equip admission is also
        // rechecked against staged hands, allowing an explicit earlier offhand
        // move instead of ever invoking the automatic offhand/mail fallback.
        for (std::size_t index = 0; index < count; ++index)
            if (steps[index].fromEquipment && !policy(steps[index], false))
            {
                if (observations(true) && captureEffects(after) && before == after && cooldownsSame() &&
                    m_itemUpdateQueue == queueBefore && finish(Outcome::Refused))
                    return result;
                return recoveryResult();
            }
        // Policy executes here, never in the PB adapter. Every step rechecks
        // exact reservations and context after its native callbacks.
        for (; current < count; ++current)
        {
            auto& step = steps[current];
            if (!policy(step, false))
                break;
            attempted = true;
            reservation.SetEffectsPhase(true);
            PB59::EffectBoundary boundary(this, &verification, verify, alive);
            result.failure = Failure::Effects;
            if (step.fromEquipment && !effects(step, false))
                break;
            result.failure = Failure::Links;
            if (!transfer(step, false))
                break;
            auto const slot = step.move.destination.position & 255;
            if (!step.fromEquipment)
            {
                auto& journal = _pb59OriginalCombatEffects[slot];
                if (!journal.BeginUnequipped(ticket, step.move.source.item->guid))
                    break;
                secondary(slot, step.item);
                if (!effects(step, true))
                    break;
                ApplyEquipCooldown(step.item);
            }
            else
                secondary(step.move.source.position & 255, nullptr);
            if (!PB59::EffectBoundary::Check(this) || !queuesValid())
                break;
            reservation.SetEffectsPhase(false);
            result.failure = Failure::Policy;
        }
        reservation.SetEffectsPhase(false);
        if (current == count)
        {
            // Irreversible notifications are delayed until every move is known
            // complete. Any failure here is recovery-required, never rollback.
            reservation.SetEffectsPhase(true);
            PB59::EffectBoundary boundary(this, &verification, verify, alive);
            for (std::size_t index = 0; index < count; ++index)
                if (!steps[index].fromEquipment)
                {
                    auto const slot = steps[index].move.destination.position & 255;
                    UpdateAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_EQUIP_ITEM, steps[index].item->GetEntry());
                    UpdateAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_EQUIP_EPIC_ITEM,
                        steps[index].item->GetEntry(), slot);
                    sScriptMgr->OnPlayerEquip(this, steps[index].item, INVENTORY_SLOT_BAG_0, slot, true);
                    UpdateForQuestWorldObjects();
                    if (!PB59::EffectBoundary::Check(this))
                        return recoveryResult();
                }
            if (finish(Outcome::Applied))
            {
                result.failure = Failure::None;
                return result;
            }
            return recoveryResult();
        }
    }
    catch (...)
    {
        uncertain = attempted;
    }
    reservation.SetEffectsPhase(false);

    try
    {
        if (!attempted && observations(true) && captureEffects(after) && before == after && cooldownsSame() &&
            m_itemUpdateQueue == queueBefore && finish(Outcome::Refused))
            return result;
        // Never invert uncertain callbacks or a changed aura domain. Even a
        // visually restored inventory cannot prove external script state back.
        if (uncertain || reservation.ExternalEffects() || !context(true) || !cooldownsSame() ||
            !captureEffects(after) || before.count != after.count || before.auras != after.auras)
            return recoveryResult();
        _pb59Transaction.RequireRecovery(ticket);
        verification.recovery = true;
        verification.violations = reservation.Violations();
        result.failure = Failure::Restoration;
        auto undoCount = std::min(count, current + 1);
        while (undoCount)
        {
            auto& step = steps[--undoCount];
            if (!step.effects && !step.linked)
                continue;
            PB59::EffectBoundary boundary(this, &verification, verify, alive);
            if (step.linked && !policy(step, true))
                return recoveryResult();
            reservation.SetEffectsPhase(true);
            if (!step.fromEquipment && step.effects)
            {
                if (!resetJournal(step) || !effects(step, false))
                    return recoveryResult();
            }
            if (step.linked)
            {
                if (!transfer(step, true))
                    return recoveryResult();
            }
            if (step.fromEquipment && step.effects && !effects(step, true))
                return recoveryResult();
            if (step.effects || step.linked)
                return recoveryResult();
            secondary((step.fromEquipment ? step.move.source.position : step.move.destination.position) & 255,
                step.fromEquipment ? step.item : nullptr);
            if (step.fromEquipment)
            {
                sScriptMgr->OnPlayerEquip(this, step.item, INVENTORY_SLOT_BAG_0, step.move.source.position & 255, true);
                UpdateForQuestWorldObjects();
            }
            if (!PB59::EffectBoundary::Check(this))
                return recoveryResult();
        }
        reservation.SetEffectsPhase(false);
        if (reservation.ExternalEffects() || !observations(true) || !captureEffects(after) || before != after ||
            !cooldownsSame() || !queuesValid())
            return recoveryResult();
        // No foreign queue entry may be overwritten. Every saved pointer is
        // still owned and every logical item/visible/effect value is original.
        m_itemUpdateQueue.swap(queueBefore);
        for (auto const& saved : queueStates)
        {
            saved.item->uState = saved.state;
            saved.item->uQueuePos = saved.index;
            if (!saved.objectUpdated)
                saved.item->ClearUpdateMask(true);
        }
        if (!originalObjectUpdated)
            ClearUpdateMask(true);
        if (!queuesValid() || !finish(Outcome::Restored))
            return recoveryResult();
        return result;
    }
    catch (...)
    {
        return recoveryResult();
    }
}
