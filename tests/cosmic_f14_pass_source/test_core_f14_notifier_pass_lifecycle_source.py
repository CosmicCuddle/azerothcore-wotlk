"""Actual Core source-bound F14 notifier lifecycle contract, default OFF.

No per-creature GUID, client delivery ACK, user consent or NPC restoration.
Tests specifically reject the old Core7 absence of real pass lifecycle entry.
"""
from pathlib import Path
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[2]
CORE7 = "891e3a622901a525e325b597e52edf2271a98a41"
MACRO = "COSMIC_F14_NOTIFIER_PASS_LIFECYCLE_EXPERIMENT"

def read(p):
    return (ROOT / p).read_text(encoding="utf-8")

def old(p):
    return subprocess.check_output(["git", "show", CORE7 + ":" + p], cwd=ROOT, text=True)


class Core8NotifierPassSource(unittest.TestCase):
    def test_real_core7_absence_and_new_core8_macro_disabled(self):
        cpp = "src/server/game/Grids/Notifiers/GridNotifiers.cpp"
        header = "src/server/game/Grids/Notifiers/GridNotifiers.h"
        self.assertNotIn(MACRO, old(cpp))
        self.assertNotIn("BeginF14NotifierPass", old(header))
        self.assertIn(MACRO, read(cpp))
        self.assertIn(MACRO, read(header))
        # An explicit compile flag, never added in source to silently activate.
        for path in [cpp, header, "src/server/game/Scripting/ScriptMgr.h",
                     "src/server/game/Scripting/ScriptDefines/WorldObjectScript.h",
                     "src/server/game/Scripting/ScriptDefines/WorldObjectScript.cpp"]:
            self.assertNotIn("#define " + MACRO, read(path))

    def test_constructor_before_visitor_and_destructor_once(self):
        header = read("src/server/game/Grids/Notifiers/GridNotifiers.h")
        self.assertIn("i_visibleNow.clear();", header)
        self.assertIn("BeginF14NotifierPass(); // before any near/far", header)
        self.assertIn("~VisibleNotifier();", header)
        self.assertLess(header.index("i_visibleNow.clear();"),
                        header.index("BeginF14NotifierPass(); // before any near/far"))
        cpp = read("src/server/game/Grids/Notifiers/GridNotifiers.cpp")
        begin = cpp.index("void VisibleNotifier::BeginF14NotifierPass()")
        end = cpp.index("VisibleNotifier::~VisibleNotifier()", begin)
        body = cpp[begin:end]
        self.assertIn("!i_player.IsInWorld()", body)
        self.assertIn("!i_player.GetSession()", body)
        self.assertIn("i_player.GetSession()->IsHeadless()", body)
        self.assertIn("f14PassTokenSource.Issue()", body)
        self.assertIn("if (!token)", body)
        self.assertIn("_f14PassToken = token;", body)
        self.assertIn("sScriptMgr->OnF14ViewerVisibilityPassBegin(&i_player, token)", body)
        closing = cpp[cpp.index("VisibleNotifier::~VisibleNotifier()"):cpp.index("void VisibleNotifier::Visit(")]
        self.assertIn("sScriptMgr->OnF14ViewerVisibilityPassEnd(&i_player, _f14PassToken)", closing)
        self.assertEqual(cpp.count("VisibleNotifier::~VisibleNotifier()"), 1)
        self.assertIn("PlayerRelocationNotifier(Player& player): VisibleNotifier(player, false)", header)

    def test_actual_core_batched_send_attempt_never_per_guid(self):
        cpp = read("src/server/game/Grids/Notifiers/GridNotifiers.cpp")
        start = cpp.index("void VisibleNotifier::SendToSelf()")
        end = cpp.index("void VisibleChangesNotifier::Visit(", start)
        body = cpp[start:end]
        self.assertLess(body.index("ZoneWideVisibleWorldObjectsSet"),
                        body.index("if (!i_data.HasData())"))
        self.assertLess(body.index("if (!i_data.HasData())"),
                        body.index("i_player.SendDirectMessage(&packet);"))
        self.assertLess(body.index("i_player.SendDirectMessage(&packet);"),
                        body.index("OnF14ViewerVisibilityPassBatchSendAttempt("))
        self.assertEqual(body.count("OnF14ViewerVisibilityPassBatchSendAttempt("), 1)
        self.assertNotIn("OnF14GuidCreate", body)
        self.assertNotIn("clientAck", body)
        self.assertNotIn("ClearRestoreDebt", body)

    def test_three_batched_entrants_but_direct_route_has_no_token(self):
        player = read("src/server/game/Entities/Player/PlayerUpdates.cpp")
        unit = read("src/server/game/Entities/Unit/Unit.cpp")
        self.assertEqual(player.count("Acore::VisibleNotifier notifier(*this, mapChange);"), 1)
        self.assertEqual(unit.count("Acore::PlayerRelocationNotifier notifier(*player);"), 2)
        p = player.index("void Player::UpdateVisibilityOf(WorldObject* target)")
        end = player.index("void Player::UpdateTriggerVisibility()", p)
        direct = player[p:end]
        self.assertIn("target->DestroyForPlayer(this);", direct)
        self.assertIn("target->SendUpdateToPlayer(this);", direct)
        self.assertNotIn("VisibleNotifier", direct)
        self.assertNotIn(MACRO, direct)
        self.assertEqual(player, old("src/server/game/Entities/Player/PlayerUpdates.cpp"))
        self.assertEqual(unit, old("src/server/game/Entities/Unit/Unit.cpp"))

    def test_real_script_hook_registration_and_no_positive_listener(self):
        h = read("src/server/game/Scripting/ScriptDefines/WorldObjectScript.h")
        mgr = read("src/server/game/Scripting/ScriptMgr.h")
        call = read("src/server/game/Scripting/ScriptDefines/WorldObjectScript.cpp")
        for key in ("BEGIN", "BATCH_SEND_ATTEMPT", "END"):
            self.assertIn("WORLDOBJECTHOOK_ON_F14_VIEWER_VISIBILITY_PASS_" + key, h)
            self.assertIn("WORLDOBJECTHOOK_ON_F14_VIEWER_VISIBILITY_PASS_" + key, call)
        for key in ("Begin", "BatchSendAttempt", "End"):
            self.assertIn("OnF14ViewerVisibilityPass" + key + "(", mgr)
            self.assertIn("OnF14ViewerVisibilityPass" + key + "(", call)
        self.assertIn("CALL_ENABLED_HOOKS(", call)
        self.assertIn("virtual void OnF14ViewerVisibilityPassBegin(", h)
        self.assertIn("virtual void OnF14ViewerVisibilityPassEnd(", h)
        self.assertEqual(read("src/server/game/Scripting/ScriptMgr.cpp"),
                         old("src/server/game/Scripting/ScriptMgr.cpp"))
        for key in ("OnF14ShouldSuppressCreatureForViewer(", "OnF14ShouldSuppressPlayerForViewer(",
                    "OnF14ShouldExcludeWhoEntryForViewer("):
            self.assertIn(key, h)
        self.assertIn("return false;", h)
        self.assertNotIn("COSMIC_F14_VISIBILITY_REFRESH_HOOK_VERSION",
                         h + mgr + call)

    def test_core7_existing_pure_producer_model_retained(self):
        for p in ("tests/cosmic_f14_pass_source/F14CorePassScopeProvenanceModel.h",
                  "tests/cosmic_f14_pass_source/f14_core_pass_scope_model.cpp"):
            self.assertEqual(read(p), old(p))


if __name__ == "__main__":
    unittest.main()
