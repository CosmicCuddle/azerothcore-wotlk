# Dormant native recovery collection and save provenance

Owner: CosmicCuddle. Assignment: coordination #51 comment7139518657.
Original collector branch `feature/codex-pb59-native-recovery-collector-20261124`, exact Core78
parent `9bfe0d6ffbfe8204b62cd64f2af02eabb638a476`. Queue identity follow-up branch
`feature/codex-pb59-recovery-queue-beforeimage-20261124`, exact Core79 parent
`d293650c77a9ce233f6e792c41632daed8e2b167`. Update observation follow-up branch
`feature/codex-pb59-recovery-update-observation-20261124`, exact Core81 parent
`62f54266395d4715c34464ff592ec09043f457bf`. Original/bag field-value follow-up branch
`feature/codex-pb59-recovery-original-values-20261124`, exact Core82 parent
`afd670fc7fc2bc8244a67718ca8cc38e975d7556`. Existing branches and PB53/PB54 build pins
remain preserved. No integration repin or native runtime caller is introduced.

## Collection protocol

Three private static Player helpers are implemented in PlayerPB59Recovery.cpp:

1. PreparePB59RecoveryCollection runs in the serialized owner context before admission.
   It uses actual native stamp/preflight, validates source/destination values and reserves
   one fixed-capacity scratch object. Allocation/refusal opens no scope and changes no
   native state. Capacity is 19 originals/destinations, 512 text bytes per observation and
   a bounded 4096-entry queue identity before-image. Future callers must refuse before movement on null.
2. BindPB59RecoveryCollection accepts the actual retained MutationScope, never an integer
   or PB identifier. Thread identity is checked before scope access, then scope incarnation
   and access before actor reads. Native owner/session/map, quarantine flag/key registry,
   stamp, exact original/destination slots/pointers and queue identity before-image are checked. Only scope.GetTicket
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

## Queue identity before-image follow-up

Prepare captures each queue index, including null tombstones, with private original Item
pointer and owned GUID/owner/queue-position values. Zero/duplicate GUIDs, foreign owners,
incorrect queue positions and over-capacity queues refuse before admission. Duplicate
GUID validation uses bounded quadratic comparison (at most 4096 entries); the adapter is dormant.
Scratch is heap-reserved before admission and statically bounded to 512 KiB including the
private image; exported CollectedValues contains no native addresses.

Bind checks the full image against the current queue after obtaining a genuine scope ticket.
A changed size, pointer or identity value refuses binding even when planned inventory slots
remain equal. During recovery only current queue pointers are dereferenced behind the retained
scope's currentness checks; the saved pointer is used solely for equality. Collection compares
all indices up to max(before size, current size), detecting appended/removed entries, reordered
entries, same-value replacements and length changes consisting only of tombstones. Over-capacity
current queues remain incomplete without indexing the fixed image.

The owned report exports the first 19 differences and a separate total changedQueueEntries.
sameNativeInstance requires a non-null current pointer equal to the saved pointer; two nulls
do not establish an instance.
queueCompared is true only after the entire bounded comparison; queueUnchanged is meaningful
only when queueCompared is true. Counts and partial samples do not imply complete observation
after an early context/lifetime failure. Collected describes observation completion and may
contain queue changes; any changed/inconsistent queue has Uncertain proof. Unchanged identities
remain Unknown: observed update masks and map registrations do not prove dirty values or
combat effects restored. Metadata values are outside this comparison. No consumer,
original executor Applied result, Playerbots behavior, release authority or persistence changes.

This does not answer whole-inventory custody by inference. Core74 verifies planned links and
depends on supported owner-wide mutation producers for unrelated items; fixture raw-field
bypasses do not establish native reachability. See coordination #51 comment7140735652.

## Dirty state, update masks and registration follow-up

Each non-null queue entry now includes native ItemUpdateState, IsInWorld, object-update pending,
registration in the original owner's map update set and a complete bounded copy of its update
mask bits. The owner Player has separate before/current mask, world, pending and map registration
values. A private Item helper reads the protected native mask; no Object API, mutator or caller
outside the dormant collector is added. The helper uses a standalone, allocation-free value copier.

Maximum mask capacities are 256 item/container fields and 2048 owner fields. The native source
statically checks CONTAINER_END/PLAYER_END fit. Native field count must be nonzero, bounded and
match UpdateMask.GetCount before any GetBit access. Output words are canonical (zero tail/padding),
including unsigned shifts at bit31. Invalid inputs leave the old mask output intact. No native
SetBit, Clear, field value write, update registration or mask restoration is performed.

Prepare refuses malformed masks or inconsistent pending/registration state before opening a scope.
Bind validates the entire prepared owner and queue update observations alongside identities.
Collection guards each observation with the retained scope checks, records complete mask/state
differences and detects pending/registration inconsistencies. Malformed current masks remain
incomplete; failed/partial scans cannot claim queueCompared. Owner availability/consistency/change
flags are separate. Unchanged queue entries plus changed owner masks/registration still produce
Uncertain; even unchanged observations remain Unknown, never Verified.

