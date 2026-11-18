#!/usr/bin/env python3
"""Compile actual committed PB59TransactionAdmission.h with save-deferral fixture.

This only validates ledger ordering; it never accesses Worldserver, DB or bots.
"""
import argparse
from pathlib import Path
import re
import subprocess
import sys
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument("--cxx", default="g++")
parser.add_argument("--sanitize", action="store_true")
args = parser.parse_args()

root = Path(__file__).resolve().parents[2]
header_dir = root / "src/server/game/Entities/Player"
source = Path(__file__).with_name("pb59_external_save_deferral.cpp")
flags = ["-std=c++20", "-Wall", "-Wextra", "-Werror", "-pedantic"]
if args.sanitize:
    flags += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
with tempfile.TemporaryDirectory(prefix="pb59-external-save-") as temp:
    executable = Path(temp) / "test_external_save_deferral"
    subprocess.run([args.cxx, *flags, "-I", str(header_dir),
                    str(source), "-o", str(executable)], check=True)
    ran = subprocess.run([str(executable)], text=True, capture_output=True, check=False)
    print(ran.stdout, end="")
    if ran.stderr:
        print(ran.stderr, end="", file=sys.stderr)
    if ran.returncode != 0:
        raise SystemExit(f"FAIL ledger fixture returned {ran.returncode}")
    # The current native-header fixture deliberately runs 56 real assertions.
    # Never treat a shortened or substituted executable as a test pass.
    counts = re.findall(r"(?m)^checks=(\d+) status=(PASS|FAIL)\s*$", ran.stdout)
    if len(counts) != 1 or counts[0][1] != "PASS" or int(counts[0][0]) < 56:
        raise SystemExit("FAIL ledger fixture missing/ambiguous/low check counter (minimum 56)")
