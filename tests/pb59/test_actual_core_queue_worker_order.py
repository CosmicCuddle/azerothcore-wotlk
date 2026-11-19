#!/usr/bin/env python3
"""Compile/run exact AzerothCore PCQueue.h against deterministic worker-order cases.

No database, Player, SQL connection, live DB or Worldserver is accessed.
"""
import argparse
import re
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
THREADING = ROOT / "src/common/Threading"
SOURCE = ROOT / "tests/pb59/pb59_actual_core_queue_worker_order.cpp"
HEADER = THREADING / "PCQueue.h"


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--cxx", default="g++")
    p.add_argument("--sanitize", action="store_true")
    args = p.parse_args()
    if not HEADER.is_file() or not SOURCE.is_file():
        print("BLOCKED exact Core PCQueue.h/source not found", file=sys.stderr)
        return 2
    with tempfile.TemporaryDirectory(prefix="pb59-queue-order-") as d:
        exe = Path(d) / "worker_order"
        cmd = [args.cxx, "-std=c++20", "-pthread", "-Wall", "-Wextra",
               "-Werror", "-pedantic", "-I", str(THREADING), str(SOURCE), "-o", str(exe)]
        if args.sanitize:
            cmd[1:1] = ["-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-g"]
        try:
            built = subprocess.run(cmd, text=True, capture_output=True, timeout=120)
            if built.returncode:
                print("FAIL build:", built.stdout + built.stderr, file=sys.stderr)
                return 1
            finished = subprocess.run([str(exe)], text=True, capture_output=True, timeout=30)
        except (OSError, subprocess.TimeoutExpired) as exc:
            print("FAIL run:", exc, file=sys.stderr)
            return 1
        observed = re.findall(r"(?m)^checks=(\d+) status=PASS$", finished.stdout)
        if finished.returncode != 0 or observed != ["17"]:
            print("FAIL checks:", finished.stdout + finished.stderr, file=sys.stderr)
            return 1
        print(f"PASS exact Core queue header checks=17 cxx={args.cxx} sanitize={args.sanitize}")
        print("SCOPE: FIFO dequeue vs completion with one/two simulated consumers; not installed DB/Worldserver")
        return 0


if __name__ == "__main__":
    raise SystemExit(main())
