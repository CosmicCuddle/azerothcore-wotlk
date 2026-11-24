#!/usr/bin/env python3
"""CS02 Layer 3e runner: independent rerun of the ACTUAL PB57 4e1f253e source (CS01 #52 7144147541) with REAL Core74
c779e7b4, reusing the unchanged PR86 L3d rows (layer3d_pb56_nonslot/l3d_checks.inc) and all PR77/PR80/PR83 rows.

Test-only and local (no hosted CI); not a worldserver build, not gameplay. See README.md.
  run_layer3e.py --pb56 <mod-playerbots@66c66ddc> --pb57 <mod-playerbots@4e1f253e> --core74 <azerothcore@c779e7b4>
                 [--compiler g++ clang++] [--sabotage] [--log DIR] [--write-pins]
Configurations: pb57 (actual) and pb56-sketch (the PR86 throwaway sketch, reference only).
Also prints a token-level equivalence check: staged PB57 members vs staged PB56 members + PR86 SKETCH.
"""
import argparse, importlib.util, re, sys, tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location("l3d", HERE.parent / "layer3d_pb56_nonslot" / "run_layer3d.py")
l3d = importlib.util.module_from_spec(spec); spec.loader.exec_module(l3d)
l3c, l3 = l3d.l3c, l3d.l3
blob, once, head_of, clean = l3.blob, l3.once, l3.head_of, l3.clean
PB57_HEAD = "4e1f253efa1b9dc1b7488f14bb56d0e35f727ef0"
PB56_HEAD = l3d.PB56_HEAD
CORE_HEAD = l3.CORE_HEAD
PINS = HERE / "PINS.txt"
SIG, ASAN = l3d.SIG, l3d.ASAN
# Field mutants written against the ACTUAL PB57 text (staged copy only).
MUTANTS = [
    ("Y1 PB57 drops owner", "item->GetOwnerGUID().GetRawValue(), ", "0, "),
    ("Y2 PB57 drops count", "uint64(item->GetCount())}", "0}"),
    ("Y3 PB57 drops identity", "std::array<uint64, 4>{item->GetGUID().GetRawValue(),", "std::array<uint64, 4>{0,"),
    ("Y4 PB57 drops entry", "uint64(item->GetEntry()), ", "0, "),
    ("Y5 PB57 others not compared", " || before->others != after->others ||", " ||"),
]


def tokens(text):
    text = re.sub(r"//[^\n]*", "", text)
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    return re.findall(r"[A-Za-z_]\w*|\d+|::|->|!=|==|&&|\|\||<<|>>|\S", text)


