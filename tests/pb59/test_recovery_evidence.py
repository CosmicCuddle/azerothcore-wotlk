"""Standalone dormant recovery inspection/proof tests; no native runtime/DB."""
import argparse
from pathlib import Path
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--compiler", nargs="+", required=True)
parser.add_argument("--ndebug", action="store_true")
args = parser.parse_args()
root = Path(__file__).resolve().parents[2]
native = root / "src/server/game/Entities/Player"
fixture = Path(__file__).with_suffix(".cpp")
with tempfile.TemporaryDirectory(prefix="pb59-recovery-evidence-") as temporary:
    work = Path(temporary)
    binary = work / "recovery.exe"
    flags = ["-std=c++20", "-Wall", "-Wextra", "-Werror", "-pedantic"]
    if args.ndebug:
        flags += ["-DNDEBUG", "-O2"]
    command = args.compiler + flags + ["-I", str(work), "-I", str(native), str(fixture), "-o", str(binary)]
    subprocess.run(command, check=True)
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
        subprocess.run(command, check=True)
        result = subprocess.run([str(binary)], capture_output=True, text=True)
        assert result.returncode != 0, (name, before, "mutant survived")
        (work / name).write_text(text, encoding="utf-8")
    print(f"{len(mutations)}/{len(mutations)} missing-proof mutants rejected")
