#!/usr/bin/env python3
"""CS02 Layer 3 runner: REAL PB53 (96c579e8) adapter/execution/factory bodies with REAL Core74 (c779e7b4) executor.

Test-only and local (no hosted CI); not a worldserver build, not gameplay. See README.md for real vs double.
  run_layer3.py --pb53 <mod-playerbots checkout@96c579e8> --core74 <azerothcore checkout@c779e7b4>
                [--compiler g++ clang++] [--sabotage] [--log DIR] [--write-pins]
"""
import argparse, hashlib, subprocess, sys, tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
PB53_HEAD = "96c579e86443c86780d6f9cd8ef51b7c252b09f8"
CORE_HEAD = "c779e7b4f735cab048f5b351ca6b9053f188787a"
CP = "src/server/game/Entities/Player/"
PB_FILES = ["src/Ai/Base/Actions/TrainerAction.cpp", "src/Bot/AutoGearCappedExecution.h", "src/Bot/AutoGearCoreBatchRequest.h",
            "src/Bot/Factory/PlayerbotFactory.cpp", "src/Bot/Factory/PlayerbotFactory.h", "src/PlayerbotAIConfig.cpp",
            "conf/playerbots.conf.dist", "src/Bot/AutoGearCommandEligibility.h", "src/Bot/AutoGearSelectionCap.h",
            "src/Bot/AutoGearProgressionCap.h", "src/Bot/AutoGearRndbotTierSync.h"]
CORE_FILES = [CP + f for f in ("PlayerPB59Inventory.cpp", "PlayerStorage.cpp", "Player.cpp", "PB59CombatRecoveryState.h",
              "PB59EffectBoundary.h", "PB59InventoryBatchPlan.h", "PB59InventoryCustody.h", "PB59InventoryReservation.h",
              "PB59InventorySlotFacts.h", "PB59OriginalCombatEffects.h", "PB59OriginalQueueCustody.h",
              "PB59TransactionAdmission.h", "EquipmentCaptureState.h")] + [
              "src/server/game/Entities/Unit/CharmInfo.h", "tests/pb59/inventory_batch_native_fixture.cpp"]
PINS = HERE / "PINS.txt"
SIG = ("SCN", "MATRIX", "CS02 layer3", "FAIL", "SRC")

