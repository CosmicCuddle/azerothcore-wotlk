# CS02 Core74 cross-check (test-only, local, not CI)

Child of PR #73 (`test/cs02-pb59-move-rollback-20261124` @ e741f4b4). Test files only.

Run: `tests/pb59/cs02_move_rollback/core74/run_core74_crosscheck.sh <core74-checkout> [boost/mysql sysinc dirs]`

## Pinned REAL inputs (read-only; runner refuses anything else)
| What | Pin |
|---|---|
| Core74 head (PR #74) | `27b6135909826434d9d5856237a68e7bfc6612f2` |
| `PB59InventoryBatchPlan.h` | blob `cb5574ab902334431ac42067013a8feb6d2fe57b` (identical at 4742d86f, 8276ff5e, 27b61359) |
| `PB59TransactionAdmission.h` | blob `6c3bf34b767f58c72232145c143263cc66d60fdd` |

Real code executed: `PB59::ValidateInventoryBatchPlan`, `PB59::TransactionAdmission` (+ `MutationScope`, `ExternalScope`).
Compile-only: `core74_api_shape_probe.cpp` (Player::CheckPB59InventoryBatchPlan, CapturePB59InventorySlot, CanAccess, Request signatures).
**Not executed:** `Player::CheckPB59InventoryBatchPlan` (needs a native Player). No worldserver build.

## SYNTHETIC parts
Inventory core, executors (incl. `LedgerGatedExecutor`, a stand-in for the future four-outcome executor),
callbacks, the `_ApplyItemMods` analogue, and all sabotage adapters. The check-only bool is never used as a move executor.

## Sections
- **A** independent oracle (set of violated rules) vs real validator: seeded valid batches n=1..19, 14 single-defect classes, empty/oversize/owner-0, 4000 multi-defect fuzz batches. Not derived from Codex's 19/27 cases.
- **N (LATER NATIVE GATE, informational)** structurally accepted: destination container identity, invalid positions, source container claim, move direction, stale facts, two-hander order, owner/session/stamp/world context.
- **L** 400 random ledger op sequences; invariants I1–I9; counts states where CanAccess(Forward)=true with a pending intent.
- **C** CS01 case: pending logout/save intent with live sentinel + active ticket must not permit forward writes (C1–C3); in-call `_ApplyItemMods` analogue: pending logout and unlink inside the equip call (C4–C5); clean control (C6).
- **S** sabotage: 7 suppressed error classes + accept-all; 3 lying ledger readers; CanAccess-only, admission-skipping and trust-returned-pointer executors. One equivalence probe (CanAccess depth/seal terms) is reported, not counted.

## Executor fault controls (provisional seam), `run_core74_executor_controls.sh <core74@fa674825>`
Pins: Core74 `fa67482573e9426b701adb9b546a44a6d6113237`; `PB59EffectBoundary.h` `008c93c4…`, `PB59TransactionAdmission.h`
`6c3bf34b…`, `PB59InventoryBatchPlan.h` `cb5574ab…`; CS01's `tests/pb59/test_effect_boundary.cpp` `410477aa…` (pinned, not re-run).
`ProvisionalExecutor` concept = `Report Execute(OwnerWorld&, Request const&)` returning Applied/Restored/Refused/RecoveryRequired
(same names and order as Claude's PB52 ee084ebb `CoreBatchOutcome`). `CandidateExecutor` is CS02's SYNTHETIC reference model, not
Codex's code; native binding waits for the real signature. Controls: A0–A4 destination bag replacement, B0–B3 A→B→A, C1–C8 pending
intent / owner loss / incarnation / other-thread producer / F5 distinction. 11 executor mutants (M1–M11).
The crosscheck runner accepts either 27b61359 or fa674825 (same two blobs).
