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

#include "WorldObjectScript.h"
#include "ScriptMgr.h"
#include "ScriptMgrMacros.h"

void ScriptMgr::OnWorldObjectDestroy(WorldObject* object)
{
    ASSERT(object);

    CALL_ENABLED_HOOKS(WorldObjectScript, WORLDOBJECTHOOK_ON_WORLD_OBJECT_DESTROY, script->OnWorldObjectDestroy(object));
}

void ScriptMgr::OnWorldObjectCreate(WorldObject* object)
{
    ASSERT(object);

    CALL_ENABLED_HOOKS(WorldObjectScript, WORLDOBJECTHOOK_ON_WORLD_OBJECT_CREATE, script->OnWorldObjectCreate(object));
}

void ScriptMgr::OnWorldObjectSetMap(WorldObject* object, Map* map)
{
    ASSERT(object);

    CALL_ENABLED_HOOKS(WorldObjectScript, WORLDOBJECTHOOK_ON_WORLD_OBJECT_SET_MAP, script->OnWorldObjectSetMap(object, map));
}

void ScriptMgr::OnWorldObjectResetMap(WorldObject* object)
{
    ASSERT(object);

    CALL_ENABLED_HOOKS(WorldObjectScript, WORLDOBJECTHOOK_ON_WORLD_OBJECT_RESET_MAP, script->OnWorldObjectResetMap(object));
}

void ScriptMgr::OnWorldObjectUpdate(WorldObject* object, uint32 diff)
{
    ASSERT(object);

    CALL_ENABLED_HOOKS(WorldObjectScript, WORLDOBJECTHOOK_ON_WORLD_OBJECT_UPDATE, script->OnWorldObjectUpdate(object, diff));
}

bool ScriptMgr::ShouldF14SuppressCreatureForViewer(
    Player const* viewer, WorldObject const* creature)
{
    ASSERT(viewer && creature);
    // No registered positive script: this API returns FALSE. The optional
    // PlayerUpdates.cpp query call sites are compile-time OFF by default;
    // Core alone supplies no creature-hiding policy or restoration events.
    CALL_ENABLED_BOOLEAN_HOOKS_WITH_DEFAULT_FALSE(
        WorldObjectScript, WORLDOBJECTHOOK_ON_F14_VIEWER_CREATURE_SUPPRESSION_QUERY,
        script->OnF14ShouldSuppressCreatureForViewer(viewer, creature));
}

bool ScriptMgr::ShouldF14ExcludeWhoEntryForViewer(
    Player const* viewer, ObjectGuid const& targetGuid, uint32 cachedRace)
{
    ASSERT(viewer);
    if (targetGuid.IsEmpty())
        return false;

    // Never trust a race-10/11 WHO cache entry as proof of RNDBOT origin.
    // There is no positive listener; normal compilation never calls this.
    CALL_ENABLED_BOOLEAN_HOOKS_WITH_DEFAULT_FALSE(
        WorldObjectScript, WORLDOBJECTHOOK_ON_F14_WHO_GUID_EXCLUSION_QUERY,
        script->OnF14ShouldExcludeWhoEntryForViewer(viewer, targetGuid, cachedRace));
}

bool ScriptMgr::ShouldF14SuppressPlayerForViewer(
    Player const* viewer, Player const* actor)
{
    ASSERT(viewer && actor);
    // No positive IP/RandomBot policy is registered in this Core draft.
    CALL_ENABLED_BOOLEAN_HOOKS_WITH_DEFAULT_FALSE(
        WorldObjectScript, WORLDOBJECTHOOK_ON_F14_VIEWER_PLAYER_SUPPRESSION_QUERY,
        script->OnF14ShouldSuppressPlayerForViewer(viewer, actor));
}

#if defined(COSMIC_F14_NOTIFIER_PASS_LIFECYCLE_EXPERIMENT) && COSMIC_F14_NOTIFIER_PASS_LIFECYCLE_EXPERIMENT >= 1
void ScriptMgr::OnF14ViewerVisibilityPassBegin(Player const* viewer, uint64 token)
{
    ASSERT(viewer && token);
    CALL_ENABLED_HOOKS(
        WorldObjectScript, WORLDOBJECTHOOK_ON_F14_VIEWER_VISIBILITY_PASS_BEGIN,
        script->OnF14ViewerVisibilityPassBegin(viewer, token));
}

void ScriptMgr::OnF14ViewerVisibilityPassBatchSendAttempt(Player const* viewer, uint64 token)
{
    ASSERT(viewer && token);
    // A send-call has happened for UpdateData. This does NOT identify which
    // GUIDs were in the packet, that they were delivered, or client ACK.
    CALL_ENABLED_HOOKS(
        WorldObjectScript, WORLDOBJECTHOOK_ON_F14_VIEWER_VISIBILITY_PASS_BATCH_SEND_ATTEMPT,
        script->OnF14ViewerVisibilityPassBatchSendAttempt(viewer, token));
}

void ScriptMgr::OnF14ViewerVisibilityPassEnd(Player const* viewer, uint64 token)
{
    ASSERT(viewer && token);
    CALL_ENABLED_HOOKS(
        WorldObjectScript, WORLDOBJECTHOOK_ON_F14_VIEWER_VISIBILITY_PASS_END,
        script->OnF14ViewerVisibilityPassEnd(viewer, token));
}
#endif

WorldObjectScript::WorldObjectScript(char const* name, std::vector<uint16> enabledHooks)
    : ScriptObject(name, WORLDOBJECTHOOK_END)
{
    // If empty - enable all available hooks.
    if (enabledHooks.empty())
        for (uint16 i = 0; i < WORLDOBJECTHOOK_END; ++i)
            enabledHooks.emplace_back(i);

    ScriptRegistry<WorldObjectScript>::AddScript(this, std::move(enabledHooks));
}

template class AC_GAME_API ScriptRegistry<WorldObjectScript>;
