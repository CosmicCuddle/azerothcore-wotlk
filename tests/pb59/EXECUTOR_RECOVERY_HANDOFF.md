# Extracted executor to collector handoff checks

Owner: CosmicCuddle. Narrow native recovery path, coordination #81 comment7141476499.
Branch `test/codex-pb59-executor-recovery-handoff-20261124`; exact Core84 parent
`8d027154e1d3b1525d5cb867fde44631995e5dd0`. Production source and integration pins unchanged.

## What is exercised

`test_native_recovery_collection.py --bridge` reuses the existing explicit engine fixture,
actual PB59 headers, actual UpdateMask/private Item helpers and actual collector bodies.
It additionally extracts the real ExecutePB59InventoryBatchPlan method and inserts only
test-local protocol hooks into its temporary copy:

1. Prepare the bounded collection frame before PrepareInventoryReservation and MutationScope.
   Failed preparation refuses before admission; it cannot strand a mutated actor.
2. Bind after the real scope, quarantine and recoveryResult lambda exist, before policy/effects.
   Failed binding uses the executor's own recoveryResult, retaining quarantine rather than
   pretending the already-admitted operation was an ordinary refusal.
3. Collect inside recoveryResult after RequireRecovery and recovery GUID construction,
   before scope unwind. Copy only owned diagnostic values from the frame.

The runner checks each insertion anchor occurs exactly once and reverses the substitutions
to reproduce the exact original body. An uninstrumented control uses that same extracted
body with only its qualified function name changed; reversal is checked too. Mutants target
only the instrumented method, leaving the control unmodified. Repository sources are never patched.
The generated fixture and binaries are temporary; normal runner mode retains its original suite.

## Effects observation reuses existing code

The recovery hook calls the executor's existing captureEffects(after) and compares its existing
before/after objects, only while context(true) permits native reads. No second effect model,
native replay or combat/aura restoration implementation is introduced. Booleans describing
observation availability/equality are exported; native effect pointer identities are not.

An uncertain effect operation can report an empty recovery GUID list while its effect image
matches. That still does not establish complete recovery proof. Changed/unreadable effects
and context loss remain unverified. This comparison does not include the executor's separate
cooldown equivalence predicate and cannot replace it. Collector proof labels stay Unknown/
Uncertain; matching images are never promoted to Verified or a Finish capability.

## Executable checks and limits

GCC16.2/C++20/-Wall/-Wextra/-Werror:100 checks ordinary and100 optimized NDEBUG. Six intentional
handoff faults are caught by runtime assertions under NDEBUG; compile/launch errors do not count.
The checks cover actual native ticket/owner/session/revision, Applied/Refused/Restored routing,
three link-failure positions, zero-GUID uncertain effects, pending save/logout preservation,
changed/unreadable effects, lost context, stale binding and synthetic scope loss. Eleven shared
scenarios match the uninstrumented control's outcome/failure/recovery GUIDs, item links, owner
fields, ledger/quarantine/pending state and native policy/transfer/effect-call counters. Refused
collector preparation and failed binding are separate protocol-fault cases, not universal parity.

Owned values survive retirement of the fixture actor/scope/frame. They are historical snapshots;
their ownerPresent flags do not assert that an actor is still alive. Test callbacks are cleared
before returning to the owner and never retain the native scope. Synthetic StableProof/Finish
injection deliberately creates a stale scope; it is not proof of legitimate native reconciliation.
Raw policy hooks and malformed aura/mask injections are fixture faults with unestablished native
reachability. No reported divergence/oracle in Grok's independent PB tests is altered.

Player/Item/Bag/session/map implementations, raw fields, constants/layouts, aura-state internals,
packets/DB and other engine services remain doubles. Source extraction plus protocol hooks is
not a real Player/Game translation unit, module ABI/link, real combat-effects proof or gameplay.
This does not clear any gate in [the native release handoff](NATIVE_RECOVERY_RELEASE_GATES.md):
configured native build still lacks a local environment; recovery-bound save admission/typed
commit association, complete effects/non-field state, durability and gameplay remain unresolved.

```text
python -B tests/pb59/test_native_recovery_collection.py --bridge --compiler g++
python -B tests/pb59/test_native_recovery_collection.py --bridge --ndebug --mutants --compiler g++
python -B tests/pb59/test_native_recovery_collection.py --compiler g++
```

No production runtime caller, release/reset/unlock, save/IO/schema/config/SQL/DBC/client change,
CI dispatch, activation, merge, deployment or new feature. Rollback: discard this test child.
