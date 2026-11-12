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

#ifndef __UPDATEDATA_H
#define __UPDATEDATA_H

#include "ByteBuffer.h"
#include "ObjectGuid.h"

#if defined(COSMIC_F14_CREATURE_BATCH_GUID_EVIDENCE_EXPERIMENT) && COSMIC_F14_CREATURE_BATCH_GUID_EVIDENCE_EXPERIMENT >= 1
#include "F14CreatureBatchGuidCandidateLedger.h"
#endif

class WorldPacket;

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
#include "F14InitialOutPassAdmission.h"
namespace Acore { struct VisibleNotifier; }
#endif

enum OBJECT_UPDATE_TYPE
{
    UPDATETYPE_VALUES               = 0,
    UPDATETYPE_MOVEMENT             = 1,
    UPDATETYPE_CREATE_OBJECT        = 2,
    UPDATETYPE_CREATE_OBJECT2       = 3,
    UPDATETYPE_OUT_OF_RANGE_OBJECTS = 4,
    UPDATETYPE_NEAR_OBJECTS         = 5
};

enum OBJECT_UPDATE_FLAGS
{
    UPDATEFLAG_NONE                 = 0x0000,
    UPDATEFLAG_SELF                 = 0x0001,
    UPDATEFLAG_TRANSPORT            = 0x0002,
    UPDATEFLAG_HAS_TARGET           = 0x0004,
    UPDATEFLAG_UNKNOWN              = 0x0008,
    UPDATEFLAG_LOWGUID              = 0x0010,
    UPDATEFLAG_LIVING               = 0x0020,
    UPDATEFLAG_STATIONARY_POSITION  = 0x0040,
    UPDATEFLAG_VEHICLE              = 0x0080,
    UPDATEFLAG_POSITION             = 0x0100,
    UPDATEFLAG_ROTATION             = 0x0200
};

class UpdateData
{
public:
    UpdateData();

    void AddOutOfRangeGUID(ObjectGuid guid);
    void AddUpdateBlock(ByteBuffer const& block);
    void AddUpdateBlock(UpdateData const& block);
    bool BuildPacket(WorldPacket& packet);
    [[nodiscard]] bool HasData() const { return m_blockCount > 0 || !m_outOfRangeGUIDs.empty(); }
    void Clear();

#if defined(COSMIC_F14_CREATURE_BATCH_GUID_EVIDENCE_EXPERIMENT) && COSMIC_F14_CREATURE_BATCH_GUID_EVIDENCE_EXPERIMENT >= 1
    // Metadata SIDE-CAR ONLY: not serialized into SMSG_UPDATE_OBJECT.
    // Only actual Player::UpdateVisibilityOf<T> creature CREATE/OUT writes may
    // nominate a GUID. Existing unconditional AddOutOfRangeGUID callers are
    // intentionally NOT attributed to F14. Unknown => no evidence.
    void F14RecordCreatureBatchCandidate(
        ObjectGuid const& creatureGuid, CosmicF14::CreatureBatchDirection direction)
    {
        _f14CreatureCandidates.Record(creatureGuid, direction);
    }
    // Include the older independent generic OUT list in the same real
    // packet. A CREATE+OUT for one GUID, or a duplicate OUT, is not evidence.
    bool F14CreatureCandidatesPoisoned() const
    {
        return _f14CreatureCandidates.Poisoned() ||
            !_f14CreatureCandidates.CompatibleWithRealOutGuids(m_outOfRangeGUIDs);
    }
    std::vector<CosmicF14::CreatureBatchGuidCandidateLedger<ObjectGuid>::Candidate> const&
    F14CreatureBatchCandidates() const { return _f14CreatureCandidates.Entries(); }
#endif
#if defined(COSMIC_F14_PRE_SEND_CREATURE_OUT_EVIDENCE_EXPERIMENT) && COSMIC_F14_PRE_SEND_CREATURE_OUT_EVIDENCE_EXPERIMENT >= 1
    // Only a genuine, still-open notifier may initiate a new creature OUT.
    // This is a negative prerequisite, NOT actor/viewer/rights authorization.
    bool F14MayAskInitialOutQuery() const { return _f14InitialOutAdmission.IsOpen(); }
#if defined(COSMIC_F14_TOKEN_BOUND_CREATURE_OUT_QUERY_EXPERIMENT) && COSMIC_F14_TOKEN_BOUND_CREATURE_OUT_QUERY_EXPERIMENT >= 1
    // Read-only token from this exact non-transferable notifier admission.
    uint64 F14InitialOutQueryToken() const { return _f14InitialOutAdmission.Token(); }
#endif

    // Only the exact Core batched positive-query OUT branch can call this.
    // Never serialized to SMSG_UPDATE_OBJECT. Duplicate/overflow fail closed.
    void F14RecordPositiveQueryCreatureOut(ObjectGuid const& guid)
    {
        if (_f14PositiveQueryOutPoisoned)
            return;
        if (guid.IsEmpty() || _f14PositiveQueryOutCandidates.size() >= 256)
        {
            _f14PositiveQueryOutPoisoned = true;
            _f14PositiveQueryOutCandidates.clear();
            return;
        }
        for (ObjectGuid const& prior : _f14PositiveQueryOutCandidates)
            if (prior == guid)
            {
                _f14PositiveQueryOutPoisoned = true;
                _f14PositiveQueryOutCandidates.clear();
                return;
            }
        _f14PositiveQueryOutCandidates.push_back(guid);
    }
    bool F14PositiveQueryOutCandidatesPoisoned() const
    {
        if (_f14PositiveQueryOutPoisoned || F14CreatureCandidatesPoisoned())
            return true;
        for (ObjectGuid const& guid : _f14PositiveQueryOutCandidates)
        {
            bool actualOutBlock = false;
            for (auto const& c : F14CreatureBatchCandidates())
                if (c.guid == guid &&
                    c.direction == CosmicF14::CreatureBatchDirection::OutOfRange)
                {
                    actualOutBlock = true;
                    break;
                }
            if (!actualOutBlock)
                return true;
        }
        return false;
    }
    std::vector<ObjectGuid> const& F14PositiveQueryOutCandidates() const
    {
        return _f14PositiveQueryOutCandidates;
    }
#endif

protected:
    uint32 m_blockCount;
    GuidVector m_outOfRangeGUIDs;
    ByteBuffer m_data;
#if defined(COSMIC_F14_CREATURE_BATCH_GUID_EVIDENCE_EXPERIMENT) && COSMIC_F14_CREATURE_BATCH_GUID_EVIDENCE_EXPERIMENT >= 1
    CosmicF14::CreatureBatchGuidCandidateLedger<ObjectGuid> _f14CreatureCandidates;
#endif
#if defined(COSMIC_F14_PRE_SEND_CREATURE_OUT_EVIDENCE_EXPERIMENT) && COSMIC_F14_PRE_SEND_CREATURE_OUT_EVIDENCE_EXPERIMENT >= 1
    std::vector<ObjectGuid> _f14PositiveQueryOutCandidates;
    bool _f14PositiveQueryOutPoisoned = false;

private:
    friend struct Acore::VisibleNotifier;
    void F14OpenInitialOutQuery(uint64 token) { _f14InitialOutAdmission.Open(token); }
    void F14CloseInitialOutQuery() { _f14InitialOutAdmission.Close(); }
    CosmicF14::InitialOutPassAdmission _f14InitialOutAdmission;
#endif
};
#endif
