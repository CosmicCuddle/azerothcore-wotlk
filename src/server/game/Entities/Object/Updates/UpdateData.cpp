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

#include "UpdateData.h"
#include "ByteBuffer.h"
#include "Errors.h"
#include "Log.h"
#include "Opcodes.h"
#include "World.h"
#include "WorldPacket.h"

UpdateData::UpdateData() : m_blockCount(0)
{
    m_outOfRangeGUIDs.reserve(15);
}

void UpdateData::AddOutOfRangeGUID(ObjectGuid guid)
{
    m_outOfRangeGUIDs.push_back(guid);
}

void UpdateData::AddUpdateBlock(ByteBuffer const& block)
{
    m_data.append(block);
    ++m_blockCount;
}

void UpdateData::AddUpdateBlock(UpdateData const& block)
{
    m_data.append(block.m_data);
    m_blockCount += block.m_blockCount;
#if defined(COSMIC_F14_CREATURE_BATCH_GUID_EVIDENCE_EXPERIMENT) && COSMIC_F14_CREATURE_BATCH_GUID_EVIDENCE_EXPERIMENT >= 1
    // A merged packet may contain independent creature blocks for which the
    // sidecar has no proof. Invalidate the ENTIRE candidate batch; never
    // emit selected per-GUID evidence from an incompletely attributed merge.
    _f14CreatureCandidates.Poison();
#endif
#if defined(COSMIC_F14_PRE_SEND_CREATURE_OUT_EVIDENCE_EXPERIMENT) && COSMIC_F14_PRE_SEND_CREATURE_OUT_EVIDENCE_EXPERIMENT >= 1
    _f14PositiveQueryOutPoisoned = true;
    _f14PositiveQueryOutCandidates.clear();
    F14CloseInitialOutQuery(); // merged data is not a new authorized notifier
#endif
}

bool UpdateData::BuildPacket(WorldPacket& packet)
{
    ASSERT(packet.empty());

    packet.reserve(4 + (m_outOfRangeGUIDs.empty() ? 0 : 1 + 4 + 9 * m_outOfRangeGUIDs.size()) + m_data.wpos());

    packet << (uint32) (!m_outOfRangeGUIDs.empty() ? m_blockCount + 1 : m_blockCount);

    if (!m_outOfRangeGUIDs.empty())
    {
        packet << (uint8) UPDATETYPE_OUT_OF_RANGE_OBJECTS;
        packet << (uint32) m_outOfRangeGUIDs.size();

        for (ObjectGuid const& guid : m_outOfRangeGUIDs)
            packet << guid.WriteAsPacked();
    }

    packet.append(m_data);
    packet.SetOpcode(SMSG_UPDATE_OBJECT);

    return true;
}

void UpdateData::Clear()
{
    m_data.clear();
    m_outOfRangeGUIDs.clear();
    m_blockCount = 0;
#if defined(COSMIC_F14_CREATURE_BATCH_GUID_EVIDENCE_EXPERIMENT) && COSMIC_F14_CREATURE_BATCH_GUID_EVIDENCE_EXPERIMENT >= 1
    _f14CreatureCandidates.Clear();
#endif
#if defined(COSMIC_F14_PRE_SEND_CREATURE_OUT_EVIDENCE_EXPERIMENT) && COSMIC_F14_PRE_SEND_CREATURE_OUT_EVIDENCE_EXPERIMENT >= 1
    _f14PositiveQueryOutCandidates.clear();
    _f14PositiveQueryOutPoisoned = false;
    F14CloseInitialOutQuery(); // clearing cannot reopen a closed pass
#endif
}
