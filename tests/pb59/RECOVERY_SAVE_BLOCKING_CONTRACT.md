# Recovery-save blocking contract and typed delivery fix

Owner: CosmicCuddle. Assignment: coordination #81 comment7142198686.
Branch `fix/codex-pb59-recovery-save-contract-20261124`; exact Core85 parent
`72efa4a5c74e9ad0636f08e561e7a00770db849b`. Core74/PB56 acceptance tuple unchanged.

## Small safe implementation

The real CharacterSaveObserver::Drain caught only std::future_error from future.get(),
whereas its ReclaimSuccessful path already catches all delivery exceptions. A legal typed
future containing std::runtime_error escaped Drain without reporting an outcome. Reproduced
against the actual parent header with a std::promise<DatabaseTransactionResult>: escaped
runtime_error, report count0. This is an observer API reproduction; native DB-worker delivery
of that exception and a live server failure are not established.

Drain now contains exceptions only around future.get(), leaving the already-initialized
Ambiguous execution result. Complete/report/acknowledgement then follow their existing path.
Report callback exceptions remain outside this catch and preserve the terminal record for
later reporting. No success, retry, rollback, recovery admission or durability is inferred.
This is the only production-source change; it matches the existing reclaim policy.

## Executed save admission and completion contract

Optional runner mode --save-contract uses the actual executor/collector fixture and actual
save guard prefixes from SaveToDB(bool,bool) and SaveToDBWithEvidence. Extraction stops before
BeginTransaction or the append body. Counters replace these boundaries, transaction type,
scheduler and logging infrastructure are explicit doubles; no database/SQL executes.

The tests confirm ordinary saves in unsettled state defer before transaction construction;
all save variants during an active native inventory reservation are cancelled before IO;
retained quarantine after scope unwind still blocks construction/append. Ordinary save intent
is retained, caller-owned save admission is rejected, and no early Finish or quarantine reset
is used to obtain save permission. Low-level guard prefixes are not a substitute for their
native caller's admission/transaction ownership contract.

Real CharacterSaveObserver Reserve/Seal/Attach/Drain, CharacterSaveEvidence, typed result and
ObserveSave implementations are exercised with synthetic promises. Tests cover pending and
terminal delivery, execution statuses, stale serial, missing records, broken promises,
exceptional delivery and report-callback failure/retention. Synthetic records are NOT saves
issued by the quarantined actor or proof that a DB task ran.

## Exact remaining recovery binding blocker

A matching sealed/terminal observer record identifies owner, player/session incarnations,
save sequence and observer ticket. It does not identify the recovery operation ticket or
evidence revision and captures dirty membership rather than the full reconciled state.
The test deliberately passes the same record to two different hypothetical recovery-ticket
values: ObserveSave can report execution in both cases. This is the current read-only contract,
not a native admission or an approved association. SaveProvenance cannot convert to the
prototype PersistenceEvidence. Committed execution is not a durability acknowledgement.

Therefore no immutable recovery association can safely be minted from a later matching GUID,
sequence or successful future. The missing native boundary remains: sufficient in-memory
custody/effects/queue evidence, reviewed recovery-save admission while quarantine stays held,
and exact operation/ticket/revision/captured-state association at capture/Seal/typed completion.
Native Game/Worldserver compilation and actual gameplay/DB validation are still required before
that IO/lifecycle path can be implemented and activated safely. No new binding API, bypass,
save/release caller, persistence policy or speculative effect model is introduced here.

## Exact verification state at this checkpoint

- Source fix and earlier fixture:180 ordinary +180 optimized NDEBUG checks passed with
  GCC16.2/C++20/-Wall/-Wextra/-Werror; actual observer and guard-prefix implementations,
  explicit remaining engine doubles. The earlier standalone parent-header failure was reproduced.
- First three contract mutants were rejected by runtime assertions. The fourth (observer-ticket
  guard removed) failed compilation on an unused-parameter warning; it was NOT counted as a kill.
  Mutation was corrected to explicitly discard that parameter without disabling warnings.
- The final fixture adds non-std exceptional delivery. The final ordinary/NDEBUG and four-mutant
  rerun was NOT executed: automatic approval review hit a usage limit. No final186/4 PASS claim.
- CPP style retains only the four inherited diagnostics (BattlegroundAV.h56/797,
  MoveSpline.cpp224, Spell.cpp3901); whitespace is clean. SQL style was attempted but its
  required git fetch failed DNS resolution in the sandbox; no current SQL style PASS claim.
- No configured approved native development build was identified in the inspected workspace/PATH;
  the existing compiler was reused for header/fixture tests. Native compilation's existing Boost
  dependency gate is not retried here. See NATIVE_RECOVERY_RELEASE_GATES.md.

Pending commands once execution approval review is available:

```text
python -B tests/pb59/test_native_recovery_collection.py --save-contract --compiler g++
python -B tests/pb59/test_native_recovery_collection.py --save-contract --ndebug --mutants --compiler g++
```

This checkpoint remains incomplete pending final validation/publication. No merge, repin,
activation, hosted CI dispatch, deployment, live/schema/config/SQL/DBC/client change or forced
recovery. Rollback: discard this isolated child; original source branches remain intact.
