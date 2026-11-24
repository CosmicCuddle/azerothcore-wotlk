# CS02 native v1 binding: Codex's actual Core74 executor at 7fdc3a65

This is a test-only, local runner. It is not hosted CI, not a worldserver build and not gameplay.

```
PATH=/workspace/cxx/bin:$PATH python3 run_native_v1.py --core74 <azerothcore checkout at 7fdc3a65> \
    --compiler g++ clang++ --sabotage [--log DIR]
```

## What is real and what is a double
- **REAL, extracted verbatim at run time** from the pinned checkout, using the same brace-matched extraction as Codex's `tests/pb59/test_inventory_batch_native.py`:
  - `Player::ExecutePB59InventoryBatchPlan` (PlayerPB59Inventory.cpp);
  - `CheckPB59InventoryBatchPlan`, `CapturePB59InventoryStamp`, `ReconcilePB59OriginalCombatEffects` (PlayerStorage.cpp);
  - `_ApplyItemMods` (Player.cpp);
  - `GlobalCooldownMgr` (CharmInfo.h).
  - All PB59 headers come from the real `src/server/game/Entities/Player` include path, including EffectBoundary, InventoryReservation, the quarantine registry and the batch plan.
- **DOUBLES:** Codex's pinned fixture `tests/pb59/inventory_batch_native_fixture.cpp`, which models Player, Item, Bag, Session, Map, auras, policy, `scripts.equip`, `RelocatePB59InventoryLinks` and `CapturePB59InventorySlot`.
  - The runner patches a temporary copy only to give each Player double its own owner GUID, which A→B→A needs. There are three asserted single-match patches.
  - The real `CapturePB59InventorySlot` (PlayerStorage.cpp:519–580) is **not** compiled. The fixture double stands in for it.
- **CS02 code:** `cs02_native_checks.inc` replaces Codex's `Checks.inc`. It holds the scenarios, a property oracle, a mixed-direction matrix and synthetic adapter controls.
- **Limitation:** freeing a Player during the call cannot be simulated in-process around the real body without UB. Owner loss is therefore modelled as `inWorld=false` plus session logout (C3).

## Pins
- HEAD: `7fdc3a655f2f46cf8cba008a8f9226413f61e7f6`.
- The runner verifies 17 blob SHAs and refuses to run on any mismatch (`PIN_BLOBS` in the runner).
- Later head c779e7b4 changes only Player.h (a friend declaration), the contract doc and a new python test. None of the extracted bodies or PB59 headers change.

## Pre-registered expectations
Expectations were fixed before the first run and were not changed after it. Divergences are printed, not adapted.

| id | fault | expected |
|---|---|---|
| A1 | raw destination-bag replace during effects (strip to bag) | RecoveryRequired |
| A2 | guarded `CheckItem` before the bag replace | RecoveryRequired, bag kept |
| A3 | bag replaced while effects run for a later move | RecoveryRequired |
| A4 | bag replaced during reverse | RecoveryRequired |
| B0 | control | both owners Applied |
| B1 | A→B→A: inner A uses a pre-captured stamp | inner Refused, B Applied, outer A RecoveryRequired (CS02 finding 2) |
| B2 | B calls A's public mutation | A RecoveryRequired, B RecoveryRequired |
| B3 | B raw-writes A's item durability | A RecoveryRequired, B Applied |
| C1/C2 | logout / save during effects | Restored or RecoveryRequired, intent retained |
| C3 | owner loss (leaves world + logout) | RecoveryRequired |
| C4 | session incarnation changes | RecoveryRequired |
| C5 | other-thread producer: frame and reservation invisible, registry visible | Applied |
| C6 | other-thread raw write | RecoveryRequired |
| D1–D5 | reverse failures: mods throw, effects-only recovery, logout during reverse, opaque callback, hidden modifier change | RecoveryRequired (D3: Restored or RecoveryRequired, intent retained) |

The property oracle runs on every result:
- **Restored** must have original custody (including the bag GUID), fields, mods, queue and context, and an Idle ledger.
- **RecoveryRequired** must be quarantined, with a GUID list exactly equal to the originals not proven at their source.
- **Applied** must be exact and Idle.
- **Refused** must have no transfers.
- An unknown outcome fails.

The adapter controls check that an inconsistent Applied, a fingerprint mismatch or an unknown value fails closed.
