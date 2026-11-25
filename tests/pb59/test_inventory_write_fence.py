"""Run the inventory write fence checks against the real Core headers."""
import argparse
from pathlib import Path
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--compiler", nargs="+", required=True)
parser.add_argument("--ndebug", action="store_true")
parser.add_argument("--mutants", action="store_true")
args = parser.parse_args()
root = Path(__file__).resolve().parents[2]
player = root / "src/server/game/Entities/Player"
database = root / "src/server/database/Database"
source = Path(__file__).with_name("inventory_write_fence_checks.cpp")
header = "PB59InventoryWriteFence.h"
flags = ["-std=c++20", "-Wall", "-Wextra", "-Werror", "-pedantic", "-pthread"]
if args.ndebug:
    flags += ["-DNDEBUG", "-O2"]
with tempfile.TemporaryDirectory(prefix="pb59-write-fence-") as temporary:
    work = Path(temporary)
    binary = work / "fence.exe"
    compile_fixture = args.compiler + flags + ["-I", str(work), "-I", str(player), "-I", str(database),
        str(source), "-o", str(binary)]
    subprocess.run(compile_fixture, check=True)
    subprocess.run([str(binary)], check=True, timeout=30)
    if args.mutants:
        original = (player / header).read_text(encoding="utf-8")
        mutants = [
            ("if (entry->writers || entry->uncertain)", "if (entry->uncertain)"),
            ("if (!entry || entry->capture)", "if (!entry)"),
            ("Release(!certain);", "Release(false);"),
            ("~Lease() { Release(_possiblySubmitted); }", "~Lease() { Release(false); }"),
            ("if (!_entry->writers && !_entry->capture", "if (!_entry->capture"),
            ("else if (!uncertain)", "else"),
            ("if (result.IsCommitted())\n                return RecoverySettlement::NeedsVerification;",
             "if (result.IsCommitted()) { _capture.Release(false); return RecoverySettlement::NeedsVerification; }"),
            ("if (capture._state != _state)", "if (!capture._state)"),
            (" || _settled)", ")"),
        ]
        for before, after in mutants:
            if original.count(before) != 1:
                raise RuntimeError(f"Expected one mutation site for {before}")
            (work / header).write_text(original.replace(before, after), encoding="utf-8")
            subprocess.run(compile_fixture, check=True)
            result = subprocess.run([str(binary)], capture_output=True, text=True, check=False, timeout=30)
            if result.returncode != 1 or not result.stderr.startswith("inventory write fence line "):
                raise RuntimeError(f"Expected a fixture assertion after removing {before}. Got {result!r}")
        print(f"{len(mutants)}/{len(mutants)} write-fence mutants rejected by runtime assertions")
