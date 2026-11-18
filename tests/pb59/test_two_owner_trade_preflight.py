#!/usr/bin/env python3
"""Compile/execute the real PB59 two-ledger preflight fixture in this checkout.

Standalone Core-header test only. Does not test Player/TradeHandler lifetime pins,
real inventory custody, CharacterDatabase, crossrealm durability or gameplay.
"""
import argparse
import re
import subprocess
import sys
import tempfile
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cxx", default="g++")
    parser.add_argument("--sanitize", action="store_true")
    parser.add_argument("--extra-cxxflag", action="append", default=[], metavar="FLAG")
    args = parser.parse_args()

    root = Path(__file__).resolve().parents[2]
    header_dir = root / "src" / "server" / "game" / "Entities" / "Player"
    test = Path(__file__).with_name("pb59_two_owner_trade_preflight.cpp")
    for path in (header_dir / "PB59TransactionAdmission.h",
                 header_dir / "PB59TwoOwnerTradePreflight.h", test):
        if not path.is_file():
            print(f"FAIL missing exact source {path}", file=sys.stderr)
            return 1
    flags = ["-std=c++20", "-Wall", "-Wextra", "-Werror", "-pedantic",
             "-I", str(header_dir), *args.extra_cxxflag]
    if args.sanitize:
        flags += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-g"]
    try:
        with tempfile.TemporaryDirectory(prefix="pb59-two-owner-preflight-") as work:
            exe = Path(work) / "preflight"
            cmd = [args.cxx, *flags, str(test), "-o", str(exe)]
            build = subprocess.run(cmd, capture_output=True, text=True, timeout=120, check=False)
            if build.returncode:
                print(f"FAIL compilation rc={build.returncode}\n{build.stdout}{build.stderr}", file=sys.stderr)
                return 1
            run = subprocess.run([str(exe)], capture_output=True, text=True, timeout=120, check=False)
            match = re.findall(r"(?m)^checks=(\d+) status=(PASS|FAIL)\s*$", run.stdout)
            if run.returncode or len(match) != 1 or int(match[0][0]) != 36 or match[0][1] != "PASS":
                print(f"FAIL runtime rc={run.returncode} output={run.stdout!r}\n{run.stderr}", file=sys.stderr)
                return 1
            print(f"PB59_TWO_OWNER_PREFLIGHT=PASS cxx={args.cxx} sanitized={args.sanitize} checks=36")
            return 0
    except (OSError, subprocess.TimeoutExpired) as exc:
        print(f"FAIL tool execution: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
