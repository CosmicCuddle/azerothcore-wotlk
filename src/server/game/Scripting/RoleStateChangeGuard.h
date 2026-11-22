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

#ifndef COSMIC_ROLE_STATE_CHANGE_GUARD_H
#define COSMIC_ROLE_STATE_CHANGE_GUARD_H

#include "ScriptMgr.h"

// Observer pairing only; does not lock role fields or alter setter behavior.
class RoleStateChangeGuard final
{
public:
    explicit RoleStateChangeGuard(bool active = true) : _active(active)
    {
        if (_active)
            sScriptMgr->OnBeforeRoleStateChange();
    }

    ~RoleStateChangeGuard()
    {
        if (_active)
            sScriptMgr->OnAfterRoleStateChange();
    }

    RoleStateChangeGuard(RoleStateChangeGuard const&) = delete;
    RoleStateChangeGuard& operator=(RoleStateChangeGuard const&) = delete;

private:
    bool const _active;
};

#endif
