"""F14 /who GUID gate: placement and default-FALSE regression only.

No live WHO packet, Playerbot GUID origin, IP era, SQL or gameplay proof.
"""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]
MISC = ROOT / "src/server/game/Handlers/MiscHandler.cpp"
MGR = ROOT / "src/server/game/Scripting/ScriptMgr.h"
HDR = ROOT / "src/server/game/Scripting/ScriptDefines/WorldObjectScript.h"
IMPL = ROOT / "src/server/game/Scripting/ScriptDefines/WorldObjectScript.cpp"
PLAYER = ROOT / "src/server/game/Entities/Player/PlayerUpdates.cpp"


class WhoGuidSourceSafety(unittest.TestCase):
    def test_compile_time_off_by_default(self):
        src = MISC.read_text()
        flag = "COSMIC_F14_WHO_GUID_QUERY_EXPERIMENT"
        self.assertEqual(src.count("defined(" + flag + ")"), 1)
        self.assertIn("&& " + flag + " >= 1", src)
        self.assertNotIn("#define " + flag, src)
        self.assertNotIn("ShouldF14ExcludeWhoEntryForViewer", PLAYER.read_text())

    def test_query_before_both_counts_and_serialization(self):
        src = MISC.read_text()
        gate = src.index("sScriptMgr->ShouldF14ExcludeWhoEntryForViewer(")
        self.assertLess(src.index("if (!s_show)"), gate)
        self.assertLess(gate, src.index("if ((matchCount++)"))
        self.assertLess(gate, src.index("data << target.GetPlayerName()"))
        self.assertIn("data.put(0, displaycount)", src)
        self.assertIn("data.put(4, matchCount)", src)
        self.assertIn("++displaycount;", src)
        self.assertIn("continue;\n#endif\n\n        // 49", src)

    def test_exact_guid_is_lookup_key_not_race_only(self):
        src = MISC.read_text()
        self.assertIn("!target.GetGuid().IsEmpty()", src)
        self.assertIn("target.GetGuid() != _player->GetGUID()", src)
        self.assertIn("target.GetGuid(), race)", src)
        self.assertIn("(race == RACE_BLOODELF || race == RACE_DRAENEI)", src)
        self.assertIn("AccountMgr::IsPlayerAccount(GetSecurity())", src)
        self.assertIn("!_player->IsGameMaster()", src)

    def test_new_hook_is_default_false(self):
        h = HDR.read_text()
        s = MGR.read_text()
        c = IMPL.read_text()
        self.assertIn("WORLDOBJECTHOOK_ON_F14_WHO_GUID_EXCLUSION_QUERY", h)
        self.assertIn("virtual bool OnF14ShouldExcludeWhoEntryForViewer(", h)
        self.assertIn("uint32 /*cachedRace*/) { return false; }", h)
        self.assertIn("bool ShouldF14ExcludeWhoEntryForViewer(", s)
        self.assertIn("CALL_ENABLED_BOOLEAN_HOOKS_WITH_DEFAULT_FALSE(", c)
        self.assertIn("WORLDOBJECTHOOK_ON_F14_WHO_GUID_EXCLUSION_QUERY", c)
        self.assertIn("script->OnF14ShouldExcludeWhoEntryForViewer(viewer, targetGuid, cachedRace)", c)

    def test_no_positive_creator_or_character_write(self):
        h = HDR.read_text()
        c = IMPL.read_text()
        src = MISC.read_text()
        who = src[src.index("// F14 /who client-list query"):src.index("// 49 is maximum player count")]
        for forbidden in ("SaveToDB(", "CommitTransaction(", "SetPhaseMask(", "SetLootRecipient(", "F14StageWriter("):
            self.assertNotIn(forbidden, who)
        self.assertNotIn("OnF14ShouldExcludeWhoEntryForViewer(", PLAYER.read_text())
        self.assertNotIn("return true;", h[h.index("virtual bool OnF14ShouldExcludeWhoEntryForViewer("):])
        self.assertNotIn("return true;", c[c.index("bool ScriptMgr::ShouldF14ExcludeWhoEntryForViewer("):])


if __name__ == "__main__":
    unittest.main()
