#!/usr/bin/env python3
"""CS02 source-coupled runner for Codex's ACTUAL Core74 v1 executor (test-only, local, not CI).

REAL (extracted verbatim from the pinned checkout): Player::ExecutePB59InventoryBatchPlan,
CheckPB59InventoryBatchPlan, CapturePB59InventoryStamp, ReconcilePB59OriginalCombatEffects,
_ApplyItemMods, GlobalCooldownMgr, and the PB59 headers on the include path.
DOUBLES: Codex's tests/pb59/inventory_batch_native_fixture.cpp (pinned blob), patched here ONLY to give
each Player double its own owner GUID (A->B->A needs two owners). Checks: CS02's cs02_native_checks.inc.
Not a worldserver build; not gameplay.

  run_native_v1.py --core74 <checkout@7fdc3a65> [--compiler g++ clang++] [--sabotage]
"""
import argparse, hashlib, subprocess, tempfile, sys, re
from pathlib import Path

PIN_HEAD = "7fdc3a655f2f46cf8cba008a8f9226413f61e7f6"
PLAYER = "src/server/game/Entities/Player/"
PIN_BLOBS = {
    PLAYER + "PlayerPB59Inventory.cpp": "ebad31a3fb693187c9e9b5b4eac8e8ac56b17810",
    PLAYER + "PlayerStorage.cpp": "048723ba41a395b3f76203197199fc6fdaaa0f8b",
    PLAYER + "Player.cpp": "48b36825773879a0af2e3a2027fc79fced5a3a39",
    "src/server/game/Entities/Unit/CharmInfo.h": "c99b238806e5eebf9d4bdeeb4041ea4e3feb8d71",
    "tests/pb59/inventory_batch_native_fixture.cpp": "1ff86282a0663b19536e4b41716d3e4f51c0031a",
    "tests/pb59/CORE74_NATIVE_INVENTORY_BATCH.md": "f8e40149976e4bd89d3a876c1baaf01ce15b3388",
    PLAYER + "PB59CombatRecoveryState.h": "df27c7ccd80e850962b2ce3d5c5fd7e0801c64b6",
    PLAYER + "PB59EffectBoundary.h": "48926d18a6241d7fa5cd8a59eac569ff0364f77e",
    PLAYER + "PB59InventoryBatchPlan.h": "ff48af6849f6f7109655a0645ca5d80e7f82c98f",
    PLAYER + "PB59InventoryCustody.h": "0b42af684479a899bc2fdfd254c18d120c57ee97",
    PLAYER + "PB59InventoryReservation.h": "f0265bf1bcd11b741c207fce363c850a977de973",
    PLAYER + "PB59InventorySlotFacts.h": "7844fbed52c0c25999eaa0d1cba6b888f18b522e",
    PLAYER + "PB59OriginalCombatEffects.h": "d3a27c90d702859401c20aa2c966cd426117e3dd",
    PLAYER + "PB59OriginalPointerCustody.h": "b7f81b88315d5611a2239366ebef1740841c193e",
    PLAYER + "PB59OriginalQueueCustody.h": "460efc66f2026c5e48f8d72ad329da79a4fa58d7",
    PLAYER + "PB59TransactionAdmission.h": "6c3bf34b767f58c72232145c143263cc66d60fdd",
    PLAYER + "EquipmentCaptureState.h": "bece088d9ef70ae2adffa7087757c31f36ce7e01",
}
# Double patches: per-owner GUID only. Each must match exactly once.
FIXTURE_PATCHES = [
    ("Guid GetGUID() const { return {1}; }", "uint64 guid = 1; Guid GetGUID() const { return {guid}; }"),
    ("return PB59::InventorySlotFacts{1, bag, pos,", "return PB59::InventorySlotFacts{guid, bag, pos,"),
    ("Item* item = ownership.back().get();", "Item* item = ownership.back().get(); item->facts.owner = GetGUID().value;"),
]
# Source mutants on the REAL executor / admission text (sabotage), and one double mutant.
MUTANTS = [
    ("S1 context() ignores pending save/logout", "Executor.inc",
     "(recovery || (!_pb59Transaction.PendingSave() && !_pb59Transaction.PendingLogout()))", "((void)recovery, true)"),
    ("S2 Restored skips effect snapshot equality", "Executor.inc",
     "!captureEffects(after) || before != after ||\n            !cooldownsSame() || !queuesValid())",
     "!captureEffects(after) ||\n            !cooldownsSame() || !queuesValid())"),
    ("S3 Restored ignores opaque external effects", "Executor.inc",
     "if (reservation.ExternalEffects() || !observations(true) || !captureEffects(after) || before != after ||",
     "if (!observations(true) || !captureEffects(after) || before != after ||"),
    ("S4 recovery GUIDs never listed when live", "Executor.inc", "if (!restored)", "if ((void)restored, false)"),
    ("S5 effect verifier ignores reservation violations", "Executor.inc",
     "value.reservation->Violations() == value.violations;", "true;"),
    ("S6 effect verifier ignores observations", "Executor.inc",
     "return (*value.current)(value.recovery) && (*value.observed)(false) &&", "return (*value.current)(value.recovery) &&"),
    ("S7 no re-entry refusal at line 51", "Executor.inc",
     "if (PB59::InventoryReservation::ActiveForState(&_pb59Equipment) || _pb59Equipment.InventoryQuarantined() ||",
     "if (_pb59Equipment.InventoryQuarantined() ||"),
    ("S8 per-step observations skip destination facts", "Executor.inc",
     "else if (*source != step.move.source || *destination != step.move.destination ||",
     "else if (*source != step.move.source ||"),
    ("D1 double: slot capture reports bag GUID 0", "fixture",
     "bag = container->GetGUID().value; }", "bag = 0; (void)container; }"),
]


