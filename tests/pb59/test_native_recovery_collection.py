"""Compile actual dormant collector/stamp/admission bodies with real PB59 headers.

Reuses the existing explicit engine doubles; does not repeat executor/PB suites.
--syntax-only never launches a generated executable. Runtime results are separate.
"""
import argparse
from pathlib import Path
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--compiler", nargs="+", required=True)
parser.add_argument("--syntax-only", action="store_true")
parser.add_argument("--ndebug", action="store_true")
parser.add_argument("--sanitizers", action="store_true")
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
header = (native / "Player.h").read_text(encoding="utf-8")
collector = (native / "PlayerPB59Recovery.cpp").read_text(encoding="utf-8")
declarations = header[header.index("    static std::unique_ptr<PB59::Recovery::NativeCollection>"):]
declarations = declarations[:declarations.index("    // Private queue-only handoff.")]
fixture = Path(__file__).with_name("inventory_batch_native_fixture.cpp").read_text(encoding="utf-8")
extras = declarations + r'''
    std::uint64_t GetPB59OwnerIncarnation() const noexcept { NoteCollectionRead(); return _pb59Transaction.OwnerIncarnation(); }
    mutable unsigned collectionReads = 0, readsAfterCollectionStop = 0;
    mutable bool collectionStopped = false;
    mutable std::function<void()> collectionHook;
    void NoteCollectionRead() const { ++collectionReads; if (collectionStopped) ++readsAfterCollectionStop; }
    void ArmCollection() {
        std::array<void const*, 1> pointers{GetItemByPos(65280)};
        if (!_pb59Equipment.PrepareInventoryReservation(GetGUID().GetRawValue(), pointers)) std::abort();
        _pb59Equipment.SetInventoryQuarantined(true); _pb59Equipment.Invalidate();
    }
    void AdvanceCollectionRevision() { _pb59Equipment.Invalidate(); }
'''
fixture = '#include "PB59NativeRecoveryCollection.h"\n' + fixture
fixture = fixture.replace("class Player {\npublic:", "using WorldSession = Session;\nclass Player {\npublic:\n" + extras)
split = fixture.index("using WorldSession = Session;")
prefix, actor_fixture = fixture[:split], fixture[split:]
for before in ("Guid GetGUID() const {", "Session* GetSession() const {", "Map* GetMap() const {",
               "bool IsInWorld() const {", "Item* GetItemByPos(uint16 pos) const {"):
    assert actor_fixture.count(before) == 1, before
    actor_fixture = actor_fixture.replace(before, before + " NoteCollectionRead();")
fixture = prefix + actor_fixture
before = "        return PB59::InventorySlotFacts{1, bag, pos, item ? std::optional{item->facts} : std::nullopt};"
assert fixture.count(before) == 1
fixture = fixture.replace(before, "        auto value = PB59::InventorySlotFacts{1, bag, pos, item ? std::optional{item->facts} : std::nullopt};\n"
                          "        if (collectionHook) collectionHook();\n        return value;")
fixture = fixture.replace('#include "Checks.inc"', '#include "Collector.inc"\n#include "NativeRecoveryChecks.inc"')
charm = (root / "src/server/game/Entities/Unit/CharmInfo.h").read_text(encoding="utf-8")
with tempfile.TemporaryDirectory(prefix="pb59-native-collector-") as temporary:
    work = Path(temporary)
    files = {
        "Admission.inc": function(storage, "bool Player::CheckPB59InventoryBatchPlan("),
        "InventoryStamp.inc": function(storage, "std::optional<PB59::Stamp> Player::CapturePB59InventoryStamp("),
        "CombatEffects.inc": function(storage, "PB59::OriginalCombatEffects::Result Player::ReconcilePB59OriginalCombatEffects("),
        "ItemMods.inc": function(player, "void Player::_ApplyItemMods("),
        "Executor.inc": "",  # this suite does not re-run the movement executor
        "GlobalCooldownMgr.inc": function(charm, "class GlobalCooldownMgr ") + ";\n",
        "Collector.inc": collector[collector.index("std::unique_ptr<PB59::Recovery::NativeCollection>"):],
        "NativeRecoveryChecks.inc": Path(__file__).with_name("native_recovery_collection_checks.cpp").read_text(encoding="utf-8"),
        "fixture.cpp": fixture,
    }
    for name, body in files.items():
        (work / name).write_text(body, encoding="utf-8")
    flags = ["-std=c++20", "-Wall", "-Wextra", "-Werror", "-pthread"]
    if args.ndebug:
        flags += ["-DNDEBUG", "-O2"]
    if args.sanitizers:
        flags += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
    binary = work / "collector.exe"
    output = ["-fsyntax-only"] if args.syntax_only else ["-o", str(binary)]
    subprocess.run(args.compiler + flags + ["-I", str(native), "-I", str(work), str(work / "fixture.cpp")] + output,
                   check=True)
    if args.syntax_only:
        print("Actual collector/stamp/admission bodies + native headers syntax PASS (engine doubles; not executed)")
    else:
        subprocess.run([str(binary)], check=True)
