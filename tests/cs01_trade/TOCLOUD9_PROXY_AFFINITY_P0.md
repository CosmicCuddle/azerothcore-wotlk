# CS01 / P0 — ToCloud9 multi-realm transaction-affinity blocker (20 Nov 2026)

**Review-only / no runtime changes.** This file is stacked on Core DRAFT PR61 for coordination and independent testing. It does not modify the external proxy, MariaDB, server, or owner configuration.

## Source evidence independently fetched

Exact upstream [walkline/ToCloud9 `7fa8f2c7c56d3f3db06c801d8c9f2a8e761a83fc` `apps/mysqlreverseproxy/proxy/proxy.go`](https://github.com/walkline/ToCloud9/blob/7fa8f2c7c56d3f3db06c801d8c9f2a8e761a83fc/apps/mysqlreverseproxy/proxy/proxy.go):

- `HandleQuery(START TRANSACTION)` sets `TransactionRequested` without executing BEGIN.
- `startTransactionIfNeeded` on the first dummy realm-context statement calls **`p.ConnByRealm(0).Execute(StartTransaction)`** and only afterwards records the supplied `transactionRealmID`. The non-dummy branch also calls `ConnByRealm(0)`.
- `HandleStmtExecute` dispatches prepared DML to `Stmts[realmID]`.
- `HandleQuery(COMMIT/ROLLBACK)` dispatches to `ConnByRealm(transactionRealmID)`.
- `ConnByRealm` falls back to an arbitrary map connection if the requested realm is missing; this is an additional fail-open risk for custody writes.
- `extractGUIDAndRealmID` defaults GUID-less statements to realm 1 outside an in-progress transaction.

**Concrete invariant violation:** With distinct configured backends realm 0 and realm 2, BEGIN is issued on backend 0, DML runs on backend 2, and COMMIT runs on backend 2. Backend 2 has not begun a transaction and can autocommit each statement; a later rollback cannot undo earlier DML. This is a **source-level P0 finding**, not a confirmed incident on the owner's proxy.

## Test artifact

`tocloud9_proxy_realm_affinity_test.go` is designed to be copied into the exact upstream proxy package beside its existing `MockDBConnection` in `proxy_test.go`. It sets two mock connections (0 and 2), issues START, realm-2 context, and COMMIT, then requires BEGIN and COMMIT both on realm 2. **It is expected to FAIL against upstream `7fa8f2c7`.** It is not compiled in the Core tree and must not be included in a Worldserver build. Claude should run this in a disposable ToCloud9 worktree with the upstream Go module dependencies; record Go version, actual failure, and a positive control with a local uncommitted candidate fix. Do not push to upstream or change owner proxy without approval.

An independent **source-equivalent minimal Go reproduction** was executed locally with Go `GO111MODULE=off go test -v`: it fails as expected with `BEGIN on realm 0, COMMIT on realm 2`. That model is not the actual proxy binary; it is supporting evidence only.

## Required safe remediation design (not implemented)

1. On first realm context, **validate the realm exists** (no fallback) before BEGIN.
2. Issue BEGIN on the *same* backend connection used for all subsequent statements, and COMMIT/ROLLBACK on that same connection.
3. Reject any different realm identity while the transaction is active; do not silently route cross-realm DML into the wrong transaction.
4. On failed BEGIN, invalid context, failed DML, lost connection or uncertain COMMIT, mark outcome unverified and require readback/reconciliation; never emit `TRADE_COMPLETE`.
5. Validate proxy transaction affinity and durable result forwarding in a disposable multi-realm MariaDB test with logs before attempting item/gold custody.
6. Confirm **owner-installed proxy exact revision and realm map**; upstream source is not proof of the owner's deployment.

**Core PR55's four asynchronous commits, scheduled autosave admission, durable snapshots, exactly-once grant/refund, and restart reconciliation remain RED.** Do not run A6, enable crossrealm trade, migrate SQL, merge or deploy without CosmicCuddle approval.
