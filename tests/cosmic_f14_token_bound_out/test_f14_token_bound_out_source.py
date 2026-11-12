"""Core18 token-bound initial OUT query is guarded, additive and unreachable from direct visibility paths."""
import pathlib
import subprocess

ROOT = pathlib.Path(__file__).resolve().parents[2]
PARENT = "34ad407467fcf6ea02fcce1595f7c0288966042f"

def require(ok, message):
    if not ok:
        raise SystemExit(message)

expected = sorted([
    ".github/workflows/cosmic-f14-token-bound-out-query.yml",
    "src/server/game/Entities/Object/Updates/F14InitialOutPassAdmission.h",
    "src/server/game/Entities/Object/Updates/UpdateData.h",
    "src/server/game/Entities/Player/PlayerUpdates.cpp",
    "src/server/game/Scripting/ScriptDefines/WorldObjectScript.cpp",
    "src/server/game/Scripting/ScriptDefines/WorldObjectScript.h",
    "src/server/game/Scripting/ScriptMgr.h",
    "tests/cosmic_f14_token_bound_out/f14_initial_out_token_test.cpp",
    "tests/cosmic_f14_token_bound_out/test_f14_token_bound_out_source.py",
])
actual = subprocess.check_output(["git","diff","--name-only",PARENT,"HEAD"], cwd=ROOT, text=True).splitlines()
require(sorted(actual) == expected, "unexpected Core18 changed-path scope")

for path in expected:
    if path.startswith(".github/") or path.startswith("tests/"):
        continue
    diff = subprocess.check_output(["git","diff","-U0",PARENT,"HEAD","--",path], cwd=ROOT, text=True)
    removed = [line for line in diff.splitlines() if line.startswith("-") and not line.startswith("---")]
    require(not removed, "Core18 runtime source must be insertion-only: " + path)

old_player = subprocess.check_output(["git","show",PARENT + ":src/server/game/Entities/Player/PlayerUpdates.cpp"], cwd=ROOT, text=True)
new_player = (ROOT / "src/server/game/Entities/Player/PlayerUpdates.cpp").read_text()
old_helper = old_player.split("static bool IsF14CreatureSuppressedFromClientView(",1)[1].split("// F14 player-character client CREATE/OUT query ONLY",1)[0]
require(old_helper in new_player, "existing creature query helper must remain byte-exact")

for needle in (
    "COSMIC_F14_TOKEN_BOUND_CREATURE_OUT_QUERY_EXPERIMENT",
    "IsF14CreatureSuppressedFromClientViewForInitialOut(",
    "data.F14InitialOutQueryToken()",
    "ShouldF14SuppressCreatureInitialOutForViewer(",
):
    require(needle in new_player, "missing token-bound initial OUT call site: " + needle)

batched = new_player.split("template <class T>\nvoid Player::UpdateVisibilityOf(T* target, UpdateData& data",1)[1].split("void Player::GetInitialVisiblePackets",1)[0]
require("IsF14CreatureSuppressedFromClientViewForInitialOut(" in batched, "batched initial OUT must carry token")
direct = new_player.split("void Player::UpdateVisibilityOf(WorldObject* target)",1)[1]
require("IsF14CreatureSuppressedFromClientViewForInitialOut(" not in direct, "direct visibility path must never gain token-bound initial OUT authority")

ud = (ROOT / "src/server/game/Entities/Object/Updates/UpdateData.h").read_text()
adm = (ROOT / "src/server/game/Entities/Object/Updates/F14InitialOutPassAdmission.h").read_text()
require("F14InitialOutQueryToken() const" in ud and "_f14InitialOutAdmission.Token()" in ud, "UpdateData token accessor missing")
require("Token() const noexcept { return IsOpen() ? _token : 0; }" in adm, "admission must expose token only while open")

woh = (ROOT / "src/server/game/Scripting/ScriptDefines/WorldObjectScript.h").read_text()
woc = (ROOT / "src/server/game/Scripting/ScriptDefines/WorldObjectScript.cpp").read_text()
sm = (ROOT / "src/server/game/Scripting/ScriptMgr.h").read_text()
for text, needle in (
    (woh, "WORLDOBJECTHOOK_ON_F14_VIEWER_CREATURE_INITIAL_OUT_TOKEN_QUERY"),
    (woh, "OnF14ShouldSuppressCreatureInitialOutForViewer"),
    (woc, "CALL_ENABLED_BOOLEAN_HOOKS_WITH_DEFAULT_FALSE"),
    (sm, "ShouldF14SuppressCreatureInitialOutForViewer"),
):
    require(needle in text, "token-bound script ABI missing: " + needle)

require("requires the pre-send notifier admission seam" in woh, "token query must hard-require pre-send admission")
print("PASS Core18 token-bound initial OUT query is additive, default-false ABI and absent from direct visibility paths")