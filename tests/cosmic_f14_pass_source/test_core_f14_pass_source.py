"""Source provenance only for a FUTURE F14 Core visibility-pass owner.
No production producer, positive listener, packet ACK or live client exists.
"""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]

def source(path):
    return (ROOT / path).read_text(encoding="utf-8")

class CoreF14PassSource(unittest.TestCase):
    def test_notifier_is_one_owning_stack_object_before_visit(self):
        h = source("src/server/game/Grids/Notifiers/GridNotifiers.h")
        self.assertIn("struct VisibleNotifier", h)
        self.assertIn("VisibleNotifier(Player& player, bool gobjOnly)", h)
        self.assertIn("PlayerRelocationNotifier(Player& player): VisibleNotifier(player, false)", h)
        self.assertIn("i_visibleNow.clear();", h)
        self.assertIn("UpdateData i_data;", h)
        self.assertNotIn("F14VisibilityPassToken", h)
        # Core8 adds an opt-in-only stack destructor to close a real pass.
        # The original Core7 absence assertion must evolve, not disappear.
        self.assertIn("~VisibleNotifier();", h)
        self.assertIn("COSMIC_F14_NOTIFIER_PASS_LIFECYCLE_EXPERIMENT", h)
        self.assertIn("BeginF14NotifierPass();", h)

    def test_visits_receive_same_visitor_by_reference(self):
        h = source("src/server/game/Grids/Cells/Cell.h")
        impl = source("src/server/game/Grids/Cells/CellImpl.h")
        self.assertIn("VisitObjects(WorldObject const* obj, T& visitor", h)
        self.assertIn("VisitFarVisibleObjects(WorldObject const* obj, T& visitor", h)
        self.assertIn("TypeContainerVisitor<T, GridTypeMapContainer> gnotifier(visitor)", impl)
        self.assertIn("TypeContainerVisitor<T, FarVisibleGridContainer> gnotifier(visitor)", impl)

    def test_three_real_batched_source_entrants(self):
        player = source("src/server/game/Entities/Player/PlayerUpdates.cpp")
        unit = source("src/server/game/Entities/Unit/Unit.cpp")
        start = player.index("void Player::UpdateVisibilityForPlayer(bool mapChange)")
        end = player.index("void Player::UpdateObjectVisibility(", start)
        body = player[start:end]
        self.assertEqual(body.count("Acore::VisibleNotifier notifier(*this, mapChange);"), 1)
        self.assertEqual(body.count("notifier.SendToSelf();"), 1)
        self.assertLess(body.index("Acore::VisibleNotifier notifier"), body.index("Cell::VisitObjects"))
        self.assertLess(body.index("Cell::VisitObjects"), body.index("Cell::VisitFarVisibleObjects"))
        self.assertLess(body.index("Cell::VisitFarVisibleObjects"), body.index("notifier.SendToSelf"))

        start = unit.index("void Unit::ExecuteDelayedUnitRelocationEvent()")
        end = unit.index("else if (Creature* unit = this->ToCreature())", start)
        body = unit[start:end]
        self.assertEqual(body.count("Acore::PlayerRelocationNotifier notifier(*player);"), 2)
        self.assertEqual(body.count("notifier.SendToSelf();"), 2)
        self.assertEqual(body.count("Cell::VisitFarVisibleObjects("), 2)
        # Two stack instances are NOT one global traversal shared across viewers.
        self.assertIn("if (this->HasSharedVision())", body)
        self.assertIn("if (Player* player = this->ToPlayer())", body)

    def test_zone_wide_append_early_empty_and_one_actual_batch_send(self):
        cpp = source("src/server/game/Grids/Notifiers/GridNotifiers.cpp")
        start = cpp.index("void VisibleNotifier::SendToSelf()")
        end = cpp.index("void VisibleChangesNotifier::Visit(", start)
        body = cpp[start:end]
        for literal in ("ZoneWideVisibleWorldObjectsSet",
                        "i_data.AddOutOfRangeGUID(", "if (!i_data.HasData())",
                        "i_data.BuildPacket(packet);", "i_player.SendDirectMessage(&packet);"):
            self.assertIn(literal, body)
        self.assertLess(body.index("ZoneWideVisibleWorldObjectsSet"),
                        body.index("if (!i_data.HasData())"))
        self.assertLess(body.index("if (!i_data.HasData())"), body.index("i_data.BuildPacket(packet);"))
        self.assertLess(body.index("i_data.BuildPacket(packet);"), body.index("i_player.SendDirectMessage(&packet);"))
        self.assertEqual(body.count("i_player.SendDirectMessage(&packet);"), 1)
        self.assertNotIn("F14VisibilityPassToken", body)

    def test_immediate_overload_bypasses_visible_notifier_batch(self):
        player = source("src/server/game/Entities/Player/PlayerUpdates.cpp")
        start = player.index("void Player::UpdateVisibilityOf(WorldObject* target)")
        end = player.index("void Player::UpdateTriggerVisibility()", start)
        body = player[start:end]
        self.assertIn("target->DestroyForPlayer(this);", body)
        self.assertIn("target->SendUpdateToPlayer(this);", body)
        self.assertNotIn("VisibleNotifier", body)
        self.assertNotIn("SendToSelf", body)
        self.assertNotIn("SendDirectMessage", body)

    def test_core6_positive_queries_still_compiled_default_false(self):
        player = source("src/server/game/Entities/Player/PlayerUpdates.cpp")
        who = source("src/server/game/Handlers/MiscHandler.cpp")
        world = source("src/server/game/Scripting/ScriptDefines/WorldObjectScript.h")
        self.assertIn("COSMIC_F14_PLAYER_CLIENT_VIEWER_QUERY_EXPERIMENT", player)
        self.assertIn("COSMIC_F14_CREATURE_CLIENT_VIEWER_QUERY_EXPERIMENT", player)
        self.assertIn("COSMIC_F14_WHO_GUID_QUERY_EXPERIMENT", who)
        self.assertIn("return false;", world)
        self.assertNotIn("F14CorePassScopeProvenanceModel", player)
        self.assertNotIn("F14CorePassScopeProvenanceModel", who)
        self.assertNotIn("F14CorePassScopeProvenanceModel", world)

    def test_no_positive_runtime_producer_or_client_ack(self):
        header = source("tests/cosmic_f14_pass_source/F14CorePassScopeProvenanceModel.h")
        self.assertIn("TESTS ONLY", header)
        self.assertIn("ImmediateDirect", header)
        self.assertIn("actualTaggedSendCall", header)
        self.assertIn("~ScopedPass() { _tracker.Close(_viewer, _token); }", header)
        self.assertNotIn("SendDirectMessage(", header)
        self.assertNotIn("CharacterDatabase", header)
        self.assertNotIn("BuildCreateUpdateBlockForPlayer(", header)

if __name__ == "__main__":
    unittest.main()
