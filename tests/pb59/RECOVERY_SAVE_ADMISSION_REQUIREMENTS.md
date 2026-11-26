# Minimum native recovery-save admission and binding requirements

Owner: CosmicCuddle. Coordination #81 comment7146046931; Core88 is the source under review.
This is a reviewable contract, not an implemented admission API, state machine or runtime caller.
Quarantine, ordinary save admission, pending intent and private release gates remain unchanged.

## Trusted origin and immutable association

An association must be created by the actual serialized native capture/save producer, never
reconstructed from a command, integer equality, matching GUIDs or a later copied save record.
It must preserve these logical identities together, immutable once capture is sealed:

| Identity | Required provenance |
| --- | --- |
| Recovery owner | GUID plus native owner and session incarnations; same validated owner/context |
| Recovery attempt | Actual retained native batch ticket, not a PB identifier or save sequence |
| Capture | Native observation revision and owned before/current state with explicit domain/completeness |
| Save receipt | Actual observer instance/lifetime, slot, serial and owner/session/save-sequence ticket |
| Execution | The one typed future attached to that exact sealed capture/receipt |

Inventory revision alone is not a version of all auras, queues, raw fields or external effects.
Save ticket fields are not globally unique across different observer instances/process restarts.
Existing SaveIdentity/SaveEvidence records have no recovery-ticket/revision/full-state binding;
ObserveSave deliberately reports execution without supplying that missing authority.

## Preconditions before any recovery write

1. Exact source/module/configured native build evidence and an approved development execution
   context exist. Synthetic/header fixtures are not native Player/Game or gameplay certification.
2. The native owner is accessed only in its serialized context after lifetime/currentness
   checks. Exact originals/containers, custody, queue/update state, combat/aura/cooldown state
   and non-field effects are accounted for. Missing/substituted objects, incomplete domains,
   uncertain callbacks or an empty recovery GUID list cannot be treated as restored.
3. A real owner/GUID economic and lifecycle fence covers capture, submission and settlement.
   The synchronous MutationScope sentinel is not a Player/Item lifetime pin and cannot be
   retained as a borrowed asynchronous handle. Workers never dereference Player/Item pointers.
4. New sessions/incarnations must not race an old in-flight save to the same character rows.
   Merely rejecting release for a changed epoch does not prevent an older queued write from
   overwriting newer state. An actually established native lifecycle/write fence is required;
   it is not supplied by this specification. No forced retirement or new cross-realm work.
5. Bounded binding/capture/receipt-consumer capacity is reserved before dirty state is consumed
   or SQL work is submitted. Allocation/saturation/incomplete capture must refuse before submit.
   Ordinary diagnostics currently permit an unobserved native-save fallback; that cannot be
   reused as proof-bearing recovery-save behavior.
6. The trusted native writer must serialize the exact validated owned capture. Whole ordinary
   SaveToDB is not automatically safe: it updates native state, calls OnPlayerSave and consumes
   dirty queues. Its current quarantine guards must not be bypassed to obtain a transaction.
   Callback/re-entry/lifecycle boundaries and parameter ownership need native validation.

No precondition here grants permission to write. The missing producer admission/lifetime/fence
implementation and actual DB execution remain review gates, not boolean assertions to mint.

## Submission, completion and receipt ownership

Seal the exact association before handing owned work to the DB queue. Submit at most once and
attach the exact typed future once. If attachment/capture delivery fails after submission, the
write may already run: hold uncertainty, never report refusal-before-write or issue a replay.
Do not add a recovery-level retry of an ambiguous attempt on top of existing native task behavior.

CharacterSaveObserver is currently a diagnostic sink with two consumers:

- Drain invokes report then acknowledges a terminal record. Report callbacks run under its
  mutex and must not re-enter the observer or perform native owner recovery actions there.
- Reserve can invoke ReclaimSuccessful: ready Committed/Empty records are consumed and ACKed
  without report, so they may disappear before the recovery consumer observes them.