def blob(data: bytes) -> str:
    return hashlib.sha1(b"blob %d\0" % len(data) + data).hexdigest()


def function(source, signature):
    start = source.index(signature)
    opening = source.index("{", start)
    depth, end = 1, opening + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end] + "\n"


def once(text, old, new, what):
    if text.count(old) != 1:
        sys.exit(f"patch '{what}' matched {text.count(old)} times")
    return text.replace(old, new)


def build(core, work, mutant=None):
    native = core / PLAYER
    storage = (native / "PlayerStorage.cpp").read_text()
    player = (native / "Player.cpp").read_text()
    executor = (native / "PlayerPB59Inventory.cpp").read_text()
    charm = (core / "src/server/game/Entities/Unit/CharmInfo.h").read_text()
    files = {
        "Admission.inc": function(storage, "bool Player::CheckPB59InventoryBatchPlan("),
        "InventoryStamp.inc": function(storage, "std::optional<PB59::Stamp> Player::CapturePB59InventoryStamp("),
        "CombatEffects.inc": function(storage, "PB59::OriginalCombatEffects::Result Player::ReconcilePB59OriginalCombatEffects("),
        "ItemMods.inc": function(player, "void Player::_ApplyItemMods("),
        "Executor.inc": function(executor, "PB59::InventoryBatchResult Player::ExecutePB59InventoryBatchPlan("),
        "GlobalCooldownMgr.inc": function(charm, "class GlobalCooldownMgr ") + ";\n",
        "Checks.inc": (Path(__file__).with_name("cs02_native_checks.inc")).read_text(),
    }
    fixture = (core / "tests/pb59/inventory_batch_native_fixture.cpp").read_text()
    for old, new in FIXTURE_PATCHES:
        fixture = once(fixture, old, new, old[:40])
    if mutant:
        name, target, old, new = mutant
        if target == "fixture":
            fixture = once(fixture, old, new, name)
        else:
            files[target] = once(files[target], old, new, name)
    for name, body in files.items():
        (work / name).write_text(body)
    (work / "fixture.cpp").write_text(fixture)
    return native


