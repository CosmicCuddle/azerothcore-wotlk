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
    // No positive IP policy listener is installed by Core. Before allowing
    // TRUE, require complete viewer rights, verified RNDBOT attacker identity
    // and exact creature restore ledger; this hook must not be enabled alone.
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
};

#endif
