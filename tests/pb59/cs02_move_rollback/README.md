# CS02 PB59 AutoGear move-rollback failure-injection harness (draft, test-only)

Owner lane: CS02/Grok, independent tests only (coordination #52 7118592534 / 7118630018 / 7118646677, #59 7118593397,
checklist `integration-build/AUTOGEAR-P0-ACCEPTANCE-20261124.md` @ b5c6e1bb). Base: Core53 `79b0b1fe2d0fac93227c28329508553cfec15c4d`.
No native source is changed; no CI workflow is added; nothing here runs on a server or database.

## What is real and what is synthetic

| Part | Status |
|---|---|
| `SyntheticInventoryCore.h` | **SYNTHETIC** value model of the Core53 PlayerStorage paths (CanUnequipItem runs `OnPlayerCanUnequipItem` first; StoreItem may return a different pointer and delete the source, incl. the self-merge branch; RemoveItem; AutoUnequipOffhandIfNeed mail path). |
| `MoveRollbackContract.h` | **PROPOSED** contract from Claude's #51 7118628149 and CS01's classes; not an agreed native interface. |
| `MoveRollbackOracle.h` | Independent oracle. Uses the **real** Core53 `PB59InventorySlotFacts.h` (`InventorySlotFacts`, `SameInventoryInstance`, `ValidInventorySlot`). |
| `Executors.h` | `ReferenceExecutor` = CS02's synthetic satisfiability baseline (not a Core proposal); `Knobs` = one-defect mutants; `Pb44StripExecutor` = transcription of PB44 `1a9e4801` TrainerAction.cpp:692-704. Uses the **real** `ExactStorageGate.h` predicates. |
| `Pb50StripAdapter.h` + `pb50_ebc3a710/` | **SYNTHETIC** run of Claude's PB50 (mod-playerbots#50 @ `ebc3a710`) through `MoveExecutor`: PB50's real `PlayerbotsAutoGearStorage::Execute`/`ValidMoves`/`OriginalAndEmpty` (verbatim copies, see `pb50_ebc3a710/PROVENANCE.md`), the StripToBags lambdas and the Prepare/ValidateEquipmentStorage checks transcribed. Only substitution: destinations come from the harness plan instead of PB50's `Match()`. H1-H6 run with the side effect on the 1st, 2nd and 3rd `OnPlayerCanUnequipItem` call (PB50 calls it at planning, validation and inside the move). Results are printed as findings, not harness failures; PB50's StripResult has no GUID list, so that check is skipped. |
| `core53_api_shape_probe.cpp` | Compile-only `static_assert`s on the **real** Core53 `Player.h`/`ScriptMgr.h` signatures and slot constants the model copies. Not a worldserver build, nothing linked. |

Every result line the binary prints is labelled `[SYNTHETIC]`. A pass means "classification consistent with the model",
never native success, lifetime safety or durable DB atomicity.

## Outcomes the oracle derives from state (never from the executor's report)

- **Refused**: no executor mutation, state unchanged.
- **Applied**: every item at its exact planned destination with identical instance facts; links/effects coherent; nothing mailed.
- **Restored**: effects happened, final state identical to the start, coherent.
- **RecoveryRequired**: anything else; the report must list exactly the GUIDs not at their original position.
Also always flagged: stale `Item*` use, mutation after logout/retirement began, mail, identity loss without RecoveryRequired,
rollback not in exact reverse order, incoherent slot field / visible field / item mods / duration registrations.

## Matrix

- P01-P30 preflight refusals (owner, incarnation, stamp, logout, retirement, re-entry, same slot/different GUID, moved item,
  foreign owner, duplicates, occupied/mergeable/invalid/NULL_SLOT destinations, trade, loot, combat, callback refusal,
  callback side effects, bags one short, bag items, two-hander next to off-hand).
- A01-A10 applied (N = 1..5, exact fit, bag destinations, equip from bag, off-hand out before a two-hander, fenced nested call).
- F: every forward failure position for N = 1..4 x {insert refuse, throw, insert-then-throw, wrong place, replacement pointer,
  remove no-op, remove throw}.
- R: every rollback failure position for N = 2..4 (insert refuse/throw/replacement pointer, remove throw) plus double failures.
- H1-H8 CS01's PB44 hazard as scenarios (callback deletes, relocates, swaps GUID, starts logout, re-enters, refuses mid-strip;
  replacement pointer; bags run out). Expected red for the PB44 transcription; the fix is Claude's.
- Main-hand-first restore after an off-hand-first strip; M4 mail path for a naive two-hander sequence.
- 16 mutants (negative controls), each must be killed, incl. "callback change labelled Refused" and "two-hander rule on final state only".

## Run

```
PATH=/path/to/toolchain/bin:$PATH tests/pb59/cs02_move_rollback/run_local.sh            # g++/clang++, plain + ASan/UBSan
tests/pb59/cs02_move_rollback/check_core53_headers.sh <boost-include> <mysql-include>   # compile-only real-header probe
```

## Adapting to Codex's native interface

Add a `NativeAdapter` satisfying `MoveExecutor` (maps `Request` to the agreed call and its result enum to `Outcome`) and a
native `Capture` that fills `PB59::InventorySlotFacts` from a real `Player`. Fault positions then come from Codex's own
injection seam; the scenario names and expected outcomes stay. This harness does not test Codex's internals and should not
duplicate Codex's own native tests.

## Agreed contract points (Codex #52 7130720231, Claude #51 7127659287) and how the harness enforces them

1. **No destination reuse, no source/destination overlap.** Every destination must be distinct and empty at preflight;
   a destination that is another move's source is refused (P11, P12, P15, P28).
2. **Callback changes are conservative.** After the callbacks the reference re-captures inventory AND owner context
   (incarnation, stamp, logout, retirement). If anything changed it returns `RecoveryRequired` with the GUIDs that are not
   at their original position and does not try to undo callback effects. A callback that only refuses is `Refused`.
   The oracle derives `Refused` only when inventory and context are proven unchanged (P21, P22, H1-H4). A context-only
   change (e.g. logout began) is `RecoveryRequired` with an empty GUID list.
3. **No exception atomicity assumed.** Throw and move-then-throw are injected at every forward and rollback position;
   the class comes only from captured postconditions.
4. **No implicit off-hand displacement.** A two-hander may enter the main hand only if the off-hand is already empty or
   an explicit, validated off-hand move earlier in the batch empties it (A09). Otherwise refuse: two-hander before the
   off-hand move (P29), invalid off-hand move (P30), occupied off-hand (P25). No mail fallback (M4 check).