def stage57(pb, core, work, mutant=None):
    l3d.stage(pb, core, work, False)
    if mutant:
        am = work / "AdapterMethods.inc"
        am.write_text(once(am.read_text(), mutant[1], mutant[2], mutant[0]))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--pb56", required=True); ap.add_argument("--pb57", required=True); ap.add_argument("--core74", required=True)
    ap.add_argument("--compiler", nargs="+", default=["g++", "clang++"]); ap.add_argument("--sabotage", action="store_true")
    ap.add_argument("--log"); ap.add_argument("--write-pins", action="store_true")
    a = ap.parse_args()
    pb56, pb57, core = Path(a.pb56).resolve(), Path(a.pb57).resolve(), Path(a.core74).resolve()
    for repo, want in ((pb56, PB56_HEAD), (pb57, PB57_HEAD), (core, CORE_HEAD)):
        if head_of(repo) != want: sys.exit(f"{repo} head {head_of(repo)} != pinned {want}")
        if not clean(repo): sys.exit(f"{repo} is not clean")
    blobs = {f"pb57:{f}": blob((pb57 / f).read_bytes()) for f in l3c.pb_files(pb57)}
    blobs.update({f"pb56:{f}": blob((pb56 / f).read_bytes()) for f in l3c.pb_files(pb56)})
    blobs.update({f"core74:{f}": blob((core / f).read_bytes()) for f in l3.CORE_FILES})
    for rel in ("layer3_pb53/l3_adapter_checks.inc", "layer3_pb53/run_layer3.py", "layer3b_pb54/l3b_checks.inc",
                "layer3b_pb54/run_layer3b.py", "layer3c_pb56/l3c_checks.inc", "layer3c_pb56/run_layer3c.py",
                "layer3d_pb56_nonslot/l3d_checks.inc", "layer3d_pb56_nonslot/run_layer3d.py"):
        blobs[f"cs02:{rel}"] = blob((HERE.parent / rel).read_bytes())
    if a.write_pins:
        PINS.write_text(f"pb57 {PB57_HEAD}\npb56 {PB56_HEAD}\ncore74 {CORE_HEAD}\n" + "".join(f"{v} {k}\n" for k, v in blobs.items()))
    pinned = {l.split(" ", 1)[1]: l.split(" ", 1)[0] for l in PINS.read_text().splitlines()[3:] if l}
    for k, v in blobs.items():
        if pinned.get(k) != v: sys.exit(f"{k} blob {v} != pinned {pinned.get(k)}")
    print(f"PB57 {PB57_HEAD} | PB56 {PB56_HEAD} | Core74 {CORE_HEAD}; {len(blobs)} blobs verified")
    status, baselines = 0, {}
    with tempfile.TemporaryDirectory(prefix="cs02-l3e-") as tmp:
        # Equivalence: staged verbatim PB57 members vs staged PB56 members with the PR86 SKETCH applied.
        e57, e56 = Path(tmp) / "eq57", Path(tmp) / "eq56"; e57.mkdir(); e56.mkdir()
        stage57(pb57, core, e57); l3d.stage(pb56, core, e56, True)
        t57, t56 = tokens((e57 / "AdapterMethods.inc").read_text()), tokens((e56 / "AdapterMethods.inc").read_text())
        print(f"EQUIV staged AdapterMethods.inc token streams (comments stripped): pb57={len(t57)} sketch={len(t56)} "
              f"{'IDENTICAL' if t57 == t56 else 'DIFFERENT'}")
        if t57 != t56:
            import difflib
            for l in list(difflib.unified_diff(t56, t57, "sketch", "pb57", n=3, lineterm=""))[:80]: print("EQUIV " + l)
        for cfg, pb, sketch in (("pb57", pb57, False), ("pb56-sketch", pb56, True)):
            print(f"=== configuration {cfg}{' (PR86 test-only sketch, reference)' if sketch else ' (ACTUAL source)'} ===")
            work = Path(tmp) / cfg; work.mkdir()
            l3d.stage(pb, core, work, sketch)
            base = None
            for cxx in a.compiler:
                for mode, flags in (("plain", ["-O2"]), ("asan-ubsan-assert", ASAN)):
                    tag = f"{cfg}-{Path(cxx).name}-{mode}"
                    codes, out = l3d.suite(cxx, flags, work, core, tag)
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
            if a.sabotage and not sketch:
                killed = 0
                for m in MUTANTS:
                    stage57(pb57, core, work, m)
                    codes, out = l3d.suite(a.compiler[0], ASAN, work, core, "mut", strict=False)
                    if codes is None:
                        print(f"[SABOTAGE] {m[0]:<34} NOT RUN: {out[-300:]}"); continue
                    sig = [l for l in out.splitlines() if l.startswith(SIG)]
                    changed = [l for l in sig if l not in base]
                    dead = any(codes) or sig != base
                    killed += dead
                    print(f"[SABOTAGE] {m[0]:<34} {'killed' if dead else 'SURVIVED'}" + (f"  first change: {changed[0][:170]}" if changed else ""))
                print(f"[SABOTAGE] {killed}/{len(MUTANTS)} killed")
                l3d.stage(pb57, core, work, False)
        s57, ssk = baselines["pb57"], baselines["pb56-sketch"]
        only57 = [l for l in s57 if l not in ssk]; onlysk = [l for l in ssk if l not in s57]
        print(f"=== row comparison pb57 vs PR86 sketch: {len(s57)} vs {len(ssk)} signature lines; "
              f"{len(only57)} only in pb57, {len(onlysk)} only in sketch ===")
        for l in only57: print("PB57   " + l)
        for l in onlysk: print("SKETCH " + l)
    return status


if __name__ == "__main__":
    sys.exit(main())