FIXTURE_PATCHES = [  # doubles only; each must match exactly once
    ("Guid GetGUID() const { return {1}; }", "uint64 guid = 1; Guid GetGUID() const { return {guid}; }"),
    ("return PB59::InventorySlotFacts{1, bag, pos,", "return PB59::InventorySlotFacts{guid, bag, pos,"),
    ("Item* item = ownership.back().get();", "Item* item = ownership.back().get(); item->facts.owner = GetGUID().value;"),
    ("struct ItemTemplate {\n", "struct ItemTemplate {\n    uint32 ItemLevel = 0, BagFamily = 0;\n"),
    ("ItemTemplate const* GetTemplate() const { return &prototype; }",
     "ItemTemplate const* GetTemplate() const { return &prototype; } uint16 GetPos() const;"),
    ("class Bag : public Item { public: using Item::Item; };",
     "class Bag : public Item { public: using Item::Item; uint32 GetBagSize() const; Item* GetItemByPos(uint8 slot) const; };"),
]
ADAPTER = [  # PlayerAutoGearInventory members, verbatim from TrainerAction.cpp
    ("PlayerbotsAutoGearExec::Snapshot Capture() const", False),
    ("PlayerbotsAutoGearExec::StripResult StripToBags(uint32 slotMask)", False),
    ("bool DestroyCreatedEquipped(uint8 slot, uint64 guid)", False),
    ("bool RestoreToSlot(ObjectGuid guid, uint8 slot)", False),
    ("bool InWorld() const", False),
    ("PlayerbotsAutoGearExec::StripResult RunCoreBatch(", True),
    ("bool AllAtDestination(std::vector<PlayerbotsAutoGearCoreBatch::PlannedMove> const& plan) const", True),
    ("struct InventoryFingerprint", True),
    ("std::optional<InventoryFingerprint> Fingerprint() const", True),
]
TA = "src/Ai/Base/Actions/TrainerAction.cpp"
PF = "src/Bot/Factory/PlayerbotFactory.cpp"
EXEC_H = "src/Bot/AutoGearCappedExecution.h"
REQ_H = "src/Bot/AutoGearCoreBatchRequest.h"
MUTANTS = [  # (name, pb file, old, new): mutate the REAL PB53 text in the work copy only
    ("P1 Refused believed without fingerprint", REQ_H, "return observed.unchangedSinceStart ? StripResult::Refused : StripResult::RecoveryRequired;",
     "return StripResult::Refused;"),
    ("P2 recovery GUIDs ignored", REQ_H, "    if (observed.missing)\n        return StripResult::RecoveryRequired;\n", ""),
    ("P3 Applied ignores Core failure", REQ_H, "observed.everyMoveAtDestination && !observed.failureReported ?", "observed.everyMoveAtDestination ?"),
    ("P4 unknown native outcome read as Refused", REQ_H, "return static_cast<CoreBatchOutcome>(0xFF);", "return CoreBatchOutcome::Refused;"),
    ("P5 Restored believed without fingerprint", REQ_H, "return observed.unchangedSinceStart ? StripResult::Restored : StripResult::RecoveryRequired;",
     "return StripResult::Restored;"),
    ("P6 strip order: off-hand not first", TA, "            if (slotMask & (1u << EQUIPMENT_SLOT_OFFHAND))\n                slots.push_back(EQUIPMENT_SLOT_OFFHAND);\n", ""),
    ("P7 nested-strip guard removed", TA, "            if (_stripping)\n                return StripResult::Refused;\n", ""),
    ("P8 RunCoreBatch: fingerprint always unchanged", TA, "observed.unchangedSinceStart = after && *after == *before;",
     "observed.unchangedSinceStart = true; (void)after;"),
    ("P9 RestoreToSlot accepts Restored", TA, "return RunCoreBatch(plan, sources, destinations) == PlayerbotsAutoGearExec::StripResult::Done;\n#endif",
     "return RunCoreBatch(plan, sources, destinations) != PlayerbotsAutoGearExec::StripResult::RecoveryRequired;\n#endif"),
    ("P10 Execute ignores owned items leaving", EXEC_H, "(outcome.factoryStopped || outcome.bisRestoreFailed || outcome.ownedMissing)",
     "(outcome.factoryStopped || outcome.bisRestoreFailed)"),
    ("P11 InitAmmo destroys owned ammo again", PF, "    if (!keepOwnedItems)\n        DestroyObsoleteAmmo(bot, ammoProto);", "    DestroyObsoleteAmmo(bot, ammoProto);"),
    ("P12 DisplacesOffhand forgets staves/polearms", PF, "(mainhand->SubClass == ITEM_SUBCLASS_WEAPON_POLEARM || mainhand->SubClass == ITEM_SUBCLASS_WEAPON_STAFF ||",
     "(false ||"),
    ("P13 strip with any empty observation proceeds", TA, "                if (!facts)\n                    return StripResult::Refused;\n                if (!facts->item)",
     "                if (!facts)\n                    continue;\n                if (!facts->item)"),
]
# Not a mutant: CS01's proposed early return (#52 7137340399), applied to the work copy only, to show the
# post-stop rows are fixable by that guard and that the oracle notices. Never pushed to PB source.
FIX_PROBE = ("FIX-PROBE CS01 early return after factory/BiS stop", EXEC_H,
             "    // Only items this command created are checked; gear the bot already owned is left alone.\n",
             "    if (outcome.factoryStopped || outcome.bisRestoreFailed)\n    {\n        outcome.ran = true;\n        outcome.capHeld = false;\n"
             "        outcome.refusal = Refusal::RecoveryRequired;\n        return outcome;\n    }\n"
             "    // Only items this command created are checked; gear the bot already owned is left alone.\n")


def blob(data):
    return hashlib.sha1(b"blob %d\0" % len(data) + data).hexdigest()


