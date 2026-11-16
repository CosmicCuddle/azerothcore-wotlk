#!/usr/bin/env python3
"""Compile exact native boundary methods with mocked connection, not a real DB."""
import argparse
from pathlib import Path
import subprocess
import tempfile
import shutil

parser = argparse.ArgumentParser()
parser.add_argument("--cxx", default="g++")
parser.add_argument("--sanitize", action="store_true")
args = parser.parse_args()
root = Path(__file__).resolve().parent.parent
native = (root / "src/server/database/Database/MySQLConnection.cpp").read_text()
start = native.index("int MySQLConnection::ExecuteTransaction(")
end = native.index("std::size_t MySQLConnection::EscapeString(", start)
with tempfile.TemporaryDirectory() as directory:
    d = Path(directory)
    (d / "native.inc").write_text(native[start:end].rstrip() + "\n")
    shutil.copyfile(root / "tests/pb59_db_transaction_boundaries.cpp", d / "fixture.cpp")
    shutil.copyfile(root / "src/server/database/Database/DatabaseTransactionResult.h", d / "DatabaseTransactionResult.h")
    flags = ["-std=c++20", "-Wall", "-Wextra", "-Werror"]
    if args.sanitize:
        flags += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
    subprocess.run([args.cxx, *flags, str(d / "fixture.cpp"), "-o", str(d / "test")], check=True)
    subprocess.run([str(d / "test")], check=True)
