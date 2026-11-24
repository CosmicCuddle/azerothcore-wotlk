# Dormant native recovery collection and save provenance

Owner: CosmicCuddle. Assignment: coordination #51 comment7139518657.
Branch `feature/codex-pb59-native-recovery-collector-20261124`, exact Core78 parent
`9bfe0d6ffbfe8204b62cd64f2af02eabb638a476`. Existing branches and PB53/PB54 build pins
remain preserved. No integration repin or native runtime caller is introduced.

## Collection protocol

Three private static Player helpers are implemented in PlayerPB59Recovery.cpp:

1. PreparePB59RecoveryCollection runs in the serialized owner context before admission.
   It uses actual native stamp/preflight, validates source/destination values and reserves
   one fixed-capacity scratch object. Allocation/refusal opens no scope and changes no
   native state. Capacity is 19 originals/destinations, 512 text bytes per observation and
   a bounded 4096-entry queue scan. Future callers must refuse before movement on null.
2. BindPB59RecoveryCollection accepts the actual retained MutationScope, never an integer
   or PB identifier. Thread identity is checked before scope access, then scope incarnation
   and access before actor reads. Native owner/session/map, quarantine flag/key registry,
   stamp and exact original/destination slots/pointers are checked. Only scope.GetTicket
   supplies the operation ticket; only native equipment capture supplies revision.
3. CollectPB59RecoveryEvidence is static: it can detect a dead sentinel without a call
   through a destroyed Player. Scope ticket/incarnation/access precede and follow every
   native getter. It copies the failure/empty-GUID report and observes original/destination
   slots, pointer quality, pending intent, journal masks and queue shape. Wrong-thread
   entry touches neither scope/actor nor shared scratch. Failed/repeated use cannot silently
   refresh the first record. No helper finishes a scope, releases quarantine, consumes intent,
   saves, moves, deletes or recreates items.

Scratch privately borrows Player/Item/Map addresses for the same synchronous call/tick.
It is not a lifetime pin, world-context proof, thread lock, asynchronous handle or lookup
capability. The caller must retain the actual scope; collection cannot be invoked after
that scope/executor returns. Same-thread validation is additional protection, not authority
to access an arbitrary Player. The future native caller remains responsible for serialized
ownership and supported lifecycle exclusion.

CopyValues exports bounded owned values only. Observation availability travels in the
summary: unobserved false/zero is not proof that no pending intent exists. Collected means
observations obtained, not Native Applied/Restored or verified reconciliation. Journal
phases are not full original combat/aura/cooldown before-images; queue shape is not full
dirty/update-mask restoration proof. Effects/queues stay Unknown or Uncertain, never Verified.
Native slot getters may allocate/throw; failure stays incomplete, not invented empty inventory.
Only collector scratch capacity is pre-reserved. Process teardown and unsupported raw deletion
remain outside this retained synchronous contract.

## Read-only save provenance

PB59RecoverySaveProvenance.h reads a copied actual CharacterSaveEvidence record and exact
expected native observer ticket. It does not Reserve/Seal/Attach/Drain/Acknowledge, query a
DB or mutate observer state. Missing records are Unavailable; malformed data, wrong owner/
session or stale serial/sequence/slot stay unverified. Reserved is not a sealed capture;
Pending has no execution result. Terminal status/code consistency, capture completeness,
duplicate/zero IDs and original GUID membership are checked independently.

Even matching terminal Committed observations cannot convert to Recovery::PersistenceEvidence.
Save sequence/observer serial are not recovery ticket/revision. Existing records lack that
operation binding, full instance/slot/effects proof and a storage durability acknowledgement.
Dirty GUID membership includes removals: it does not prove existence after commit. Empty,
ambiguous, not-started, failed, rejected and cancelled execution outcomes are preserved.
No new association or recovery authorization is fabricated.

Exact unchanged source traced from the parent:

| File | Git blob |
| --- | --- |
| PB59CharacterSaveEvidence.h | 752bde5dcee5ae0ae36d258cd6080cde1042f4ef |
| PB59CharacterSaveObserver.h | 2582a3307657dce7276dda9645c44c382612ae03 |
| PlayerStorage.cpp | 048723ba41a395b3f76203197199fc6fdaaa0f8b |
| DatabaseTransactionResult.h | 1f3ce4d16fefba7a514914af71a8539381303c5a |
| Transaction.cpp | a0335dfc1836f57f657d96dbcb39ffe6f936e6e1 |
| DatabaseWorkerPool.cpp | 8d5ed036ef219ae3c58ac2ceaaf224aa7877c439 |

SaveToDB reserves identity before dirty-state consumption, seals IDs, then attaches the
typed commit future. _SaveInventory records raw GUIDs before item-state handling, including
removal. The typed task reports rejected admission, cancellation or ambiguity and maps
execution codes to results. The observer consumes futures/reclaims/acknowledges records;
later absence is not proof of success/failure. No observer hook or save bypass is added.

## Executable evidence and limits

- GCC16.2/C++20/-Wall/-Wextra/-Werror: 64 extracted native collector checks pass ordinary
  and optimized NDEBUG. Actual collector/stamp/admission bodies and PB59 headers are used;
  engine, slot capture, map/session/item infrastructure are explicit reused doubles.
  Cases cover allocation/capacity refusal, actual ticket/revision, wrong/missing scope,
  context/lifetime loss, stale access during observation, wrong thread, wrong pointer,
  missing originals, pending intent, queue/journal uncertainty and no writes/unlock.
- Actual save-record/transaction-result constexpr contracts pass ordinary/NDEBUG; eight
  one-defect mutants fail static assertions in each mode. No executable/DB is involved.
- Inherited Core78 model: NDEBUG now executes 158 checks and rejects eight runtime mutants.
  Its ordinary executable still encounters Windows WinError5. The new collector fixture
  runs normally without security changes; the earlier execution issue is not globally
  claimed resolved. No controls were disabled and no environment was installed.
- SQL style passes; CPP retains four inherited diagnostics: BattlegroundAV.h56/797,
  MoveSpline.cpp224, Spell.cpp3901. Commit whitespace checked separately.
- No full real Player translation-unit/Worldserver/module ABI, DB/gameplay, shutdown,
  native reconciliation or durability PASS. Full build remains unavailable locally.

```text
python -B tests/pb59/test_native_recovery_collection.py --compiler g++
python -B tests/pb59/test_native_recovery_collection.py --ndebug --compiler g++
python -B tests/pb59/test_native_recovery_collection.py --syntax-only --compiler g++
python -B tests/pb59/test_recovery_save_provenance.py --compiler g++
python -B tests/pb59/test_recovery_save_provenance.py --ndebug --compiler g++
```

For an appropriate isolated Linux toolchain the collector runner accepts --sanitizers;
that mode was not run locally. No deliberate crash/shutdown or live stack test was performed.

## Remaining boundary and rollback

No runtime collection wiring, reset/unlock, replay, forced retirement, schema/IO/config/SQL/
DBC/client change, hosted CI, merge or deployment. Full native effects/queue before-images
and operation-bound save provenance remain open. New persistence or global shutdown policy
still requires owner review before wiring. Discard this child to roll back; original source
branches/pins and established custom features remain intact.