def body(source, signature, struct=False):
    if source.count(signature) != 1:
        sys.exit(f"signature matched {source.count(signature)} times: {signature}")
    start = source.index(signature)
    opening = source.index("{", start)
    depth, end = 1, opening + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    text = source[start:end]
    if struct:
        text += ";"
    return text + "\n"


def static_function(source, signature):
    return body(source, signature)


def head_of(repo):
    return subprocess.run(["git", "-C", str(repo), "rev-parse", "HEAD"], capture_output=True, text=True).stdout.strip()


def clean(repo):
    return not subprocess.run(["git", "-C", str(repo), "status", "--porcelain"], capture_output=True, text=True).stdout.strip()


def once(text, old, new, what):
    if text.count(old) != 1:
        sys.exit(f"patch {what!r} matched {text.count(old)} times")
    return text.replace(old, new)


def stage(pb, core, work, mutant=None):
    """Copy the PB headers we include (possibly mutated) and generate every extracted .inc."""
    pbtext = {f: (pb / f).read_text() for f in PB_FILES}
    if mutant:
        pbtext[mutant[1]] = once(pbtext[mutant[1]], mutant[2], mutant[3], mutant[0])
    pbinc = work / "pbinc"
    pbinc.mkdir(exist_ok=True)
    for f in PB_FILES:
        if f.endswith(".h") and f.startswith("src/Bot/") and "Factory" not in f:
            (pbinc / Path(f).name).write_text(pbtext[f])
    # Core bodies (same extraction as Codex's test_inventory_batch_native.py).
    storage = (core / CP / "PlayerStorage.cpp").read_text()
    player = (core / CP / "Player.cpp").read_text()
    executor = (core / CP / "PlayerPB59Inventory.cpp").read_text()
    charm = (core / "src/server/game/Entities/Unit/CharmInfo.h").read_text()
    incs = {
        "Admission.inc": body(storage, "bool Player::CheckPB59InventoryBatchPlan("),
        "InventoryStamp.inc": body(storage, "std::optional<PB59::Stamp> Player::CapturePB59InventoryStamp("),
        "CombatEffects.inc": body(storage, "PB59::OriginalCombatEffects::Result Player::ReconcilePB59OriginalCombatEffects("),
        "ItemMods.inc": body(player, "void Player::_ApplyItemMods("),
        "Executor.inc": body(executor, "PB59::InventoryBatchResult Player::ExecutePB59InventoryBatchPlan("),
        "GlobalCooldownMgr.inc": body(charm, "class GlobalCooldownMgr ") .rstrip("\n") + ";\n",
        "Checks.inc": (HERE / "l3_adapter_checks.inc").read_text(),
    }
    ta = pbtext[TA]
    members = []
    for sig, gated in ADAPTER:
        text = body(ta, sig, struct=sig.startswith("struct"))
        members.append(("#if PB59_AUTOGEAR_CORE_BATCH\n" + text + "#endif\n") if gated else text)
    incs["AdapterMethods.inc"] = ("// Runner-assembled class around VERBATIM PB53 PlayerAutoGearInventory members (TrainerAction.cpp).\n"
        "class PlayerAutoGearInventory\n{\npublic:\n"
        "    PlayerAutoGearInventory(Bot* bot, uint32 quality, void const* sources) : _bot(bot), _quality(quality), _sources(sources) { }\n"
        + "".join(members) +
        "    Bot* _bot;\n    uint32 _quality;\n    void const* _sources;\n    bool _stripping = false;\n};\n")
    pf = pbtext[PF]
    incs["DestroyObsoleteAmmo.inc"] = body(pf, "static void DestroyObsoleteAmmo(Player* bot, ItemTemplate const* ammo)")
    incs["InitAmmo.inc"] = body(pf, "void PlayerbotFactory::InitAmmo()")
    incs["DisplacesOffhand.inc"] = body(pf, "bool PlayerbotFactory::DisplacesOffhand(Player* bot, ItemTemplate const* mainhand)")
    for name, text in incs.items():
        (work / name).write_text(text)
    fixture = (core / "tests/pb59/inventory_batch_native_fixture.cpp").read_text()
    for old, new in FIXTURE_PATCHES:
        fixture = once(fixture, old, new, old[:40])
    (work / "adapter_tu.cpp").write_text(fixture)
    (work / "factory_tu.cpp").write_text((HERE / "l3_factory.cpp").read_text())


