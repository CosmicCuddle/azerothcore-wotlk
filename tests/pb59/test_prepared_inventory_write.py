"""Exercise the native prepared-task and queue handoff with a DB execution double."""
import argparse
from pathlib import Path
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--compiler", nargs="+", required=True)
parser.add_argument("--ndebug", action="store_true")
parser.add_argument("--mutants", action="store_true")
args = parser.parse_args()
root = Path(__file__).resolve().parents[2]
database = root / "src/server/database/Database"
player = root / "src/server/game/Entities/Player"
queue = root / "src/common/Threading"
source = Path(__file__).with_name("prepared_inventory_write_checks.cpp")


def section(filename, begin, end):
    text = (database / filename).read_text(encoding="utf-8")
    start = text.index(begin)
    return text[start:text.index(end, start + len(begin))]


pool = "DatabaseWorkerPool.cpp"
transaction = "Transaction.cpp"
definitions = [
    section(pool, "template <class T>\nSQLTransaction<T> DatabaseWorkerPool<T>::BeginTransaction(",
            "template <class T>\nvoid DatabaseWorkerPool<T>::CommitTransaction("),
    section(pool, "template <class T>\nstd::future<DatabaseTransactionResult> DatabaseWorkerPool<T>::"
            "AsyncCommitTransactionWithOutcome(", "template <class T>\nvoid DatabaseWorkerPool<T>::"
            "DirectCommitTransaction("),
    section(transaction, "void TransactionBase::Append(std::string_view sql)", "//- Append a prepared statement"),
    section(transaction, "TransactionWithOutcomeTask::~TransactionWithOutcomeTask()", "bool TransactionCallback::"),
]
flags = ["-std=c++20", "-Wall", "-Wextra", "-Werror", "-pedantic", "-pthread"]
if args.ndebug:
    flags += ["-DNDEBUG", "-O2"]

with tempfile.TemporaryDirectory(prefix="pb59-prepared-inventory-write-") as temporary:
    work = Path(temporary)
    binary = work / "prepared-write.exe"
    (work / "Define.h").write_text(
        "#pragma once\n#include <cstdint>\n#define AC_DATABASE_API\n"
        "using uint8 = std::uint8_t;\nusing uint32 = std::uint32_t;\n", encoding="utf-8")
    (work / "StringFormat.h").write_text(
        "#pragma once\n#include <string>\n#include <string_view>\n"
        "namespace Acore { template<class... Args> std::string StringFormat(std::string_view, Args&&...); }\n",
        encoding="utf-8")
    native = "\n".join(definitions)
    definition_file = work / "PreparedTaskDefinitions.inc"
    definition_file.write_text(native, encoding="utf-8")
    includes = [argument for path in (work, player, database, queue) for argument in ("-I", str(path))]
    compile_fixture = args.compiler + flags + includes + [str(source), "-o", str(binary)]
    subprocess.run(compile_fixture, check=True, timeout=90)
    subprocess.run([str(binary)], check=True, timeout=30)
    if args.mutants:
        header_name = "PB59PreparedInventoryWrite.h"
        bridge = (player / header_name).read_text(encoding="utf-8")
        mutants = [
            (definition_file, native, "auto task = std::move(prepared._task);",
             "auto task = std::make_unique<TransactionWithOutcomeTask>(BeginTransaction());"),
            (work / header_name, bridge, "(void)_writer.Complete(result);", ""),
            (work / header_name, bridge, " || !_writer.MarkPossiblySubmitted()", ""),
        ]
        for target, original, before, after in mutants:
            if original.count(before) != 1:
                raise RuntimeError(f"Expected one mutation site for {before}")
            target.write_text(original.replace(before, after), encoding="utf-8")
            subprocess.run(compile_fixture, check=True, timeout=90)
            result = subprocess.run([str(binary)], capture_output=True, text=True, check=False, timeout=30)
            if result.returncode != 1 or not result.stderr.startswith("prepared inventory write line "):
                raise RuntimeError(f"Expected a fixture assertion after changing {before}. Got {result!r}")
            target.write_text(original, encoding="utf-8")
        print(f"{len(mutants)}/{len(mutants)} prepared-task mutants rejected by runtime assertions")
