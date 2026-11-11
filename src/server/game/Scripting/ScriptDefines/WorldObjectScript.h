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

#ifndef SCRIPT_OBJECT_WORLD_OBJECT_SCRIPT_H_
#define SCRIPT_OBJECT_WORLD_OBJECT_SCRIPT_H_

#include "ScriptObject.h"
#include "ObjectGuid.h"
#include <vector>

#if defined(COSMIC_F14_PRE_SEND_CREATURE_OUT_EVIDENCE_EXPERIMENT) && COSMIC_F14_PRE_SEND_CREATURE_OUT_EVIDENCE_EXPERIMENT >= 1
#if !defined(COSMIC_F14_NOTIFIER_PASS_LIFECYCLE_EXPERIMENT) || COSMIC_F14_NOTIFIER_PASS_LIFECYCLE_EXPERIMENT < 1
#error "F14 pre-send OUT evidence needs Core8 notifier tokens"
#endif
#if !defined(COSMIC_F14_CREATURE_BATCH_GUID_EVIDENCE_EXPERIMENT) || COSMIC_F14_CREATURE_BATCH_GUID_EVIDENCE_EXPERIMENT < 1
#error "F14 pre-send OUT evidence needs Core9 creature sidecar"
#endif
#if !defined(COSMIC_F14_CREATURE_CLIENT_VIEWER_QUERY_EXPERIMENT) || COSMIC_F14_CREATURE_CLIENT_VIEWER_QUERY_EXPERIMENT < 1
#error "F14 pre-send OUT evidence needs the creature query"
#endif
#endif

enum WorldObjectHook
{
    WORLDOBJECTHOOK_ON_WORLD_OBJECT_DESTROY,
    WORLDOBJECTHOOK_ON_WORLD_OBJECT_CREATE,
    WORLDOBJECTHOOK_ON_WORLD_OBJECT_SET_MAP,
    WORLDOBJECTHOOK_ON_WORLD_OBJECT_RESET_MAP,
    WORLDOBJECTHOOK_ON_WORLD_OBJECT_UPDATE,
    WORLDOBJECTHOOK_ON_F14_VIEWER_CREATURE_SUPPRESSION_QUERY,
    WORLDOBJECTHOOK_ON_F14_WHO_GUID_EXCLUSION_QUERY,
    WORLDOBJECTHOOK_ON_F14_VIEWER_PLAYER_SUPPRESSION_QUERY,
#if defined(COSMIC_F14_NOTIFIER_PASS_LIFECYCLE_EXPERIMENT) && COSMIC_F14_NOTIFIER_PASS_LIFECYCLE_EXPERIMENT >= 1
    WORLDOBJECTHOOK_ON_F14_VIEWER_VISIBILITY_PASS_BEGIN,
    WORLDOBJECTHOOK_ON_F14_VIEWER_VISIBILITY_PASS_BATCH_SEND_ATTEMPT,
    WORLDOBJECTHOOK_ON_F14_VIEWER_VISIBILITY_PASS_END,
#endif
#if defined(COSMIC_F14_CREATURE_BATCH_GUID_EVIDENCE_EXPERIMENT) && COSMIC_F14_CREATURE_BATCH_GUID_EVIDENCE_EXPERIMENT >= 1
    WORLDOBJECTHOOK_ON_F14_VIEWER_CREATURE_BATCH_GUID_SEND_ATTEMPT,
#endif
#if defined(COSMIC_F14_PRE_SEND_CREATURE_OUT_EVIDENCE_EXPERIMENT) && COSMIC_F14_PRE_SEND_CREATURE_OUT_EVIDENCE_EXPERIMENT >= 1
    WORLDOBJECTHOOK_ON_F14_VIEWER_CREATURE_POSITIVE_QUERY_OUT_PRESEND,
#endif
    WORLDOBJECTHOOK_END
};

class WorldObjectScript : public ScriptObject
{
protected:
    WorldObjectScript(char const* name, std::vector<uint16> enabledHooks = std::vector<uint16>());

public:
    [[nodiscard]] bool IsDatabaseBound() const override { return false; }

    /**
     * @brief This hook called before destroy world object
     *
     * @param object Contains information about the WorldObject
     */
    virtual void OnWorldObjectDestroy(WorldObject* /*object*/) { }

    /**
     * @brief This hook called after create world object
     *
     * @param object Contains information about the WorldObject
     */
    virtual void OnWorldObjectCreate(WorldObject* /*object*/) { }

    /**
     * @brief This hook called after world object set to map
     *
     * @param object Contains information about the WorldObject
     */
    virtual void OnWorldObjectSetMap(WorldObject* /*object*/, Map* /*map*/ ) { }

    /**
     * @brief This hook called after world object reset
     *
     * @param object Contains information about the WorldObject
     */
    virtual void OnWorldObjectResetMap(WorldObject* /*object*/) { }

    /**
     * @brief This hook called after world object update
     *
     * @param object Contains information about the WorldObject
     * @param diff Contains information about the diff time
     */
    virtual void OnWorldObjectUpdate(WorldObject* /*object*/, uint32 /*diff*/) { }

    // Diagnostic/compatibility API preparation: only the compile-time-OFF
    // Player::UpdateVisibilityOf experiment may consult this hook.
    // No positive IP policy listener is installed by Core. A future listener
    // MUST return FALSE unless COSMIC_F14_PRE_SEND_CREATURE_OUT_EVIDENCE_EXPERIMENT
    // is compiled >=1; with the creature-query macro alone, legacy direct OUT
    // can hide a creature without a notifier token or debt reconciliation.
    // Before answering TRUE, require complete human viewer rights, verified
    // RNDBOT attacker identity and exact creature restore ledger. Follow the
    // OUT-only initial reservation / read-only idempotent CREATE contract
    // beside OnF14ViewerCreaturePositiveQueryOutPreSend below. R2/L1 remain
    // blocking; the query hook must never be enabled as a positive policy alone.
    [[nodiscard]] virtual bool OnF14ShouldSuppressCreatureForViewer(
        Player const* /*viewer*/, WorldObject const* /*creature*/) { return false; }