def source_checks(pb):
    """Source-coupled text checks for code that cannot be compiled here (labelled SRC, not runtime)."""
    out = []
    cfg = (pb / "src/PlayerbotAIConfig.cpp").read_text()
    conf = (pb / "conf/playerbots.conf.dist").read_text()
    ta = (pb / TA).read_text()
    def row(id_, expected, observed, ok, where):
        out.append(f"SRC {id_:<44} expected={expected:<28} observed={observed:<28} {'as-expected' if ok else 'DIVERGENCE'} {where}")
        return ok
    line = next(i + 1 for i, l in enumerate(cfg.splitlines()) if "Playerbots.AutoGearIPCap.Enable" in l)
    row("S1 default-OFF in code", "GetOption(...,false)", "false" if '"Playerbots.AutoGearIPCap.Enable", false)' in cfg else "other",
        '"Playerbots.AutoGearIPCap.Enable", false)' in cfg, f"PlayerbotAIConfig.cpp:{line}")
    cline = next(i + 1 for i, l in enumerate(conf.splitlines()) if l.startswith("Playerbots.AutoGearIPCap.Enable"))
    row("S2 default-OFF in conf.dist", "= 0", conf.splitlines()[cline - 1].split("=")[1].strip(),
        conf.splitlines()[cline - 1].split("=")[1].strip() == "0", f"playerbots.conf.dist:{cline}")
    fn = body(ta, "bool RunPB59AutoGearWorldPlan(")
    start = ta.index("bool RunPB59AutoGearWorldPlan(")
    base = ta[:start].count("\n") + 1
    first = fn.splitlines()[2] if len(fn.splitlines()) > 2 else ""
    row("S3 command returns early when OFF", "!AutoGearIPCapEnable -> false", "present" if "if (!sPlayerbotAIConfig.AutoGearIPCapEnable" in fn.splitlines()[2] else "absent",
        "if (!sPlayerbotAIConfig.AutoGearIPCapEnable" in first, f"TrainerAction.cpp:{base + 2}")
    # Outer caller after Execute: any revalidation of bot/sender before the log lines and whisper?
    lines = fn.splitlines()
    ex = next(i for i, l in enumerate(lines) if "PlayerbotsAutoGearExec::Execute(inventory, plan)" in l)
    tail = lines[ex + 1:]
    reval = any(("IsInWorld()" in l or "GetSession()" in l or "IsPB59" in l) for l in tail)
    uses = [base + ex + 1 + i for i, l in enumerate(tail) if ("bot->GetGUID()" in l or "sender->GetGUID()" in l or "sender->GetName()" in l or "ai->Whisper" in l)]
    row("S4 outer caller revalidates bot/sender after Execute", "revalidation before use", "none; uses at " + ",".join(map(str, uses[:6])) + ("..." if len(uses) > 6 else ""),
        reval, f"TrainerAction.cpp:{base + ex}")
    return out


def compile_run(cxx, flags, tu, work, inc, tag, defines=(), strict=True):
    binary = work / f"{tag}"
    cmd = [cxx, *flags, "-std=c++20", "-Wall", "-Wextra", *(["-Werror"] if strict else []), "-pthread",
           *[f"-D{d}" for d in defines], *sum((["-I", str(i)] for i in inc), []), str(work / tu), "-o", str(binary)]
    built = subprocess.run(cmd, capture_output=True, text=True)
    if built.returncode:
        if strict:
            sys.exit(built.stderr[-6000:])
        return None, built.stderr
    run = subprocess.run([str(binary)], capture_output=True, text=True,
                         env={"ASAN_OPTIONS": "detect_leaks=1", "UBSAN_OPTIONS": "print_stacktrace=1:halt_on_error=1"})
    return run.returncode, run.stdout + run.stderr


