"""Exercise row decoding with exact extracted value declarations, no DB or server."""
import argparse
from pathlib import Path
import shutil
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--compiler", nargs="+", required=True)
parser.add_argument("--ndebug", action="store_true")
args = parser.parse_args()
root = Path(__file__).resolve().parents[2]
player = root / "src/server/game/Entities/Player"
source = (player / "PB59InventoryReadbackComparison.h").read_text(encoding="utf-8")
evidence = (player / "PB59RecoveryEvidence.h").read_text(encoding="utf-8")

def extract(text, start, end):
    begin = text.index(start)
    return text[begin:text.index(end, begin)].rstrip() + "\n"

rows = extract(source, "struct InventoryReadbackLink", "enum class InventoryValueOutcome")
utf8 = extract(source, "inline bool Utf8(", "template<typename T, typename Rows")
limit = extract(evidence, "constexpr std::size_t MaximumTextBytes", "\n")
with tempfile.TemporaryDirectory(prefix="pb59-row-decode-") as temp:
    work = Path(temp)
    (work / "PB59InventoryReadbackComparison.h").write_text(
        "#pragma once\n#include <cstdint>\n#include <optional>\n#include <string>\n#include <vector>\n"
        "using uint8 = std::uint8_t;\nusing uint16 = std::uint16_t;\nusing uint32 = std::uint32_t;\n"
        "using int16 = std::int16_t;\nusing int32 = std::int32_t;\nnamespace PB59::Recovery\n{\n"
        + limit + rows + "namespace InventoryComparisonDetail\n{\n" + utf8 + "}\n}\n", encoding="utf-8")
    for name in ("PB59ReadbackWireValues.h", "PB59ReadbackRowDecoder.h"):
        shutil.copyfile(player / name, work / name)
    flags = ["-std=c++20", "-Wall", "-Wextra", "-Werror", "-pedantic"]
    if args.ndebug:
        flags += ["-DNDEBUG", "-O2"]
    binary = work / "row-decoder"
    subprocess.run(args.compiler + flags + ["-I", str(work),
        str(Path(__file__).with_name("readback_row_decoder_checks.cpp")), "-o", str(binary)],
        check=True, timeout=90)
    subprocess.run([str(binary)], check=True, timeout=30)