    // Cached /who GUID is NOT authentication. This intentionally returns FALSE
    // absent a future, separate, verified headless-RNDBOT-only IP listener.
    // Real humans, Altbots, GMs and eligible TBC+ viewers stay visible.
    [[nodiscard]] virtual bool OnF14ShouldExcludeWhoEntryForViewer(
        Player const* /*viewer*/, ObjectGuid const& /*targetGuid*/,
        uint32 /*cachedRace*/) { return false; }

    // Source-only F14 per-viewer player actor query, default FALSE.
    // Headless/race is NOT genuine RandomBot identity or IP era authority.
    [[nodiscard]] virtual bool OnF14ShouldSuppressPlayerForViewer(
        Player const* /*viewer*/, Player const* /*actor*/) { return false; }

#if defined(COSMIC_F14_NOTIFIER_PASS_LIFECYCLE_EXPERIMENT) && COSMIC_F14_NOTIFIER_PASS_LIFECYCLE_EXPERIMENT >= 1
    // These are SYNCHRONOUS batch-stack lifetime signals only, NOT per-GUID
    // creature nominations, network delivery, client ACK or hide authorization.
    // Never store the Player* or mark a hidden NPC restored from these alone.
    virtual void OnF14ViewerVisibilityPassBegin(Player const* /*viewer*/, uint64 /*token*/) { }
    virtual void OnF14ViewerVisibilityPassBatchSendAttempt(Player const* /*viewer*/, uint64 /*token*/) { }
    virtual void OnF14ViewerVisibilityPassEnd(Player const* /*viewer*/, uint64 /*token*/) { }
#endif

#if defined(COSMIC_F14_CREATURE_BATCH_GUID_EVIDENCE_EXPERIMENT) && COSMIC_F14_CREATURE_BATCH_GUID_EVIDENCE_EXPERIMENT >= 1
    // Synchronous, event-only per-NONOWNED-creature GUID from a real batch.
    // isCreate=true means a CREATE block was appended; false = OUT block.
    // Token was born in the actual VisibleNotifier. This is a packet send
    // CALL, never network delivery, client ACK or restoration clearance.
    virtual void OnF14ViewerCreatureBatchGuidSendAttempt(
        Player const* /*viewer*/, ObjectGuid const& /*creatureGuid*/,
        bool /*isCreate*/, uint64 /*passToken*/) { }
#endif
#if defined(COSMIC_F14_PRE_SEND_CREATURE_OUT_EVIDENCE_EXPERIMENT) && COSMIC_F14_PRE_SEND_CREATURE_OUT_EVIDENCE_EXPERIMENT >= 1
    // OBSERVER/RECORD-ONLY. This notification is AFTER the server has
    // already unlinked the creature and built the OUT packet; it CANNOT veto
    // a hide. Any future positive ShouldF14SuppressCreatureForViewer
    // listener MUST distinguish the actual at-client -> hidden batch OUT
    // transition (viewer->HaveAtClient(creature) true) from an already-hidden
    // pair's batch/direct CREATE visibility check (HaveAtClient false).
    // ONLY a verified initial batch OUT may atomically reserve ONE exact
    // (human viewer epoch, creature incarnation, owning map) restore debt
    // under confirmed rights, BEFORE answering TRUE. No reservation or
    // rights => FALSE (VISIBLE); never reserve on a read-only CREATE check.
    // For an already-hidden pair, TRUE must be an idempotent read of the
    // SAME existing owed record, not a second allocation; until a safe
    // restore path exists, a missing/uncertain record cannot authorize hiding.
    // The post-send GUID/token signal must reconcile new OUT reservations,
    // including unsent/aborted passes; never silently drop restore debt.
    // A future direct CREATE which returns FALSE on an outstanding record
    // MUST retain "restore send attempted, delivery UNKNOWN" debt until an
    // independently reviewed R2 reconciliation. That path emits no Core14
    // per-GUID CREATE event and cannot be treated as a client render ACK.
    // These are future-consumer contracts, NOT implemented runtime guards.
    // The OUT-only reservation discriminator is valid ONLY with the above
    // COSMIC_F14_PRE_SEND_CREATURE_OUT_EVIDENCE_EXPERIMENT >=1: without it,
    // legacy direct OUT still queries and can hide with no notifier evidence.
    // No positive listener may answer TRUE with PRE_SEND disabled, even if
    // COSMIC_F14_CREATURE_CLIENT_VIEWER_QUERY_EXPERIMENT is enabled.
    // Without R2/L1 and an authenticated listener, keep positive F14 OFF.
    // Listener MUST NOT call visibility updates, send packets, access DB, or
    // re-enter VisibleNotifier from this pre-send callback. Only record
    // value-only facts synchronously, then release locks before Core work.
    // A positively answered query caused an actual batched creature OUT,
    // but this event is NOT proof of IP rights, normal human login epoch,
    // live creature incarnation, client receipt, ACK or restoration.
    virtual void OnF14ViewerCreaturePositiveQueryOutPreSend(
        Player const* /*viewer*/, ObjectGuid const& /*creatureGuid*/,
        uint64 /*passToken*/) { }
#endif
};

#endif
