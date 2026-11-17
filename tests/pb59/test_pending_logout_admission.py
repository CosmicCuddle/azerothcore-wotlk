#!/usr/bin/env python3
"""Compile and run the exact native PB59 pending-logout admission fixture.

No DB, map, session, Worldserver or gameplay operations are performed.
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
source = Path(__file__).with_name("pb59_pending_logout_admission.cpp")
header_dir = root / "src/server/game/Entities/Player"
flags = ["-std=c++20", "-Wall", "-Wextra", "-Werror", "-pedantic"]
if args.sanitize:
    flags += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
with tempfile.TemporaryDirectory(prefix="pb59-logout-admission-") as temp:
    target = Path(temp) / "test_pb59_pending_logout_admission"
    subprocess.run([args.cxx, *flags, "-I", str(header_dir),
                    str(source), "-o", str(target)], check=True)
    subprocess.run([str(target)], check=True)
