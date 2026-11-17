#!/usr/bin/env python3
"""Compile and run the exact native PB59 economic admission regression.

This only tests the ledger's source-level admission policy. It does not
compile TradeHandler/WorldSession or exercise a live character or database.
"""
import argparse
from pathlib import Path
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument("--cxx", default="g++")
parser.add_argument("--sanitize", action="store_true")
args = parser.parse_args()

root = Path(__file__).resolve().parents[2]
header_dir = root / "src/server/game/Entities/Player"
source = Path(__file__).with_name("pb59_economic_scope_admission.cpp")
flags = ["-std=c++20", "-Wall", "-Wextra", "-Werror", "-pedantic"]
if args.sanitize:
    flags += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
with tempfile.TemporaryDirectory(prefix="pb59-economic-scope-") as temp:
    target = Path(temp) / "test_economic_scope_admission"
    subprocess.run([args.cxx, *flags, "-I", str(header_dir),
                    str(source), "-o", str(target)], check=True)
    subprocess.run([str(target)], check=True)
