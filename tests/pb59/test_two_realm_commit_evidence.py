#!/usr/bin/env python3
"""Compile real Core DB-result/PB59 two-realm evidence headers (NOT live trading).

Fail closed on compiler/test errors or missing real source. Run from Core checkout:
  python3 tests/pb59/test_two_realm_commit_evidence.py --cxx g++
  python3 tests/pb59/test_two_realm_commit_evidence.py --cxx clang++ --sanitize
"""

import argparse
import re
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
FIXTURE = ROOT / "tests" / "pb59" / "pb59_two_realm_commit_evidence.cpp"
PLAYER_HEADERS = ROOT / "src" / "server" / "game" / "Entities" / "Player"
DATABASE_HEADERS = ROOT / "src" / "server" / "database" / "Database"
EXPECTED_CHECKS = 1151


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cxx", default="g++", help="C++20 compiler")
    parser.add_argument("--sanitize", action="store_true", help="ASan and UBSan")
    args = parser.parse_args()

    for path in (FIXTURE, PLAYER_HEADERS / "PB59TwoRealmCommitEvidence.h",
                 DATABASE_HEADERS / "DatabaseTransactionResult.h"):
        if not path.is_file():
            print(f"BLOCKED missing required checkout file: {path}", file=sys.stderr)
            return 2

    with tempfile.TemporaryDirectory(prefix="pb59-two-realm-") as work:
        executable = Path(work) / "pb59_two_realm_commit_evidence"
        command = [args.cxx, "-std=c++20", "-Wall", "-Wextra", "-Werror",
                   "-I", str(PLAYER_HEADERS), "-I", str(DATABASE_HEADERS),
                   str(FIXTURE), "-o", str(executable)]
        if args.sanitize:
            command[1:1] = ["-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-g"]
        try:
            build = subprocess.run(command, capture_output=True, text=True, timeout=120, check=False)
            if build.returncode:
                print(f"FAIL compile rc={build.returncode}\n{build.stdout}{build.stderr}", file=sys.stderr)
                return 1
            result = subprocess.run([str(executable)], capture_output=True,
                                    text=True, timeout=120, check=False)
        except (OSError, subprocess.TimeoutExpired) as exc:
            print(f"FAIL compiler or fixture execution: {exc}", file=sys.stderr)
            return 1
        if result.returncode:
            print(f"FAIL fixture rc={result.returncode}\n{result.stdout}{result.stderr}", file=sys.stderr)
            return 1
        match = re.findall(r"(?m)^checks=(\d+) status=PASS$", result.stdout)
        if len(match) != 1 or int(match[0]) < EXPECTED_CHECKS:
            print(f"FAIL counted checks: expected >= {EXPECTED_CHECKS}; got {result.stdout!r}", file=sys.stderr)
            return 1
        print(f"PASS two-realm typed-DB-outcome fixture checks={match[0]} "
              f"compiler={args.cxx} sanitized={args.sanitize}")
        print("NOTE pure source-level helper: no native Player lifetime, DB transaction, journal, or gameplay proof")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
