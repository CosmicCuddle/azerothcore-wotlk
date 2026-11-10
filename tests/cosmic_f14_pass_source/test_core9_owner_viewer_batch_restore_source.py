"""F14 Core9 viewer-only restoration ROUTE source contracts, NOT a runtime producer.

Pinned review base: Core9 865724cee8d0fb94d7496854c0f426f60bac2232.
Parent design: Core DRAFT #11 ca63fab51650c4aa4813aaa7aeb7c03da6a32c74.
These tests prove code-shape/negative mutations, NOT any queued restore,
3.3.5a redraw, client receipt, identity authority, or in-game behavior.
"""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]
PLAYER = ROOT / "src/server/game/Entities/Player/PlayerUpdates.cpp"
UNIT = ROOT / "src/server/game/Entities/Unit/Unit.cpp"
NOTIFIER = ROOT / "src/server/game/Grids/Notifiers/GridNotifiers.cpp"
NOTIFIER_H = ROOT / "src/server/game/Grids/Notifiers/GridNotifiers.h"
MAP = ROOT / "src/server/game/Maps/Map.cpp"


def excerpt(source: str, start: str, end: str) -> str:
    assert source.count(start) == 1, f"Not exactly one start anchor: {start}"
    i = source.index(start)
    j = source.find(end, i + len(start))
    assert j != -1, f"End anchor not found after {start}: {end}"
    return source[i:j]


def require_order(section: str, *anchors: str) -> None:
    last = -1
    for anchor in anchors:
        assert section.count(anchor) == 1, f"Expected exactly one {anchor}"
        pos = section.index(anchor)
        assert pos > last, f"Wrong order for {anchor}"
        last = pos


def require_viewer_batch_path(section: str) -> None:
    require_order(
        section,
        "Acore::VisibleNotifier notifier(*this, mapChange);",
        "Cell::VisitObjects(",
        "Cell::VisitFarVisibleObjects(",
        "notifier.SendToSelf();",
    )
    assert "GetSightPosition()" in section, "Visibility uses the viewer's sight position"
    assert "m_seer != this" in section, "Seer handling needs separate review"
    for forbidden in (
        "Unit::UpdateObjectVisibility(true)",
        "WorldObject::UpdateObjectVisibility(true)",
        "AIRelocationNotifier",
        "VisibleChangesNotifier",
        "bRequestForcedVisibilityUpdate = true",
    ):
        assert forbidden not in section, f"Broadcast/AI path entered viewer-only batch: {forbidden}"


