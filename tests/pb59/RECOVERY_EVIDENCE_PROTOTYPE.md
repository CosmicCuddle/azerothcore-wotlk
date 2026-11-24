# AutoGear recovery evidence and proof boundary — dormant prototype

Owner: CosmicCuddle. Assignment: coordination #81 comment 7138488409.
Branch `feature/codex-pb59-recovery-evidence-20261124`, exact parent Core76
`e90600d4d30c5d5d2a904a5fd4e9df2dc4f321c1`. Core74 `c779e7b4` and the paired
PB53/PB54 integration candidates remain unchanged. CS01 owns PB development,
acceptance and source-tuple approval; independent integration testing remains Grok's lane.

## Retained diagnostic

Core76's exact-original-pointer check remains in recoveryResult. Value equality alone
does not clear an original from the diagnostic GUID list: the attached pointer must
also be the original pointer. Re-executed the existing extracted-lambda fixture locally
with GCC16.2/C++20/-Werror: patched 8/8, old expression 6/8 with the expected wrong/null
pointer failures. These inconsistent doubles are not proof of a reachable native defect.
No additional movement/outcome/quarantine change was made for this step.

## Value-only inspection

`PB59RecoveryEvidence.h` is not included by Player or any runtime producer. It accepts
copied values, has no allocation or IO in its record/inspection operations, and exposes
no reset, release, save, replay or item creation API. Evidence is trivially copyable and
at most 48 KiB: 19 originals and 19 reported GUIDs, with 512 text bytes per item observation.
CopySlot refuses larger text without modifying its output; it never truncates metadata.
Do not serialize raw struct bytes: padding is not a persistence format.

The record keeps the reported native outcome/failure, operation owner/ticket and owner/session
incarnations, evidence revision,
full bounded original/current item facts, observation availability, instance-observation
quality, effects/queue uncertainty and pending intents. Item facts preserve creators,
random properties, enchantments, charges, text including embedded NULs and other native
instance fields. A copied ExactNativeInstance claim is not a transferable lifetime proof.
The native collector has not been implemented or wired by this prototype.
The reported failure is retained as supplied; it does not reconstruct a forward failure
that the existing executor replaced with Restoration. A None/unknown failure accompanying
RecoveryRequired is inconsistent evidence and cannot pass inspection.

Inspect validates counts before indexing, identity/version, duplicate originals/observations,
reported GUID membership/duplicates and canonical facts. It retains every issue bit and
uses this deterministic primary-status order: invalid, stale, owner unavailable, session
unavailable, missing facts, custody unproven, effects unproven, queues unproven, pending
intents, then InMemoryCandidate. Derived unproven GUIDs retain original record order.

Empty reported GUIDs do not imply clean effects. Missing observations remain unknown,
not empty inventory. Even InMemoryCandidate is an inspection classification of a
RecoveryRequired record; it does not become Applied/Restored or clear quarantine.

## Reconciliation boundary for contract review

`PB59RecoveryProof.h` is also unwired. Assess requires an inspectable record, the exact
operation/revision and independent serialized-context, retained-lifetime, owner/world,
full-custody, complete-effects and exact-queue proof claims. A missing claim fails closed.
No result converts to TransactionAdmission::StableProof and no function invokes Finish.
Booleans supplied by a command or copied from a record are not a native proof collector.

MemoryAssessment::VerifiedForReview and PersistenceAssessment::ConfirmedForReview are
separate labels. Pending, failed, missing, stale, unknown or sequence-zero persistence
claims cannot become confirmed. The prototype checks supplied provenance fields; it does
not authenticate a DB acknowledgement. Even both positive labels confer no unlock power.
A character-inventory commit also does not establish that transient combat effects were
persisted or that relog/restart reconstructs them correctly.

A future native collector must, before any permitted release is considered:

1. Check the retained ledger sentinel and Recovery access for the exact owner incarnation
   **before** dereferencing Player; require the serialized native context. A dead owner
   yields unavailable evidence with no post-loss native read, lookup or write.
2. Verify owner/session/map/world identity and observation freshness. Re-resolve only
   through the approved lifetime/context mechanism, never from an old raw pointer.
