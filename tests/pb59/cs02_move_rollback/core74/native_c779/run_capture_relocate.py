#!/usr/bin/env python3
"""CS02 source-coupled runner: REAL CapturePB59InventorySlot + RelocatePB59InventoryLinks at Core74 c779e7b4.

Test-only and local (no hosted CI). Not a worldserver build, not gameplay. See README.md for real vs double.
  run_capture_relocate.py --core74 <checkout@c779e7b4> [--compiler g++ clang++] [--sabotage] [--log DIR]
"""
import argparse, hashlib, subprocess, sys, tempfile
from pathlib import Path

PIN_HEAD = "c779e7b4f735cab048f5b351ca6b9053f188787a"
P = "src/server/game/Entities/Player/"
I = "src/server/game/Entities/Item/"
PIN_BLOBS = {
    P + "PlayerStorage.cpp": None, P + "Player.h": None, I + "Item.cpp": None, I + "Item.h": None,
    I + "Container/Bag.cpp": None, I + "Container/Bag.h": None,
    P + "PB59InventorySlotFacts.h": None, P + "PB59InventoryCustody.h": None, P + "PB59TransactionAdmission.h": None,
    P + "EquipmentCaptureState.h": None, P + "PB59InventoryReservation.h": None,
    P + "PlayerPB59Inventory.cpp": None, "tests/pb59/test_inventory_custody_body.cpp": None,
    "tests/pb59/CORE74_NATIVE_INVENTORY_BATCH.md": None,
}
PIN_FILE = Path(__file__).with_name("PINS.txt")

