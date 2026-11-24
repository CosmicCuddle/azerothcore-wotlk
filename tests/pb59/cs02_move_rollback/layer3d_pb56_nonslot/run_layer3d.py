#!/usr/bin/env python3
"""CS02 Layer 3d runner: test-first non-slot custody specification (K9/K10) against real PB56 66c66ddc and a
TEST-ONLY throwaway sketch of PB56 (never committed to any PB branch), with REAL Core74 c779e7b4.

Test-only and local (no hosted CI); not a worldserver build, not gameplay. See README.md.
  run_layer3d.py --pb56 <mod-playerbots@66c66ddc> --core74 <azerothcore@c779e7b4> [--compiler g++ clang++] [--sabotage] [--log DIR]
Each configuration builds per toolchain: adapter ON (all PR77/PR80/PR83 rows + L3d), adapter OFF, factory, and
adapter ON with CS02_REALCAP=1 (L3d rows only, closer-to-real capture double).
"""
import argparse, importlib.util, subprocess, sys, tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
L3C = HERE.parent / "layer3c_pb56"
spec = importlib.util.spec_from_file_location("l3c", L3C / "run_layer3c.py")
l3c = importlib.util.module_from_spec(spec); spec.loader.exec_module(l3c)
l3 = l3c.l3
blob, once, head_of, clean = l3.blob, l3.once, l3.head_of, l3.clean
PB56_HEAD = l3c.HEADS["pb56"]
CORE_HEAD = l3.CORE_HEAD
PINS = HERE / "PINS.txt"
SIG = l3c.SIG
ASAN = l3c.ASAN

# TEST-ONLY sketch of a minimal PB56 extension, applied to the staged copy of the verbatim PB56 members.
SKETCH = [
    ("            std::vector<uint64> others;  // shirt, tabard and bag items, which slot facts do not cover\n",
     "            // CS02 SKETCH (test-only): identity, owner, entry and count of shirt, tabard and bag items; the index is the location.\n"
     "            std::vector<std::array<uint64, 4>> others;\n"),
    ("                print.others.push_back(item ? item->GetGUID().GetRawValue() : 0);\n",
     "                print.others.push_back(item ? std::array<uint64, 4>{item->GetGUID().GetRawValue(), item->GetOwnerGUID().GetRawValue(),\n"
     "                                                                  uint64(item->GetEntry()), uint64(item->GetCount())}\n"
     "                                            : std::array<uint64, 4>{});\n"),
]
SKETCH_MUTANTS = [
    ("X1 sketch drops owner", "item->GetOwnerGUID().GetRawValue(),\n", "0,\n"),
    ("X2 sketch drops count", "uint64(item->GetCount())}", "0}"),
    ("X3 sketch drops identity", "std::array<uint64, 4>{item->GetGUID().GetRawValue(),", "std::array<uint64, 4>{0,"),
    ("X4 sketch others not compared", " || before->others != after->others ||", " ||"),
]
FIXTURE = [  # harness-only; doubles
    ("Guid GetOwnerGUID() const { return {facts.owner}; }", "Guid GetOwnerGUID() const { ++g_itemFieldReads; return {facts.owner}; }"),
    ("uint32 GetCount() const { return facts.count; } uint32 GetEntry() const { return facts.entry; }",
     "uint32 GetCount() const { ++g_itemFieldReads; return facts.count; } uint32 GetEntry() const { ++g_itemFieldReads; return facts.entry; }"),
    ("        auto found = slots.find(pos); if (found == slots.end()) return {};\n",
     "        auto found = slots.find(pos); if (found == slots.end()) return {};\n#if CS02_REALCAP\n"
     "        { uint8 b_ = uint8(pos >> 8), s_ = uint8(pos & 255);  // PlayerStorage.cpp:531-534, :551-552\n"
     "          if (b_ == 255 && !((s_ < 19 && s_ != 3 && s_ != 18) || (s_ >= 23 && s_ < 39))) return {};\n"
     "          if (found->second && g_cs02Bags.count(found->second)) return {}; }  // PlayerStorage.cpp:564 IsBag\n#endif\n"),
    ("if (!container) return {}; bag = container->GetGUID().value; }",
     "if (!container) return {};\n#if CS02_REALCAP\n"
     "            if (container->facts.owner != guid || GetItemByPos(uint16(0xFF00 | (pos >> 8))) != container) return {};  // PlayerStorage.cpp:543-546\n"
     "#endif\n            bag = container->GetGUID().value; }"),
]
FIXTURE_PRELUDE = ("#include <set>\n#ifndef CS02_REALCAP\n#define CS02_REALCAP 0\n#endif\n"
                   "static int g_itemFieldReads = 0;\nstatic std::set<void const*> g_cs02Bags;\n")


