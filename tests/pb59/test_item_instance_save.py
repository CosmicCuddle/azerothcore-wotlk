"""Run native item statement capture and save bodies with local Item and pool doubles."""
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
item = root / "src/server/game/Entities/Item"
source = Path(__file__).with_name("item_instance_save_checks.cpp")


def section(path, begin, end):
    text = path.read_text(encoding="utf-8")
    start = text.index(begin)
    return text[start:text.index(end, start + len(begin))]


definitions = "\n".join([
    section(database / "PreparedStatement.cpp", "PreparedStatementBase::PreparedStatementBase(", "//- Execution"),
    section(database / "Transaction.cpp", "void TransactionBase::AppendPreparedStatement(",
            "bool TransactionTask::Execute()"),
    section(item / "Item.cpp", "bool Item::AppendItemInstanceSave(", "bool Item::LoadFromDB("),
])
flags = ["-std=c++20", "-Wall", "-Wextra", "-Werror", "-pedantic"]
if args.ndebug:
    flags += ["-DNDEBUG", "-O2"]

with tempfile.TemporaryDirectory(prefix="pb59-item-instance-save-") as temporary:
    work = Path(temporary)
    binary = work / "item-save.exe"
    aliases = "\n".join(f"using {sign}{width} = std::{sign}{width}_t;"
                        for sign in ("int", "uint") for width in (8, 16, 32, 64))
    (work / "Define.h").write_text(
        f"#pragma once\n#include <cstdint>\n#define AC_DATABASE_API\n{aliases}\n", encoding="utf-8")
    (work / "StringFormat.h").write_text(
        "#pragma once\n#include <string>\n#include <string_view>\n"
        "namespace Acore { template<class... Args> std::string StringFormat(std::string_view, Args&&...); }\n",
        encoding="utf-8")
    extracted = work / "ItemInstanceSaveDefinitions.inc"
    extracted.write_text(definitions, encoding="utf-8")
    includes = [argument for path in (work, database, root / "src/common/Utilities") for argument in ("-I", str(path))]
    compile_fixture = args.compiler + flags + includes + [str(source), "-o", str(binary)]
    subprocess.run(compile_fixture, check=True, timeout=90)
    subprocess.run([str(binary)], check=True, timeout=30)
    if args.mutants:
        variants = [
            ("auto const owner = GetOwnerGUID().GetRawValue();", "auto const owner = GetOwnerGUID().GetCounter();"),
            ("if (state == ITEM_CHANGED && IsWrapped())", "if (IsWrapped())"),
            ("i < MAX_ENCHANTMENT_SLOT;", "i < MAX_ENCHANTMENT_SLOT - 1;"),
            ("instance->SetData(++index, GetItemRandomPropertyId());",
             "instance->SetData(++index, static_cast<uint32>(GetItemRandomPropertyId()));"),
            ("            (void)AppendItemInstanceSave(*trans);\n", ""),
        ]
        for before, after in variants:
            if definitions.count(before) != 1:
                raise RuntimeError(f"Expected one mutation site for {before}")
            extracted.write_text(definitions.replace(before, after), encoding="utf-8")
            subprocess.run(compile_fixture, check=True, timeout=90)
            result = subprocess.run([str(binary)], capture_output=True, text=True, check=False, timeout=30)
            if result.returncode != 1 or not result.stderr.startswith("item instance save line "):
                raise RuntimeError(f"Expected a fixture assertion after changing {before}. Got {result!r}")
        print(f"{len(variants)}/{len(variants)} item-instance variants rejected by runtime assertions")
