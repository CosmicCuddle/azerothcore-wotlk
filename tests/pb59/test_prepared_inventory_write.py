"""Exercise the native prepared-task and queue handoff with a DB execution double."""
import argparse
from pathlib import Path
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--compiler", nargs="+", required=True)
parser.add_argument("--ndebug", action="store_true")
parser.add_argument("--mutants", action="store_true")
parser.add_argument("--completion-mutant", action="store_true")
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
    header_name = "PB59PreparedInventoryWrite.h"
    store_name = "PB59PendingInventoryWrites.h"
    bridge = (player / header_name).read_text(encoding="utf-8")
    store = (player / store_name).read_text(encoding="utf-8")
    # Keep quoted includes and variant headers in the same include tree.
    (work / header_name).write_text(bridge, encoding="utf-8")
    (work / store_name).write_text(store, encoding="utf-8")
    includes = [argument for path in (work, player, database, queue) for argument in ("-I", str(path))]
    compile_fixture = args.compiler + flags + includes + [str(source), "-o", str(binary)]
    subprocess.run(compile_fixture, check=True, timeout=90)
    subprocess.run([str(binary)], check=True, timeout=30)
    if args.completion_mutant:
        guard = "if (!task || !result.valid())"
        if native.count(guard) != 1:
            raise RuntimeError("Expected one protected completion guard")
        definition_file.write_text(native.replace(guard, "if (!task)"), encoding="utf-8")
        subprocess.run(compile_fixture, check=True, timeout=90)
        result = subprocess.run([str(binary)], capture_output=True, text=True, check=False, timeout=30)
        if result.returncode != 1 or not result.stderr.startswith("prepared inventory write line "):
            raise RuntimeError(f"Expected ordinary dispatch to be caught. Got {result!r}")
        print("1/1 protected completion fault variant rejected by runtime assertion")
        definition_file.write_text(native, encoding="utf-8")
    if args.mutants:
        mutants = [
            (definition_file, native, "auto task = std::move(prepared._task);",
             "auto task = std::make_unique<TransactionWithOutcomeTask>(BeginTransaction());"),
            (work / header_name, bridge, "(void)_writer.Complete(result);", ""),
            (work / header_name, bridge, " || !_writer.MarkPossiblySubmitted()", ""),
            (work / store_name, store, "return !entry.ticket.sequence;", "return !entry.write;"),
            (work / store_name, store, "                reservation.Keep();\n", ""),
            (work / store_name, store, "        if (_closed)\n            return {Admission::Closed};\n", ""),
            (work / store_name, store, "auto& slot = _slots[_nextPoll];", "auto& slot = _slots[count];"),
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
        print(f"{len(mutants)}/{len(mutants)} prepared-task and store mutants rejected by runtime assertions")

        handoff = "(void)task.release(); // queue/worker owns it after successful admission"
        if native.count(handoff) != 1:
            raise RuntimeError("Expected one successful queue handoff")
        lost = native.replace(handoff, '{ (void)task.release(); throw std::runtime_error("lost dispatch result"); }')
        definition_file.write_text(lost, encoding="utf-8")
        subprocess.run(compile_fixture + ["-DPB59_TEST_LOST_DISPATCH_RESULT"], check=True, timeout=90)
        subprocess.run([str(binary)], check=True, timeout=30)