Throwing from report preserves the immediate terminal record, but is NOT a retention guarantee:
a later Reserve can still reclaim a successful record. Therefore a recovery receipt needs a
reviewed, pre-reserved ownership/handoff rule that prevents loss before the trusted consumer
retains the exact bound completion. Ordinary observer behavior is not changed by this contract.
A missing/reclaimed/acknowledged record does not imply success, failure, durability or release.

Completion handling must happen through a validated native consumer, outside DB worker actor
access and observer report locks. Re-resolve/check the exact owner/incarnations/attempt/capture;
never retarget a record to a replacement actor or revise its expected ticket to make it match.
Ordinary pending save/logout intent remains held for its existing native producer; diagnostics
must not consume it or manufacture lifecycle replay.

## Fail-closed release gate

| Condition | Required disposition |
| --- | --- |
| Reserved, unsealed, pending or missing association | Hold; no commit proof or release |
| Changed owner/session/attempt/revision/state domain | Hold; no actor substitution or stale release |
| Missing native lifetime/context/fence/effects proof | Hold before write; no inferred recovery authority |
| Queue rejection, Empty, NotStarted, Cancelled, failed or Ambiguous execution | No recovery release/replay inferred |
| Committed but wrong/unretained receipt or incomplete binding | Hold; execution does not repair missing identity |
| Committed with matching binding | Still require fresh native memory/lifecycle proof and reviewed persistence policy |
| Failed report or lost receipt | Preserve uncertainty; do not use absence as an acknowledgement |
| Owner removal/relog/shutdown/restart | Hold; no borrowed-pointer or inherited-fence authority; policy unresolved |

Even matching Committed execution is not a storage durability acknowledgement. Sufficient
native memory proof, exact captured-state receipt ownership, fresh post-completion currentness,
reviewed persistence/lifecycle semantics and safe pending-intent handoff are all necessary.
Their conjunction is not itself a StableProof conversion or permission to call Finish/unlock.
Release must remain a separately reviewed private native operation, not a public reset API.

An in-process association cannot certify crash recovery after its memory is lost. No new durable
journal/schema/SQL/shutdown policy is selected here; restart/relog/in-flight write handling must
be resolved before activation rather than silently losing the fence and admitting new writes.

## Registered inventory writers

`PB59InventoryWriteFence.h` supplies an unwired, process-local tracker keyed by
character GUID. The caller must use one shared tracker across all writers of the
same character, including across Player/session replacement. A move-only writer
lease is acquired before touching mutable inventory state. An exclusive capture
reservation excludes registered writers and other captures for that GUID. It is
read-only and automatically releases on scope exit. A hot or uncertain GUID
uses one capacity entry. When the fixed table fills, admission for new GUIDs
fails before mutation.

Mark a writer before any handoff that could execute its transaction, then
transfer the lease to the owner of that exact task's typed outcome. Dropping an
unsubmitted lease clears it. A committed, empty, not-started, not-enqueued or
cancelled-before-execution result clears that writer. A failed, ambiguous,
inconsistent or lost result leaves the GUID ineligible for exclusive capture.
Later completions do not clear earlier uncertainty. There is no reset or
recovery-write submission API. Clearing a completed writer only establishes
quiescence for that registration.

The local executable fixture tests the real tracker and database result types.
It includes overlapping writes that finish in reverse order, ownership transfer
to another thread, loss after possible submission, saturation, and concurrent
capture-versus-writer admission.

Before wiring this into native code, map admission, exact outcome ownership and
failure behavior for each writer. `Player::SaveToDB` owns standalone submission,
but `SaveInventoryAndGoldToDB` appends to a caller-owned transaction. Trade can
write both characters in one transaction and modifies inventory before the
append. Both leases must be acquired before either character is touched and
carried through the outer outcome. The same audit must cover mail, auction,
direct item writes including login fixups, and character administration. Some
existing paths submit without a typed outcome. Ordering the observed saves alone
cannot cover those paths, writes already in flight before registration, or work
surviving the process. Capture remains dormant until coverage is established.
The later write path must hold a fence through owned payload submission,
completion and fresh persisted-state verification before originals can be
disposed of.

