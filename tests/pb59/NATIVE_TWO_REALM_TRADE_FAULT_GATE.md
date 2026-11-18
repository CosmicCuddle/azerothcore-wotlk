# PB59 native two-realm trade fault gate — review specification (18 Nov 2026)

**Status: RED / NOT IMPLEMENTED.** This document is a test plan, not a
Worldserver test result, production permission, or an assertion that the
existing trade path is safe. Parent Core PR53 at `f07cefde3e7413b951336dcb21ee9b1670f8f590`
has only isolated value-future tests (931 checks/config **expected at that
head**, not independently compiled). This follow-up changes no handler.

## Exact native write sequence to instrument

Source `TradeHandler.cpp` (parent PR52) has independent crossrealm
`CharacterDatabase.CommitTransaction(trans)` submissions at approximately
545 and 578 (source item deletion), then 657 and 661 (inventory and gold
saves), before trade completion at 673–675. **Four submissions are not an
atomic transaction.** A worker's `Committed` status proves neither the
other three commits nor durability of a two-owner settlement. Source
locations must be revalidated against the final integration head.

## Mandatory native ownership contract

1. Acquire **both** live Player lifetime authorities on their serialized
   owner context *before* first item, gold or DB side effect. Capture two
   nonzero, distinct `TransactionAdmission::OwnerIncarnation()` values
   and a non-reused trade generation. A GUID or equipment stamp alone is
   insufficient. Reject same-owner, stale owner, sealed/logout/redirect,
   active economic work, or partial admission before mutation.
2. No database worker captures or dereferences Player, session, item,
   inventory, trade window, or ledger. It only fulfills a typed
   `DatabaseTransactionResult` promise; the owner-context consumer
   revalidates both live owners before touching native state.
3. Preserve actual source-item custody through every early delete, clone,
   move, rollback, callback and logout path. **Never** send
   `TRADE_COMPLETE`, delete OriginalGear, retry an ambiguous commit, or
   free either owner based solely on four driver acknowledgements.
4. After the first irreversible side effect, failure must enter durable,
   identity-keyed reconciliation. A process-local `RecoveryReviewRequired`
   flag cannot survive restart. A native recovery journal and exact
   idempotent replay/compensation contract must be designed separately.
5. A deferred logout or shutdown must not silently release the two-owner
   lifetime authority while an outstanding result could still mutate
   inventory. If retention cannot be proven, fail closed before mutation.

## Fault-injection matrix (all must be native integration tests)

| Fault point | Required observable outcome |
|---|---|
| First owner admission succeeds, second fails | No inventory/DB side effect; first scope unwound |
| First source deletion rejected before enqueue | No trade complete; no destructive retry; recover/abort per custody proof |
| First deletion commits, second rejects | Persistent recovery record, no trade complete, no duplicate grant |
| Both deletes commit, first final save fails | Persistent recovery; exact source custody and gold audit |
| Three writes commit, fourth ambiguous | Persistent recovery; never classify as success |
| All four driver results committed | Still **not** proof of crossrealm atomic settlement; reconciliation gate required |
| DB result arrives after one Player logout/destruction | No stale pointer dereference; old incarnation rejected |
| Player GUID reused for new login | New incarnation cannot consume old result |
| Same realm/phase callback duplicated | Fail closed; no overwritten status |
| Results arrive in any of 24 orders | Phase identities preserved; no premature completion |
| Worker promise abandoned / broken | Unknown result, recovery required; no guessed rollback |
| Shutdown between any two write submissions | Durable journal can reconstruct exact attempted/confirmed phases |
| Crash after final write but before completion packet | Restart reconciles without granting items/gold twice |
| Item clone fails mid-transfer | Native rollback restores original item ownership or retains recovery quarantine |
| Callback recursively requests save/logout/trade | Admission prevents independent mutation; no lifetime escape |
| Altbot or non-RNDBOT AutoGear context | No PB59 AutoGear gear strip; OriginalGear preserved |

## Evidence needed to change RED to GREEN

- Pin exact Core/Playerbots/IP hashes and custom modifications; protect
  Hunter, paid TBC/WotLK access and Vanilla cap 60.
- A native instrumented test executable using actual Player/inventory and
  database worker scheduling, including ASan/UBSan lifetime coverage.
- A disposable two-realm DB fixture with restart/crash injection and
  per-owner item GUID/gold conservation checks.
- Independent reviewer logs for every fault row, including first failing
  assertion and all compiler/build flags.
- Explicit owner approval **after** source, integrated, gameplay and
  recovery evidence. Never merge, enable AutoGear, alter SQL/DBC/config or
  deploy from this document.

**Current outcome:** all rows above are **NOT RUN**; PR50/51/52/53 remain
draft/unmerged, PR51 native gate intentionally RED, AutoGear OFF, N-Questie
Workstream B OFF.
