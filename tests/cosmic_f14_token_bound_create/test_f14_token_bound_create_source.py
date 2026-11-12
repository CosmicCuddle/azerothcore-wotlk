"""Core19 routes batched initial CREATE through a genuine token-bound default-FALSE query."""
import pathlib
import subprocess

ROOT = pathlib.Path(__file__).resolve().parents[2]
PARENT = "4eea6bb6edc634ef4b4eefab71a27adba29e28db"

def require(ok, message):
    if not ok:
        raise SystemExit(message)

expected = sorted([
    ".github/workflows/cosmic-f14-token-bound-create-query.yml",
    "src/server/game/Entities/Object/Updates/UpdateData.h",
    "src/server/game/Entities/Player/PlayerUpdates.cpp",
    "src/server/game/Scripting/ScriptDefines/WorldObjectScript.cpp",
    "src/server/game/Scripting/ScriptDefines/WorldObjectScript.h",
    "src/server/game/Scripting/ScriptMgr.h",
    "tests/cosmic_f14_token_bound_create/test_f14_token_bound_create_source.py",
])
actual = subprocess.check_output(["git","diff","--name-only",PARENT,"HEAD"], cwd=ROOT, text=True).splitlines()
require(sorted(actual) == expected, "unexpected Core19 changed-path scope")

player = (ROOT / "src/server/game/Entities/Player/PlayerUpdates.cpp").read_text()
old_player = subprocess.check_output(["git","show",PARENT + ":src/server/game/Entities/Player/PlayerUpdates.cpp"], cwd=ROOT, text=True)

old_out = old_player.split("#if defined(COSMIC_F14_TOKEN_BOUND_CREATURE_OUT_QUERY_EXPERIMENT)",1)[1].split("#endif",1)[0]
require(old_out in player, "Core18 token-bound OUT helper/query branch must remain present")

for needle in (
    "COSMIC_F14_TOKEN_BOUND_CREATURE_CREATE_QUERY_EXPERIMENT",
    "IsF14CreatureSuppressedFromClientViewForInitialCreate(",
    "data.F14MayAskInitialCreateQuery()",
    "data.F14InitialCreateQueryToken()",
    "ShouldF14SuppressCreatureInitialCreateForViewer(",
):
    require(needle in player, "missing token-bound CREATE contract: " + needle)

batched = player.split("template <class T>\nvoid Player::UpdateVisibilityOf(T* target, UpdateData& data",1)[1].split("void Player::GetInitialVisiblePackets",1)[0]
require("IsF14CreatureSuppressedFromClientViewForInitialCreate(" in batched, "batched CREATE path lacks token-bound helper")
require("data.F14MayAskInitialCreateQuery()" in batched, "CREATE policy must require open notifier admission")
create_macro = batched.split("#if defined(COSMIC_F14_TOKEN_BOUND_CREATURE_CREATE_QUERY_EXPERIMENT)",1)[1].split("#else",1)[0]
require("IsF14CreatureSuppressedFromClientView(this, target)" not in create_macro, "token-bound CREATE must not fall back to tokenless positive query")
default_branch = batched.split("#if defined(COSMIC_F14_TOKEN_BOUND_CREATURE_CREATE_QUERY_EXPERIMENT)",1)[1].split("#else",1)[1].split("#endif",1)[0]
require("IsF14CreatureSuppressedFromClientView(this, target)" in default_branch, "macro-OFF path must preserve existing tokenless default-FALSE behavior")

direct = player.split("void Player::UpdateVisibilityOf(WorldObject* target)",1)[1]
require("IsF14CreatureSuppressedFromClientViewForInitialCreate(" not in direct, "direct visibility path must never gain token-bound CREATE authority")

ud = (ROOT / "src/server/game/Entities/Object/Updates/UpdateData.h").read_text()
require("F14MayAskInitialCreateQuery() const" in ud, "CREATE admission-open read missing")
require("F14InitialCreateQueryToken() const" in ud and "_f14InitialOutAdmission.Token()" in ud, "CREATE token must come from same non-transferable notifier admission")

woh = (ROOT / "src/server/game/Scripting/ScriptDefines/WorldObjectScript.h").read_text()
woc = (ROOT / "src/server/game/Scripting/ScriptDefines/WorldObjectScript.cpp").read_text()
sm = (ROOT / "src/server/game/Scripting/ScriptMgr.h").read_text()
for text, needle in (
    (woh, "WORLDOBJECTHOOK_ON_F14_VIEWER_CREATURE_INITIAL_CREATE_TOKEN_QUERY"),
    (woh, "OnF14ShouldSuppressCreatureInitialCreateForViewer"),
    (woc, "WORLDOBJECTHOOK_ON_F14_VIEWER_CREATURE_INITIAL_CREATE_TOKEN_QUERY"),
    (woc, "CALL_ENABLED_BOOLEAN_HOOKS_WITH_DEFAULT_FALSE"),
    (sm, "ShouldF14SuppressCreatureInitialCreateForViewer"),
):
    require(needle in text, "CREATE script ABI missing: " + needle)
require("requires the Core18 token-bound notifier admission seam" in woh, "CREATE macro must hard-require Core18 token seam")

print("PASS Core19 batched initial CREATE uses genuine pass token, distinct default-FALSE query, no direct/tokenless positive fallback")