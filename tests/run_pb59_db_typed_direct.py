#!/usr/bin/env python3
"""Exercise exact direct pool methods with a mock connection, not MySQL."""
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
db = root / "src/server/database/Database"
source = (db / "DatabaseWorkerPool.cpp").read_text()
start = source.index("template <class T>\nvoid DatabaseWorkerPool<T>::DirectCommitTransaction(")
end = source.index("template <class T>\nPreparedStatement<T>*", start)
with tempfile.TemporaryDirectory() as directory:
    d = Path(directory)
    (d / "native.inc").write_text(source[start:end].rstrip() + "\n")
    shutil.copyfile(db / "DatabaseTransactionResult.h", d / "DatabaseTransactionResult.h")
    shutil.copyfile(root / "tests/pb59_db_typed_direct.cpp", d / "fixture.cpp")
    flags = ["-std=c++20", "-Wall", "-Wextra", "-Werror"]
    if args.sanitize:
        flags += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
    subprocess.run([args.cxx, *flags, str(d / "fixture.cpp"), "-o", str(d / "test")], check=True)
    subprocess.run([str(d / "test")], check=True)
