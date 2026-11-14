"""Guarded pre-removal hook: exact insertion-only source regression."""
import pathlib
import subprocess

ROOT = pathlib.Path(__file__).resolve().parents[2]
PARENT = "ee13e1edfdbccd56895f3adefc2b32df94ef19e9"
GUARD = "#if defined(COSMIC_F14_PRE_MAP_REMOVAL_EXPERIMENT) && COSMIC_F14_PRE_MAP_REMOVAL_EXPERIMENT >= 1"
PATHS = {
    "src/server/game/Scripting/ScriptDefines/AllMapScript.h": [
        ("    ALLMAPHOOK_END\n", GUARD + "\n    ALLMAPHOOK_ON_F14_BEFORE_PLAYER_MAP_REMOVAL,\n#endif\n    ALLMAPHOOK_END\n"),
        ("    [[nodiscard]] virtual bool CanSendObjectUpdatesToPlayer(Map* /*map*/, Player* /*player*/) { return true; }\n",
         "    [[nodiscard]] virtual bool CanSendObjectUpdatesToPlayer(Map* /*map*/, Player* /*player*/) { return true; }\n\n" +
         GUARD + "\n    // F14 observation only: synchronous pre-removal source boundary, not a\n"
         "    // permanent worker-thread proof. Do not retain pointers, veto removal,\n"
         "    // dispatch visibility, or mutate gameplay from this callback.\n"
         "    virtual void OnF14BeforePlayerMapRemoval(Map const* /*map*/, Player const* /*player*/, bool /*remove*/) { }\n#endif\n"),
    ],
    "src/server/game/Scripting/ScriptMgr.h": [
        ("    void OnPlayerLeaveMap(Map* map, Player* player);\n",
         "    void OnPlayerLeaveMap(Map* map, Player* player);\n" + GUARD +
         "\n    void OnF14BeforePlayerMapRemoval(Map const* map, Player const* player, bool remove);\n#endif\n"),
    ],
    "src/server/game/Scripting/ScriptDefines/AllMapScript.cpp": [
        ("void ScriptMgr::OnPlayerLeaveMap(Map* map, Player* player)\n",
         GUARD + "\nvoid ScriptMgr::OnF14BeforePlayerMapRemoval(Map const* map, Player const* player, bool remove)\n"
         "{\n    ASSERT(map);\n    ASSERT(player);\n"
         "    CALL_ENABLED_HOOKS(AllMapScript, ALLMAPHOOK_ON_F14_BEFORE_PLAYER_MAP_REMOVAL,\n"
         "                       script->OnF14BeforePlayerMapRemoval(map, player, remove));\n"
         "}\n#endif\n\nvoid ScriptMgr::OnPlayerLeaveMap(Map* map, Player* player)\n"),
    ],
    "src/server/game/Maps/Map.cpp": [
        ("void Map::RemovePlayerFromMap(Player* player, bool remove)\n{\n",
         "void Map::RemovePlayerFromMap(Player* player, bool remove)\n{\n" + GUARD +
         "\n    // Observe the old map/player identity before any removal-side effects.\n"
         "    // This is not permission to block teardown or send a visibility update.\n"
         "    sScriptMgr->OnF14BeforePlayerMapRemoval(this, player, remove);\n#endif\n"),
    ],
}

def require(ok, message):
    if not ok:
        raise SystemExit(message)

for path, replacements in PATHS.items():
    expected = subprocess.check_output(["git", "show", PARENT + ":" + path], cwd=ROOT).decode()
    for old, new in replacements:
        require(expected.count(old) == 1, "unexpected exact parent anchor " + path)
        expected = expected.replace(old, new, 1)
    current = (ROOT / path).read_text()
    require(current == expected, "non-exact guarded insertion " + path)

mapcpp = (ROOT / "src/server/game/Maps/Map.cpp").read_text()
body = mapcpp.split("void Map::RemovePlayerFromMap(Player* player, bool remove)", 1)[1].split("void Map::AfterPlayerUnlinkFromMap", 1)[0]
call = "sScriptMgr->OnF14BeforePlayerMapRemoval(this, player, remove);"
require(body.count(call) == 1 and body.index(call) < body.index("UpdatePlayerZoneStats"), "hook must precede first removal effect")
require(body.index(call) < body.index("player->RemoveFromWorld()") < body.index("sScriptMgr->OnPlayerLeaveMap"), "invalid old/new hook ordering")
print("PASS byte-exact guarded insertion, default-OFF source, const observer and pre-removal ordering")
