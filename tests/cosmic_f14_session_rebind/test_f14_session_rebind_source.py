"""Core17 F14 session-rebind hook is guarded, insertion-only and ordered before client visibility rebuild."""
import pathlib, subprocess

ROOT = pathlib.Path(__file__).resolve().parents[2]
PARENT = "9e30516cccfe095f95948ceb0ac1d6ca265c54e4"

def require(ok, message):
    if not ok:
        raise SystemExit(message)

expected = sorted([
    ".github/workflows/cosmic-f14-session-rebind-source.yml",
    "src/server/game/Handlers/CharacterHandler.cpp",
    "src/server/game/Scripting/ScriptDefines/PlayerScript.cpp",
    "src/server/game/Scripting/ScriptDefines/PlayerScript.h",
    "src/server/game/Scripting/ScriptMgr.h",
    "tests/cosmic_f14_session_rebind/test_f14_session_rebind_source.py",
])
actual = subprocess.check_output(["git","diff","--name-only",PARENT,"HEAD"], cwd=ROOT, text=True).splitlines()
require(sorted(actual) == expected, "unexpected Core17 path scope")

for path in expected:
    if path.startswith(".github/") or path.startswith("tests/"):
        continue
    diff = subprocess.check_output(["git","diff","-U0",PARENT,"HEAD","--",path], cwd=ROOT, text=True)
    removed = [line for line in diff.splitlines() if line.startswith("-") and not line.startswith("---")]
    require(not removed, "Core17 runtime source must be insertion-only: " + path)

ch = (ROOT / "src/server/game/Handlers/CharacterHandler.cpp").read_text()
segment = ch.split("if (!sWorld->getBoolConfig(CONFIG_ENABLE_LOGIN_AFTER_DC))",1)[1].split("std::shared_ptr<LoginQueryHolder>",1)[0]
require(segment.count("sScriptMgr->OnF14PlayerSessionRebound(p);") == 1, "reconnect hook missing or duplicated")
require(segment.index("p->SetSession(this);") < segment.index("sScriptMgr->OnF14PlayerSessionRebound(p);"), "hook must follow new session bind")
require(segment.index("sScriptMgr->OnF14PlayerSessionRebound(p);") < segment.index("delete p->PlayerTalkClass;"), "hook must precede new client UI/visibility rebuild")
require(segment.index("sScriptMgr->OnF14PlayerSessionRebound(p);") < segment.index("HandlePlayerLoginToCharInWorld(p);"), "hook must precede reconnect visibility reset")

for path in (
    "src/server/game/Handlers/CharacterHandler.cpp",
    "src/server/game/Scripting/ScriptDefines/PlayerScript.cpp",
    "src/server/game/Scripting/ScriptDefines/PlayerScript.h",
    "src/server/game/Scripting/ScriptMgr.h",
):
    text = (ROOT / path).read_text()
    require("COSMIC_F14_SESSION_REBIND_EXPERIMENT" in text, "missing compile guard: " + path)

ph = (ROOT / "src/server/game/Scripting/ScriptDefines/PlayerScript.h").read_text()
require("PLAYERHOOK_ON_F14_SESSION_REBOUND" in ph and "OnF14PlayerSessionRebound" in ph, "PlayerScript ABI missing")
pc = (ROOT / "src/server/game/Scripting/ScriptDefines/PlayerScript.cpp").read_text()
require("CALL_ENABLED_HOOKS(PlayerScript, PLAYERHOOK_ON_F14_SESSION_REBOUND" in pc, "dispatcher implementation missing")
sm = (ROOT / "src/server/game/Scripting/ScriptMgr.h").read_text()
require("void OnF14PlayerSessionRebound(Player* player);" in sm, "ScriptMgr declaration missing")

print("PASS Core17 guarded reconnect-session hook after SetSession and before new-client visibility rebuild")