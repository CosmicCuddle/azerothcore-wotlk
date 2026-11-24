"""Actual native executor/admission/journal/mod bodies and real PB59 headers.

Engine, physical transfer, map, aura, policy and DB are explicit doubles.
The actual raw transfer body has its separate existing source-coupled suite.
No Worldserver link, real plugins, DB durability or gameplay is implied.
"""
import argparse
from pathlib import Path
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--compiler", nargs="+", required=True)
parser.add_argument("--ndebug", action="store_true")
args = parser.parse_args()
root = Path(__file__).resolve().parents[2]
native = root / "src/server/game/Entities/Player"


def function(source, signature):
    start = source.index(signature)
    opening = source.index("{", start)
    depth, end = 1, opening + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end] + "\n"


storage = (native / "PlayerStorage.cpp").read_text(encoding="utf-8")
player = (native / "Player.cpp").read_text(encoding="utf-8")
executor = (native / "PlayerPB59Inventory.cpp").read_text(encoding="utf-8")
charm = (root / "src/server/game/Entities/Unit/CharmInfo.h").read_text(encoding="utf-8")
with tempfile.TemporaryDirectory(prefix="pb59-native-batch-") as temporary:
    work = Path(temporary)
    files = {
        "Admission.inc": function(storage, "bool Player::CheckPB59InventoryBatchPlan("),
        "InventoryStamp.inc": function(storage, "std::optional<PB59::Stamp> Player::CapturePB59InventoryStamp("),
        "CombatEffects.inc": function(storage, "PB59::OriginalCombatEffects::Result Player::ReconcilePB59OriginalCombatEffects("),
        "ItemMods.inc": function(player, "void Player::_ApplyItemMods("),
        "Executor.inc": function(executor, "PB59::InventoryBatchResult Player::ExecutePB59InventoryBatchPlan("),
        "GlobalCooldownMgr.inc": function(charm, "class GlobalCooldownMgr ") + ";\n",
        "Checks.inc": Path(__file__).with_name("inventory_batch_native_checks.cpp").read_text(encoding="utf-8"),
    }
    for name, body in files.items():
        (work / name).write_text(body, encoding="utf-8")
    binary = work / "native-batch.exe"
    flags = ["-DNDEBUG", "-O2"] if args.ndebug else []
    subprocess.run(args.compiler + flags + ["-std=c++20", "-Wall", "-Wextra", "-Werror", "-I", str(native),
                                    "-I", str(work), str(Path(__file__).with_name("inventory_batch_native_fixture.cpp")),
                                    "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
