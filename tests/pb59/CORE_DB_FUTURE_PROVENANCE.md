# PB59 Core database future provenance — source audit (18 Nov 2026)

**Scope:** review-only evidence for draft PR #53 at `495200e63b0e0ac54b953f208cfa9e2597752f90`, stacked on PR #52. This document does not wire a trade handler, grant owner lifetime, authorize trade completion, or prove native Worldserver behavior.

## Verified source chain (exact parent blobs)

1. `DatabaseWorkerPool.h` blob `eac8ebb042d833ac1182821d613e3d37e3447b04`, line 184: `AsyncCommitTransactionWithOutcome(SQLTransaction<T>)` returns `std::future<DatabaseTransactionResult>`.
2. `DatabaseWorkerPool.cpp` blob `8d5ed036ef219ae3c58ac2ceaaf224aa7877c439`, lines 301–310: creates `std::make_unique<TransactionWithOutcomeTask>`, takes `task->GetFuture()`, transfers task to the queue only after `_typedTransactionsReady && _queue->TryPush(task.get())`; otherwise invokes `task->Reject()`. Returns the result future by value. **No `std::async` appears on this source path.**
3. `Transaction.h` blob `e83cd05e6c05c871e52f12faabf0d8326cf3715c`, lines 113–127: `TransactionWithOutcomeTask` owns `std::promise<DatabaseTransactionResult> _result`; `GetFuture()` returns `_result.get_future()`. No Player/session pointer or callback is captured by the value-future mechanism.
4. `Transaction.cpp` blob `a0335dfc1836f57f657d96dbcb39ffe6f936e6e1`, lines 159–184: task destructor publishes `AmbiguousCode` if started and not completed, `CancelledBeforeExecutionCode` if never started; `Reject()` publishes `NotEnqueuedCode`; `Execute()` marks started, calls `ExecuteWithCode()`, publishes typed result. This is execution-outcome evidence, not crash durability.
5. `DatabaseTransactionResult.h` blob `1f3ce4d16fefba7a514914af71a8539381303c5a`, lines 17–47: `Committed` is driver acknowledgement, while other outcomes are not safe replay proof.

**Conclusion limited to this exact source path:** futures produced by the named Core API use a `std::promise` shared state, rather than `std::async(std::launch::async)`. Dropping an outstanding consumer future therefore does not itself join an async task. The DB worker may still be executing; dropping the future is not cancellation, settlement, recovery, or a lifetime pin. The bridge's `wait_for(0)` is only a nonblocking readiness check.

## Native integration gates — still RED

- Verify **every** attached future is directly produced by the above API, with no `std::async` wrapper or other future source. The bridge's `std::future` type cannot enforce provenance.
- Verify `_typedTransactionsReady`, queue admission, worker shutdown and task cancellation under the actual integrated Worldserver lifecycle, including partial execution and exception paths.
- Establish serialized owner-thread delivery, independently pinned two-Player lifetimes, unique trade generation, phase/realm correlation, and a retained recovery authority surviving logout/shutdown.
- Establish durable idempotent reconciliation across four separate crossrealm transactions before any `TRADE_STATUS_TRADE_COMPLETE` or item custody decision. Current native `TradeHandler.cpp` blob `9584fb99919060614b9481d7db3cadd9dfb81920` submits early source deletions at lines 545/578 and final inventory/gold saves at 657/661, then sends completion at 673–675. This remains an **unmodified, unverified native path**, not evidence that PR #53 protects it.
- Compile full native Core/Playerbots/Worldserver and execute fault-injected two-owner trade, logout, split commit, rollback and crash/restart recovery tests in an isolated environment.

The existing PR #53 fixture's reported 886 checks/config under GCC/Clang normal+sanitizers are **standalone header tests**, not evidence of the above native gates. AutoGear remains OFF; no production/SQL/DBC/config changes or merges are authorized.
