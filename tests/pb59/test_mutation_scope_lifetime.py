#!/usr/bin/env python3
"""Compile/run exact Core MutationScope lifetime fixture only.

The fixture uses a ledger object, not Player or the live server. Its passing
result is not a Worldserver/link, DB, item-lifetime or gameplay result.
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
source = Path(__file__).with_name("pb59_mutation_scope_lifetime.cpp")
flags = ["-std=c++20", "-Wall", "-Wextra", "-Werror", "-pedantic"]
if args.sanitize:
    flags += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
with tempfile.TemporaryDirectory(prefix="pb59-mut-scope-") as temp:
    target = Path(temp) / "test_mutation_scope_lifetime"
    subprocess.run([args.cxx, *flags, "-I", str(header_dir),
                    str(source), "-o", str(target)], check=True)
    subprocess.run([str(target)], check=True)
