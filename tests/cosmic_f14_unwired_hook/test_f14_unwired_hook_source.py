#!/usr/bin/env python3
"""Core fork: F14 default-FALSE SCRIPT API, NEVER called by visibility code.

Source contracts only. This is not a compile, gameplay, client or SQL test.
No feature may be enabled by this draft.
"""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]
MGR = ROOT / "src/server/game/Scripting/ScriptMgr.h"
WORLD_H = ROOT / "src/server/game/Scripting/ScriptDefines/WorldObjectScript.h"
WORLD_CPP = ROOT / "src/server/game/Scripting/ScriptDefines/WorldObjectScript.cpp"
MACROS = ROOT / "src/server/game/Scripting/ScriptMgrMacros.h"
UPDATES = ROOT / "src/server/game/Entities/Player/PlayerUpdates.cpp"
VIEW = ROOT / "src/server/game/Entities/Object/Object.cpp"


class DefaultFalseScriptAPIGuard(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.mgr = MGR.read_text(encoding="utf-8")
        cls.header = WORLD_H.read_text(encoding="utf-8")
        cls.cpp = WORLD_CPP.read_text(encoding="utf-8")
        cls.macros = MACROS.read_text(encoding="utf-8")
        cls.updates = UPDATES.read_text(encoding="utf-8")
        cls.view = VIEW.read_text(encoding="utf-8")

    def test_exact_one_query_declaration(self):
        self.assertEqual(self.mgr.count("bool ShouldF14SuppressCreatureForViewer("), 1)
        self.assertIn("Player const* viewer, WorldObject const* creature);", self.mgr)
        self.assertIn("public: /* WorldObjectScript */", self.mgr)

    def test_world_script_hook_default_false(self):
        self.assertEqual(self.header.count("WORLDOBJECTHOOK_ON_F14_VIEWER_CREATURE_SUPPRESSION_QUERY"), 1)
        self.assertEqual(self.header.count("OnF14ShouldSuppressCreatureForViewer("), 1)
        self.assertIn("Player const* /*viewer*/, WorldObject const* /*creature*/) { return false; }",
                      self.header)
        self.assertLess(
            self.header.index("WORLDOBJECTHOOK_ON_WORLD_OBJECT_UPDATE,"),
            self.header.index("WORLDOBJECTHOOK_ON_F14_VIEWER_CREATURE_SUPPRESSION_QUERY,"),
        )
        self.assertLess(
            self.header.index("WORLDOBJECTHOOK_ON_F14_VIEWER_CREATURE_SUPPRESSION_QUERY,"),
            self.header.index("WORLDOBJECTHOOK_END"),
        )

    def test_manager_dispatch_has_default_false_contract(self):
        self.assertEqual(self.cpp.count("bool ScriptMgr::ShouldF14SuppressCreatureForViewer("), 1)
        self.assertEqual(self.cpp.count("CALL_ENABLED_BOOLEAN_HOOKS_WITH_DEFAULT_FALSE("), 1)
        self.assertIn(
            "WorldObjectScript, WORLDOBJECTHOOK_ON_F14_VIEWER_CREATURE_SUPPRESSION_QUERY,",
            self.cpp,
        )
        self.assertIn("script->OnF14ShouldSuppressCreatureForViewer(viewer, creature)", self.cpp)
        self.assertIn("ASSERT(viewer && creature);", self.cpp)

    def test_macro_rejects_no_listeners_and_no_positive_response(self):
        body = self.macros.split(
            "#define CALL_ENABLED_BOOLEAN_HOOKS_WITH_DEFAULT_FALSE(", 1
        )[1].split("\n\n#endif", 1)[0]
        self.assertIn("EnabledHooks[hookType].empty()", body)
        self.assertIn("return false;", body)
        self.assertIn("if (action) return true;", body)
        self.assertEqual(body.count("return false;"), 2)

    def test_no_active_visibility_or_packet_call_site(self):
        # Critical: adding an API does NOT wire it to Player's two visibility
        # pathways, even for real Blood Elf/Draenei human actors.
        for name, source in (("PlayerUpdates", self.updates), ("WorldObject", self.view)):
            with self.subTest(name=name):
                self.assertNotIn("ShouldF14SuppressCreatureForViewer", source)
                self.assertNotIn("f14PositiveSuppression", source)
                self.assertNotIn("F14BeforeCreatureSuppression", source)
        self.assertNotIn("OnF14CreatureSuppressionDecision", self.cpp)
        self.assertNotIn("SendUpdateToPlayer(", self.cpp)

    def test_original_world_object_lifecycle_hooks_remain(self):
        for name in (
            "OnWorldObjectDestroy", "OnWorldObjectCreate", "OnWorldObjectSetMap",
            "OnWorldObjectResetMap", "OnWorldObjectUpdate",
        ):
            with self.subTest(hook=name):
                self.assertIn("void ScriptMgr::" + name, self.cpp)
                self.assertIn("void " + name + "(", self.mgr)
                self.assertIn("virtual void " + name + "(", self.header)


if __name__ == "__main__":
    unittest.main()
