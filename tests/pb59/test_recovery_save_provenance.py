"""Evaluate real save-record provenance contracts without executing a binary."""
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
database = root / "src/server/database/Database"
fixture = Path(__file__).with_name("recovery_save_provenance_checks.cpp")
name = "PB59RecoverySaveProvenance.h"
source = (native / name).read_text(encoding="utf-8")
mutations = [
    ("id.session != expected.sessionIncarnation", "false"),
    ("record.ticket != expectedSave", "false"),
    ("record.ticket.slot >= Capacity", "false"),
    ("result.captureComplete = !evidence.incomplete;", "result.captureComplete = true;"),
    ("result.allOriginalGuidsListed = false;", "result.allOriginalGuidsListed = true;"),
    ("result.observation = SaveObservation::Pending;",
     "result.observation = SaveObservation::Pending, result.executionKnown = true;"),
    ("DatabaseTransactionResult::FromExecutionCode(record.result.executionCode).status != record.result.status", "false"),
    ("std::find(values.begin(), values.begin() + index, values[index]) != values.begin() + index", "false"),
]
with tempfile.TemporaryDirectory(prefix="pb59-save-provenance-") as temporary:
    work = Path(temporary)
    flags = ["-std=c++20", "-Wall", "-Wextra", "-Werror", "-pedantic", "-fsyntax-only"]
    if args.ndebug:
        flags += ["-DNDEBUG", "-O2"]
    command = args.compiler + flags + ["-I", str(work), "-I", str(native), "-I", str(database), str(fixture)]
    subprocess.run(command, check=True)
    print("Actual save-record/transaction-result constexpr contracts PASS (no executable)", flush=True)
    for before, after in mutations:
        assert source.count(before) == 1, before
        (work / name).write_text(source.replace(before, after), encoding="utf-8")
        result = subprocess.run(command, capture_output=True, text=True)
        assert result.returncode != 0 and "static assertion" in result.stderr, (before, result.stderr)
    print(f"{len(mutations)}/{len(mutations)} save-provenance mutants rejected by static assertions")
