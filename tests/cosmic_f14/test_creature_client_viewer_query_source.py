"""F14 Core player CREATE/OUT source gates: compile-time OFF, NPC only.

No positive policy, saved progression, DB state, action denial or live
visibility change is exercised or inferred by this source-only check.
"""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[2]
PLAYER = ROOT / "src/server/game/Entities/Player/PlayerUpdates.cpp"
POLICY = ROOT / "src/server/game/Entities/Player/F14CreatureClientVisibilityQuery.h"
WORLDSCRIPT = ROOT / "src/server/game/Scripting/ScriptDefines/WorldObjectScript.cpp"
WORLDSCRIPT_H = ROOT / "src/server/game/Scripting/ScriptDefines/WorldObjectScript.h"
SCRIPTMGR_H = ROOT / "src/server/game/Scripting/ScriptMgr.h"


class NativeSourceContract(unittest.TestCase):
    def test_one_strict_opt_in_and_default_false(self):
        src = PLAYER.read_text(encoding="utf-8")
        name = "COSMIC_F14_CREATURE_CLIENT_VIEWER_QUERY_EXPERIMENT"
        self.assertEqual(src.count("defined(" + name + ")"), 1)
        self.assertIn("&& " + name + " >= 1", src)
        self.assertIn("    (void)viewer;\n    (void)target;\n    return false;\n#endif", src)
        self.assertNotIn("#define " + name, src)
        self.assertNotIn("#define " + name, POLICY.read_text(encoding="utf-8"))
        self.assertIn('return MayAskScript(c) &&', src)
        self.assertIn('sScriptMgr->ShouldF14SuppressCreatureForViewer(viewer, creature)', src)

    def test_both_actual_player_visibility_overloads_are_covered(self):
        src = PLAYER.read_text(encoding="utf-8")
        self.assertEqual(
            src.count("IsF14CreatureSuppressedFromClientView(this, target)"), 5)
        # Four original guarded caller positions remain, plus exactly one
        # separate compile-off positive-query reason before a real batch OUT.
        self.assertIn('COSMIC_F14_PRE_SEND_CREATURE_OUT_EVIDENCE_EXPERIMENT', src)

        self.assertEqual(
            src.count("!CanSeeOrDetect(target, false, true) ||\n"
                      "            IsF14CreatureSuppressedFromClientView(this, target)"), 2)
        self.assertEqual(
            src.count("CanSeeOrDetect(target, false, true) &&\n"
                      "            !IsF14CreatureSuppressedFromClientView(this, target)"), 2)
        self.assertIn(
            "void Player::UpdateVisibilityOf(T* target, UpdateData& data,", src)
        self.assertIn("void Player::UpdateVisibilityOf(WorldObject* target)", src)
        self.assertIn("target->BuildOutOfRangeUpdateBlock(&data);", src)
        self.assertIn("target->DestroyForPlayer(this);", src)
        self.assertIn("target->BuildCreateUpdateBlockForPlayer(&data, this);", src)
        self.assertIn("target->SendUpdateToPlayer(this);", src)
        self.assertIn("GetObjectVisibilityContainer().UnlinkWorldObjectVisibility(target)", src)
        self.assertIn("GetObjectVisibilityContainer().LinkWorldObjectVisibility(target)", src)

    def test_noncreatures_owned_pets_and_gmmode_do_not_query(self):
        src = PLAYER.read_text(encoding="utf-8")
        hdr = POLICY.read_text(encoding="utf-8")
        self.assertIn("if (!viewer || !target || !target->IsCreature())", src)
        self.assertIn("viewer->IsGameMaster()", src)
        self.assertIn("creature->GetCharmerOrOwnerGUID()", src)
        for method in ("IsPet", "IsSummon", "IsGuardian", "IsTotem"):
            self.assertIn("creature->" + method + "()", src)
        for gate in ("targetIsCreature", "gmModeViewer", "ownedOrCharmed",
                     "pet", "summon", "guardian", "totem"):
            self.assertIn(gate, hdr)
        self.assertIn("return c.targetIsCreature && !c.gmModeViewer", hdr)
        self.assertNotIn("SetPhaseMask", src[src.index("static bool IsF14CreatureSuppressedFromClientView"):src.index("template <class T>\nvoid Player::UpdateVisibilityOf")])
        self.assertNotIn("SaveToDB", hdr)

    def test_script_is_unwired_default_false_and_not_positive(self):
        impl = WORLDSCRIPT.read_text(encoding="utf-8")
        hdr = WORLDSCRIPT_H.read_text(encoding="utf-8")
        mgr = SCRIPTMGR_H.read_text(encoding="utf-8")
        self.assertIn("CALL_ENABLED_BOOLEAN_HOOKS_WITH_DEFAULT_FALSE(", impl)
        self.assertIn("WORLDOBJECTHOOK_ON_F14_VIEWER_CREATURE_SUPPRESSION_QUERY", impl)
        self.assertIn("script->OnF14ShouldSuppressCreatureForViewer(viewer, creature)", impl)
        self.assertIn("virtual bool OnF14ShouldSuppressCreatureForViewer(", hdr)
        self.assertIn("WorldObject const* /*creature*/) { return false; }", hdr)
        self.assertIn("bool ShouldF14SuppressCreatureForViewer(", mgr)
        self.assertNotIn("OnF14ShouldSuppressCreatureForViewer(", PLAYER.read_text(encoding="utf-8"))

    def test_unrelated_policies_not_added_here(self):
        src = PLAYER.read_text(encoding="utf-8")
        hdr = POLICY.read_text(encoding="utf-8")
        for forbidden in (
            "SetPhaseMask(", "CommitTransaction(", "SaveToDB(",
            "ForceValuesUpdateAtIndex(", "UpdateObjectVisibility(true)",
            "SetLootRecipient(", "SetGUIDValue(", "F14StageWriter(",
        ):
            self.assertNotIn(forbidden, hdr)
        # The experiment must NOT add new on-map recipient writes; the rest
        # of the stock PlayerUpdates.cpp remains upstream-defined.
        snippet = src[src.index("static bool IsF14CreatureSuppressedFromClientView"):
                      src.index("template <class T>\nvoid Player::UpdateVisibilityOf")]
        for forbidden in ("SaveToDB(", "CommitTransaction(", "SetPhaseMask(",
                          "SetLootRecipient(", "ForceValuesUpdateAtIndex("):
            self.assertNotIn(forbidden, snippet)


if __name__ == "__main__":
    unittest.main()
