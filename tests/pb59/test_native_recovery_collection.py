"""Compile actual dormant collector/stamp/admission bodies with real PB59 headers.

Reuses existing explicit engine doubles. --bridge adds the explicit test-only
executor handoff and actual-body control; no Playerbots suite is repeated.
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
parser.add_argument("--bridge", action="store_true", help="Exercise a test-only executor-to-collector handoff")
parser.add_argument("--mutants", action="store_true",
                    help="Require queue-identity guard mutants to fail runtime checks")
args = parser.parse_args()
if args.mutants and args.syntax_only:
    parser.error("--mutants requires executable checks")
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
item_header = (native.parent / "Item/Item.h").read_text(encoding="utf-8")
mask_header = (native.parent / "Object/Updates/UpdateMask.h").read_text(encoding="utf-8")
declarations = header[header.index("    static std::unique_ptr<PB59::Recovery::NativeCollection>"):]
declarations = declarations[:declarations.index("    // Private queue-only handoff.")]
fixture = Path(__file__).with_name("inventory_batch_native_fixture.cpp").read_text(encoding="utf-8")
extras = declarations + r'''
    UpdateMask _changesMask;
    std::uint16_t m_valuesCount = PLAYER_END;
    std::uint64_t GetPB59OwnerIncarnation() const noexcept { NoteCollectionRead(); return _pb59Transaction.OwnerIncarnation(); }
    mutable unsigned collectionReads = 0, readsAfterCollectionStop = 0;
    mutable bool collectionStopped = false;
    mutable std::function<void()> collectionHook;
    void NoteCollectionRead() const { ++collectionReads; if (collectionStopped) ++readsAfterCollectionStop; }
    void ArmCollection() {
        auto* original = GetItemByPos(65280);
        if (!original && !ownership.empty()) original = ownership.front().get();
        std::array<void const*, 1> pointers{original};
        if (!_pb59Equipment.PrepareInventoryReservation(GetGUID().GetRawValue(), pointers)) std::abort();
        _pb59Equipment.SetInventoryQuarantined(true); _pb59Equipment.Invalidate();
    }
    void AdvanceCollectionRevision() { _pb59Equipment.Invalidate(); }
'''
fixture = '#include "PB59NativeRecoveryCollection.h"\n#include <cstring>\n' + fixture
# Fixed underlying type keeps deliberately unknown test states defined. Native
# ItemUpdateState itself is unchanged; no native corruption reachability claim.
fixture = fixture.replace("enum ItemUpdateState {", "enum ItemUpdateState : std::int32_t {")
fixture = fixture.replace("struct ItemTemplate {", '#include "UpdateMask.inc"\nstruct ItemTemplate {')
fixture = fixture.replace("class Player {\npublic:", "using WorldSession = Session;\nclass Player {\npublic:\n" + extras)
split = fixture.index("using WorldSession = Session;")
prefix, actor_fixture = fixture[:split], fixture[split:]
for before in ("Guid GetGUID() const {", "Session* GetSession() const {", "Map* GetMap() const {",
               "bool IsInWorld() const {", "Item* GetItemByPos(uint16 pos) const {"):
    assert actor_fixture.count(before) == 1, before
    actor_fixture = actor_fixture.replace(before, before + " NoteCollectionRead();")
fixture = prefix + actor_fixture
fixture = fixture.replace("class Item {\npublic:", "class Item {\npublic:\n" + r'''
    UpdateMask _changesMask;
    std::uint16_t m_valuesCount = 64;
    std::array<std::uint32_t, PB59::Recovery::MaximumItemUpdateFields> rawFields{};
    mutable unsigned collectionFieldReads = 0;
    std::uint32_t GetUInt32Value(std::uint16_t index) const { ++collectionFieldReads; return rawFields.at(index); }
    mutable unsigned collectionQueueReads = 0;
    mutable std::function<void()> collectionQueueHook;
    void NoteQueueRead() const { ++collectionQueueReads; if (collectionQueueHook) collectionQueueHook(); }
''' + function(item_header, "    bool PB59CopyUpdateMask(") + function(item_header, "    bool PB59CopyFieldValues("))
fixture = fixture.replace("facts.guid = guid;", "_changesMask.SetCount(m_valuesCount); facts.guid = guid;")
fixture = fixture.replace("    Player() {", "    Player() { _changesMask.SetCount(m_valuesCount);")
fixture = fixture.replace("struct Map {", "struct Map { mutable unsigned collectionMapReads = 0; "
                          "mutable std::function<void()> collectionMapHook;")
fixture = fixture.replace("bool PB59HasUpdateObject(void const* pointer) const {",
                          "bool PB59HasUpdateObject(void const* pointer) const { "
                          "++collectionMapReads; if (collectionMapHook) collectionMapHook();")
for before in ("Guid GetGUID() const { return {facts.guid}; }",
               "Guid GetOwnerGUID() const { return {facts.owner}; }"):
    assert fixture.count(before) == 1, before
    fixture = fixture.replace(before, before.replace("{ return", "{ NoteQueueRead(); return"))
before = "        return PB59::InventorySlotFacts{1, bag, pos, item ? std::optional{item->facts} : std::nullopt};"
assert fixture.count(before) == 1
fixture = fixture.replace(before, "        auto value = PB59::InventorySlotFacts{1, bag, pos, item ? std::optional{item->facts} : std::nullopt};\n"
                          "        if (collectionHook) collectionHook();\n        return value;")
fixture = fixture.replace('#include "Checks.inc"', '#include "Collector.inc"\n#include "NativeRecoveryChecks.inc"')
executor_body = ""
if args.bridge:
    declaration = "    PB59::InventoryBatchResult ExecutePB59InventoryBatchPlanControl(uint64, uint64, uint64, "
    declaration += "PB59::Stamp const&, std::span<PB59::InventoryBatchMove const>);\n"
    fixture = fixture.replace("class Player {\npublic:\n", "class Player {\npublic:\n" + declaration)
    fixture = fixture.replace('#include "Executor.inc"',
                              '#include "executor_recovery_bridge_hooks.h"\n#include "Executor.inc"')
    executor_body = function((native / "PlayerPB59Inventory.cpp").read_text(encoding="utf-8"),
                             "PB59::InventoryBatchResult Player::ExecutePB59InventoryBatchPlan(")
    original_executor = executor_body
    replacements = [
        ("    if (!_pb59Equipment.PrepareInventoryReservation(owner, protectedItems, protectedCounters))",
         "    auto bridgeCollection = PreparePB59RecoveryCollection(*this, request);\n"
         "    Bridge.prepared = bool(bridgeCollection);\n"
         "    if (!bridgeCollection) return result;\n"
         "    if (!_pb59Equipment.PrepareInventoryReservation(owner, protectedItems, protectedCounters))"),
        ("        return result;\n    };\n    auto cooldownsSame = [&]()",
         "        ++Bridge.recoveryCalls;\n"
         "        if (Bridge.beforeCapture) Bridge.beforeCapture(scope);\n"
         "        Bridge.scopeLiveBefore = live();\n"
         "        if (context(true))\n"
         "        {\n"
         "            try { Bridge.effectsObserved = captureEffects(after);\n"
         "                Bridge.effectsSame = Bridge.effectsObserved && before == after; }\n"
         "            catch (...) { Bridge.effectsObserved = false; }\n"
         "        }\n"
         "        Bridge.collectStatus = CollectPB59RecoveryEvidence(*bridgeCollection, scope, result);\n"
         "        Bridge.values = bridgeCollection->CopyValues();\n"
         "        Bridge.scopeLiveAfter = live();\n"
         "        return result;\n    };\n"
         "    if (Bridge.beforeBind) Bridge.beforeBind();\n"
         "    Bridge.nativeTicket = ticket;\n"
         "    Bridge.bindStatus = BindPB59RecoveryCollection(*bridgeCollection, scope);\n"
         "    if (Bridge.bindStatus != PB59::Recovery::CollectionStatus::Bound) return recoveryResult();\n"
         "    auto cooldownsSame = [&]()"),
    ]
    for before, after in replacements:
        if executor_body.count(before) != 1:
            raise RuntimeError("Executor instrumentation boundary changed")
        executor_body = executor_body.replace(before, after)
    restored_executor = executor_body
    for before, after in reversed(replacements):
        restored_executor = restored_executor.replace(after, before)
    if restored_executor != original_executor:
        raise RuntimeError("Instrumented executor cannot reproduce its exact original body")
    control = original_executor.replace("Player::ExecutePB59InventoryBatchPlan(",
                                        "Player::ExecutePB59InventoryBatchPlanControl(", 1)
    if control.replace("Player::ExecutePB59InventoryBatchPlanControl(",
                       "Player::ExecutePB59InventoryBatchPlan(", 1) != original_executor:
        raise RuntimeError("Control executor differs beyond its qualified function name")
    executor_body = control + "\n" + executor_body
charm = (root / "src/server/game/Entities/Unit/CharmInfo.h").read_text(encoding="utf-8")
with tempfile.TemporaryDirectory(prefix="pb59-native-collector-") as temporary:
    work = Path(temporary)
    files = {
        "Admission.inc": function(storage, "bool Player::CheckPB59InventoryBatchPlan("),
        "InventoryStamp.inc": function(storage, "std::optional<PB59::Stamp> Player::CapturePB59InventoryStamp("),
        "CombatEffects.inc": function(storage, "PB59::OriginalCombatEffects::Result Player::ReconcilePB59OriginalCombatEffects("),
        "ItemMods.inc": function(player, "void Player::_ApplyItemMods("),
        "Executor.inc": executor_body,  # empty except the explicit test-only --bridge protocol
        "UpdateMask.inc": "constexpr int CONTAINER_END = 138, INVENTORY_SLOT_BAG_START = 19;\n"
                          "struct ByteBuffer { template<typename T> ByteBuffer& operator<<(T) { return *this; } };\n"
                          "#define ASSERT(value) do { if (!(value)) std::abort(); } while (false)\n" +
                          function(mask_header, "class UpdateMask\n") + ";\n",
        "GlobalCooldownMgr.inc": function(charm, "class GlobalCooldownMgr ") + ";\n",
        "Collector.inc": collector[collector.index("std::unique_ptr<PB59::Recovery::NativeCollection>"):],
        "NativeRecoveryChecks.inc": Path(__file__).with_name(
            "executor_recovery_bridge_checks.cpp" if args.bridge else "native_recovery_collection_checks.cpp"
        ).read_text(encoding="utf-8"),
        "fixture.cpp": fixture,
    }
    if args.bridge:
        files["executor_recovery_bridge_hooks.h"] = Path(__file__).with_name(
            "executor_recovery_bridge_hooks.h").read_text(encoding="utf-8")
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
        mode = "Test-instrumented executor/control + collector" if args.bridge else "Collector/stamp/admission bodies"
        print(mode + " + native headers syntax PASS (engine doubles; not executed)")
    else:
        subprocess.run([str(binary)], check=True)
    if args.mutants and args.bridge:
        original = files["Executor.inc"][len(control) + 1:]
        capture = "Bridge.collectStatus = CollectPB59RecoveryEvidence(*bridgeCollection, scope, result);"
        mutants = {
            "binding omitted": original.replace(
                "Bridge.bindStatus = BindPB59RecoveryCollection(*bridgeCollection, scope);",
                "Bridge.bindStatus = PB59::Recovery::CollectionStatus::Prepared;"),
            "capture omitted": original.replace(capture,
                "Bridge.collectStatus = PB59::Recovery::CollectionStatus::Prepared;"),
            "capture before failure report": original.replace(
                "        result.outcome = Outcome::RecoveryRequired;", "        " + capture +
                "\n        result.outcome = Outcome::RecoveryRequired;"),
            "capture after scope released": original.replace(capture,
                "(void)scope.Finish({true, true, true, true}); " + capture),
            "effects read without context": original.replace("        if (context(true))\n",
                "        if (true)\n"),
            "equal effects promoted to proof": original.replace(
                "Bridge.values = bridgeCollection->CopyValues();",
                "Bridge.values = bridgeCollection->CopyValues(); "
                "if (Bridge.effectsSame) Bridge.values->evidence.effects = PB59::Recovery::ProofState::Verified;"),
        }
        for name, body in mutants.items():
            if body == original:
                raise RuntimeError("Bridge mutation did not change source: " + name)
            (work / "Executor.inc").write_text(control + "\n" + body, encoding="utf-8")
            subprocess.run(args.compiler + flags + ["-I", str(native), "-I", str(work), str(work / "fixture.cpp"),
                                                   "-o", str(binary)], check=True)
            result = subprocess.run([str(binary)], capture_output=True, text=True)
            if result.returncode != 1 or "bridge line " not in result.stderr:
                raise RuntimeError(f"Bridge mutant not rejected by a fixture assertion: {name}: {result.returncode}")
        print(f"{len(mutants)}/{len(mutants)} executor/collector handoff mutants rejected by runtime checks")
    elif args.mutants:
        original = files["Collector.inc"]
        bind_start = original.index("        // The prepared image must still be exact at admission.")
        bind_end = original.index("        auto const boundStamp = *stamp;", bind_start)
        mutants = {
            "replacement pointer ignored": original.replace(
                "before.item != item || before.value != now", "before.value != now"),
            "queue values ignored": original.replace(
                "before.item != item || before.value != now", "before.item != item"),
            "trailing tombstones ignored": original.replace(
                "index >= output.summary.preparedQueueEntries || index >= output.summary.observedQueueEntries ||",
                "false ||"),
            "bind image not validated": original[:bind_start] + original[bind_end:],
            "identity promoted to proof": original.replace(
                "!output.summary.changedWatchedItems ? ProofState::Unknown",
                "!output.summary.changedWatchedItems ? ProofState::Verified"),
            "item registration inconsistency ignored": original.replace(
                "now.updates.objectPending != now.updates.mapRegistered", "false"),
            "owner changes ignored": original.replace(
                "ownerUpdate == output.ownerUpdateBefore", "true"),
            "owner bind image ignored": original.replace(
                "ownerUpdate != output.ownerUpdateBefore", "false"),
            "watched field values ignored at bind": original.replace(
                "now != observation.before", "now.state != observation.before.state"),
            "watched field differences ignored": original.replace(
                "observation.now == observation.before", "observation.now.state == observation.before.state"),
            "watched original pointer ignored": original.replace(
                "item && item == watched.item", "item != nullptr"),
        }
        for name, body in mutants.items():
            if body == original:
                raise RuntimeError("Mutation did not change source: " + name)
            (work / "Collector.inc").write_text(body, encoding="utf-8")
            subprocess.run(args.compiler + flags + ["-I", str(native), "-I", str(work), str(work / "fixture.cpp"),
                                                   "-o", str(binary)], check=True)
            result = subprocess.run([str(binary)], capture_output=True, text=True)
            if result.returncode != 1 or "collector line " not in result.stderr:
                raise RuntimeError(f"Mutant not rejected by a fixture assertion: {name}: {result.returncode}")
        (work / "Collector.inc").write_text(original, encoding="utf-8")
        mask_mutant = files["fixture.cpp"].replace(
            "return PB59::Recovery::CopyUpdateMask(_changesMask, m_valuesCount, output);",
            "auto valid = PB59::Recovery::CopyUpdateMask(_changesMask, m_valuesCount, output); "
            "output.words = {}; return valid;")
        if mask_mutant == files["fixture.cpp"]:
            raise RuntimeError("Private Item mask mutation did not change source")
        (work / "fixture.cpp").write_text(mask_mutant, encoding="utf-8")
        subprocess.run(args.compiler + flags + ["-I", str(native), "-I", str(work), str(work / "fixture.cpp"),
                                               "-o", str(binary)], check=True)
        result = subprocess.run([str(binary)], capture_output=True, text=True)
        if result.returncode != 1 or "collector line " not in result.stderr:
            raise RuntimeError("Private Item mask mutant not rejected by a fixture assertion")
        count = len(mutants) + 1
        print(f"{count}/{count} queue/update observation mutants rejected by runtime checks")
