#!/usr/bin/env python3
"""CS02 Layer 3c runner: PB56 66c66ddc focused rerun NEXT TO the PB54 ec276855 baseline, against REAL Core74 c779e7b4.

Test-only and local (no hosted CI); not a worldserver build, not gameplay. See README.md.
  run_layer3c.py --pb54 <mod-playerbots@ec276855> --pb56 <mod-playerbots@66c66ddc> --core74 <azerothcore@c779e7b4>
                 [--compiler g++ clang++] [--sabotage] [--log DIR] [--write-pins]
Every binary runs the unchanged PR77 Layer 3 rows, the unchanged PR80 Layer 3b rows, then the new rows (l3c_checks.inc).
Staging reuses ../layer3b_pb54/run_layer3b.py unchanged and adds the PB56 custody header when present.
"""
import argparse, importlib.util, subprocess, sys, tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
L3B = HERE.parent / "layer3b_pb54"
spec = importlib.util.spec_from_file_location("l3b", L3B / "run_layer3b.py")
l3b = importlib.util.module_from_spec(spec); spec.loader.exec_module(l3b)
l3 = l3b.l3
blob, once, head_of, clean = l3.blob, l3.once, l3.head_of, l3.clean

HEADS = {"pb54": "ec276855d869bdb30b9aa1907739e386e0fcac1e", "pb56": "66c66ddc3dc07ea81b5f27d96fec4f77f907afe3"}
CORE_HEAD, TA = l3.CORE_HEAD, l3.TA
CUSTODY_H = "src/Bot/AutoGearBatchCustody.h"
PINS = HERE / "PINS.txt"
SIG = l3b.SIG
# libstdc++ assertions are added to the ASan+UBSan builds (baseline and mutants alike) so a disengaged std::optional
# dereference aborts instead of reading stale storage (added after W2 first survived).
ASAN = l3b.ASAN + ["-D_GLIBCXX_ASSERTIONS"]
INCLUDE_CUSTODY = '#if __has_include("AutoGearBatchCustody.h")\n#include "AutoGearBatchCustody.h"  // REAL PB56 (TrainerAction.cpp includes it under PB59_AUTOGEAR_CORE_BATCH)\n#endif\n'
FIXED_EXPECT = {"pb54": None, "pb56": None}


def pb_files(repo):
    return l3.PB_FILES + ([CUSTODY_H] if (repo / CUSTODY_H).exists() else [])


def mutants(cfg):
    if cfg != "pb56":
        return []
    return [
        ("C1 SameCustody ignores GUID", CUSTODY_H, "return a.guid == b.guid && a.owner == b.owner", "return a.owner == b.owner"),
        ("C2 SameCustody ignores owner", CUSTODY_H, "a.guid == b.guid && a.owner == b.owner && a.entry", "a.guid == b.guid && a.entry"),
        ("C3 SameCustody ignores entry", CUSTODY_H, "a.owner == b.owner && a.entry == b.entry && a.count", "a.owner == b.owner && a.count"),
        ("C4 SameCustody ignores count", CUSTODY_H, " && a.count == b.count;", ";"),
        ("C5 size check removed", CUSTODY_H, "before.empty() || before.size() != after.size() ||", "before.empty() ||"),
        ("C6 per-slot validity removed", CUSTODY_H, "    if (!valid(before) || !valid(after))\n        return false;\n", ""),
        ("C7 duplicate position/GUID check removed", CUSTODY_H,
         "            for (std::size_t j = 0; j < i; ++j)\n                if (s.position == slots[j].position ||\n"
         "                    (s.item && slots[j].item && s.item->guid == slots[j].item->guid))\n                    return false;\n", ""),
        ("C8 planned-source verification removed", CUSTODY_H,
         "        if (!source || !dest || !source->item || dest->item || source->bag != move.source.bag ||\n"
         "            dest->bag != move.destination.bag || !SameCustody(*source->item, *move.source.item))\n            return false;\n",
         "        (void)source; (void)dest;\n"),
        ("C9 container (bag GUID) check removed", CUSTODY_H, "        if (!now || now->bag != old.bag)", "        if (!now)"),
        ("C10 planned destination not swapped in", CUSTODY_H,
         "            else if (old.position == move.destination.position)\n                expected = &move.source;\n", ""),
        ("C11 presence compare removed", CUSTODY_H, "        if (expected->item.has_value() != now->item.has_value() ||\n            (expected->item && !SameCustody(*expected->item, *now->item)))",
         "        if (expected->item && now->item && !SameCustody(*expected->item, *now->item))"),
        ("W1 custody check not wired", TA, "            if (outcome == CoreBatchOutcome::Applied &&\n                (!after ||",
         "            if (false && outcome == CoreBatchOutcome::Applied &&\n                (!after ||"),
        ("W2 missing after-snapshot accepted", TA, "                (!after || before->session", "                (false || before->session"),
        ("W3 non-slot GUID vector ignored", TA, " || before->others != after->others ||", " ||"),
        ("W4 comparator result ignored", TA, "                 !AppliedCustodyPreserved(owner, before->slots, after->slots, built.moves)))",
         "                 (AppliedCustodyPreserved(owner, before->slots, after->slots, built.moves) && false)))"),
        ("W5 context fields not compared", TA,
         "(!after || before->session != after->session || before->inWorld != after->inWorld ||\n                 before->alive != after->alive || before->logout != after->logout ||",
         "(!after ||\n                 "),
    ]


