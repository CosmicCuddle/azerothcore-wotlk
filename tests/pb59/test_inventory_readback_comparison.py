"""Compile the actual comparator with extracted source declarations, no DB or engine.
The isolated adapter copies the real statement enum, binding variant, expected
write structures and recovery text limit. It is not a full native Core build.
"""
import argparse
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--compiler", nargs="+", required=True)
parser.add_argument("--ndebug", action="store_true")
args = parser.parse_args()
root = Path(__file__).resolve().parents[2]
player = root / "src/server/game/Entities/Player"
database = root / "src/server/database/Database"


def section(path, start, finish):
    text = path.read_text(encoding="utf-8")
    a = text.index(start)
    b = text.index(finish, a + len(start))
    return text[a:b + len(finish)]


with tempfile.TemporaryDirectory(prefix="pb59-value-compare-") as temp:
    work = Path(temp)
    aliases = "\n".join(f"using {sign}{width} = std::{sign}{width}_t;"
                        for sign in ("int", "uint") for width in (8, 16, 32, 64))
    preamble = "#pragma once\n#include <cstdint>\n#include <cstddef>\n" + aliases + "\n"
    enum = section(database / "Implementation/CharacterDatabase.h", "enum CharacterDatabaseStatements", "\n};")
    (work / "CharacterDatabase.h").write_text(preamble + enum + "\n", encoding="utf-8")
    binding = section(database / "PreparedStatement.h", "struct PreparedStatementData", "\n};")
    (work / "PreparedStatement.h").write_text(preamble + "#include <string>\n#include <variant>\n#include <vector>\n"
                                              + binding + "\n", encoding="utf-8")
    bound = re.findall(r"constexpr std::size_t MaximumTextBytes = [0-9]+;",
                       (player / "PB59RecoveryEvidence.h").read_text(encoding="utf-8"))
    if len(bound) != 1:
        raise RuntimeError("Recovery text bound not found exactly once")
    (work / "PB59RecoveryEvidence.h").write_text(
        "#pragma once\n#include <cstddef>\nnamespace PB59::Recovery { " + bound[0] + " }\n", encoding="utf-8")
    for name in ("PB59ExpectedInventoryWrites.h", "PB59InventoryReadbackComparison.h"):
        shutil.copyfile(player / name, work / name)
    flags = ["-std=c++20", "-Wall", "-Wextra", "-Werror", "-pedantic"]
    if args.ndebug:
        flags += ["-DNDEBUG", "-O2"]
    binary = work / "comparison.exe"
    subprocess.run(args.compiler + flags + ["-I", str(work),
        str(Path(__file__).with_name("inventory_readback_comparison_checks.cpp")), "-o", str(binary)],
        check=True, timeout=90)
    subprocess.run([str(binary)], check=True, timeout=30)
