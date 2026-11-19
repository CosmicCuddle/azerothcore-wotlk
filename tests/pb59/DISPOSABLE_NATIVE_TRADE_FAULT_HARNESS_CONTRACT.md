# PB59 crossrealm trade: native disposable-database fault-harness contract

Status: **DESIGN / RED**. This is a review checklist, not an implemented harness, a production migration, a passing native test, or authorization to wire `TradeHandler.cpp`. Parent: PR #53 (`b3ebe68f` at time of drafting). Keep the existing trade handler unchanged until the gates below are demonstrated on an isolated Core build and disposable databases.

## Why the standalone fixture cannot prove settlement

The four futures model **two realms × two independent writes**: source-item deletion and final inventory/gold save. Four driver-level `Committed` statuses do not make four independent commits atomic. A process crash can occur after any one of them, and a client can disconnect while the other owner is still alive. A worker callback cannot safely dereference `Player*`, `WorldSession*`, `Item*`, or a ledger just because a `std::future` has become ready.

The bridge's `wait_for(0)` is nonblocking only for the **promise-backed** Core `AsyncCommitTransactionWithOutcome` result. The type `std::future<DatabaseTransactionResult>` does not encode that provenance. The future passed to `Attach` must be created by the verified DB promise path; `std::async` is prohibited in a native producer because destruction can join its worker. `SealOutstanding` is in-memory and is not a recovery journal.

## Required disposable native harness seams

1. Build a separate test Worldserver with **two disposable character databases** and explicit test-only fault injection; never point it at the owner's live databases. Snapshot baseline characters, item GUIDs, bags, mail, currency and account entitlements before each case.
2. Introduce a **test-only driver interception seam** for each of the four transaction submissions, identifying realm and phase before enqueue and after the driver outcome. Verify actual native source order from `TradeHandler.cpp` at the pinned Core head. Do not alter the production handler to make a fixture pass.
3. Introduce a **test-only owner-context scheduler** capable of pausing before first mutation, after each commit, before/after the completion callback, and at logout/destruction. All access to a live `Player` must be on its authorized owner context, with a separately verified lifetime hold; value-only worker results are delivered without native pointer captures.
4. Generate a nonzero, nonrecycled trade generation and bind it to two distinct, currently alive `OwnerIncarnation()` values. Reject mismatched identities, missing owner, stale generation, duplicate phase result and callbacks after owner retirement. Never use a client-supplied generation.
5. Persist a **durable, idempotent recovery intent** before irreversible operations. Specify transaction and crash consistency for the journal itself, not merely for the character tables. Capture source item GUID/quantity, destination ownership, gold before/after, both owner incarnations, all four phase identities, and a deterministic reconciliation key.
6. Inject DB outcomes per phase: committed, not enqueued, cancelled before execution, deadlock/rollback, broken promise, ambiguous execution, and process death before callback. Run all four phase positions and 24 completion orders. Reopen the databases after restart and compare durable state, not just in-memory review status.
7. Simulate both logout orders, character deletion/redirect, disconnect during commit, duplicate callback, server shutdown, and a replay of the same journal key. Assert **no duplicated item GUID, no duplicated currency, no silent item loss, and no client trade-complete acknowledgement before durable reconciliation**. A failed/unknown operation must not be blindly retried.
8. Validate existing Vanilla level-60, Individual Progression, paid TBC/WotLK passage and custom Hunter behavior remain unchanged. Trade recovery must not create a bypass through mail, bots or alternative ownership paths.

## Minimum fail-closed acceptance

- **Pre-side-effect refusal:** when either owner cannot be pinned or both cannot be admitted, neither owner's inventory, gold, trade window, DB rows nor journal is modified.
- **Post-side-effect uncertainty:** quarantine both parties and retain a durable recovery record; never issue an unverified success, refund or replay.
- **Four driver acknowledgements:** still require an independent durable state reconciliation across both realms and source-item custody before completion.
- **Crash/restart:** deterministic idempotent reconciliation must reconstruct ownership without live `Player*` and must not depend on callbacks from the prior process.
- **Scope:** native harness remains RED until implemented and executed; existing `test_two_realm_commit_future_bridge.py` is a standalone typed C++ model only. Its expected 1027 checks at `b3ebe68f` are not a recorded compile PASS.

## Evidence required before native integration approval

Record exact Git SHA and database schema snapshot, compiler/sanitizer versions, fixture output, fault case ID, injected commit phase, durable before/after item and currency rows, recovery journal state, owner-lifetime evidence, and restart outcome. Report first failing case. No merge, production SQL/DBC, configuration change, AutoGear activation or deployment without separate owner approval.
