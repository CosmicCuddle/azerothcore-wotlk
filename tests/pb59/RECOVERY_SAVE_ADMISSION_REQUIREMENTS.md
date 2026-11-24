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