EXTRACT = [  # (include name, file, exact signature)
    ("ItemGetBagSlot.inc", I + "Item.cpp", "uint8 Item::GetBagSlot() const"),
    ("ItemSetPB59State.inc", I + "Item.cpp", "void Item::SetPB59State(ItemUpdateState state, Player* forplayer)"),
    ("ItemAddPB59ToUpdateQueueOf.inc", I + "Item.cpp", "void Item::AddPB59ToUpdateQueueOf(Player* player)"),
    ("BagGetItemByPos.inc", I + "Container/Bag.cpp", "Item* Bag::GetItemByPos(uint8 slot) const"),
    ("PlayerGetItemByPos16.inc", P + "PlayerStorage.cpp", "Item* Player::GetItemByPos(uint16 pos) const"),
    ("PlayerGetItemByPos.inc", P + "PlayerStorage.cpp", "Item* Player::GetItemByPos(uint8 bag, uint8 slot) const"),
    ("PlayerGetBagByPos.inc", P + "PlayerStorage.cpp", "Bag* Player::GetBagByPos(uint8 bag) const"),
    ("CapturePB59InventorySlot.inc", P + "PlayerStorage.cpp",
     "std::optional<PB59::InventorySlotFacts> Player::CapturePB59InventorySlot(uint16 pos) const"),
    ("RelocatePB59InventoryLinks.inc", P + "PlayerStorage.cpp", "PB59::LinkTransferResult Player::RelocatePB59InventoryLinks("),
]
# Inline accessors the doubles copy: the real header text must still say exactly this.
INLINE_PARITY = [
    (I + "Item.h", "uint16 GetPos() const { return uint16(GetBagSlot()) << 8 | GetSlot(); }"),
    (I + "Item.h", "bool IsInUpdateQueue() const { return uQueuePos != -1; }"),
    (I + "Item.h", "uint32 GetQueuePos() const { return uQueuePos; }"),
    (I + "Item.h", "void SetPB59Slot(uint8 slot) { m_slot = slot; }"),
    (I + "Item.h", "void SetPB59Container(Bag* container) { m_container = container; }"),
    (I + "Item.h", "bool IsBag() const { return GetTemplate()->InventoryType == INVTYPE_BAG; }"),
    (I + "Item.h", "bool IsInTrade() const { return mb_in_trade; }"),
    (I + "Item.h", "bool IsRefundable() const { return HasFlag(ITEM_FIELD_FLAGS, ITEM_FIELD_FLAG_REFUNDABLE); }"),
    (I + "Item.h", "bool IsBOPTradable() const { return HasFlag(ITEM_FIELD_FLAGS, ITEM_FIELD_FLAG_BOP_TRADEABLE); }"),
    (I + "Item.h", "bool IsWrapped() const { return HasFlag(ITEM_FIELD_FLAGS, ITEM_FIELD_FLAG_WRAPPED); }"),
    (I + "Item.h", "ObjectGuid GetOwnerGUID() const { return GetGuidValue(ITEM_FIELD_OWNER); }"),
    (I + "Item.h", "Bag* ToBag() { if (IsBag()) return reinterpret_cast<Bag*>(this); else return nullptr; }"),
    (I + "Container/Bag.h", "uint32 GetBagSize() const { return GetUInt32Value(CONTAINER_FIELD_NUM_SLOTS); }"),
    (P + "Player.h", "bool IsBeingTeleportedFar() const { return mSemaphoreTeleport_Far != 0; }"),
    (P + "Player.h", "ObjectGuid GetLootGUID() const { return m_lootGuid; }"),
    (I + "Container/Bag.h", "#define MAX_BAG_SIZE 36"),
    (P + "Player.h", "#define INVENTORY_SLOT_BAG_0    255"),
]
CAP, REL, ADD = "CapturePB59InventorySlot.inc", "RelocatePB59InventoryLinks.inc", "ItemAddPB59ToUpdateQueueOf.inc"
MUTANTS = [
    ("K1 capture: no container owner check", CAP, "container->GetOwnerGUID() != GetGUID() || ", ""),
    ("K2 capture: no container CONTAINED check", CAP, "container->GetGuidValue(ITEM_FIELD_CONTAINED) != GetGUID() ||", ""),
    ("K3 capture: no player bag-slot GUID check", CAP, "GetGuidValue(PLAYER_FIELD_INV_SLOT_HEAD + bag * 2) != container->GetGUID() ||", ""),
    ("K4 capture: no item position check", CAP, "item->GetPos() != pos ||", ""),
    ("K5 capture: no in-trade refusal", CAP, "item->IsInTrade() || ", ""),
    ("K6 capture: duplicate enchant timer accepted", CAP, "seen[index] || ", ""),
    ("K7 capture: facts drop container GUID", CAP, "container ? container->GetGUID().GetRawValue() : 0, pos", "0, pos"),
    ("K8 capture: logout session accepted", CAP, "session->PlayerLogout() || ", ""),
    ("K9 capture: slot beyond bag size accepted", CAP, "slot >= container->GetBagSize() ||", ""),
    ("K10 capture: item CONTAINED unchecked", CAP,
     "item->GetGuidValue(ITEM_FIELD_CONTAINED) != (container ? container->GetGUID() : GetGUID()) ||", ""),
    ("K11 relocate: destination pre-check dropped", REL, "*fromNow != source || *toNow != destination)", "*fromNow != source)"),
    ("K12 relocate: post-check ignores destination bag GUID", REL, "toNow->bag != destination.bag || ", ""),
    ("K13 relocate: post-check ignores instance identity", REL, " || !PB59::SameInventoryInstance(source, *toNow))", ")"),
    ("K14 relocate: whole post-check removed", REL, "            return recovery();\n        // Linked is only", "            (void)0;\n        // Linked is only"),
    ("K15 relocate: queue pre-validation dropped", REL, "if (!validQueue(item, false) || (fromBag", "if ((fromBag"),
    ("K16 relocate: no same-owner fact check", REL, " || source.owner != destination.owner", ""),
    ("K17 item: enqueue ignores owner mismatch", ADD, "    if (player->GetGUID() != GetOwnerGUID())", "    if (false)"),
]
SIG = ("SCN", "MATRIX", "CS02 native", "FAIL")


def blob(data):
    return hashlib.sha1(b"blob %d\0" % len(data) + data).hexdigest()


def function(source, signature):
    if source.count(signature) != 1:
        sys.exit(f"signature matched {source.count(signature)} times: {signature}")
    start = source.index(signature)
    opening = source.index("{", start)
    depth, end = 1, opening + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end] + "\n"


def build(core, work, mutant=None):
    here = Path(__file__).parent
    for name, rel, sig in EXTRACT:
        body = function((core / rel).read_text(), sig)
        if mutant and mutant[1] == name:
            if body.count(mutant[2]) != 1:
                sys.exit(f"mutant {mutant[0]} matched {body.count(mutant[2])} times")
            body = body.replace(mutant[2], mutant[3])
        (work / name).write_text(body)
    for f in ("cs02_capture_relocate_fixture.cpp", "cs02_capture_relocate_checks.inc"):
        (work / f).write_text((here / f).read_text())


