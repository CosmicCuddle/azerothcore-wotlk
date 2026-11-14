"""Core21: token-bound F14 initial-OUT query must fail visible on malformed input."""
import pathlib
import subprocess

ROOT = pathlib.Path(__file__).resolve().parents[2]
PARENT = "be46b96a0d2ab73ee22a946305b1308a02751ae9"

def require(ok, message):
    if not ok:
        raise SystemExit(message)

expected = sorted([
    ".github/workflows/cosmic-f14-token-query-fail-visible-source.yml",
    "src/server/game/Scripting/ScriptDefines/WorldObjectScript.cpp",
    "tests/cosmic_f14_token_query_fail_visible/test_f14_token_query_fail_visible_source.py",
])
actual = subprocess.check_output(["git","diff","--name-only",PARENT,"HEAD"], cwd=ROOT, text=True).splitlines()
require(sorted(actual) == expected, "unexpected Core21 changed-path scope")

path = "src/server/game/Scripting/ScriptDefines/WorldObjectScript.cpp"
diff = subprocess.check_output(["git","diff","-U2",PARENT,"HEAD","--",path], cwd=ROOT, text=True)
require(diff.count("-    ASSERT(viewer && creature && token);") == 1,
        "Core21 must replace exactly the token-query ASSERT")
for needle in (
    "+    if (!viewer || !creature || !token)",
    "+        return false;",
):
    require(needle in diff, "missing fail-visible replacement: " + needle)

cpp = (ROOT / path).read_text()
fn = cpp.split("bool ScriptMgr::ShouldF14SuppressCreatureInitialOutForViewer(",1)[1].split("#endif",1)[0]
require("ASSERT(viewer && creature && token)" not in fn,
        "token-bound initial OUT query must not assert on malformed input")
require("if (!viewer || !creature || !token)" in fn and "return false;" in fn,
        "malformed token-query input must fail visible")
require("CALL_ENABLED_BOOLEAN_HOOKS_WITH_DEFAULT_FALSE" in fn,
        "valid token-query path must retain default-false listener dispatch")
require("OnF14ShouldSuppressCreatureInitialOutForViewer(viewer, creature, token)" in fn,
        "valid token-query dispatch must remain unchanged")

old = subprocess.check_output(["git","show",PARENT + ":" + path], cwd=ROOT, text=True)
old_before, old_after = old.split("    ASSERT(viewer && creature && token);",1)
new_before, new_tail = cpp.split("    // This is a fail-visible policy seam.",1)
require(new_before == old_before, "Core21 must not alter source before the token-query guard")
dispatch = "    CALL_ENABLED_BOOLEAN_HOOKS_WITH_DEFAULT_FALSE("
require(dispatch in new_tail and dispatch in old_after, "dispatch anchor missing")
require(new_tail.split(dispatch,1)[1] == old_after.split(dispatch,1)[1],
        "Core21 must preserve the valid listener dispatch byte-for-byte")

print("PASS Core21 malformed token-bound initial OUT input fails visible without changing valid dispatch")
