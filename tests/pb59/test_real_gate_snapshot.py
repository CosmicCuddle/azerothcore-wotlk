"""Exercise the bound snapshot with full recovery headers and simulated client calls.
Requires installed MySQL/MariaDB client headers; no database connection.
"""
import argparse
from pathlib import Path
import shutil
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--compiler", nargs="+", required=True)
parser.add_argument("--ndebug", action="store_true")
parser.add_argument("--client-include", default="/usr/include/mysql")
args = parser.parse_args()
root = Path(__file__).resolve().parents[2]
player = root / "src/server/game/Entities/Player"
database = root / "src/server/database/Database"


def section(path, start, finish):
    text = path.read_text(encoding="utf-8")
    a = text.index(start)
    b = text.index(finish, a + len(start))
    return text[a:b + len(finish)]


with tempfile.TemporaryDirectory(prefix="pb59-readback-request-") as temp:
    work = Path(temp)
    aliases = "\n".join(f"using {sign}{width} = std::{sign}{width}_t;"
                        for sign in ("int", "uint") for width in (8, 16, 32, 64))
    preamble = "#pragma once\n#include <cstdint>\n#include <cstddef>\n" + aliases + "\n"
    enum = section(database / "Implementation/CharacterDatabase.h", "enum CharacterDatabaseStatements", "\n};")
    (work / "CharacterDatabase.h").write_text(preamble + enum + "\n", encoding="utf-8")
    binding = section(database / "PreparedStatement.h", "struct PreparedStatementData", "\n};")
    (work / "PreparedStatement.h").write_text(preamble + "#include <string>\n#include <variant>\n#include <vector>\n"
                                              + binding + "\n", encoding="utf-8")
    for name in ("PB59ExpectedInventoryWrites.h", "PB59InventoryReadbackComparison.h",
                 "PB59InventoryReadbackRequest.h", "PB59RetainedInventoryReadback.h", "PB59RecoveryEvidence.h",
                 "PB59InventoryBatchPlan.h", "PB59InventorySlotFacts.h",
                 "PB59CharacterSaveEvidence.h", "PB59BoundSnapshotReadback.h",
                 "PB59SnapshotReadback.h", "PB59ExecuteDecodedReadback.h", "PB59ExecuteReadbackQueries.h",
                 "PB59BufferedReadbackFetch.h", "PB59CompleteReadbackResult.h", "PB59ReadbackQueryPlan.h",
                 "PB59ReadbackRowDecoder.h", "PB59ReadbackWireValues.h", "PB59DecodeCompleteReadback.h"):
        shutil.copyfile(player / name, work / name)
    shutil.copyfile(database / "DatabaseTransactionResult.h", work / "DatabaseTransactionResult.h")
    flags = ["-std=c++20", "-Wall", "-Wextra", "-Werror", "-pedantic"]
    if args.ndebug:
        flags += ["-DNDEBUG", "-O2"]
    for test in ("real_gate_snapshot_checks.cpp",):
        binary = work / (Path(test).stem + ".exe")
        subprocess.run(args.compiler + flags + ["-I", str(work), "-I", args.client_include,
            str(Path(__file__).with_name(test)), "-o", str(binary)],
            check=True, timeout=90)
        subprocess.run([str(binary)], check=True, timeout=30)
