# CS02 Layer 3: real PB53 adapter + real Core74 executor (test-only, local)

Asked for by CS01 in coordination #79 7137314627, #52 7137340399 and #79 7137342114 (Michael approved 16:37 UK, 24 Nov 2026).
Independent tester: CS02/Grok. These are local test files only. They are **not** a worldserver build, **not** gameplay, and there are **no** hosted CI runs.

## Pins (checked on every run; the run stops on any mismatch)
- PB53 = CosmicCuddle/mod-playerbots#53, `claude/pb52-core74-wiring-20261124`, head `96c579e86443c86780d6f9cd8ef51b7c252b09f8`, clean tree.
- Core74 = `c779e7b4f735cab048f5b351ca6b9053f188787a`, clean tree.
- `PINS.txt` holds 26 git blob hashes (11 PB53 files and 15 Core74 files). It includes `AutoGearCappedExecution.h` = `ca946539d182d352a10545895f89beaf15286d3c`.
- PB source is **read** from a checkout of PB53. It is not vendored here and is never edited. Mutants and the FIX-PROBE change only a temporary copy.

## What is real and what is a double
| Part | Status |
|---|---|
| `PlayerAutoGearInventory::Capture`, `StripToBags`, `DestroyCreatedEquipped`, `RestoreToSlot`, `InWorld`, `RunCoreBatch`, `AllAtDestination`, `InventoryFingerprint`, `Fingerprint` (TrainerAction.cpp) | REAL. Extracted verbatim by signature. The runner adds only the class shell and members. |
| `AutoGearCappedExecution.h` (`PlanExecution`, `Execute`), `AutoGearCoreBatchRequest.h` (`ToStripResult`, `OutcomeFrom`, `MayMoveOwnedItems`, `BuildMoves`), the selection/eligibility headers | REAL. Included unchanged. |
| `DestroyObsoleteAmmo`, `PlayerbotFactory::InitAmmo`, `PlayerbotFactory::DisplacesOffhand` (PlayerbotFactory.cpp) | REAL bodies in a separate translation unit (`l3_factory.cpp`), with its own Player/Item doubles. |
| Core74 `ExecutePB59InventoryBatchPlan`, admission, stamp, `_ApplyItemMods`, combat effects, PB59 headers | REAL. Same extraction as Codex's `test_inventory_batch_native.py`. |
| Player/Item/Bag/Session and `CapturePB59InventorySlot`/`RelocatePB59InventoryLinks` | DOUBLES. Codex's `inventory_batch_native_fixture.cpp` (blob 1ff86282), plus 6 asserted patches that touch only the doubles. |
| `Bot` facade | DOUBLE. It forwards to the fixture Player and counts every Player call. That is how "reads after a stop / after RR / after owner loss" are measured. |
| `RunFactory`, `ApplyBisOverlay`, `ApplyFinishers` inside `Execute` | SCRIPTED. `PlayerbotFactory` is too large to compile here. The real factory pieces relevant to these cases (ammo, off-hand displacement) are tested in TU3 instead. |
| `RunPB59AutoGearWorldPlan` (outer caller), config defaults | SOURCE-TEXT checks (`SRC` rows), not run. |

**Why real capture + real relocation + real executor are not in one translation unit:** the real `CapturePB59InventorySlot` body needs engine update fields (CONTAINED, owner, template, item state). Codex's executor fixture does not model those; it uses its own Guid and slot map. The real capture and relocation bodies at c779e7b4 are already covered against an engine-shaped double in `../native_c779` (2196 checks, commit 8a8fe2f1). Here they are doubled, and every report says so.

**Owner deallocation mid-call** cannot be simulated without undefined behaviour. Instead, "owner lost" is modelled as a lost session/world state, and every later Player access is counted.

## Rules for the oracle
Expectations were registered before the first run and have not been changed since. A row that disagrees is printed as `DIVERGENCE` and kept. Only crashes, sanitizer reports or `Require` failures count as property failures. Rows A12b and A13 were **added after** the first mutant run because P7 and P13 survived. They are new rows with their own expectations. The Core76 diagnostic child is separate and is not touched here.

## Run
```
PATH=/workspace/cxx/bin:$PATH python3 run_layer3.py --pb53 <mod-playerbots@96c579e8> --core74 <azerothcore@c779e7b4> \
    --compiler g++ clang++ --sabotage --log <dir>
```
Each compiler runs plain `-O2` and `-O1` ASan+UBSan, all with `-std=c++20 -Wall -Wextra -Werror`. There are 3 binaries per toolchain: adapter ON, adapter OFF (`PB59_AUTOGEAR_CORE_BATCH=0`), and factory. `--sabotage` runs 13 PB-text mutants and a labelled FIX-PROBE (CS01's proposed early return after a stop, applied to a temporary copy only).
