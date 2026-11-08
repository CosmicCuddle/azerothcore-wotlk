"""Owner Core F14 player actor client CREATE/OUT seam: default-OFF source tests.
No positive policy, playerbot identity, rights, client or gameplay validation.
"""
from pathlib import Path
import unittest
ROOT = Path(__file__).resolve().parents[2]
UP = (ROOT / "src/server/game/Entities/Player/PlayerUpdates.cpp").read_text()
POLICY = (ROOT / "src/server/game/Entities/Player/F14PlayerClientVisibilityQuery.h").read_text()
H = (ROOT / "src/server/game/Scripting/ScriptDefines/WorldObjectScript.h").read_text()
CPP = (ROOT / "src/server/game/Scripting/ScriptDefines/WorldObjectScript.cpp").read_text()
MGR = (ROOT / "src/server/game/Scripting/ScriptMgr.h").read_text()
PLAYER = (ROOT / "src/server/game/Entities/Player/Player.cpp").read_text()

class F14PlayerClientSource(unittest.TestCase):
    def test_independent_undefined_compile_macro_and_no_positive_default(self):
        macro="COSMIC_F14_PLAYER_CLIENT_VIEWER_QUERY_EXPERIMENT"
        self.assertEqual(UP.count("defined("+macro+")"),1)
        self.assertIn("&& "+macro+" >= 1",UP)
        self.assertNotIn("#define "+macro,UP)
        self.assertNotIn("#define "+macro,POLICY)
        snippet=UP.split("static bool IsF14PlayerSuppressedFromClientView(",1)[1].split("template <class T>",1)[0]
        self.assertIn("(void)viewer;\n    (void)target;\n    return false;\n#endif",snippet)
        self.assertIn("return MayAskScript(c) &&",snippet)

    def test_only_genuine_player_and_headless_race_candidate_can_ask(self):
        snippet=UP.split("static bool IsF14PlayerSuppressedFromClientView(",1)[1].split("template <class T>",1)[0]
        for key in ("!target->IsPlayer()", "target->ToPlayer()", "!viewer->GetSession()",
                    "!actor->GetSession()", "actor == viewer", "viewer->IsGameMaster()",
                    "viewer->GetSession()->IsHeadless()", "actor->GetSession()->IsHeadless()",
                    "actor->getRace() == RACE_BLOODELF", "actor->getRace() == RACE_DRAENEI",
                    "actor->IsGameMaster()", "ShouldF14SuppressPlayerForViewer(viewer, actor)"):
            self.assertIn(key,snippet)
        for field in ("targetIsPlayer","sameIdentity","gmModeViewer","viewerHeadless",
                      "targetHeadless","selectedTbcRace","gmModeTarget"):
            self.assertIn(field,POLICY)
        self.assertIn("c.targetIsPlayer && !c.sameIdentity && !c.gmModeViewer",POLICY)
        self.assertIn("c.targetHeadless && c.selectedTbcRace",POLICY)

    def test_both_client_visibility_overloads_all_four_create_out_transitions(self):
        self.assertIn("void Player::UpdateVisibilityOf(T* target, UpdateData& data,",UP)
        self.assertIn("void Player::UpdateVisibilityOf(WorldObject* target)",UP)
        self.assertEqual(UP.count("IsF14CreatureSuppressedFromClientView(this, target)"),4)
        self.assertEqual(UP.count("IsF14PlayerSuppressedFromClientView(this, target)"),4)
        self.assertEqual(UP.count("IsF14CreatureSuppressedFromClientView(this, target) ||\n            IsF14PlayerSuppressedFromClientView(this, target)"),2)
        self.assertEqual(UP.count("!IsF14CreatureSuppressedFromClientView(this, target) &&\n            !IsF14PlayerSuppressedFromClientView(this, target)"),2)
        for call in ("target->BuildOutOfRangeUpdateBlock(&data);","target->DestroyForPlayer(this);",
                     "target->BuildCreateUpdateBlockForPlayer(&data, this);",
                     "target->SendUpdateToPlayer(this);"):
            self.assertIn(call,UP)

    def test_new_script_hook_and_dispatcher_are_default_false(self):
        self.assertIn("WORLDOBJECTHOOK_ON_F14_VIEWER_PLAYER_SUPPRESSION_QUERY",H)
        self.assertIn("virtual bool OnF14ShouldSuppressPlayerForViewer(",H)
        self.assertIn("Player const* /*viewer*/, Player const* /*actor*/) { return false; }",H)
        self.assertIn("bool ShouldF14SuppressPlayerForViewer(",MGR)
        body=CPP.split("bool ScriptMgr::ShouldF14SuppressPlayerForViewer(",1)[1].split("WorldObjectScript::WorldObjectScript(",1)[0]
        self.assertIn("CALL_ENABLED_BOOLEAN_HOOKS_WITH_DEFAULT_FALSE(",body)
        self.assertIn("script->OnF14ShouldSuppressPlayerForViewer(viewer, actor)",body)
        self.assertNotIn("return true;",body)

    def test_no_global_policy_rewrite_or_stage_or_combat_side_effect(self):
        self.assertIn("bool Player::IsVisibleGloballyFor(Player const* u) const",PLAYER)
        self.assertIn("if (IsVisible())\n        return true;",PLAYER)
        zone=UP.split("static bool IsF14PlayerSuppressedFromClientView(",1)[1].split("template <class T>",1)[0]
        for dangerous in ("SaveToDB(","SetPhaseMask(","ForceUpdateProgressionState(",
                          "SetLootRecipient(","DestroyForPlayer(","SendUpdateToPlayer(",
                          "HasAuditedRowAndUnretiredAdmission("):
            self.assertNotIn(dangerous,zone+POLICY)
        self.assertNotIn("ShouldF14SuppressPlayerForViewer(",PLAYER)

if __name__ == "__main__":
    unittest.main()
