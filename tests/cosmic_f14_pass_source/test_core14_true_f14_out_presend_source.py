"""Core14 actual Core-only pre-send source seam, NO IP consumer or client ACK."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]
NOTIFIER = (ROOT / "src/server/game/Grids/Notifiers/GridNotifiers.cpp").read_text()
NOTIFIER_H = (ROOT / "src/server/game/Grids/Notifiers/GridNotifiers.h").read_text()
PLAYER = (ROOT / "src/server/game/Entities/Player/PlayerUpdates.cpp").read_text()
DATA = (ROOT / "src/server/game/Entities/Object/Updates/UpdateData.h").read_text()
DATA_CPP = (ROOT / "src/server/game/Entities/Object/Updates/UpdateData.cpp").read_text()
WORLD_H = (ROOT / "src/server/game/Scripting/ScriptDefines/WorldObjectScript.h").read_text()
WORLD_CPP = (ROOT / "src/server/game/Scripting/ScriptDefines/WorldObjectScript.cpp").read_text()
MANAGER_H = (ROOT / "src/server/game/Scripting/ScriptMgr.h").read_text()

CALL = "sScriptMgr->OnF14ViewerCreaturePositiveQueryOutPreSend("
SOURCE_MACRO = "COSMIC_F14_PRE_SEND_CREATURE_OUT_EVIDENCE_EXPERIMENT"
class PreSendSource(unittest.TestCase):
    def test_real_send_site_not_fake_postsend_simulation(self):
        self.assertEqual(NOTIFIER.count(CALL), 1)
        body = NOTIFIER.split("void VisibleNotifier::SendToSelf()", 1)[1].split(
            "void VisibleChangesNotifier::Visit(", 1)[0]
        a, b, c = [body.index(key) for key in (
            "i_data.BuildPacket(packet);", CALL, "i_player.SendDirectMessage(&packet);")]
        self.assertLess(a, b)
        self.assertLess(b, c)
        self.assertLess(c, body.index("OnF14ViewerCreatureBatchGuidSendAttempt("))
        self.assertLess(body.index("_f14PreSendDispatched = true;"), b)
        self.assertIn("if (_f14PassToken && !_f14PreSendDispatched)", body)
        self.assertIn("!i_data.F14PositiveQueryOutCandidatesPoisoned()", body)

    def test_source_cause_requires_ordinary_visibility_and_actual_out(self):
        tpl = PLAYER.split("template <class T>\nvoid Player::UpdateVisibilityOf(", 1)[1].split(
            "void Player::GetInitialVisiblePackets", 1)[0]
        self.assertIn("ordinaryVisible && IsF14CreatureSuppressedFromClientView(this, target)",tpl)
        self.assertIn("if (!ordinaryVisible || positiveF14CreatureQuery ||",tpl)
        self.assertIn("F14EligibleUncontrolledCreatureBatchCandidate(target)",tpl)
        block = tpl.index("target->BuildOutOfRangeUpdateBlock(&data);")
        nomination = tpl.index("data.F14RecordPositiveQueryCreatureOut(target->GetGUID());")
        self.assertLess(block, nomination)
        self.assertLess(nomination, tpl.index("GetObjectVisibilityContainer().UnlinkWorldObjectVisibility(target);"))
        # Existing original paths still available when new macro is undefined.
        self.assertIn("#else\n        if (!CanSeeOrDetect(target, false, true) ||",tpl)
        self.assertIn("!IsF14CreatureSuppressedFromClientView(this, target)",tpl)

    def test_no_direct_or_restore_create_evidence(self):
        direct = PLAYER.split("void Player::UpdateVisibilityOf(WorldObject* target)",1)[1].split(
            "void Player::UpdateTriggerVisibility()",1)[0]
        self.assertNotIn("F14RecordPositiveQueryCreatureOut(", direct)
        self.assertNotIn("OnF14ViewerCreaturePositiveQueryOutPreSend(", direct)
        tpl=PLAYER.split("template <class T>\nvoid Player::UpdateVisibilityOf(",1)[1].split(
            "void Player::GetInitialVisiblePackets",1)[0]
        create=tpl.split("else\n    {\n        if (CanSeeOrDetect(",1)[1]
        self.assertNotIn("F14RecordPositiveQueryCreatureOut(", create)

    def test_opted_only_with_three_explicit_parent_macros(self):
        for text in (WORLD_H, DATA):
            self.assertIn("defined("+SOURCE_MACRO+")",text)
            for flag in ("COSMIC_F14_NOTIFIER_PASS_LIFECYCLE_EXPERIMENT",
                         "COSMIC_F14_CREATURE_BATCH_GUID_EVIDENCE_EXPERIMENT",
                         "COSMIC_F14_CREATURE_CLIENT_VIEWER_QUERY_EXPERIMENT"):
                self.assertIn("!defined("+flag+")",text)
            self.assertIn("#error",text)
            self.assertNotIn("#define "+SOURCE_MACRO,text)
        self.assertIn("bool _f14PreSendDispatched = false;",NOTIFIER_H)

    def test_no_partial_packet_evidence_and_no_serialization(self):
        for needle in ("_f14PositiveQueryOutCandidates.size() >= 256",
                       "_f14PositiveQueryOutPoisoned = true;",
                       "F14CreatureCandidatesPoisoned()",
                       "c.direction == CosmicF14::CreatureBatchDirection::OutOfRange",
                       "if (!actualOutBlock)"):
            self.assertIn(needle, DATA)
        self.assertIn("_f14PositiveQueryOutPoisoned = true;", DATA_CPP)
        self.assertIn("_f14PositiveQueryOutPoisoned = false;", DATA_CPP)
        self.assertIn("_f14PositiveQueryOutCandidates.clear();",DATA_CPP)
        self.assertNotIn("F14PositiveQueryOutCandidates()",DATA_CPP.split(
            "bool UpdateData::BuildPacket(",1)[1].split("void UpdateData::Clear()",1)[0])

    def test_script_callback_is_event_not_positive_policy(self):
        self.assertIn("virtual void OnF14ViewerCreaturePositiveQueryOutPreSend(",WORLD_H)
        self.assertIn("void OnF14ViewerCreaturePositiveQueryOutPreSend(",MANAGER_H)
        self.assertIn("void ScriptMgr::OnF14ViewerCreaturePositiveQueryOutPreSend(",WORLD_CPP)
        self.assertIn("CALL_ENABLED_HOOKS(",WORLD_CPP)
        self.assertIn("WorldObjectScript, WORLDOBJECTHOOK_ON_F14_VIEWER_CREATURE_POSITIVE_QUERY_OUT_PRESEND,",WORLD_CPP)
        self.assertIn("WorldObject const* /*creature*/) { return false; }",WORLD_H)

    def test_mutation_send_after_old_core_callback_would_be_detected(self):
        body=NOTIFIER.split("void VisibleNotifier::SendToSelf()",1)[1].split(
            "void VisibleChangesNotifier::Visit(",1)[0]
        changed=body.replace("    i_player.SendDirectMessage(&packet);",
             "    i_player.SendDirectMessage(&packet);\n    "+CALL+" &i_player, ObjectGuid{}, 1);",1)
        self.assertNotEqual(changed,body)
        self.assertFalse(changed.rfind(CALL) < changed.rfind("i_player.SendDirectMessage(&packet);"))
        self.assertTrue(body.index(CALL) < body.index("i_player.SendDirectMessage(&packet);"))
if __name__ == "__main__":
    unittest.main()
