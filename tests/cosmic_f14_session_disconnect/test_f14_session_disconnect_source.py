"""Core20 F14 disconnect hook fires only for the keep-in-world offline-session transition."""
import pathlib
import subprocess

ROOT = pathlib.Path(__file__).resolve().parents[2]
PARENT = "4eea6bb6edc634ef4b4eefab71a27adba29e28db"

def require(ok, message):
    if not ok:
        raise SystemExit(message)

expected = sorted([
    ".github/workflows/cosmic-f14-session-disconnect-source.yml",
    "src/server/game/Server/WorldSession.cpp",
    "src/server/game/Scripting/ScriptDefines/PlayerScript.cpp",
    "src/server/game/Scripting/ScriptDefines/PlayerScript.h",
    "src/server/game/Scripting/ScriptMgr.h",
    "tests/cosmic_f14_session_disconnect/test_f14_session_disconnect_source.py",
])
actual = subprocess.check_output(["git","diff","--name-only",PARENT,"HEAD"], cwd=ROOT, text=True).splitlines()
require(sorted(actual) == expected, "unexpected Core20 path scope")

for path in expected:
    if path.startswith(".github/") or path.startswith("tests/"):
        continue
    diff = subprocess.check_output(["git","diff","-U0",PARENT,"HEAD","--",path], cwd=ROOT, text=True)
    removed = [line for line in diff.splitlines() if line.startswith("-") and not line.startswith("---")]
    require(not removed, "Core20 runtime source must be insertion-only: " + path)

ws = (ROOT / "src/server/game/Server/WorldSession.cpp").read_text()
body = ws.split("bool WorldSession::HandleSocketClosed()",1)[1].split("bool WorldSession::IsSocketClosed()",1)[0]
condition = "m_Socket && !m_Socket->IsOpen() && !IsKicked() && GetPlayer() && !PlayerLogout() && GetPlayer()->m_taxi.empty() && GetPlayer()->IsInWorld() && !World::IsStopped()"
require(condition in body, "keep-in-world guard changed or missing")
require(body.count("sScriptMgr->OnF14PlayerSessionDisconnected(GetPlayer());") == 1, "disconnect callback missing or duplicated")
require(body.index("m_Socket = nullptr;") < body.index("OnF14PlayerSessionDisconnected"), "callback must observe detached socket boundary")
require(body.index("OnF14PlayerSessionDisconnected") < body.index("GetPlayer()->TradeCancel(false);"), "callback must precede offline-pool handoff side effects")

ph = (ROOT / "src/server/game/Scripting/ScriptDefines/PlayerScript.h").read_text()
pc = (ROOT / "src/server/game/Scripting/ScriptDefines/PlayerScript.cpp").read_text()
sm = (ROOT / "src/server/game/Scripting/ScriptMgr.h").read_text()
for text, needle in (
    (ph, "PLAYERHOOK_ON_F14_SESSION_DISCONNECTED"),
    (ph, "OnF14PlayerSessionDisconnected(Player* /*player*/)"),
    (pc, "CALL_ENABLED_HOOKS(PlayerScript, PLAYERHOOK_ON_F14_SESSION_DISCONNECTED"),
    (sm, "void OnF14PlayerSessionDisconnected(Player* player);"),
):
    require(needle in text, "missing disconnect ABI: " + needle)

for path in ("src/server/game/Server/WorldSession.cpp","src/server/game/Scripting/ScriptDefines/PlayerScript.h","src/server/game/Scripting/ScriptDefines/PlayerScript.cpp","src/server/game/Scripting/ScriptMgr.h"):
    require("COSMIC_F14_SESSION_DISCONNECT_EXPERIMENT" in (ROOT/path).read_text(), "missing compile guard: " + path)

print("PASS Core20 guarded disconnect-kept-in-world callback after socket detach; kick/logout/taxi/shutdown guard preserved")