3. Re-prove every full original instance, native pointer and container/slot link in the
   current retained context. A GUID or copied facts alone cannot establish exact custody.
4. Independently compare native combat modifiers, aura/application/set/cooldown state,
   visible fields and save/update registrations against the original operation evidence.
   Existing executor observations are a starting point, not proof of later recovery.
5. Retain pending save/logout intents and distinguish an in-memory observation from an
   exact operation-bound native DB outcome. Do not bypass the guarded save path or reuse
   an unrelated successful commit receipt. Existing save-observer provenance must be
   examined before designing any persistence adapter.
6. Present explicit evidence for review. Do not infer permission to call Finish, repeat
   non-idempotent effects, recreate/move/delete originals or force native retirement.

## Native shutdown trace and admission prototype

The following source blobs were rechecked locally against preserved Core76; they match
CS01's existing [shutdown acceptance note](CS01_CORE74_RECOVERY_SHUTDOWN_ACCEPTANCE.md):

| File | Exact blob |
| --- | --- |
| worldserver/Main.cpp | 808deb591c12e013f0fc9e108e9c74a433361334 |
| Server/WorldSessionMgr.cpp | eb57d4b5e3b3b1c72afa117cc50446ab5272bab2 |
| Server/WorldSession.cpp | c73d2b4ac51f93a1e5cd486e87e4bba129f97eea |
| Maps/MapMgr.cpp | 46f627a10515001f7edfd956ccdbf4359a267400 |
| Maps/Map.cpp | 7542ef3e722591affe54a5de6a4ee6589fc9fb41 |
| Player/EquipmentCaptureState.h | bece088d9ef70ae2adffa7087757c31f36ce7e01 |
| Player/PB59TransactionAdmission.h | 6c3bf34b767f58c72232145c143263cc66d60fdd |

Main's socket-management teardown calls KickAll and one UpdateSessions before stopping
networking; the map-management teardown calls MapMgr::UnloadAll, which unloads/deletes maps.
Normal session cleanup retains quarantined owners; the session singleton destructor later
directly deletes sessions. WorldSession destruction calls the guarded logout. Per-owner
guards therefore do not establish safe process teardown or durable recovery handoff.

InspectRetirement is a testable **per-owner observation prototype**, not a shutdown policy.
Missing/stale context, quarantine/outstanding recovery, pending intents and a non-idle
ledger hold that owner. An ordinary clean owner, including one with no prior batch ticket,
can only proceed to CheckExistingNativeAdmission: the actual producer must still acquire
its existing lease/check. The model does not veto unrelated owners or bypass any native gate.
Nothing in Main, MapMgr or session teardown is changed or calls this prototype.

Before any runtime shutdown wiring, owner review must settle evidence retention across
planned teardown/process loss, approved persistence/restore provenance, and what the
operator should do when evidence is incomplete. A process-wide delay/abort policy, a new
schema or external recovery IO is not selected here. Reaching process shutdown is never
treated as successful reconciliation. CS01's native acceptance matrix remains open.

## Tests, environment and rollback

Standalone GCC16.2/C++20/-Wall/-Wextra/-Werror/-pedantic checks validate bounded copying,
all instance scalars, empty GUID effects uncertainty, missing/duplicate facts, owner/session
loss, stale operation/revision, pending intents, missing native proofs, persistence
distinctions and per-owner retirement admission. Seven one-defect mutants remove custody,
effect, queue, lifetime, commit sequence or instance-quality guards; each must be caught.

```text
python -B tests/pb59/test_recovery_evidence.py --compiler g++
python -B tests/pb59/test_recovery_evidence.py --ndebug --compiler g++
```

This is executable prototype evidence, not native Worldserver/DB/gameplay or shutdown
validation. The local Windows environment has a standalone workspace GCC; Docker/CMake
were not found and WSL reports not installed. An exact Core/PB54 disposable native build
cannot be performed in this environment. No system installation or hosted CI was attempted.
The previously reported Core74/PB54 build remains evidence for its own immutable tuple.

No runtime include/caller, schema, external IO, config/SQL/DBC/client change or gameplay
rule change. Development rollback: discard this child, retaining Core76 and original
contributor branches. Do not deploy, activate or silently repin an integration candidate.