def suite(cxx, flags, work, core, tag, strict=True):
    inc = [core / CP, work, work / "pbinc"]
    outs, codes = [], []
    for name, tu, defs in (("adapter", "adapter_tu.cpp", ()), ("adapter-off", "adapter_tu.cpp", ("CS02_CORE_BATCH=0",)),
                           ("factory", "factory_tu.cpp", ())):
        code, out = compile_run(cxx, flags, tu, work, inc, f"{tag}-{name}", defs, strict)
        if code is None:
            return None, out
        outs.append(out); codes.append(code)
    return codes, "\n".join(outs)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--pb53", required=True); ap.add_argument("--core74", required=True)
    ap.add_argument("--compiler", nargs="+", default=["g++", "clang++"]); ap.add_argument("--sabotage", action="store_true")
    ap.add_argument("--log"); ap.add_argument("--write-pins", action="store_true")
    a = ap.parse_args()
    pb, core = Path(a.pb53).resolve(), Path(a.core74).resolve()
    for repo, want in ((pb, PB53_HEAD), (core, CORE_HEAD)):
        if head_of(repo) != want:
            sys.exit(f"{repo} head {head_of(repo)} != pinned {want}")
        if not clean(repo):
            sys.exit(f"{repo} is not clean")
    blobs = {f"pb53:{f}": blob((pb / f).read_bytes()) for f in PB_FILES}
    blobs.update({f"core74:{f}": blob((core / f).read_bytes()) for f in CORE_FILES})
    if a.write_pins:
        PINS.write_text(f"pb53 {PB53_HEAD}\ncore74 {CORE_HEAD}\n" + "".join(f"{v} {k}\n" for k, v in blobs.items()))
    pinned = {l.split(" ", 1)[1]: l.split(" ", 1)[0] for l in PINS.read_text().splitlines()[2:] if l}
    for k, v in blobs.items():
        if pinned.get(k) != v:
            sys.exit(f"{k} blob {v} != pinned {pinned.get(k)}")
    print(f"PB53 {PB53_HEAD} + Core74 {CORE_HEAD}; {len(blobs)} blobs verified")
    src = source_checks(pb)
    print("\n".join(src))
    status, baseline = 0, None
    with tempfile.TemporaryDirectory(prefix="cs02-l3-") as tmp:
        work = Path(tmp)
        stage(pb, core, work)
        for cxx in a.compiler:
            for mode, flags in (("plain", ["-O2"]), ("asan-ubsan", ["-O1", "-g", "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
                                                                    "-fno-omit-frame-pointer"])):
                tag = f"{Path(cxx).name}-{mode}"
                codes, out = suite(cxx, flags, work, core, tag)
                if a.log:
                    Path(a.log).mkdir(parents=True, exist_ok=True)
                    (Path(a.log) / f"{tag}.log").write_text(out)
                sig = [l for l in out.splitlines() if l.startswith(SIG)]
                if baseline is None:
                    baseline = sig
                    print("\n".join(sig))
                totals = [l for l in out.splitlines() if l.startswith("CS02 layer3")]
                print(f"{tag}: exits {codes}; " + " | ".join(totals) + f"; {'identical to first toolchain' if sig == baseline else 'DIFFERS'}")
                status |= any(codes) or sig != baseline
        if a.sabotage:
            killed = 0
            for m in MUTANTS + [FIX_PROBE]:
                stage(pb, core, work, m)
                codes, out = suite(a.compiler[0], ["-O1"], work, core, "mut", strict=False)
                if codes is None:
                    print(f"[SABOTAGE] {m[0]:<50} NOT RUN (does not compile): {out[-300:]}")
                    continue
                sig = [l for l in out.splitlines() if l.startswith(SIG)]
                changed = [l for l in sig if l not in baseline]
                dead = any(codes) or sig != baseline
                if m is FIX_PROBE:
                    print(f"[FIX-PROBE] {m[0]}: rows that changed:")
                    for l in changed:
                        print("   " + l)
                    continue
                killed += dead
                print(f"[SABOTAGE] {m[0]:<50} {'killed' if dead else 'SURVIVED'}" + (f"  first change: {changed[0][:150]}" if changed else ""))
            print(f"[SABOTAGE] {killed}/{len(MUTANTS)} killed")
    return status


if __name__ == "__main__":
    sys.exit(main())
