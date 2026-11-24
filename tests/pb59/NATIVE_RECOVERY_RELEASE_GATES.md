# Native recovery release gates

Owner: CosmicCuddle. Narrow completion handoff for coordination #81 comment7141476499.
Source: isolated Core82 child `feature/codex-pb59-recovery-original-values-20261124`,
parent `afd670fc7fc2bc8244a67718ca8cc38e975d7556`. No integration tuple change.

## Actual native compilation gate

No configured CMake build/compile_commands.json or cmake/cl/docker executable was found in
the inspected workspace/PATH. No environment installation or configuration was performed.
An exploratory syntax-only compile of the real PlayerPB59Recovery.cpp with the existing
GCC16.2 MinGW compiler, real Player.h and source/public dependency include roots stopped at:

```text
src/server/game/Entities/Unit/Unit.h:35:10:
fatal error: boost/container/flat_map.hpp: No such file or directory
```

This is a failed, unconfigured native probe, not a supported Windows build or an exhaustive
dependency inventory. An initial overly broad include search selected unrelated jemalloc
internal/MSVC headers and was discarded; corrected roots include the in-repository public
Detour/G3D headers. No dependency stub, security change, package download, CI dispatch or
production host was used. Local response file/output are in the workspace audit artifact
`core82-real-player-compile-probe-20261124/`; absolute local paths are not needed for handoff.

Smallest useful next native gate: in an existing approved development build environment,
compile the real `game` target with this exact child source, its generated build settings,
supported compiler and pinned module dependencies. `src/server/game/CMakeLists.txt` defines
that static target. `deps/boost/CMakeLists.txt` requires Boost>=1.78 on Windows or>=1.74
elsewhere, plus filesystem/program_options/iostreams/regex (thread when configured modules
require it). This does not establish that Boost is the only missing dependency.

```text
cmake --build <existing-approved-build-directory> --target game --parallel 2
```

Command is a proposed development gate, not an executed build. In that configured build,
the narrow first diagnostics should come from the actual PlayerPB59Recovery.cpp object;
then game and Worldserver/module linking must pass. Do not substitute fixture Player/Item,
type-only extracts, generated fake headers or ad-hoc dependency stubs. Preserve Core74/PB56
acceptance pins separately; CS01 owns combined acceptance/GRM, Grok owns independent PB tests.

## Minimum runtime collection path, still dormant

Reuse the existing executor's admission, before-images and retained MutationScope rather
than introducing a second executor or a new combat-effect model. Future reviewed wiring:

1. Reserve collector storage before native admission/movement, using the real move plan.
2. Bind after the actual scope and quarantine are established, before fallible effect work.
   Scope.GetTicket and native revision are the provenance; caller-supplied IDs are insufficient.
3. Copy recovery evidence inside the existing recoveryResult path while that same scope and
   serialized owner context are retained, before unwind. Lost sentinel permits value-only
   failure reporting, never a call through the retired owner or saved Item pointers.
4. Reuse the executor's existing captureEffects/nativeCombat before-image logic for effect
   comparison; journal phase bits alone are not effects proof. No duplicate effect implementation
   or replay of uncertain non-idempotent effects is justified by the new field snapshots.

This child supplies original-item and affected-bag raw update-field values plus update-state
observations, including unqueued instances. Missing/replaced objects remain incomplete.
Unchanged values do not prove auras, cooldowns, non-field state, complete queues or persistence.
No runtime call site or release path is implemented by this handoff.

A subsequent isolated test child exercises this ordering with temporary hooks in the actual
extracted executor and the real collector, alongside an uninstrumented control. See
[the handoff fixture](EXECUTOR_RECOVERY_HANDOFF.md). This validates executable fixture flow,
not the blocked real Player/Game build, native effects ABI or runtime activation.

## Operation-bound persistence gate

Current SaveToDB(bool,bool) records a pending standalone-save request while quarantined,
then InventoryMutationAllowed prevents native save construction. Unsettled originals also
defer ordinary saves before BeginTransaction. SaveToDBWithEvidence has the same mutation gate.
PB59TransactionAdmission::Request defers standalone saves in RecoveryRequired, retaining intent.
Therefore calling ordinary SaveToDB from a recovery frame does not produce a recovery commit.

When enabled for ordinary saves, the existing observer reserves owner/session/save-sequence,
captures dirty IDs, seals, submits the typed commit future and attaches it. Its record does
not bind the recovery ticket/revision, full original values or effect before-image. Committed
is execution evidence, not storage durability; deleted GUIDs are included in dirty membership.
ObserveSave deliberately cannot convert this record to Recovery::PersistenceEvidence.

The missing path is a reviewed native recovery-save admission and immutable association at
the actual save capture/Seal/commit boundary, after in-memory evidence is sufficient, with
exact operation/ticket/revision and captured state held through typed completion. Existing
execution ambiguity, missing records, stale revisions and failed writes must stay unproven.
Do not manufacture that association from a later matching GUID/save sequence, bypass quarantine,
Finish early to obtain save permission, consume pending intent or retry an ambiguous commit.
Such admission/IO/release wiring remains a separate safety decision; none is added here.

The focused follow-up [blocking contract and typed delivery fix](RECOVERY_SAVE_BLOCKING_CONTRACT.md)
exercises actual save guard prefixes and the real typed observer, including a reproduced future
exception containment gap. It does not create the missing operation-bound recovery-save admission.
Its document records the final test rerun still pending after approval-review usage exhaustion.

## Evidence distinction

The source-pinned draft PR reports extracted-native executable counts and mutant results.
Actual collector/Item helper/UpdateMask bodies are exercised with explicit engine doubles;
raw field storage/lookup remains a double. That is not a real Player/Game build, DB write,
native effect reconciliation, durability acknowledgement or gameplay validation. No merge,
activation, deployment, SQL/schema/config/DBC/client change or forced recovery is authorized
by any result here. Rollback: discard this isolated child.