## Preparing a standalone inventory write

`PB59PreparedInventoryWrite.h` couples one writer registration, one prepared
typed DB task and that task's future. Prepare obtains the GUID lease and
allocates the empty transaction, task and result storage before the producer
builds the payload. The caller must stop appending after dispatch. The DB pool
uses its existing typed queue admission for the prepared task. Dispatch is
single-use, and only the future from that task can settle this handle. Polling
is non-blocking. Discarding an undispatched handle clears its lease. Discarding
it after possible submission retains uncertainty and blocks capture.

This helper is dormant. A future native producer needs an owner-independent
pending store reserved before capture, and must retain the exact handle across
owner retirement until settlement. The pool must stay valid and prepared
through dispatch. Queue rejection or an empty task can establish quiescence,
but neither repairs dirty state if a producer consumed it during capture.
The producer must keep captured data owned and handle refusal without data
loss. No ordinary Player save calls this helper.

The prepared-write fixture runs the real holder, native submission and outcome
methods, and the actual queue. The DB execution and pool setup are doubles.
It checks payload identity, single submission, unpolled completion, reversed
completion, rejection, cancellation, interruption and loss after handoff.

## Keeping pending inventory writes across sessions

`PB59PendingInventoryWrites.h` stores the exact prepared task beyond the
producer call. A slot is claimed before preparation and payload building.
The synchronous builder receives a transaction reference only for that call
and must append an owned payload without consuming dirty state. Build refusal
or exception releases the unused slot and the undispatched writer lease.
Dispatch transfers the handle into the slot before obtaining the native
future. A lost dispatch result records uncertainty in the shared GUID fence.

The process-local service owns the store and polls it outside DB workers.
Polling returns one ready outcome with its GUID and store-local sequence, then
releases the slot. Ready slots are visited in rotation even when earlier slots
are reused. The caller can correlate outcomes for two writes to the same GUID
without confusing their completion order. The sequence is local to this store
lifetime and supplies no recovery provenance or persistence evidence.

Closing admission rejects new requests and refuses any build that reaches
dispatch after closure. Existing tasks remain available for polling until
their exact outcomes arrive. Idle means every slot has been released,
including slots whose ambiguous write still blocks recovery in the fence.
The service must outlive producers and keep the pool and shared fence valid
through shutdown. Dropping an unsettled store conservatively retains
uncertainty in that fence. Native shutdown and cross-process recovery policy
remain separate.

## Capturing item-instance statements

`Item::AppendItemInstanceSave` is a private, const serializer. It appends
the 14 existing item-instance parameters to a prepared transaction in the
serialized owner context. Statement buffers own the strings and scalars
after the item leaves scope. A changed wrapped item also contributes the
existing gift-owner update. The helper accepts NEW and CHANGED and refuses
UNCHANGED and REMOVED. The caller discards its prepared transaction if
building throws.

Ordinary `Item::SaveToDB` shares this serializer for NEW and CHANGED. Its
mutation guard, queue-state transitions, removal and submission still belong
to the original caller. The helper is a candidate for a future read-only
capture, under exact owner/custody and writer admission checks. Inventory
links, bag membership, refund/enchantment timing, gift-row state beyond
the existing update and recovery disposition require separate validation.

## Staging exact inventory links

The private `Player::AppendPB59InventoryItemSave` uses the same physical
slot and update-field checks as the existing AutoGear observation. It
requires the expected live Item at the requested equipment, backpack or
equipped-bag position. Bag position, owner, size, parent field and child field
must agree with the native pointers and GUIDs. The transaction owns the four
existing inventory-link binds followed by the item-instance statements.

An empty or substituted slot and unsupported item state refuse before any
append. This staged path also refuses refund state, active enchantment
timers, trade/loot custody and new wrapped items. The caller supplies the
serialized live owner context and discards the whole prepared transaction
if any item refuses or throws. The helper does not change the update queue
or admit a save. Whole-inventory preflight, competing writers and persisted
custody remain separate checks.

## Preflighting the inventory save queue