The queue update domain is the owner Player and items present in the native item-save queue at
preparation or collection. The next section extends observation to original items and affected
bags. Other unqueued objects, unrelated item field values, registration in other maps,
combat/aura/cooldown state and persistence are not established. A changed unrelated value under
an already-set mask bit may leave queue observation unchanged; no whole-inventory proof follows.
Allocation-failure injection passes for this fixture's capture
path, not all real engine getters. Process/lifetime/concurrency limits above are unchanged.

## Original items and affected bags, including unqueued instances

Prepare now captures each planned original and deduplicates the source/destination carried bag
containers (at most19+4 watched instances). Each watched before-image includes full bounded raw
update-field values, dirty state, mask, queue membership, world/pending and original-map update
registration. Field copying is allocation-free, validates nonzero count<=256 before any getter,
zeros unused tail words and leaves previous output intact on failure. Bag field bounds are
checked against native CONTAINER_END; native Bag.h is explicitly included for its real hierarchy.

Bind re-resolves each watched primary link and compares the original pointer and complete value
image. Collection resolves originals only at their planned source/destination and containers by
their carried bag index. Missing/replaced links remain incomplete; saved addresses are never
dereferenced during binding/recovery. Thus even a same-GUID replacement cannot become original
field evidence. A found original at its planned destination can still be observed. Native scope
ticket/incarnation/access checks surround every getter in the shared private Player observer.

CopyValues exports watched totals, observation totals and changed totals. CopyWatchedItem(index)
returns an owned bounded optional observation for that one instance; invalid indices return empty.
This avoids embedding large before/current field arrays into every CopyValues stack copy.
Check observed and sameNativeInstance before using now; fields may be partial after failure.
All copy access retains the same coordinated/quiescent scratch requirement as CopyValues.

Changed watched fields make queue/recovery evidence Uncertain even if the item-save queue and
mask bits stayed unchanged. Unchanged complete images still remain Unknown, never Verified.
Missing originals previously allowed complete slot observations; their missing raw-value image
now makes the extended collection Incomplete. Native executor outcomes are unchanged.

This closes the bounded raw-field/update-state observation gap for planned originals/affected
bags. Non-field metadata, native combat effects, unrelated objects/field values, durable storage
and complete gameplay remain unproven. See [native compile/persistence release-gate handoff](
NATIVE_RECOVERY_RELEASE_GATES.md) for the concrete environment and operation-bound IO blockers.

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

- GCC16.2/C++20/-Wall/-Wextra/-Werror: 479 extracted native collector checks pass ordinary
  and optimized NDEBUG. Actual collector/stamp/admission bodies and PB59 headers are used;
  engine, slot capture, map/session/item infrastructure are explicit reused doubles.
  Cases cover allocation/capacity refusal, actual ticket/revision, wrong/missing scope,
  context/lifetime loss, stale access during observation, wrong thread, wrong pointer,
  missing originals, pending intent, queue/journal uncertainty and no writes/unlock. Follow-up
  cases cover malformed/stale prepared queues, metadata versus identity, same-value pointer
  replacement, missing/added/reordered entries, 4096 changes with 19 exported samples, and
  sentinel loss at an Item GUID getter with no subsequent owner read. Added real UpdateMask
  implementation and extracted private Item helper, mask boundaries/canonical tails/mismatches,
  native dirty/world/registration changes, malformed capture and map-getter sentinel loss.
  Twelve deliberate queue/update guard mutants are rejected by runtime assertions in optimized
  NDEBUG mode; compile/launch failures do not count as a killed mutant.
  The fixture uses a fixed underlying ItemUpdateState type to make deliberately unknown-state
  fault injections defined; the native enum is unchanged. ByteBuffer serialization dependencies,
  Player/Item layouts and map infrastructure remain doubles; actual mask class is copied verbatim.
  New tests cover raw-field bounds/tails, already-set-bit changes, stale binding, missing/replaced
  originals/containers, both move directions, shared bag deduplication and all17 supported combat
  slots plus all four bags. Raw field getter/storage and bag lookup remain explicit doubles.
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
python -B tests/pb59/test_native_recovery_collection.py --ndebug --mutants --compiler g++
python -B tests/pb59/test_native_recovery_collection.py --syntax-only --compiler g++
python -B tests/pb59/test_recovery_save_provenance.py --compiler g++
python -B tests/pb59/test_recovery_save_provenance.py --ndebug --compiler g++
```

For an appropriate isolated Linux toolchain the collector runner accepts --sanitizers;
that mode was not run locally. No deliberate crash/shutdown or live stack test was performed.

## Remaining boundary and rollback

No runtime collection wiring, reset/unlock, replay, forced retirement, schema/IO/config/SQL/
DBC/client change, hosted CI, merge or deployment. Native effects, non-field state, unrelated
field values/update-state observations outside the bounded domain, and operation-bound save provenance remain
open. New persistence or global shutdown policy
still requires owner review before wiring. Discard this child to roll back; original source
branches/pins and established custom features remain intact.