def compile_run(compiler, flags, native, work, tag, strict=True):
    binary = work / f"cs02-{tag}"
    if not strict:
        built = subprocess.run([compiler, *flags, "-std=c++20", "-Wall", "-Wextra", "-pthread", "-I", str(native),
                                "-I", str(work), str(work / "fixture.cpp"), "-o", str(binary)],
                               capture_output=True, text=True)
        if built.returncode:
            return None, built.stderr
        env = {}
        run = subprocess.run([str(binary)], capture_output=True, text=True, env=env)
        return run.returncode, run.stdout + run.stderr
    subprocess.run([compiler, *flags, "-std=c++20", "-Wall", "-Wextra", "-Werror", "-pthread", "-I", str(native),
                    "-I", str(work), str(work / "fixture.cpp"), "-o", str(binary)], check=True)
    env = {"ASAN_OPTIONS": "detect_leaks=1:abort_on_error=1", "UBSAN_OPTIONS": "print_stacktrace=1:halt_on_error=1"}
    run = subprocess.run([str(binary)], capture_output=True, text=True, env=env)
    return run.returncode, run.stdout + run.stderr


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--core74", required=True)
    ap.add_argument("--compiler", nargs="+", default=["g++", "clang++"])
    ap.add_argument("--sabotage", action="store_true")
    ap.add_argument("--log", default=None)
    a = ap.parse_args()
    core = Path(a.core74).resolve()
    head = subprocess.run(["git", "-C", str(core), "rev-parse", "HEAD"], capture_output=True, text=True).stdout.strip()
    if head != PIN_HEAD:
        sys.exit(f"Core74 head {head} != pinned {PIN_HEAD}")
    for rel, want in PIN_BLOBS.items():
        got = blob((core / rel).read_bytes())
        if got != want:
            sys.exit(f"{rel} blob {got} != pinned {want}")
    print(f"Core74 pinned {head}; {len(PIN_BLOBS)} blobs verified")
    status = 0
    baseline = None
    with tempfile.TemporaryDirectory(prefix="cs02-native-v1-") as tmp:
        work = Path(tmp)
        native = build(core, work)
        for cxx in a.compiler:
            for mode, flags in (("plain", ["-O2"]),
                                ("asan-ubsan", ["-O1", "-g", "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
                                                "-fno-omit-frame-pointer"])):
                tag = f"{Path(cxx).name}-{mode}"
                code, out = compile_run(cxx, flags, native, work, tag)
                if a.log:
                    Path(a.log).mkdir(parents=True, exist_ok=True)
                    (Path(a.log) / f"{tag}.log").write_text(out)
                sig = [l for l in out.splitlines() if l.startswith(("SCN", "MATRIX", "CS02 native", "FAIL"))]
                if baseline is None:
                    baseline = sig
                    print("\n".join(sig))
                print(f"{tag}: exit {code}; {out.strip().splitlines()[-1]}; "
                      f"{'identical to first toolchain' if sig == baseline else 'DIFFERS from first toolchain'}")
                status |= code != 0 or sig != baseline
        if a.sabotage:
            killed = 0
            for m in MUTANTS:
                build(core, work, m)
                code, out = compile_run(a.compiler[0], ["-O1"], native, work, "mutant", strict=False)
                if code is None:
                    print(f"[SABOTAGE] {m[0]:<52} NOT RUN (mutant does not compile)")
                    continue
                sig = [l for l in out.splitlines() if l.startswith(("SCN", "MATRIX", "CS02 native", "FAIL"))]
                diff = [l for l in sig if l not in baseline]
                dead = code != 0 or sig != baseline
                killed += dead
                print(f"[SABOTAGE] {m[0]:<52} {'killed' if dead else 'SURVIVED'}"
                      + (f"  first change: {diff[0][:150]}" if diff else ""))
            print(f"[SABOTAGE] {killed}/{len(MUTANTS)} killed")
    return status


if __name__ == "__main__":
    sys.exit(main())