`Player::AppendPB59InventorySaveBatch` starts with an empty prepared
transaction and a caller-supplied current native stamp. It matches each
non-null queue entry to an independently reached equipment, backpack or
equipped-bag cell. It validates queue positions, per-item eligibility and
unique persisted item counters before preparing any statements. Queue null
entries are retained as tombstones. The helper also refuses an empty or
over-capacity queue, unsupported container changes, active buyback, refund
or enchantment work, and a dirty supported cell missing from the queue.

An eligible batch uses the established item and link writers in queue order.
The stamp, queue identity and global eligibility are checked before and
after staging. A refusal after staging or any exception requires discarding
the undispatched transaction as a whole. Native item state, queue positions
and pending intents are left intact. The caller supplies serialized owner
lifetime and admission. Owner/session and receipt binding, competing writer
coverage, whole persisted custody and queue settlement remain separate.

## Binding a staged recovery payload to its receipt

`Player::PreparePB59RecoverySavePayload` checks the retained recovery scope
and exact ticket before reading the actor. The live GUID, owner incarnation,
session incarnation, quarantine and native stamp must match the copied
request. It prepares one native typed task and reserves one protected receipt
before building the payload. The private holder keeps that task, receipt and
captured item membership together by value. Membership overflow refuses
before the first bind. The final owner/session/scope and stamp check rejects
a stale payload even when all statements were prepared.

The holder is returned opaque and undispatched. A rejected or dropped
holder destroys its staged task and returns its exact unsealed reservation.
The protected observer record stays reserved until an admitted submission
path can seal it and attach the same task's typed outcome. Single submission,
the native write fence and persisted-state verification remain requirements
for that future path. The holder neither pins the Player nor grants release
authority.

## Handing off the exact protected completion

The dormant holder can bind its prepared task to the protected observer.
Validation, sealing and transfer of the task's sole future happen under
the same observer lock. Invalid or stale reservations leave the task and
receipt unchanged. A second bind cannot take the same future. The ordinary
typed submission entry point refuses a prepared task after that transfer.

This handoff still leaves the task undispatched. Dropping the bound holder
destroys its task first, then drains its own protected cancellation.
A pending observer record means completion is attached. Queue admission
and persisted-state verification remain distinct. The existing staging
factory returns a reserved receipt, and a future native admission path
must establish complete writer coverage and owner lifetime before using
this handoff.

## Preserving the character hold through recovery settlement

The dormant inventory fence can transfer an exact capture reservation to
a recovery write lease. The GUID remains exclusive throughout promotion.
A foreign fence, wrong GUID or previously moved reservation refuses.
Abandoning the lease before possible submission releases the process-local
hold. After submission begins, the trusted exact task consumer can settle
the lease once. Definitive no-write results clear the hold. A commit needs
persisted verification, and an ambiguous, inconsistent or lost outcome
keeps the character held, including after the handle is destroyed.

Promotion and settlement establish process-local exclusion only. The
future native adapter still needs complete competing-writer coverage,
exact owner and receipt binding, an admitted dispatch path and persisted
custody verification. The fixture pairs the lease with the protected
task's own cancellation and closes ordinary admission while it is pending.

## Keeping the exact lease with the prepared holder

The dormant Player preparation path receives the shared fence from its
caller. It acquires and promotes the character's hold before allocating
the task, reserving the receipt or building SQL statements. Refusal drops
only the undispatched payload and its unused reservation. The holder keeps
the lease, exact task and protected receipt together.

Both completion binding and marking possible submission recheck the live
owner/session/ticket and stamp. Once marked, polling consumes only the
holder's protected outcome and settles its own lease. Polling requires no
Player, session or mutation scope. Dropping an undispatched marked task
can drain its exact cancellation. Missing delivery retains exclusivity.

## Owning pending protected tasks

The dormant protected owner accepts a staged payload from its shared GUID
fence. It reserves a slot, revalidates the live operation, binds the exact
observer receipt and marks the hold before handing the task to the pool.
The protected queue entry takes a task whose future has already moved to
its consumer. Rejection and cancellation surface through that same receipt.

