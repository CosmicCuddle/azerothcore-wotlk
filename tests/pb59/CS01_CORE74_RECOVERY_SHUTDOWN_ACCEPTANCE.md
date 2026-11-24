# PB59 recovery / shutdown acceptance — CS01 temporary cover

Owner: CosmicCuddle. 24 November 2026. Development-only evidence; no live activation, merge, shutdown, DB changes or forced recovery authorized.

## Immutable baseline

Core PR74 `c779e7b4f735cab048f5b351ca6b9053f188787a`; paired Playerbots PR53 `96c579e86443c86780d6f9cd8ef51b7c252b09f8`. Claude reported successful 1549/1549 integrated build and default-OFF startup in coordination #4 comment 7136962374. Keep this exact candidate unchanged for comparison. PR76 is a separate diagnostic child; do not silently repin. PB recovery-stop correction is Claude's lane (#52 comment 7137340399); independent integration testing is Grok's lane.

## Source observations, not a shutdown execution

Read the exact Core74 files and matched 17 expected source fragments across 8 files. No native shutdown, DB operation or gameplay test was performed.

| File | Exact blob prefix | Relevant observations |
| --- | --- | --- |
| src/server/apps/worldserver/Main.cpp | 808deb591c12 | 322–331 map teardown RAII; 372–382 kick all, one session update then network stop; 417–436 shutdown hooks and normal return |
| src/server/game/Server/WorldSessionMgr.cpp | eb57d4b5e3b3 | 44–59 singleton destructor directly deletes active/offline sessions; 144/209 normal update skips quarantined players; 244–255 KickAll |
| src/server/game/Server/WorldSession.cpp | c73d2b4ac51f | 170–184 destructor calls LogoutPlayer then closes socket; 388–389 update retains quarantined owner; 704–710 logout records intent and returns when mutation blocked |
| src/server/game/Maps/MapMgr.cpp | 46f627a10515 | 328–335 unload and delete each map |
| src/server/game/Maps/Map.cpp | 7542ef3e7225 | 175–183 guarded Player deletion; 1068–1083 removal via teleport; 2332–2342 InstanceMap::UnloadAll requires no players |
| src/server/game/Entities/Player/PB59InventoryReservation.h | f0265bf1bcd1 | Process-lifetime in-memory key registry, Forget erases record |
| src/server/game/Entities/Player/EquipmentCaptureState.h | bece088d9ef7 | Destructor forgets prepared reservation; quarantine not a durable journal |
| src/server/game/Entities/Player/PB59TransactionAdmission.h | 6c3bf34b767f | CanRetire needs Idle, no pending intent; RequireRecovery latches non-idle phase; Finish requires conjunction of native proofs |

Paired PB53 PlayerbotMgr.cpp blob dc2a4bce624f: 821–823 and 958–960 normal bot retirement requires lease; 295–305 timed mailbox shutdown reports unresolved requests without native drain. This is useful normal-operation protection, not proof of process shutdown safety.

The normal shutdown session-update pass can retain a quarantined session; the later map teardown and singleton destructors do not show a corresponding durable recovery handoff. A quarantined owner can fail logout or teleport/removal while teardown assumes removal. Possible consequences include unresolved custody, lost in-memory evidence and failed teardown assertions. No actual crash, lost item, use-after-free or DB inconsistency is established here. Existing Core74 contract already explicitly excludes process shutdown as recovery. This is concrete source evidence for that existing release gate, not new gameplay scope.

## Acceptance matrix (all native rows OPEN)

| Case | Required proof |
| --- | --- |
| A: ordinary successful command then normal logout/restart | Exact original custody, permitted save, clean unload, persisted/reloaded items |
| B: Refused and Restored, pending save/logout | No further economic action until deferred native intent safely replays; no duplicate save or lost intent |
| C: RecoveryRequired, empty GUID list | Stop all optional mutations; preserve quarantine; effects-only uncertainty remains visible |
| D: RecoveryRequired, known original GUIDs | Retain original identifiers and state; no deletion, recreation, mail, retry or unsafe release |
| E: Timed/explicit bot logout, client handover, offline expiry | Preserve owner identity and prevent premature teardown; record blocked outcome |
| F: Planned shutdown with quarantined bot (disposable fixture ONLY) | Prove safe operator escalation and reconciliation before teardown, or explicit safe failure behavior; no claim that a restart recovers gear |
| G: Process loss (synthetic crash/reopen fixture ONLY) | Distinguish last durable character state from unsaved effects; prove recovery record/backup strategy; no duplicate items, no blind replay |
| H: Incomplete/ambiguous evidence or unavailable DB | Fail closed; no automatic release, no invented result |

A, B, E may exercise existing native contracts on a disposable test tuple. C–H must remain release holds wherever proof is absent. Never test deliberate crash/shutdown on the owner's live server. Existing operator records/backup policy remains protected.

## Proposed next design review (not implementation authority)

1. Define minimum recovery evidence and owner-context collection before a dangerous teardown: exact tuple, operation identity, owner/session incarnation, original item GUID and slot facts, outcome, uncertainty and pending save/logout. Do not store raw pointers or publish private data.
2. Separate the verified-in-memory restoration decision from durable character persistence. A zero recovery-GUID count is not a clean bill of health.
3. Determine safe planned shutdown behavior and restart/reconciliation with owner approval before altering server-wide shutdown policy or adding persistence schema. Do not introduce a reset command, timeout, automatic unlock, save bypass, process-kill workaround or blanket shutdown veto.
4. Use disposable synthetic data to prove the matrix and rollback. Full integrated source and gameplay verification is still necessary.

## Handback

Codex's baseline remains immutable. CS01 covers required Core integration work until the owner confirms Codex's return; review PR76 and this matrix together on handback. Discard the isolated branch to undo this note and its diagnostic patch. No production changes or migrations.
