#!/usr/bin/env python3
"""Actual-header nonblocking DB promise-future adapter test. Not a server build."""
import argparse
import re
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PLAYER = ROOT / "src/server/game/Entities/Player"
DB = ROOT / "src/server/database/Database"
FIXTURE = ROOT / "tests/pb59/pb59_two_realm_commit_future_bridge.cpp"
FLOOR = 886


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cxx", default="g++")
    parser.add_argument("--sanitize", action="store_true")
    args = parser.parse_args()
    required = [FIXTURE, PLAYER / "PB59TwoRealmCommitFutureBridge.h",
                PLAYER / "PB59TwoRealmCommitEvidence.h", DB / "DatabaseTransactionResult.h"]
    if not all(p.is_file() for p in required):
        print("BLOCKED: exact source checkout missing", file=sys.stderr)
        return 2
    with tempfile.TemporaryDirectory(prefix="pb59-futures-") as work:
        exe = Path(work) / "bridge"
        command = [args.cxx, "-std=c++20", "-pthread", "-Wall", "-Wextra", "-Werror",
                   "-I", str(PLAYER), "-I", str(DB), str(FIXTURE), "-o", str(exe)]
        if args.sanitize:
            command[1:1] = ["-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-g"]
        try:
            build = subprocess.run(command, text=True, capture_output=True, timeout=120)
            if build.returncode:
                print(f"FAIL build {build.returncode}: {build.stdout}{build.stderr}", file=sys.stderr)
                return 1
            run = subprocess.run([str(exe)], text=True, capture_output=True, timeout=120)
        except (OSError, subprocess.TimeoutExpired) as exc:
            print(f"FAIL execution: {exc}", file=sys.stderr)
            return 1
        counts = re.findall(r"(?m)^checks=(\d+) status=PASS$", run.stdout)
        if run.returncode != 0 or len(counts) != 1 or int(counts[0]) < FLOOR:
            print(f"FAIL count/run: {run.stdout}{run.stderr}", file=sys.stderr)
            return 1
        print(f"PASS real Core typed DB future fixture checks={counts[0]} "
              f"compiler={args.cxx} sanitizers={args.sanitize}")
        print("NOT native Player lifetime, Worldserver DB callbacks, crash recovery or gameplay")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