After handoff, the owner can poll without the player or session. A definite
no-write result clears the hold and frees the slot. A committed or uncertain
result leaves the exact hold owned for later verification. The owner can
report its adopted work drained while these settled holds are still present.
Close admission before draining and retire producers with staged payloads
before destroying the owner or closing the pool. The future lifecycle
service must keep the same fence and pool available for both writer paths.
Complete writer registration and persisted-custody verification remain
separate integration requirements.

## Tracking native save builders

The dormant native-save entry point reserves pending capacity and a writer
lease before invoking the existing kind of destructive builder. It marks
that dirty state may be consumed while admission is still open. The builder
runs outside the store mutex. Once started, its exact task is handed off
even if the store closes to new work in the meantime. The service must keep
the DB pool available while accepted builders or outstanding tasks remain.
A thrown builder or any noncommitted result retains recovery uncertainty.
The original typed result remains available for reporting, and a legacy
fallback after a possibly destructive build could duplicate work.

The natural standalone seam is the ordinary non-creation, non-logout
`Player::SaveToDB` path after native custody checks and before its transaction
build. The dormant `Player::TryPB59TrackedStandaloneSave` adapter checks the
live owner and native custody restrictions, then calls `SaveToDBWithEvidence`
through the admitted native builder. `SaveToDBWithEvidence` changes timers,
invokes save scripts and drains dirty maps.

When character save diagnostics are enabled, the pending slot carries the
owner and session incarnations, ticket sequence and bounded save membership.
`PollOne` settles the writer and returns the original outcome. The existing
reporter uses the value snapshot after the player is gone. Every tracked
noncommitted result reports its ticket, status and execution code, including
`Empty` and saves with evidence disabled. A pre-build refusal leaves dirty
values with the caller. A builder or handoff failure returns `Uncertain`
with the exact ticket and logs it immediately.
That ticket may have no pollable completion. Do not wait for one or fall
back to legacy save. Even a typed no-write completion keeps capture blocked
after a native builder may have consumed dirty state.

Creation and logout need their own lifecycle handling. The existing world
tick drains diagnostics, and `StopDB` closes CharacterDatabase before its
final legacy diagnostic drain. The dormant
`DrainPB59TrackedStandaloneSavesForShutdown` step closes admission, reports
ready completions and returns whether the tracker has drained. It can
return while admitted work runs, including when called inside that work.
The future service must outlive callers and keep the pool and shared GUID
fence available until active producers return and the tracked work drains.
The database pool then performs its own graceful queue stop and worker join.
An accepted task with a lost handoff result can still be in that queue
after the tracker drains. The protected owner has its own adoption and
drain boundary. Persisted proof needs separate verification.

## Owning both save trackers through shutdown

The dormant save service owns the shared GUID fence and both trackers.
Each producer enters before building or staging and retires after its
submission and any unused payload cleanup. The shutdown poll closes
both admission gates. An admitted standalone build can finish handoff
to the open pool. A protected payload still staging at close is refused
at adoption and releases its staged resources before the producer retires.
The poll remains nonblocking, including when called by a running producer.

The service can poll ready work in ordinary operation. During shutdown
each step reports ready standalone work and returns at most one exact
protected completion by value. The lifecycle caller consumes the returned
completion even on the final step. Drained means every admitted producer
has retired and both trackers have consumed their pending results.
It still requires graceful pool shutdown to finish any accepted queue
work whose ordinary handoff result was lost.

The caller must retain the service and its fence while committed or
uncertain protected holds are needed for verification. Those holds
continue to occupy protected slots after drain. At 256 occupied slots a
new protected request returns Full before staging, and final adoption
rechecks capacity after staging. The legacy logout writer and world
lifetime ordering remain separate integration work.

## Retaining the exact terminal receipt

The protected holder saves the terminal observer record alongside the
operation, capture stamp and settlement before returning its completion.
Definite no-write discards the holder after reporting the result. For a
held result, the service can copy the retained record by the exact
operation, stamp and observer ticket. This remains available after
admission closes and after the observer reuses the slot. It uses value
evidence and needs no player, session, or additional DB operation.

