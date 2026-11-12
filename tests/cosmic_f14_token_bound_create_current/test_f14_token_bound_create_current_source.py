"""Core23: reconcile token-bound initial CREATE onto current Core21, fail visible."""
import pathlib
import subprocess

ROOT = pathlib.Path(__file__).resolve().parents[2]
PARENT = "991f2c86c928222d501a559a61434cfd15055dc8"

def require(ok, message):
    if not ok:
        raise SystemExit(message)

expected = sorted([
    ".github/workflows/cosmic-f14-token-bound-create-current.yml",
    "docs/COSMIC_CUSTOM_MODIFICATIONS_REGISTER.md",
    "src/server/game/Entities/Object/Updates/UpdateData.h",
    "src/server/game/Entities/Player/PlayerUpdates.cpp",
    "src/server/game/Scripting/ScriptDefines/WorldObjectScript.cpp",
    "src/server/game/Scripting/ScriptDefines/WorldObjectScript.h",
    "src/server/game/Scripting/ScriptMgr.h",
    "tests/cosmic_f14_token_bound_create_current/test_f14_token_bound_create_current_source.py",
])
actual = subprocess.check_output(["git","diff","--name-only",PARENT,"HEAD"], cwd=ROOT, text=True).splitlines()
require(sorted(actual) == expected, "unexpected Core23 changed-path scope")

player_path = "src/server/game/Entities/Player/PlayerUpdates.cpp"
player = (ROOT / player_path).read_text()
old_player = subprocess.check_output(["git","show",PARENT + ":" + player_path], cwd=ROOT, text=True)

for needle in (
    "COSMIC_F14_TOKEN_BOUND_CREATURE_CREATE_QUERY_EXPERIMENT",
    "IsF14CreatureSuppressedFromClientViewForInitialCreate(",
    "data.F14MayAskInitialCreateQuery()",
    "data.F14InitialCreateQueryToken()",
    "ShouldF14SuppressCreatureInitialCreateForViewer(",
):
    require(needle in player, "missing current token-bound CREATE contract: " + needle)

# The existing Core21 OUT helper remains present and token-bound.
require("IsF14CreatureSuppressedFromClientViewForInitialOut(" in player,
        "Core21 token-bound OUT helper disappeared")
require("ShouldF14SuppressCreatureInitialOutForViewer(" in player,
        "Core21 OUT dispatcher call disappeared")

# Limit CREATE authority to the templated batched visibility path.
batched = player.split("template <class T>\nvoid Player::UpdateVisibilityOf(T* target, UpdateData& data",1)[1].split("void Player::GetInitialVisiblePackets",1)[0]
require("IsF14CreatureSuppressedFromClientViewForInitialCreate(" in batched,
        "batched CREATE lacks token-bound helper")
require("data.F14MayAskInitialCreateQuery()" in batched,
        "batched CREATE must require open notifier admission")
create_macro = batched.split("#if defined(COSMIC_F14_TOKEN_BOUND_CREATURE_CREATE_QUERY_EXPERIMENT)",1)[1].split("#else",1)[0]
require("IsF14CreatureSuppressedFromClientView(this, target)" not in create_macro,
        "macro-ON CREATE must not fall back to tokenless positive query")
create_else = batched.split("#if defined(COSMIC_F14_TOKEN_BOUND_CREATURE_CREATE_QUERY_EXPERIMENT)",1)[1].split("#else",1)[1].split("#endif",1)[0]
require("IsF14CreatureSuppressedFromClientView(this, target)" in create_else,
        "macro-OFF CREATE must preserve historical tokenless default-FALSE query")

direct = player.split("void Player::UpdateVisibilityOf(WorldObject* target)",1)[1]
require("IsF14CreatureSuppressedFromClientViewForInitialCreate(" not in direct,
        "direct visibility path gained token-bound CREATE authority")
require("F14InitialCreateQueryToken" not in direct,
        "direct visibility path borrowed notifier token")

ud = (ROOT / "src/server/game/Entities/Object/Updates/UpdateData.h").read_text()
require("F14MayAskInitialCreateQuery() const" in ud,
        "CREATE admission-open read missing")
