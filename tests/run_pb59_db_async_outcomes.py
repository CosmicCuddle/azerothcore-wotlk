#!/usr/bin/env python3
"""Exact committed queue + transaction methods with a mocked DB connection."""
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

def excerpt(path, start, end):
    source = path.read_text()
    first = source.index(start)
    last = source.index(end, first)
    return source[first:last].rstrip() + "\n"

with tempfile.TemporaryDirectory() as directory:
    d = Path(directory)
    shutil.copyfile(db / "DatabaseTransactionResult.h", d / "DatabaseTransactionResult.h")
    shutil.copyfile(root / "src/common/Threading/PCQueue.h", d / "PCQueue.h")
    shutil.copyfile(root / "tests/pb59_db_async_outcomes.cpp", d / "fixture.cpp")
    (d / "task.inc").write_text(excerpt(db / "Transaction.h",
        "class AC_DATABASE_API TransactionWithOutcomeTask",
        "class AC_DATABASE_API TransactionCallback\n"))
    (d / "native.inc").write_text(excerpt(db / "Transaction.cpp",
        "bool TransactionTask::Execute()", "bool TransactionCallback::InvokeIfReady()"))
    (d / "pool.inc").write_text(excerpt(db / "DatabaseWorkerPool.cpp",
        "template <class T>\nstd::future<DatabaseTransactionResult>",
        "template <class T>\nvoid DatabaseWorkerPool<T>::DirectCommitTransaction"))
    flags = ["-std=c++20", "-Wall", "-Wextra", "-Werror", "-pthread"]
    if args.sanitize:
        flags += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
    subprocess.run([args.cxx, *flags, str(d / "fixture.cpp"), "-o", str(d / "test")], check=True)
    subprocess.run([str(d / "test")], check=True, timeout=30)
