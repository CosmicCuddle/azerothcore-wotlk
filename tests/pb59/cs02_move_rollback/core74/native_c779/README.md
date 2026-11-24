# CS02: real CapturePB59InventorySlot and raw relocation at Core74 c779e7b4

This is a test-only, local runner. It is not hosted CI, not a worldserver build and not gameplay.

```
PATH=/workspace/cxx/bin:$PATH python3 run_capture_relocate.py --core74 <checkout at c779e7b4> \
    --compiler g++ clang++ --sabotage [--log DIR]
```

## Pins
- HEAD: `c779e7b4f735cab048f5b351ca6b9053f188787a`. The runner refuses any other head or a dirty tree.
- `PINS.txt` lists 14 blob hashes that the runner verifies.
- 17 inline-parity lines are also checked: each inline accessor the doubles copy must still appear verbatim in the real Item.h, Bag.h or Player.h.

What changed from 7fdc3a65:
- Player.h `5f1d0211` → `af442554`: one line, `friend void Item::AddPB59ToUpdateQueueOf(Player*)`.
- CORE74_NATIVE_INVENTORY_BATCH.md `f8e40149` → `9dc29ac1`.
- New file `tests/pb59/test_item_queue_access.py`.
- PlayerStorage.cpp (`048723ba`), PlayerPB59Inventory.cpp (`ebad31a3`) and all PB59 headers are unchanged.

## Real code vs doubles
**REAL.** These bodies are extracted verbatim, by exact signature and brace matching, and compiled together in one translation unit:
- From PlayerStorage.cpp:
  - `Player::CapturePB59InventorySlot` (:519–610);
  - `Player::RelocatePB59InventoryLinks` (:969–1120);
  - `Player::GetItemByPos(uint16)` and `GetItemByPos(uint8, uint8)` (:451, :458);
  - `Player::GetBagByPos` (:1122).
- From Item.cpp: `Item::GetBagSlot` (:864), `Item::SetPB59State` (:780) and `Item::AddPB59ToUpdateQueueOf` (:819).
- From Bag.cpp: `Bag::GetItemByPos` (:255).
- Real headers from the include path: PB59InventorySlotFacts.h (`ValidInventorySlot`, `SameInventoryInstance`), PB59InventoryCustody.h (`RelocateInventoryPointer`), PB59TransactionAdmission.h (ticket and ledger), EquipmentCaptureState.h (`Mutation`) and PB59InventoryReservation.h.

**DOUBLES** (`cs02_capture_relocate_fixture.cpp`, written independently of Codex's custody fixture):
- `ObjectGuid`, as a raw value.
- Object update-field storage. `SetGuidValue` keeps the real behaviour of skipping an unchanged value. `ForceValuesUpdateAtIndex` is included. Fault and corruption hooks sit on the write primitive only.
- `WorldSession`.
- Player/Item/Bag data members, plus inline accessors copied from the headers (parity-checked).
- `ItemTemplate` and enum and field constants. Slot constants match the real values; update-field indices are distinct stand-ins.
- `LOG_DEBUG` and `ASSERT` macros.
- The Item delete-path helpers (`RemoveFromUpdateQueueOf`, `DeleteRefundReference`). They are unreachable with `ITEM_CHANGED` and abort if reached.

**Cannot be compiled in isolation, and why:**
- The full PlayerStorage.cpp, Item.cpp and Bag.cpp translation units, and the real `Object`, `ObjectGuid`, `Unit` and `WorldSession`. They pull in the whole game library (DB, ScriptMgr, maps, packets).
- The real `Object::SetGuidValue`, `ForceValuesUpdateAtIndex` and update-mask behaviour are replaced by the double.
- The real `Item::SetState` gate (`InventoryMutationAllowed`) is not used by the relocation body. It calls `SetPB59State` directly.

Codex's `test_inventory_custody_body.py` compiles the real Relocate body against a **doubled** CapturePB59InventorySlot. This lane is the first to compile the real capture together with the real relocation. It does not repeat Codex's ticket-access, queue-duplicate or repair-throw rows.

## Scenarios and pre-registered expectations
Expectations were registered before the first run and are not adapted. Divergences are printed.

| group | what | expected |
|---|---|---|
| R | real capture over 256 positions: base slots 0–39 (minus 19–22), bags 19–23 and bank 67 | equipment (minus body/tabard), backpack and in-size bag slots capture; everything else refuses |
| P | 45 pre-call faults (session, source item, source bag, destination slot, destination bag incl. GUID/owner/CONTAINED/slot/nesting/removed/size/queue/coherent replacement) × 5 directions; facts captured before the fault | Rejected, and the world is byte-identical |
| V | the same faults on the real capture | refuses, except stale-facts, occupied-destination, coherent-replacement and queue faults, which still capture (added after mutant K6 survived P) |
| F | forged or other-owner facts, same position, equipment→equipment, backpack→bag | Rejected |
| Q | ITEM_NEW source, already-queued source, queue blocked between capture and call | Linked, Linked, Rejected |
| C | clean controls in 5 directions, plus a round trip back | Linked |
| M | 13 raw link corruptions at field write k ∈ 1..3, before or after the write, × 5 directions | RecoveryRequired |
| T | one-shot write throw plus a replaced bag GUID on the bagged side | RecoveryRequired |

The property oracle runs on every relocation:
- **Rejected** means the whole world is unchanged and the ledger is still Active.
- **Linked** means my independent consistency checker passes (it does not call the real capture), the real re-capture shows the same instance, and the ledger is Active.
- **RecoveryRequired** means the ledger is RR and the item is reachable exactly once.
- An inconsistent final world must never be Linked.
