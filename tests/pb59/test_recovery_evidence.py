"""Standalone dormant recovery inspection/proof tests; no native runtime/DB."""
import argparse
from pathlib import Path
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--compiler", nargs="+", required=True)
parser.add_argument("--ndebug", action="store_true")
parser.add_argument("--compile-only", action="store_true",
                    help="evaluate C++ constexpr contracts and reject mutants without launching an executable")
args = parser.parse_args()
root = Path(__file__).resolve().parents[2]
native = root / "src/server/game/Entities/Player"
fixture = (Path(__file__).with_name("recovery_compiletime_checks.cpp") if args.compile_only
           else Path(__file__).with_suffix(".cpp"))
with tempfile.TemporaryDirectory(prefix="pb59-recovery-evidence-") as temporary:
    work = Path(temporary)
    binary = work / "recovery.exe"
    flags = ["-std=c++20", "-Wall", "-Wextra", "-Werror", "-pedantic"]
    if args.ndebug:
        flags += ["-DNDEBUG", "-O2"]
    output = ["-fsyntax-only"] if args.compile_only else ["-o", str(binary)]
    command = args.compiler + flags + ["-I", str(work), "-I", str(native), str(fixture)] + output
    subprocess.run(command, check=True)
    if args.compile_only:
        print("C++ compile-time recovery contracts PASS (no executable launched)", flush=True)
    else:
        subprocess.run([str(binary)], check=True)
    mutations = [
        ("PB59RecoveryProof.h", "!native.exactCustody || ", ""),
        ("PB59RecoveryProof.h", "!native.completeEffects || ", ""),
        ("PB59RecoveryProof.h", " || !native.exactQueues", ""),
        ("PB59RecoveryProof.h", "!native.retainedLifetime || ", ""),
        ("PB59RecoveryProof.h", " && durable.commitSequence", ""),
        ("PB59RecoveryEvidence.h", "if (evidence.effects != ProofState::Verified)", "if (false)"),
        ("PB59RecoveryEvidence.h", "original.instance != InstanceState::ExactNativeInstance", "false"),
        ("PB59RecoveryEvidence.h", "add(Issue::InvalidRecord);\n        return result;",
         "result.issues = static_cast<std::uint32_t>(Issue::InvalidRecord);\n        return result;"),
    ]
    # Keep the proof header alongside each candidate so quoted includes bind
    # to a mutated evidence header rather than the original header's directory.
    for name in ("PB59RecoveryProof.h", "PB59RecoveryEvidence.h"):
        (work / name).write_text((native / name).read_text(encoding="utf-8"), encoding="utf-8")
    for name, before, after in mutations:
        text = (native / name).read_text(encoding="utf-8")
        assert text.count(before) == 1, (name, before)
        (work / name).write_text(text.replace(before, after), encoding="utf-8")
        if args.compile_only:
            result = subprocess.run(command, capture_output=True, text=True)
            assert result.returncode != 0 and "static assertion" in result.stderr, (name, before, result.stderr)
        else:
            subprocess.run(command, check=True)
            result = subprocess.run([str(binary)], capture_output=True, text=True)
            assert result.returncode != 0, (name, before, "mutant survived")
        (work / name).write_text(text, encoding="utf-8")
    mode = "compile-time" if args.compile_only else "runtime"
    print(f"{len(mutations)}/{len(mutations)} proof/diagnostic mutants rejected ({mode})")