require("F14InitialCreateQueryToken() const" in ud,
        "CREATE token read missing")
create_token_piece = ud.split("F14InitialCreateQueryToken() const",1)[1].split("#endif",1)[0]
require("_f14InitialOutAdmission.Token()" in create_token_piece,
        "CREATE token must come from same non-transferable notifier admission")

woh = (ROOT / "src/server/game/Scripting/ScriptDefines/WorldObjectScript.h").read_text()
woc = (ROOT / "src/server/game/Scripting/ScriptDefines/WorldObjectScript.cpp").read_text()
sm = (ROOT / "src/server/game/Scripting/ScriptMgr.h").read_text()

for text, needle in (
    (woh, "WORLDOBJECTHOOK_ON_F14_VIEWER_CREATURE_INITIAL_CREATE_TOKEN_QUERY"),
    (woh, "OnF14ShouldSuppressCreatureInitialCreateForViewer"),
    (woc, "WORLDOBJECTHOOK_ON_F14_VIEWER_CREATURE_INITIAL_CREATE_TOKEN_QUERY"),
    (sm, "ShouldF14SuppressCreatureInitialCreateForViewer"),
):
    require(needle in text, "CREATE script ABI missing: " + needle)
require("requires the Core18 token-bound notifier admission seam" in woh,
        "CREATE macro must require current notifier-token seam")

# Both current token-bound policy dispatchers must fail visible on malformed input.
out_fn = woc.split("bool ScriptMgr::ShouldF14SuppressCreatureInitialOutForViewer(",1)[1].split("#endif",1)[0]
create_fn = woc.split("bool ScriptMgr::ShouldF14SuppressCreatureInitialCreateForViewer(",1)[1].split("#endif",1)[0]
for name, fn in (("OUT", out_fn), ("CREATE", create_fn)):
    require("if (!viewer || !creature || !token)" in fn and "return false;" in fn,
            name + " malformed token query must fail visible")
    require("ASSERT(viewer && creature && token)" not in fn,
            name + " token query must not assert on malformed provenance")
    require("CALL_ENABLED_BOOLEAN_HOOKS_WITH_DEFAULT_FALSE" in fn,
            name + " valid dispatch must remain default FALSE")

# Core23 must not alter the existing direct path bytes from Core21.
old_direct = old_player.split("void Player::UpdateVisibilityOf(WorldObject* target)",1)[1]
require(direct == old_direct, "Core23 changed the direct visibility path")

old_register = subprocess.check_output(["git","show",PARENT + ":docs/COSMIC_CUSTOM_MODIFICATIONS_REGISTER.md"], cwd=ROOT)
new_register = (ROOT / "docs/COSMIC_CUSTOM_MODIFICATIONS_REGISTER.md").read_bytes()
require(new_register.startswith(old_register),
        "Core23 custom modifications register must preserve Core21 bytes as exact prefix")
require(b"F14 Core23 token-bound initial CREATE reconciled onto current Core21" in new_register,
        "Core23 register entry missing")

# No positive/debt authority is introduced in added runtime lines.
runtime_paths = [
    "src/server/game/Entities/Object/Updates/UpdateData.h",
    player_path,
    "src/server/game/Scripting/ScriptDefines/WorldObjectScript.cpp",
    "src/server/game/Scripting/ScriptDefines/WorldObjectScript.h",
    "src/server/game/Scripting/ScriptMgr.h",
]
added = []
for path in runtime_paths:
    diff = subprocess.check_output(["git","diff","-U0",PARENT,"HEAD","--",path], cwd=ROOT, text=True)
    added.extend(line[1:] for line in diff.splitlines()
                 if line.startswith("+") and not line.startswith("+++"))
joined = "\n".join(added)
for forbidden in (".Open(", ".Reserve(", ".Commit(", "DestroyForPlayer(",
                  "SendDirectMessage(", "SetPhaseMask(", "return true;"):
    require(forbidden not in joined, "Core23 added forbidden positive/debt action: " + forbidden)

print("PASS Core23: current token-bound CREATE uses genuine notifier admission, direct path unchanged, OUT+CREATE malformed input fail visible")