def compile_run(cxx, flags, core, work, tag, strict=True):
    binary = work / f"cs02-{tag}"
    cmd = [cxx, *flags, "-std=c++20", "-Wall", "-Wextra", *(["-Werror"] if strict else []), "-pthread",
           "-I", str(core / P), "-I", str(work), str(work / "cs02_capture_relocate_fixture.cpp"), "-o", str(binary)]
    built = subprocess.run(cmd, capture_output=True, text=True)
    if built.returncode:
        if strict:
            sys.exit(built.stderr)
        return None, built.stderr
    run = subprocess.run([str(binary)], capture_output=True, text=True,
                         env={"ASAN_OPTIONS": "detect_leaks=1", "UBSAN_OPTIONS": "print_stacktrace=1:halt_on_error=1"})
    return run.returncode, run.stdout + run.stderr


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--core74", required=True)
    ap.add_argument("--compiler", nargs="+", default=["g++", "clang++"])
    ap.add_argument("--sabotage", action="store_true")
    ap.add_argument("--log")
    ap.add_argument("--write-pins", action="store_true", help="record blob pins (only for the first pinned run)")
    a = ap.parse_args()
    core = Path(a.core74).resolve()
    head = subprocess.run(["git", "-C", str(core), "rev-parse", "HEAD"], capture_output=True, text=True).stdout.strip()
    if head != PIN_HEAD:
        sys.exit(f"Core74 head {head} != pinned {PIN_HEAD}")
    if subprocess.run(["git", "-C", str(core), "status", "--porcelain"], capture_output=True, text=True).stdout.strip():
        sys.exit("Core74 checkout is not clean")
    blobs = {rel: blob((core / rel).read_bytes()) for rel in PIN_BLOBS}
    if a.write_pins:
        PIN_FILE.write_text("".join(f"{v} {k}\n" for k, v in blobs.items()))
    pinned = dict(reversed(l.split(" ", 1)) for l in PIN_FILE.read_text().split("\n") if l)
    for rel, got in blobs.items():
        if pinned.get(rel) != got:
            sys.exit(f"{rel} blob {got} != pinned {pinned.get(rel)}")
    for rel, text in INLINE_PARITY:
        if text not in (core / rel).read_text():
            sys.exit(f"inline parity lost in {rel}: {text}")
    print(f"Core74 pinned {head}; {len(blobs)} blobs verified; {len(INLINE_PARITY)} inline-parity lines verified")
    status, baseline = 0, None
    with tempfile.TemporaryDirectory(prefix="cs02-c779-") as tmp:
        work = Path(tmp)
        build(core, work)
        for cxx in a.compiler:
            for mode, flags in (("plain", ["-O2"]), ("asan-ubsan", ["-O1", "-g", "-fsanitize=address,undefined",
                                                                    "-fno-sanitize-recover=all", "-fno-omit-frame-pointer"])):
                tag = f"{Path(cxx).name}-{mode}"
                code, out = compile_run(cxx, flags, core, work, tag)
                if a.log:
                    Path(a.log).mkdir(parents=True, exist_ok=True)
                    (Path(a.log) / f"{tag}.log").write_text(out)
                sig = [l for l in out.splitlines() if l.startswith(SIG)]
                if baseline is None:
                    baseline = sig
                    print("\n".join(sig))
                print(f"{tag}: exit {code}; {out.strip().splitlines()[-1]}; "
                      f"{'identical to first toolchain' if sig == baseline else 'DIFFERS from first toolchain'}")
                status |= code != 0 or sig != baseline
        if a.sabotage:
            killed = 0
            for m in MUTANTS:
                build(core, work, m)
                code, out = compile_run(a.compiler[0], ["-O1"], core, work, "mutant", strict=False)
                if code is None:
                    print(f"[SABOTAGE] {m[0]:<52} NOT RUN (does not compile)")
                    continue
                sig = [l for l in out.splitlines() if l.startswith(SIG)]
                diff = [l for l in sig if l not in baseline]
                dead = code != 0 or sig != baseline
                killed += dead
                missing = [l for l in baseline if l not in sig]
                print(f"[SABOTAGE] {m[0]:<52} {'killed' if dead else 'SURVIVED'} (exit {code})" +
                      (f"  first change: {diff[0][:140]}" if diff else f"  first missing: {missing[0][:140]}" if missing else ""))
            print(f"[SABOTAGE] {killed}/{len(MUTANTS)} killed")
    return status


if __name__ == "__main__":
    sys.exit(main())