def stage(pb, core, work, mutant=None):
    inner = mutant if mutant and mutant[1] != CUSTODY_H else None
    l3b.stage(pb, core, work, inner)
    if (pb / CUSTODY_H).exists():
        text = (pb / CUSTODY_H).read_text()
        if mutant and mutant[1] == CUSTODY_H:
            text = once(text, mutant[2], mutant[3], mutant[0])
        (work / "pbinc" / "AutoGearBatchCustody.h").write_text(text)
    else:
        (work / "pbinc" / "AutoGearBatchCustody.h").unlink(missing_ok=True)
    am = work / "AdapterMethods.inc"
    am.write_text(INCLUDE_CUSTODY + am.read_text())
    checks = (work / "Checks.inc").read_text()
    checks = once(checks, "#undef main\n", "#undef main\n#define main cs02_l3b_main\n", "rename L3b main")
    # Harness-only hook (L3c): run a callback right after the native call returns (row K13b).
    checks = once(checks, "int forceOutcome = -1;", "int forceOutcome = -1; std::function<void()> afterCore;", "afterCore member")
    checks = once(checks, '        if (g_trace) g_trace->push_back({nullptr, "CORE_RETURN"});\n',
                  '        if (g_trace) g_trace->push_back({nullptr, "CORE_RETURN"});\n        if (afterCore) afterCore();\n', "afterCore call")
    (work / "Checks.inc").write_text(checks + "#undef main\n" + (HERE / "l3c_checks.inc").read_text())


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--pb54", required=True); ap.add_argument("--pb56", required=True); ap.add_argument("--core74", required=True)
    ap.add_argument("--compiler", nargs="+", default=["g++", "clang++"]); ap.add_argument("--sabotage", action="store_true")
    ap.add_argument("--log"); ap.add_argument("--write-pins", action="store_true")
    a = ap.parse_args()
    repos = {"pb54": Path(a.pb54).resolve(), "pb56": Path(a.pb56).resolve()}
    core = Path(a.core74).resolve()
    for name, repo in list(repos.items()) + [("core74", core)]:
        want = HEADS.get(name, CORE_HEAD)
        if head_of(repo) != want: sys.exit(f"{repo} head {head_of(repo)} != pinned {want}")
        if not clean(repo): sys.exit(f"{repo} is not clean")
    blobs = {}
    for name, repo in repos.items():
        blobs.update({f"{name}:{f}": blob((repo / f).read_bytes()) for f in pb_files(repo)})
    blobs.update({f"core74:{f}": blob((core / f).read_bytes()) for f in l3.CORE_FILES})
    for rel in ("layer3_pb53/l3_adapter_checks.inc", "layer3_pb53/run_layer3.py", "layer3b_pb54/l3b_checks.inc", "layer3b_pb54/run_layer3b.py"):
        blobs[f"cs02:{rel}"] = blob((HERE.parent / rel).read_bytes())
    if a.write_pins:
        PINS.write_text("".join(f"{k} {v}\n" for k, v in HEADS.items()) + f"core74 {CORE_HEAD}\n" + "".join(f"{v} {k}\n" for k, v in blobs.items()))
    pinned = {l.split(" ", 1)[1]: l.split(" ", 1)[0] for l in PINS.read_text().splitlines()[3:] if l}
    for k, v in blobs.items():
        if pinned.get(k) != v: sys.exit(f"{k} blob {v} != pinned {pinned.get(k)}")
    print(f"PB54 {HEADS['pb54']} | PB56 {HEADS['pb56']} | Core74 {CORE_HEAD}; {len(blobs)} blobs verified")
    status, baselines = 0, {}
    with tempfile.TemporaryDirectory(prefix="cs02-l3c-") as tmp:
        for cfg, repo in repos.items():
            print(f"=== configuration {cfg} ({HEADS[cfg][:8]}) ===")
            print("\n".join(l3.source_checks(repo)))
            work = Path(tmp) / cfg; work.mkdir()
            stage(repo, core, work)
            base = None
            for cxx in a.compiler:
                for mode, flags in (("plain", ["-O2"]), ("asan-ubsan-assert", ASAN)):
                    tag = f"{cfg}-{Path(cxx).name}-{mode}"
                    codes, out = l3.suite(cxx, flags, work, core, tag)
                    if codes is None: sys.exit(out[-6000:])
                    if a.log:
                        Path(a.log).mkdir(parents=True, exist_ok=True); (Path(a.log) / f"{tag}.log").write_text(out)
                    sig = [l for l in out.splitlines() if l.startswith(SIG)]
                    if base is None:
                        base = sig; print("\n".join(f"[{cfg}] {l}" for l in sig))
                    totals = [l for l in out.splitlines() if l.startswith("CS02 layer3")]
                    print(f"{tag}: exits {codes}; " + " | ".join(totals) + f"; {'identical to first toolchain' if sig == base else 'DIFFERS'}")
                    status |= any(codes) or sig != base
            baselines[cfg] = base
            if a.sabotage:
                ms = mutants(cfg); killed = 0
                for m in ms:
                    stage(repo, core, work, m)
                    codes, out = l3.suite(a.compiler[0], ASAN, work, core, "mut", strict=False)
                    if codes is None:
                        print(f"[SABOTAGE] {m[0]:<45} NOT RUN (does not compile): {out[-400:]}"); continue
                    sig = [l for l in out.splitlines() if l.startswith(SIG)]
                    changed = [l for l in sig if l not in base]
                    dead = any(codes) or sig != base
                    killed += dead
                    why = changed[0][:170] if changed else ("exit " + str(codes))
                    print(f"[SABOTAGE] {m[0]:<45} {'killed' if dead else 'SURVIVED'}" + (f"  first change: {why}" if dead else ""))
                stage(repo, core, work)
                if ms: print(f"[SABOTAGE] {killed}/{len(ms)} killed")
        print("=== rows that differ PB54 -> PB56 ===")
        a54, a56 = set(baselines["pb54"]), set(baselines["pb56"])
        for l in baselines["pb56"]:
            if l not in a54 and l.startswith(("SCN", "MATRIX", "SRC")): print("PB56 " + l)
        for l in baselines["pb54"]:
            if l not in a56 and l.startswith(("SCN", "MATRIX", "SRC")): print("PB54 " + l)
    return status


if __name__ == "__main__":
    sys.exit(main())