The lookup preserves execution provenance for future persisted-custody
verification. The shared fence and held slot stay in place throughout
lookup. Durable verification and eventual disposition remain separate
private operations.

## Expected values bound to the protected write

The dormant serializer copies the bound parameters of the inventory link,
the instance insert or update, and any wrapped-gift owner update. It uses
the same statements later queued by the protected task. The snapshot
retains the original full item GUID alongside the bound database key.
The batch publishes immutable values only after the complete queue and
capture stamp pass validation. It uses the existing evidence item and
recovery text bounds. An oversized item text refuses staging and the
factory discards the entire payload. Ordinary saves keep their existing
serialization path.

The holder and terminal receipt share these immutable values. A later
comparison must establish the presence of the expected link and instance
rows and account for database widths, signedness and exact string bytes.
An instance or wrapped-gift update may match no row. The gift statement
covers owner and item key, while gift entry and flags remain outside its
write set. Untouched rows and relationships need separate evidence.
The captured parameters describe the intended write; durable observation
and disposition remain separate operations.

## Dormant value-only comparison

`CompareInventoryWriteValues` accepts the immutable expected bindings and a caller-supplied
exact-scope typed row set. It validates statement kinds, arity, original variant types,
single-realm full item GUID/low-key identity, one owner, slot uniqueness, range and UTF-8
representability before comparing. SQL-nullable values are compared without coercing
NULL to zero or empty strings; strings are compared by exact bytes. Missing, duplicate,
unexpected and mismatched rows are explicit failures with item/table/column diagnostics.
A gift-owner binding only establishes that value, not preservation of gift entry/flags.
The typed rows assume separately proven lossless decoding and schema; malformed query
results must not be narrowed into these types. Exact scoped rows must be independently
selected; equal values do not prove an operation, a durable commit, an unchanged neighbour,
bag chain, external custody or exclusion of competing writers. No caller or release authority.

Test driver: `python3 tests/pb59/test_inventory_readback_comparison.py --compiler g++`
(and `--ndebug`, Clang). It uses extracted declarations, not a native server or database.
Local 59/59 checks per five configurations used exact comparator and check blobs with
symbolic statement constants, matching binding variant layout and recovery bound adapters;
the checked-in extraction driver itself still needs exact-checkout execution.

Caller-owned writes require reservations at the outer operation. The
source paths include trade, mail, auctions, guild, item and spell handlers,
TC9, additional saves and refunds. They can mutate inventory before calling
`SaveInventoryAndGoldToDB`. Item and login fixups and module writers still
need coverage or a source-proven exclusion. The new entry point has no live
caller and does not establish complete registration or storage durability.

## Source and executable evidence

Sources: PlayerStorage.cpp SaveToDB/SaveToDBWithEvidence; PB59TransactionAdmission::MutationScope,
Request and deferred-intent handoff; PB59CharacterSaveEvidence/Observer; PB59RecoverySaveProvenance;
DatabaseTransactionResult. The Core88 blocking-contract tests cover guards/typed results, not IO.

Additional actual-header receipt-retention tests (no engine doubles or database):51 checks pass
ordinary and optimized NDEBUG with GCC16.2/C++20/-Wall/-Wextra/-Werror/-pedantic. Synthetic futures
show successful/Empty receipts can be reclaimed before report and after a failed report;
uncertain/failed/pending receipts remain held until the existing reporting path. Exceptional
delivery reclaimed under pressure becomes Ambiguous. This confirms a dependency, not a new
recovery implementation or a defect in the intended diagnostic reclamation policy.

```text
python -B tests/pb59/test_recovery_receipt_retention.py --compiler g++
python -B tests/pb59/test_recovery_receipt_retention.py --ndebug --compiler g++
```

Native resource/pins/compile instructions are in NATIVE_GAME_BUILD_BRIEF.md. No production
runtime code, IO, release, activation, installer, CI dispatch, merge, repin or live changes.