class Core9ViewerBatchRestoreSource(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.player = PLAYER.read_text(encoding="utf-8")
        cls.unit = UNIT.read_text(encoding="utf-8")
        cls.notifier = NOTIFIER.read_text(encoding="utf-8")
        cls.notifier_h = NOTIFIER_H.read_text(encoding="utf-8")
        cls.map = MAP.read_text(encoding="utf-8")

    @classmethod
    def batch(cls) -> str:
        return excerpt(
            cls.player,
            "void Player::UpdateVisibilityForPlayer(bool mapChange)",
            "void Player::UpdateObjectVisibility(bool forced, bool fromUpdate)",
        )

    def test_public_viewer_only_batch_is_a_real_notifier_route(self) -> None:
        require_viewer_batch_path(self.batch())

    def test_forced_player_path_is_not_equivalent(self) -> None:
        forced = excerpt(
            self.player,
            "void Player::UpdateObjectVisibility(bool forced, bool fromUpdate)",
            "template <class T>\ninline void UpdateVisibilityOf_helper",
        )
        self.assertIn("bRequestForcedVisibilityUpdate = true", forced)
        self.assertIn("Unit::UpdateObjectVisibility(true);", forced)
        self.assertIn("UpdateVisibilityForPlayer();", forced)
        self.assertIn("AddToNotify(NOTIFY_VISIBILITY_CHANGED)", forced)
        unit = excerpt(
            self.unit, "void Unit::UpdateObjectVisibility(bool forced, bool /*fromUpdate*/)",
            "void Unit::KnockbackFrom(",
        )
        require_order(unit, "WorldObject::UpdateObjectVisibility(true);",
                      "Acore::AIRelocationNotifier notifier(*this);",
                      "Cell::VisitObjects(this, notifier, radius);")

    def test_direct_path_has_no_notifier_token(self) -> None:
        direct = excerpt(self.player, "void Player::UpdateVisibilityOf(WorldObject* target)",
                         "void Player::UpdateTriggerVisibility()")
        require_order(direct, "target->DestroyForPlayer(this);",
                      "target->SendUpdateToPlayer(this);")
        self.assertNotIn("VisibleNotifier", direct)
        self.assertNotIn("_f14PassToken", direct)
        self.assertNotIn("OnF14ViewerCreatureBatchGuidSendAttempt", direct)

    def test_batch_send_call_is_post_packet_not_client_ack(self) -> None:
        send = excerpt(self.notifier, "void VisibleNotifier::SendToSelf()",
                       "void VisibleChangesNotifier::Visit(PlayerMapType& m)")
        require_order(send, "i_data.BuildPacket(packet);",
                      "i_player.SendDirectMessage(&packet);",
                      "OnF14ViewerVisibilityPassBatchSendAttempt(",
                      "OnF14ViewerCreatureBatchGuidSendAttempt(")
        # The sweep adds ordinary range OUTs; never interpret them as F14.
        require_order(send, "i_data.AddOutOfRangeGUID(obj->GetGUID());",
                      "i_player.SendDirectMessage(&packet);")
        self.assertIn("if (!i_data.HasData())", send)
        self.assertIn("GetZoneWideVisibleWorldObjectsForZone", send)

    def test_notifier_real_lifetime_but_no_authenticated_viewer_epoch(self) -> None:
        begin = excerpt(self.notifier, "void VisibleNotifier::BeginF14NotifierPass()",
                        "void VisibleNotifier::Visit(GameObjectMapType& m)")
        require_order(begin, "f14PassTokenSource.Issue();",
                      "OnF14ViewerVisibilityPassBegin(",
                      "VisibleNotifier::~VisibleNotifier()",
                      "OnF14ViewerVisibilityPassEnd(")
        self.assertIn("IsHeadless()", begin)
        self.assertNotIn("loginEpoch", begin)
        self.assertNotIn("Player::CustomData", begin)
        self.assertIn("i_visibleNow(player.m_newVisible)", self.notifier_h)
        self.assertIn("i_visibleNow.clear();", self.notifier_h)

    def test_map_frame_is_not_an_automatic_restore_scheduler(self) -> None:
        # Core9's existing map update ordering alone does not schedule an F14 restore.
        frame = excerpt(
            self.map,
            "void Map::Update(const uint32 t_diff, const uint32 s_diff, bool  /*thread*/)",
            "void Map::UpdateNonPlayerObjects(uint32 const diff)",
        )
        self.assertIn("HandleDelayedVisibility();", frame)
        self.assertIn("sScriptMgr->OnMapUpdate(this, t_diff);", frame)
        self.assertNotIn("F14Restore", frame)
        self.assertNotIn("UpdateVisibilityForPlayer(false)", frame)

    def test_negative_mutations_catch_forced_and_unsent_route(self) -> None:
        original = self.batch()
        mutations = (
            original.replace("notifier.SendToSelf();", "", 1),
            original.replace("Acore::VisibleNotifier notifier(*this, mapChange);",
                             "Acore::VisibleChangesNotifier notifier(*this, mapChange);", 1),
            original.replace("notifier.SendToSelf();",
                             "Unit::UpdateObjectVisibility(true);\n    notifier.SendToSelf();", 1),
            original.replace("notifier.SendToSelf();", "bRequestForcedVisibilityUpdate = true;\n    notifier.SendToSelf();", 1),
            original.replace("Cell::VisitFarVisibleObjects(",
                             "notifier.SendToSelf();\n    Cell::VisitFarVisibleObjects(", 1).replace(
                                 "notifier.SendToSelf();\n\n    if (mapChange)",
                                 "if (mapChange)", 1),
        )
        for changed in mutations:
            self.assertNotEqual(changed, original)
            with self.assertRaises(AssertionError):
                require_viewer_batch_path(changed)

    def test_not_a_synthetic_client_ack_or_positive_hide_proof(self) -> None:
        # Absence of F14 runtime subscription is not proved exhaustively here.
        # No source contract may substitute for a human 3.3.5a client test.
        self.assertNotIn("OnF14ClientReceipt", self.batch())
        self.assertNotIn("OnF14ClientReceipt", self.notifier)
        self.assertNotIn("F14PositiveRestore", self.batch())


if __name__ == "__main__":
    unittest.main()
