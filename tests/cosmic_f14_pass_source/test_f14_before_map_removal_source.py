"""Guarded pre-removal hook: exact source delta and default-OFF invariants."""
import pathlib
import re
import subprocess

ROOT = pathlib.Path(__file__).resolve().parents[2]
PARENT = "ee13e1edfdbccd56895f3adefc2b32df94ef19e9"
GUARD = "#if defined(COSMIC_F14_PRE_MAP_REMOVAL_EXPERIMENT) && COSMIC_F14_PRE_MAP_REMOVAL_EXPERIMENT >= 1"
PATHS = {
    "src/server/game/Scripting/ScriptDefines/AllMapScript.h": 2,
    "src/server/game/Scripting/ScriptMgr.h": 1,
    "src/server/game/Scripting/ScriptDefines/AllMapScript.cpp": 1,
    "src/server/game/Maps/Map.cpp": 1,
}

def require(ok, message):
    if not ok:
        raise SystemExit(message)

for path, count in PATHS.items():
    original = subprocess.check_output(["git", "show", PARENT + ":" + path], cwd=ROOT).decode()
    current = (ROOT / path).read_text()
    pattern = re.escape(GUARD) + r"\n.*?^#endif\n"
    stripped, actual = re.subn(pattern, "", current, flags=re.M | re.S)
    require(actual == count, "wrong guard count " + path)
    # New guards adjacent to existing blank lines may leave one additional
    # blank line; normalize only blank-line runs for the source comparison.
    normalize = lambda s: re.sub(r"\n{3,}", "\n\n", s)
    require(normalize(stripped) == normalize(original), "default OFF changed parent " + path)

mapcpp = (ROOT / "src/server/game/Maps/Map.cpp").read_text()
body = mapcpp.split("void Map::RemovePlayerFromMap(Player* player, bool remove)", 1)[1].split("void Map::AfterPlayerUnlinkFromMap", 1)[0]
call = "sScriptMgr->OnF14BeforePlayerMapRemoval(this, player, remove);"
require(body.count(call) == 1 and body.index(call) < body.index("UpdatePlayerZoneStats"), "hook must precede first removal effect")
require(body.index(call) < body.index("player->RemoveFromWorld()") < body.index("sScriptMgr->OnPlayerLeaveMap"), "invalid old/new hook ordering")
header = (ROOT / "src/server/game/Scripting/ScriptDefines/AllMapScript.h").read_text()
require("virtual void OnF14BeforePlayerMapRemoval(Map const* /*map*/, Player const* /*player*/, bool /*remove*/) { }" in header, "not a const no-op observer")
dispatch = (ROOT / "src/server/game/Scripting/ScriptDefines/AllMapScript.cpp").read_text()
require("CALL_ENABLED_HOOKS(AllMapScript, ALLMAPHOOK_ON_F14_BEFORE_PLAYER_MAP_REMOVAL," in dispatch, "missing selected dispatch")
require("script->OnF14BeforePlayerMapRemoval(map, player, remove)" in dispatch, "missing forwarding")
print("PASS exact default-OFF source, guarded const observer, pre-removal ordering")
