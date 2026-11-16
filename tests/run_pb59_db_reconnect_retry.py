#!/usr/bin/env python3
"""Compile exact reconnect handler with mocked handles, time and fail-stop."""
import argparse
from pathlib import Path
import shutil
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument("--cxx", default="g++")
parser.add_argument("--sanitize", action="store_true")
args = parser.parse_args()
root = Path(__file__).resolve().parent.parent
source = (root / "src/server/database/Database/MySQLConnection.cpp").read_text()
start = source.index("bool MySQLConnection::_HandleMySQLErrno(")
with tempfile.TemporaryDirectory() as directory:
    d = Path(directory)
    (d / "native.inc").write_text(source[start:].rstrip() + "\n")
    shutil.copyfile(root / "tests/pb59_db_reconnect_retry.cpp", d / "fixture.cpp")
    flags = ["-std=c++20", "-Wall", "-Wextra", "-Werror"]
    if args.sanitize:
        flags += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
    subprocess.run([args.cxx, *flags, str(d / "fixture.cpp"), "-o", str(d / "test")], check=True)
    subprocess.run([str(d / "test")], check=True)
