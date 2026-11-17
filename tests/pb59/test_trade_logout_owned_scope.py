#!/usr/bin/env python3
"""Compile and run the source-included PB59 two-party logout admission fixture.

This is a standalone ledger/producer-interleaving MODEL test. It does not
compile Worldserver, edit live state, execute DB operations, or prove native
trade/logout wiring, rollback or two-character atomicity.
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
source = Path(__file__).with_name("pb59_trade_logout_owned_scope.cpp")
headers = root / "src/server/game/Entities/Player"
flags = ["-std=c++20", "-Wall", "-Wextra", "-Werror", "-pedantic"]
if args.sanitize:
    flags += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
with tempfile.TemporaryDirectory(prefix="pb59-owned-trade-") as temp:
    executable = Path(temp) / "test_pb59_trade_logout_owned_scope"
    subprocess.run([args.cxx, *flags, "-I", str(headers),
                    str(source), "-o", str(executable)], check=True)
    subprocess.run([str(executable)], check=True)