def stage(pb, core, work, sketch=False, mutant=None):
    l3c.stage(pb, core, work)
    am = (work / "AdapterMethods.inc").read_text()
    if sketch:
        for old, new in SKETCH:
            am = once(am, old, new, "sketch")
        if mutant:
            am = once(am, mutant[1], mutant[2], mutant[0])
    (work / "AdapterMethods.inc").write_text("#include <array>\n" + am)
    fx = (work / "adapter_tu.cpp").read_text()
    for old, new in FIXTURE:
        fx = once(fx, old, new, old[:40])
    (work / "adapter_tu.cpp").write_text(FIXTURE_PRELUDE + fx)
    checks = (work / "Checks.inc").read_text()
    checks = once(checks, "int main()\n{\n    int const rc = cs02_l3b_main();", "int cs02_l3c_main()\n{\n    int const rc = cs02_l3b_main();", "rename L3c main")
    (work / "Checks.inc").write_text(checks + (HERE / "l3d_checks.inc").read_text())


def suite(cxx, flags, work, core, tag, strict=True):
    inc = [core / l3.CP, work, work / "pbinc"]
    outs, codes = [], []
    for name, tu, defs in (("adapter", "adapter_tu.cpp", ()), ("adapter-off", "adapter_tu.cpp", ("CS02_CORE_BATCH=0",)),
                           ("factory", "factory_tu.cpp", ()), ("adapter-realcap", "adapter_tu.cpp", ("CS02_REALCAP=1",))):
        code, out = l3.compile_run(cxx, flags, tu, work, inc, f"{tag}-{name}", defs, strict)
        if code is None:
            return None, out
        outs.append(out); codes.append(code)
    return codes, "\n".join(outs)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--pb56", required=True); ap.add_argument("--core74", required=True)
    ap.add_argument("--compiler", nargs="+", default=["g++", "clang++"]); ap.add_argument("--sabotage", action="store_true")
    ap.add_argument("--log"); ap.add_argument("--write-pins", action="store_true")
    a = ap.parse_args()
    pb, core = Path(a.pb56).resolve(), Path(a.core74).resolve()
    for repo, want in ((pb, PB56_HEAD), (core, CORE_HEAD)):
        if head_of(repo) != want: sys.exit(f"{repo} head {head_of(repo)} != pinned {want}")
        if not clean(repo): sys.exit(f"{repo} is not clean")
    blobs = {f"pb56:{f}": blob((pb / f).read_bytes()) for f in l3c.pb_files(pb)}
    blobs.update({f"core74:{f}": blob((core / f).read_bytes()) for f in l3.CORE_FILES})
    for rel in ("layer3_pb53/l3_adapter_checks.inc", "layer3_pb53/run_layer3.py", "layer3b_pb54/l3b_checks.inc",
                "layer3b_pb54/run_layer3b.py", "layer3c_pb56/l3c_checks.inc", "layer3c_pb56/run_layer3c.py"):
        blobs[f"cs02:{rel}"] = blob((HERE.parent / rel).read_bytes())
    if a.write_pins:
        PINS.write_text(f"pb56 {PB56_HEAD}\ncore74 {CORE_HEAD}\n" + "".join(f"{v} {k}\n" for k, v in blobs.items()))
    pinned = {l.split(" ", 1)[1]: l.split(" ", 1)[0] for l in PINS.read_text().splitlines()[2:] if l}
    for k, v in blobs.items():
        if pinned.get(k) != v: sys.exit(f"{k} blob {v} != pinned {pinned.get(k)}")
    print(f"PB56 {PB56_HEAD} | Core74 {CORE_HEAD}; {len(blobs)} blobs verified")
    status, baselines = 0, {}
    with tempfile.TemporaryDirectory(prefix="cs02-l3d-") as tmp:
        for cfg, sketch in (("pb56", False), ("pb56-sketch", True)):
            print(f"=== configuration {cfg}{' (TEST-ONLY throwaway sketch, not a PB patch)' if sketch else ''} ===")
            work = Path(tmp) / cfg; work.mkdir()
            stage(pb, core, work, sketch)
            base = None
            for cxx in a.compiler:
                for mode, flags in (("plain", ["-O2"]), ("asan-ubsan-assert", ASAN)):
                    tag = f"{cfg}-{Path(cxx).name}-{mode}"
                    codes, out = suite(cxx, flags, work, core, tag)
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
            if a.sabotage and sketch:
                killed = 0
                for m in SKETCH_MUTANTS:
                    stage(pb, core, work, True, m)
                    codes, out = suite(a.compiler[0], ASAN, work, core, "mut", strict=False)
                    if codes is None:
                        print(f"[SABOTAGE] {m[0]:<40} NOT RUN: {out[-300:]}"); continue
                    sig = [l for l in out.splitlines() if l.startswith(SIG)]
                    changed = [l for l in sig if l not in base]
                    dead = any(codes) or sig != base
                    killed += dead
                    print(f"[SABOTAGE] {m[0]:<40} {'killed' if dead else 'SURVIVED'}" + (f"  first change: {changed[0][:170]}" if changed else ""))
                print(f"[SABOTAGE] {killed}/{len(SKETCH_MUTANTS)} killed")
        print("=== rows that differ PB56 -> sketch ===")
        a, b = set(baselines["pb56"]), set(baselines["pb56-sketch"])
        for l in baselines["pb56-sketch"]:
            if l not in a and l.startswith(("SCN", "MATRIX", "SRC")): print("SKETCH " + l)
        for l in baselines["pb56"]:
            if l not in b and l.startswith(("SCN", "MATRIX", "SRC")): print("PB56   " + l)
    return status


if __name__ == "__main__":
    sys.exit(main())
