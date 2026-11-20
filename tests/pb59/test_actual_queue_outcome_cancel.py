#!/usr/bin/env python3
"""Build and run PB59 queue/typed-outcome cancellation fixture (NOT native DB)."""
import argparse
import re
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "tests/pb59/pb59_actual_queue_outcome_cancel.cpp"
QUEUE = ROOT / "src/common/Threading/PCQueue.h"
OUTCOME = ROOT / "src/server/database/Database/DatabaseTransactionResult.h"


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--cxx", default="g++")
    p.add_argument("--sanitize", action="store_true")
    args = p.parse_args()
    if not (SOURCE.is_file() and QUEUE.is_file() and OUTCOME.is_file()):
        print("BLOCKED: missing exact Core fixture/header", file=sys.stderr)
        return 2
    with tempfile.TemporaryDirectory(prefix="pb59-outcome-cancel-") as temp:
        exe = Path(temp) / "outcome-cancel"
        cmd = [args.cxx, "-std=c++20", "-pthread", "-Wall", "-Wextra", "-Werror",
               "-pedantic", "-I", str(QUEUE.parent), "-I", str(OUTCOME.parent),
               str(SOURCE), "-o", str(exe)]
        if args.sanitize:
            cmd[1:1] = ["-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-g"]
        try:
            build = subprocess.run(cmd, capture_output=True, text=True, timeout=90)
            if build.returncode:
                print(build.stdout + build.stderr, file=sys.stderr)
                return build.returncode
            result = subprocess.run([str(exe)], capture_output=True, text=True, timeout=20)
        except (OSError, subprocess.TimeoutExpired) as exc:
            print(f"FAIL: {exc}", file=sys.stderr)
            return 1
        print(result.stdout, end="")
        if result.returncode or not re.search(r"(?m)^checks=27 status=PASS$", result.stdout):
            print("FAIL: missing expected assertions", result.stderr, file=sys.stderr)
            return 1
    print(f"PASS 27 source-header/model checks cxx={args.cxx} sanitize={args.sanitize}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
