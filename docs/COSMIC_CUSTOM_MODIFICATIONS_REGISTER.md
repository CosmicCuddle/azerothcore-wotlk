# CosmicCuddle — AzerothCore Custom Modifications Register (DRAFT)

**Owner:** CosmicCuddle. **7 November 2026; Core4 reviewed 8 November.** All source/versions/risks/tests/decision/rollback are tracked per change. This is NOT a live/installed Core inventory.

## Core player-settings paid receipt malformed-raw preservation (first owner Core fork port)
- **Intent/owner:** preserve existing `ip_paid_expansion` character-settings source row EXACTLY when malformed; prevent old permissive parser from dropping tokens, silently shifting paid slot values or overwriting untrusted raw bytes on login/get/save/offline update. All other Core settings sources retain existing permissive semantics. No paid grant and no price/era policy selection.
- **Source/provenance:** Core fork `CosmicCuddle/azerothcore-wotlk`, exact Grimfeather parent `0d754d1921261dfd939f91060ebc022eae280f71`. Exact original disposable tested patch in IP `tests/core_patches/preserve_paid_receipt_row.patch` at `17aaeadb3d1c017bbb56a557073d9e89f82f5288` applied strictly to source hunk line contracts. Original Core `PlayerSettings.cpp` blob `f1eb86367ce69111867ef7187125f5fe456c5367`, `Player.h` blob `5a29ac7b40ade7a11f1fcf36623dc7530ea39595`.
- **Affected paths:** ONLY Core `src/server/game/Entities/Player/PlayerSettings.cpp`, `src/server/game/Entities/Player/Player.h`, one source test, inherited actual-C++ mock DB harness + four supporting stub headers, one source/harness workflow and this register. This is a **global Core runtime guard** even while the paid feature flag is OFF; because it affects damaged paid rows, review its compatibility before deciding to merge. Other `source` namespaces must not change.
- **Dependencies/risks:** IP paid eight-slot vector, Character Services purchase/billing, owner-effective Core `CONFIG_PLAYER_SETTINGS_ENABLED`, real `character_settings` rows and any unknown owner-host custom code. Ordinary Worldserver/autosave, offline direct update, reload after admin repair and raw malformed row must be preserved. Owner first-use/legacy receipt A1/A2, GM bypass and excluded-account payment rules in [Coord #18](https://github.com/CosmicCuddle/Naxxramas-Server-Coordination/issues/18) remain unresolved. No SQL/DB mutations in this branch.
- **Tests/status:** source mutation checks and original `core_settings_harness.cpp` fake-DB **actual `PlayerSettings.cpp` translation unit** C++17/C++20 GCC/Clang, normal/NDEBUG workflow prepared; results must be separately checked. Existing historical disposable combined native Worldserver builds compiled the raw-guard patch but are not a new-fork exact-head PASS. New Core full native with IP/PB/Naxx/Services, real MySQL/character rows, purchase/relog/refund/recoverability and real clients **NOT RUN**.
- **Default/live/rollback:** No config, world/characters/auth DB, SQL/DBC/client or production host change; DRAFT/UNMERGED, no paid activation or owner production approval. Reject DRAFT for rollback. Before any install, require exact existing Core/custom source inventory, recoverable DB/config/binary backups, review of potential noncanonical legacy rows, rollback and explicit CosmicCuddle approval.

## Core F14 default-FALSE uninvoked script hook — isolated child of paid #1
- **Intended behaviour:** expose one Core script API/query callback that defaults to `false` with no registered positive implementation and **NO runtime caller**. Nothing is hidden: do not change direct/batched Player/Creature visibility, human Blood Elves/Draenei, Altbots, NPC quest/loot/interaction, client WHO, stage or GM permissions.
- **Owning component/source:** owner `CosmicCuddle/azerothcore-wotlk`, child development branch from Core paid #1 exact `1e37f3343cf08618101b625601cd299f0a1770ca` on original pinned `0d754d1921261dfd939f91060ebc022eae280f71`. Historical disposable proof interface in IP #123 `tests/core_patches/apply_f14_viewer_suppression_attribution.py`; only its independent default-FALSE API subset is ported, NOT its unsafe-to-port PlayerUpdates or callback observer assumptions.
- **Affected runtime files:** `src/server/game/Scripting/ScriptMgr.h`, `src/server/game/Scripting/ScriptDefines/WorldObjectScript.h`, `src/server/game/Scripting/ScriptDefines/WorldObjectScript.cpp`. Tests: `tests/cosmic_f14_unwired_hook/test_f14_unwired_hook_source.py`, own source CI; technical design `docs/COSMIC_F14_CORE_UNWIRED_API_20261107.md`. Core #1 paid Player.h/PlayerSettings.cpp source remains unchanged on the child.
- **Dependencies/risks:** Future ScriptRegistry virtual ABI requires exact native link with IP, PB, Naxx, Services (and installed additional modules). `CALL_ENABLED_BOOLEAN_HOOKS_WITH_DEFAULT_FALSE` exists in pinned original Core. This hook is inert; future invocation before full rights/actor/old-pair ledger would be UNSAFE, so do not wire `Player::UpdateVisibilityOf` or use it as evidence of map-thread exclusivity. PB #3 first creation is not durable; original Claude IP #94 restoration is unwired; no in-game test.
- **SQL/DBC/client/config:** NONE changed. All F14/paid flags remain OFF; no database or character write. No owner-live installation or approval.
- **Tests/lifecycle:** source CI added with strict exact-parent file set; at initial commit the new Core F14 API has **no exact-head native Worldserver compile/ABI test** or 3.3.5a client proof. Update the evidence only after checking completed run. Rollback: drop this child DRAFT, leaving paid Core #1 and original baseline untouched; before future install require installed source/config/SQL/DBC inventory, recoverable backups and separate owner approval.

## Core F14 client creature CREATE/OUT query — dependent Core4 (8 Nov 2026)

- **Intent:** Child [Core F14 DRAFT #4](https://github.com/CosmicCuddle/azerothcore-wotlk/pull/4) extends the original Core #2 default-FALSE script callback by adding a strictly per-viewer possible creature query in **both** Player::UpdateVisibilityOf overloads, CREATE and OUT (four call sites). It **does not actually conceal anything** on default builds. The preceding Core #2 entry's statement "NO runtime caller" is accurate only for that immutable older Core #2 SHA; the new child carries a **compile-time-OFF** consumer, not a positive listener.
- **Owner/source:** `CosmicCuddle/azerothcore-wotlk`, new branch `feature/cs01-f14-creature-client-viewer-seam-off-20261108`, parent `d2066943e19cd959de2a0e9897135573e4124219`. Older Core #1/#2 and separate Core #3 source-only work unmodified.
- **Runtime paths:** `src/server/game/Entities/Player/PlayerUpdates.cpp` and new pure `src/server/game/Entities/Player/F14CreatureClientVisibilityQuery.h`; related `ScriptMgr.h`, `ScriptDefines/WorldObjectScript.{h,cpp}` **comment changes only**. No modification of world detection, target combat, generic phase or Core paid receipt bodies.
- **Build/config/SQL/DBC/client:** `COSMIC_F14_CREATURE_CLIENT_VIEWER_QUERY_EXPERIMENT` is **undefined by default**, hence OFF; if explicitly compiled ON it can only consult the existing script query, which remains FALSE without a separately installed, approved positive listener. NO production config, world/character SQL, cleanup file, DBC, MPQ, client or DB change.
- **Negative query guards:** noncreature player/corpse/gameobject, GM-mode viewer, charmed/owned creature, pet, summon, guardian and totem are not queried. These are not independently sufficient to hide a hostile NPC; correct firstborn genuine headless RNDBOT identity, IP earned-era, human quest/tap/loot/DoT/pet/assist rights and exact map-safe previous-hidden restoration must be verified first.
- **Tests & evidence:** five precise-source tests, one 128-case C++ precondition suite over GCC/Clang C++17/20 normal/NDEBUG, inherited **six** original Core default-FALSE API tests updated to allow only a compile-time-OFF consumer, and a dependent actual five-owner full native Worldserver S4 test [Services DRAFT #26](https://github.com/CosmicCuddle/mod-character-services/pull/26). **New exact-head CI remains pending**, not game tested or owner installed. See [full review note](COSMIC_F14_CREATURE_CLIENT_VIEWER_SEAM_OFF_20261108.md).
- **Live state/risk/rollback:** DRAFT/unmerged/default OFF, no owner-host inventory or backup, no live test. If a true callback is ever introduced prematurely it can hide rights-bearing NPCs, orphan client state or break loot/pets. Discard/revert child. Before any approved install preserve ignored and untracked custom SQL/cleanups, actual binary/source/config/DBC/client versions and restore-tested database copies; owner gives final consent.

## Core F14 per-viewer WHO exact-GUID query — dependent review DRAFT (8 Nov 2026)

- **Owner and base:** CosmicCuddle, `CosmicCuddle/azerothcore-wotlk`, child of Core F14 client CREATE/OUT [DRAFT #4](https://github.com/CosmicCuddle/azerothcore-wotlk/pull/4) exact `4e0a3452ea50299e9a80cb250927417d3cc4b652`; original paid receipt, creature suppression and six inherited tests remain unchanged.
- **Intent:** prepare one observer-specific, read-only WHO exclusion query immediately before both WHO counters are updated, using the cached target GUID as a lookup key and race 10/11 ONLY as a narrowing precondition, never identity proof. The default `WorldObjectScript` virtual method and dispatcher both return FALSE absent a future separately reviewed positive listener; ALL normal builds compile the handler query OFF through undefined `COSMIC_F14_WHO_GUID_QUERY_EXPERIMENT`.
- **Affected runtime:** `src/server/game/Handlers/MiscHandler.cpp`, `src/server/game/Scripting/ScriptMgr.h`, and `src/server/game/Scripting/ScriptDefines/WorldObjectScript.{h,cpp}`; source tests/workflow and this register. No IP/PB/Naxx/Services changes, no SQL, schema, DB, DBC, client, MPQ, config or owner host modifications.
- **Negative preconditions:** no script call for GM/security-elevated observers, empty/self GUID, races other than BE/Draenei; regular `SMSG_WHO` filters, matchCount/displaycount, serialization and maximum remain untouched with macro OFF. The future policy MUST prove positively signed headless RNDBOT GUID, never real human/Altbot/legacy, and actual observer effective Vanilla stage, with same policy as world-object visibility.
- **Risks:** WHO cache is a snapshot, not a source of true Playerbot identity or durable first-created DB receipt. Enabling a true listener without GUID trust can incorrectly hide human/Altbot entries and desynchronise WHO counts; virtual ABI requires exact Core/IP/PB/Naxx/Services native link, not just Python tests. F14 NPC/human rights, corpse restore, first-birth stage8 and real game acceptance are unresolved.
- **Tests/live state/rollback:** new source assertions + original Core2 creature regression updated only to scope its callback (because WHO adds a second default-FALSE dispatcher), while all six original safety tests remain + Core4's five tests; exact-head CI and five-owner native/gameplay tests must be verified separately. DRAFT, OFF, not merged, installed, enabled or gameplay-tested. Drop this review-only child to roll back, preserving Core4 untouched. Any later host installation requires owner approval, offline source/config/ignored+untracked SQL and character DB backups, and verified rollback.

## F14 real per-viewer PLAYER character client query (8 Nov 2026; review DRAFT/OFF)

- **Owner, parent:** CosmicCuddle owner AzerothCore child of Core #5 `5dca9d74bb1b676dedeffaa5aa92c128c9a31778`. Original creature and /who script queries, source regressions and paid receipt safeguards preserved.
- **Intent:** prepare an explicit real-player-target client CREATE/OUT script query in both `Player::UpdateVisibilityOf` overloads. Ordinary visible players short-circuit `Player::IsVisibleGloballyFor` before older script hook. New client-only helper is independent of global detection/combat/phase.
- **Paths:** `PlayerUpdates.cpp`, new pure `F14PlayerClientVisibilityQuery.h`, `ScriptMgr.h`, `ScriptDefines/WorldObjectScript.{h,cpp}`, original Custom Modifications Register, tests/docs/CI. No SQL/DB/DBC/MPQ/client/config changes.
- **Default/security:** `COSMIC_F14_PLAYER_CLIENT_VIEWER_QUERY_EXPERIMENT` undefined/OFF, new `WorldObjectScript` hook returns FALSE and no positive IP listener is registered. Preconditions: target Player different from viewer, viewer non-GM/non-headless, actor headless race10/11 and non-GM. This is NEVER positive RNDBOT proof; human and Altbot remain protected, viewer effective Vanilla era and human rights are not checked yet. Experimental macro MUST remain OFF until those independent proofs, interaction/quest/loot/combat and client restoration exist.
- **Dependencies, risk, test, rollback:** independent current IP/Playerbots GUID identity, paid/stage-floor/group safeguards and client WHO/NPC coherence; client-only hiding is inadequate for safe active gameplay. New source/native pure model and retained original Core source tests, followed by exact five-owner native Worldserver CI + owner-approved offline 3.3.5a two-human gameplay and rollback. DRAFT, no tests/host claimed at creation. Discard child to roll back; preserve all tracked/untracked/ignored local SQL cleanup/migrations, installed source/binaries/config/DB/DBC/client with restore-proven backup before any deployment.

## F14 Core6 future viewer visibility-pass provenance model (9 Nov 2026; TEST ONLY/OFF)

- **Owner/parent:** CosmicCuddle / CS01 Maintenance; immutable owner Core DRAFT #6 `6a0c1dba53617c77353718eb4338fda634fca5eb`. Separate Claude CS03-IP E1 fix remains assigned, pending delivery. No Core runtime implementation or external module behavior changed.
- **Intent:** create a test-only model and exact real-source checks for the **three existing** batched `VisibleNotifier` routes (standard scan, normal relocation, shared vision) and the separately unbatched direct `Player::UpdateVisibilityOf(WorldObject*)`. Future visible token lifetime must be born before near/far visits, cover zone-wide additions, end/cancel with the stack notifier, reject nested/reordered/replayed tokens, require actual independent human login epoch, and never infer a client ACK from a send-call attempt.
- **Affected paths:** `tests/cosmic_f14_pass_source/F14CorePassScopeProvenanceModel.h`, `f14_core_pass_scope_model.cpp`, `test_core_f14_pass_source.py`, new scoped CI workflow `.github/workflows/core-f14-pass-source-model-20261109.yml`, `docs/COSMIC_F14_PASS_SOURCE_MODEL_20261109.md`, and this append-only register. **Zero `src/`, conf, SQL, database, DBC/MPQ/client, production or gameplay paths edited.**
- **Dependencies and known risks:** model has no real Core callback/producer/Worldserver or client send evidence; real shared-vision and direct/immediate paths need separate thread/lifetime/identity verification. Core6 current F14 actor/WHO/NPC policy is FALSE/OFF; human/Altbot rights and quest/tap/loot/pet/DoT/assist/corpse restoration unresolved. Do not integrate into Claude E1 or clear the owed-restore ledger from any test-model observation. Token overflow/nesting/epoch mismatch must stay conservative.
- **Tests and live state:** strict six-path exact-parent diff, actual owner-source codepath tests, native GCC/Clang C++17/20 normal/NDEBUG pure model and retained Core6 negative tests via new CI; verify final hosted status before claiming success. No native Worldserver, owner S5 source/SQL/DB/client inventory, backup+restore, S6 observed client behavior, merge, install or feature activation. **Rollback:** discard only this review branch/PR and retain immutable Core6 customizations, historical files and SQL.

## F14 actual Core VisibleNotifier batched pass lifetime event producer (9 Nov 2026; DRAFT / COMPILE-TIME OFF)

- **Owner/parent/source:** CosmicCuddle; CS01 Maintenance; immutable parent [Core #7](https://github.com/CosmicCuddle/azerothcore-wotlk/pull/7) `891e3a622901a525e325b597e52edf2271a98a41`, itself Core6 `6a0c1dba53617c77353718eb4338fda634fca5eb`. Separate assigned Claude E1 pass-ended replay and IP positive viewer/paid authority untouched.
- **Intended behaviour:** when **separately explicitly compiled** with `COSMIC_F14_NOTIFIER_PASS_LIFECYCLE_EXPERIMENT>=1`, the genuine per-human non-headless in-world `VisibleNotifier` begins a globally unique nonzero monotonic atomic pass token **before** near/far traversals, announces one batch packet-send-call attempt **after** `SendDirectMessage` only if `HasData`, and unconditionally closes on the real stack notifier destructor including empty early-return. Duplicate notifier copying is forbidden under flag; inherited normal/shared-vision relocation uses same base. In ordinary Core builds all this is **COMPILED OUT** and new script events have no listeners. Immediate `UpdateVisibilityOf(WorldObject*)` bypasses notifier and is NOT attributed.
- **Affected paths:** `src/server/game/Grids/Notifiers/GridNotifiers.h` and `.cpp`, new `src/server/game/Grids/Notifiers/F14VisibilityPassTokenSource.h`, `src/server/game/Scripting/ScriptMgr.h`, `src/server/game/Scripting/ScriptDefines/WorldObjectScript.h` and `.cpp`, `tests/cosmic_f14_pass_source/test_core_f14_pass_source.py`, new `tests/cosmic_f14_pass_source/test_core_f14_notifier_pass_lifecycle_source.py`, new `tests/cosmic_f14_pass_source/f14_core_notifier_token_native.cpp`, new one-workflow dedicated `.github/workflows/core-f14-notifier-pass-events-off-20261109.yml`, new `docs/COSMIC_F14_CORE_NOTIFIER_PASS_EVENTS_20261109.md`, and this append-only register. **No PlayerUpdates, Unit, Core query suppression, conf, SQL, DB, DBC/client, packet construction, player data, quest/loot logic, or owner-host change.**
- **Key risks/independent dependencies:** batch send-call attempt is NOT per-creature GUID CREATE/OUT evidence, network delivery or client ACK; must not release prior-hidden NPC restore debt, grant a stage or hide a human/Altbot/GM. Token does not prove user login epoch and consumers MUST reject stale, nested, reused, unknown or post-end callbacks. No direct immediate path tag, Core refresh ABI, Claude E1 replay correction, per-GUID/incarnation, NPC combat/death/despawn, tap/loot/quest/pet/assist/DoT, first birth/stage8, paid rules, map/WHO concurrent reader guarantees or successful live client. Keep all F14 options OFF and do not fake `COSMIC_F14_VISIBILITY_REFRESH_HOOK_VERSION`.
- **Tests/status/rollback:** exact 12-path diff vs Core7 and source old-absence/new-real-events, inherited Core7 provenance source tests, actual new atomic token header GCC+Clang C++17/20 normal/NDEBUG including 8-thread race and near-wrap, no runtime Core Worldserver compile claimed until separate owner-five native gated CI, no S5 installed module/tracked+ignored+untracked SQL and restorable backups, no S6 client. **DRAFT/unmerged/not deployed/NOT activated. Rollback:** abandon this branch/PR; all Core7 and owner files/SQL remain intact.

## F14 Core9 exact uncontrolled-creature batch GUID candidate send-call sidecar (9 Nov 2026; DRAFT/OFF)

- **Owner/source:** CosmicCuddle, CS01 Maintenance. Child of owner [Core8 #8](https://github.com/CosmicCuddle/azerothcore-wotlk/pull/8) exact `2e2a21ae2224a9738cd92abf00bab957dbfb7604`; prior Core8 production OFF. This is a new opt-in SOURCE instrument, **not Claude IP E1** and not an approved player/NPC hide.
- **Intended behaviour:** new compile-only `COSMIC_F14_CREATURE_BATCH_GUID_EVIDENCE_EXPERIMENT>=1` requires Core8 `COSMIC_F14_NOTIFIER_PASS_LIFECYCLE_EXPERIMENT>=1`; no normal build defines either. In actual `Player::UpdateVisibilityOf<T>(T*,UpdateData&,visibleNow)`, after an exact real uncontrolled/non-owned creature `BuildCreateUpdateBlockForPlayer` / `BuildOutOfRangeUpdateBlock` append, nominate the ObjectGuid+direction into the SAME packet's value-only sidecar. `UpdateData::BuildPacket` is byte-identical/unmodified. A real parent pass token and completed nonempty batched `SendDirectMessage` allow ScriptMgr to broadcast exact GUID+direction **send-CALL attempt**, NOT client receipt. No registered consumer; no state mutation or concealment.
- **Boundaries:** only actual non-controlled Creature, never player, pet, summon, totem, guardian, charmed/owned mob, corpse. Duplicate GUID (same or different direction), missing/invalid, over 256 candidates, and `UpdateData::AddUpdateBlock(UpdateData const&)` merge poison **ALL** candidate evidence, never partial restorer proof; Clear resets. The independent direct overload remains unchanged and entirely untagged. Ordinary `AddOutOfRangeGUID` unpaired calls untagged. A future E1 correlator must independently prove source viewer login epoch, live creature incarnation and prior owed F14 restoration; must NEVER accept batch send-call as ACK or settle hidden debt.
- **Affected fourteen files:** `src/server/game/Entities/Player/PlayerUpdates.cpp`, `src/server/game/Entities/Object/Updates/UpdateData.h/.cpp`, new `F14CreatureBatchGuidCandidateLedger.h`, `src/server/game/Grids/Notifiers/GridNotifiers.cpp`, `src/server/game/Scripting/ScriptMgr.h`, `src/server/game/Scripting/ScriptDefines/WorldObjectScript.h/.cpp`, updated `tests/cosmic_f14_pass_source/test_core_f14_notifier_pass_lifecycle_source.py`, new `test_core_f14_creature_batch_guid_source.py` and `f14_core_creature_batch_guid_ledger_native.cpp`, new `.github/workflows/core-f14-creature-batch-guid-off-20261109.yml`, new `docs/COSMIC_F14_CORE_CREATURE_BATCH_GUID_SEND_ATTEMPT_20261109.md`, this append-only register. No changes to original Unit/direct player overload/WHO/serialization/config/SQL/DB/DBC/client, Playerbots, IP, Naxx, Services, production host.
- **Independent generic OUT ambiguity:** The existing `Object::BuildOutOfRangeUpdateBlock` and the independent `VisibleNotifier::SendToSelf` far/visibility sweep may append the same GUID to the actual packet `m_outOfRangeGUIDs`. Before dispatch, require an OUT candidate to appear **exactly once** in the real packet OUT list, and a CREATE candidate **zero** times. Same packet CREATE+OUT or duplicated OUT GUID -> **no F14 GUID evidence for ANY candidate in this packet**. No normal packet or visibility rights changed.
- **Dependencies and tests:** exact 14-path diff vs Core8, source old-absence/new-ON guarded events, after real original batch CREATE/OUT only, original Core8 real notifier pass and Core7 pure/provenance regressions intact; GCC+Clang C++17/20 normal/NDEBUG exercise actual pure GUID ledger with capacity, poison, direction/ambiguity/merge/reset AND actual-out-vector disagreement/duplicate/nominated-CREATE contradiction. Require focused CI terminal then separately actual 5-owner Worldserver link with the **new macro** for opt-in ABI, runtime flags OFF. Neither test proves client ACK, live packet, true Core direct immediate refresh, NPC combat/corpse/loot restoration or original gameplay. No merge/production approval.
- **Known risks/rollback:** Original Core candidate nomination is observer-neutral and can include unowned creatures unrelated to F14; no IP E1 code or observer epoch grants it authority. Current true Player/GUID incarnation and human/Altbot/GM rights still need separate review, and no Core refresh version exists. S5 owner extras/dirty modified/untracked/ignored SQL/DB/DBC/client and tested backup/restore, S6 real two-human 3.3.5a gameplay blocked. Keep F14 flags OFF and **never spoof** missing Core refresh hook macro. Rollback discards this isolated source DRAFT and preserves all owner custom changes/data.


## F14 Core14/Core15 genuine pre-send cause + one-shot notifier admission — 11–12 Nov 2026 (DRAFT/OFF)

- **Owner/source:** CosmicCuddle. Core14 exact head `6dd1589b9067b02f88bb4e04de33818e36bc87c3`, descended from the reviewed Core9 batch-GUID/notifier lineage. Core15 exact head `ee13e1edfdbccd56895f3adefc2b32df94ef19e9`, strict child of Core14.
- **Intent:** Core14 distinguishes a genuinely positive F14 creature-query OUT from ordinary Core invisibility, records only the actual batched OUT candidate, and emits one pre-send callback before `SendDirectMessage` on the genuine notifier pass. Direct OUT is prohibited from initiating F14 creature concealment under the new opt-in path; direct CREATE remains read-only/idempotent and is not client ACK evidence. Core15 adds a non-transferable one-shot `F14InitialOutPassAdmission` owned by the exact `VisibleNotifier`/UpdateData instance, opened only after a valid pass begin and sealed before packet build/send/end, merge or clear.
- **Affected runtime:** `UpdateData.{h,cpp}`, `PlayerUpdates.cpp`, `GridNotifiers.{h,cpp}`, `WorldObjectScript.{h,cpp}`, `ScriptMgr.h`, plus the admission helper header. No SQL/config/DB/DBC/client changes.
- **Safety:** compile-time `COSMIC_F14_PRE_SEND_CREATURE_OUT_EVIDENCE_EXPERIMENT` remains undefined by default. No Core listener grants concealment. Pre-send/server-send evidence is not client presentation/ACK and cannot retire restoration debt.
- **Tests/status:** source contracts and native helper models exist. These ancestors are exercised by later Services native gates. No live gameplay or deployment. Rollback: abandon/revert the guarded children.

## F14 Core16–Core18 current map/session-token lineage — 12 Nov 2026 (DRAFT/OFF)

- **Core16** `9e30516cccfe095f95948ceb0ac1d6ca265c54e4`: guarded `COSMIC_F14_PRE_MAP_REMOVAL_EXPERIMENT` calls a const observer at the first line of `Map::RemovePlayerFromMap`, before threat/world/grid removal effects. It cannot veto teardown or send visibility.
- **Core17** `34ad407467fcf6ea02fcce1595f7c0288966042f`: guarded reconnect-session callback immediately after `p->SetSession(this)` on the keep-existing-Player login-after-disconnect path and before client UI/visibility rebuild. This publishes an authenticated client-session replacement boundary.
- **Core18** `4eea6bb6edc634ef4b4eefab71a27adba29e28db`: exposes the genuine still-open notifier token to a distinct default-FALSE initial batched creature OUT query. Direct visibility paths cannot call this token-bound OUT seam. The underlying token is non-transferable and valid only during the exact original notifier admission.
- **Dependencies/risks:** IP current viewer/session/pass identity and restoration accounting must consume these hooks conservatively. None proves actor rights, client receipt or R2 restoration success. Macro definitions must be uniform across Core/modules in any test build.
- **Evidence:** source contracts are replayed immutably by current Services gates. Services64/Core20+IP165 and Services65/Core20+IP166 later compiled/linked successfully with these ancestors present. No gameplay/deployment claim.

## F14 Core19 token-bound initial CREATE — PARALLEL DRAFT, NOT IN CURRENT CORE21

- **Source:** Core PR #19 branch `feature/cs01-core19-f14-token-bound-create-query-20261112`, a direct child of Core18 `4eea6bb6...`.
- **Intent:** experiment with a separate default-FALSE, genuine-pass-token-bound initial batched CREATE query using the same non-transferable notifier admission. Direct visibility does not gain this token-bound CREATE authority.
- **Critical lineage distinction:** Core20 also branched directly from Core18. Therefore the current Core20/Core21 candidate **does not inherit Core19**. Do not silently merge Core19 into the current candidate during upstream restack; it needs a separate design/review decision, especially because positive concealment and R2 are still blocked.
- **Live/safety:** DRAFT/OFF, no positive listener, no deployment.

## F14 Core20/Core21 kept-in-world disconnect + malformed-query fail-visible — current candidate (12 Nov 2026)

- **Core20** `be46b96a0d2ab73ee22a946305b1308a02751ae9`: under `COSMIC_F14_SESSION_DISCONNECT_EXPERIMENT`, publishes a PlayerScript callback only in `WorldSession::HandleSocketClosed()` after the client socket is detached and before `TradeCancel(false)`, while the existing keep-Player-in-world guard remains true. Kick/logout/taxi/shutdown paths do not take this callback. **Same-account replacement is timing-dependent:** `WorldSessionMgr::AddSession_` kicks the prior session and then probes `HandleSocketClosed()`; if the socket is already observed closed the disconnect callback performs negative revocation, otherwise the old session is deleted and ordinary `LogoutPlayer` performs the negative lifecycle. Do not document replacement as guaranteed to use the disconnect callback.
- **Core21** `991f2c86c928222d501a559a61434cfd15055dc8`: replaces the token-bound initial-OUT query's malformed-input `ASSERT(viewer && creature && token)` with a conservative `return false`. Valid listener dispatch remains unchanged and defaults FALSE.
- **Purpose:** Core20 lets IP invalidate human viewer/session identity when the authenticated client disappears without a normal Player logout. Core21 ensures a malformed future caller cannot turn missing F14 identity into a Worldserver crash.
- **Tests/status:** Core20 has full native evidence through Services64 and Services65 (actual Worldserver compile/link + binary verification). Core21 source/current-regression contracts have passed the pre-build stages of Services67/68; terminal full-native result must be checked separately. No server start or gameplay acceptance.
- **Safety/live:** no positive concealment authority; F14/Paid runtime settings remain OFF. Rollback is source-branch reversion; no migration.

## Current upstream-restack preservation checkpoint — 12 Nov 2026

- **Pins:** protected current Core21 `991f2c86c928222d501a559a61434cfd15055dc8`; current Grimfeather upstream `afd7036bab385e96a2c540272680cc0ec2f7dd4d`; shared merge base `0d754d1921261dfd939f91060ebc022eae280f71`.
- **Conflict surface:** only four source paths are changed on both sides from the shared base: `GridNotifiers.h`, `CharacterHandler.cpp`, `ScriptMgr.h`, and `WorldSession.cpp`. The reconnect `p->SetSession(this)` boundary, kept-in-world `HandleSocketClosed` boundary and `VisibleNotifier` structural anchors still exist upstream, so current evidence supports a controlled manual restack rather than architecture replacement.
- **SQL preservation:** upstream-only existing-server chain contains 23 world updater files plus 1 characters updater file. Separately, the base `spell_proc_event.sql` file is removed. High-impact updater items include creation/migration of `acore_string_locale`, dropping legacy `acore_string.locale_*` columns, dropping obsolete `spell_proc_event`, and a character-pet current-slot cleanup. Current protected and upstream SpellMgr source both load `spell_proc`, not `spell_proc_event`, but installed custom SQL/schema dependencies remain unverified.
- **Required sync method:** real Git three-way merge/rebase on a development branch; manually preserve every register entry and resolve only the four overlapping Core files with source-contract replay. Do not emulate a 92-commit upstream integration by file replacement. Retain all applicable updater SQL until installed DB updater history proves already applied/inapplicable.
- **Release gate:** after sync, rerun exact source contracts, full Core/IP/PB/Naxx/Services native link and controlled gameplay acceptance. Installed source/config/DB/custom SQL/backups and explicit CosmicCuddle approval remain mandatory before production.


## F14 Core23 token-bound initial CREATE reconciled onto current Core21 — 12 Nov 2026 (DRAFT/OFF)

- **Owner/lineage:** CosmicCuddle; Server-Side-Maintenance. Core23 is a strict current-line child of Core21 `991f2c86c928222d501a559a61434cfd15055dc8`. Historical Core19 `0402dda93f2be8649e18ba44c2609aac62cf7cac` remains an untouched parallel Core18 child and is the design provenance for this reconciliation.
- **Intent:** close the current-line provenance gap for a viewer entering range during a future already-hidden eligible RandomBot fight. Under `COSMIC_F14_TOKEN_BOUND_CREATURE_CREATE_QUERY_EXPERIMENT`, the batched initial CREATE branch can ask a distinct default-FALSE script query only while the same genuine non-transferable VisibleNotifier/UpdateData admission is open and only with that exact pass token.
- **Current-line safety correction:** unlike historical Core19, the Core23 CREATE ScriptMgr dispatcher does **not** assert on missing viewer/creature/token. Malformed provenance returns FALSE (visible), matching Core21's hardened initial-OUT dispatcher. Valid CREATE listener dispatch still defaults FALSE.
- **Direct-path boundary:** `Player::UpdateVisibilityOf(WorldObject*)` does not call the token-bound CREATE helper and cannot borrow a pass token. Macro-OFF batched behavior preserves the existing tokenless default-FALSE query. The CREATE macro hard-requires the Core18 token-bound OUT/notifier seam.
- **No positive authority:** Core registers no positive CREATE listener. This child adds no Store Open/Reserve/Commit, prior-hidden debt, packet ACK, restoration completion, fence reset, actor/rights proof, SQL, config, DB, client or deployment behavior. A future module still must prove exact viewer identity, RandomBot actor, creature incarnation, complete pair rights, transactional restore reservation and R2 before returning TRUE.
- **Tests/status:** exact-scope source contract and inherited non-transferable admission model are required first; full macro-ON Worldserver compilation is a separate Services descendant. No gameplay acceptance or production deployment. **Rollback:** abandon/revert Core23; no migration/live-state rollback is required.

## CORE-F14-GUILD-POLICY-SEAMS-20261114 — neutral guild admission/chat hooks (DRAFT)

- **Owner:** CosmicCuddle. **Component:** AzerothCore fork / F14 social-isolation support.
- **Parent:** Core DRAFT #23 head `dbb1dd5150f330d88522a10b85e93546fff72bad` (pre-upstream Phase A); hooks ported from #28 v2, without importing upstream.
- **Intended behaviour:** add two neutral, default-allow script seams only: (1) a pre-mutation guild-member admission veto receiving the immutable guild ID value plus exact member GUID, including offline/off-world adds, while retaining the Guild pointer for world-thread consumers; (2) a per-online-recipient guild/officer-chat delivery veto. Core contains no RNDBOT/race/progression policy.
- **Affected paths:** `Guild.cpp`, `ScriptMgr.h`, `GuildScript.{h,cpp}`, source contract test, this register.
- **Dependencies:** IP may consume the hooks only under its separately reviewed/default-OFF F14 policy. Existing GuildScript APIs remain intact.
- **Risks:** module hook code must be thread-safe for any off-world-thread AddMember caller. Hook version 2 supplies the guild ID as a value so off-world consumers need not dereference Guild merely to queue identity; the Guild pointer must still be treated as world-thread-only for mutable/member reads. Recipient hook is on the normal guild broadcast path and must remain cheap. Addon/Warden guild traffic must remain allowed unless an owning module explicitly and safely decides otherwise.
- **Tests:** source-contract test only at creation; no native Worldserver compile/link or gameplay claim yet.
- **Live status:** DRAFT / unmerged / not deployed.
- **Rollback:** abandon/revert this child; with no consuming hook or no registered script, stock behaviour is unchanged.


## Core74 native AutoGear exact inventory executor (24 November 2026; DRAFT)

- **Intent/component:** Core owns exact combat-slot/storage movement, native policy/effects,
  reverse restoration and four explicit outcomes for the dormant Playerbots AutoGear adapter.
  Owner CosmicCuddle approved bounded shared reservation protection in coordination #81
  comment 7131868276. Technical evidence is in #51, review queue #68 and Core PR74.
- **Source/dependencies:** isolated branch `feature/codex-pb59-exact-inventory-batch-20261124`,
  exact Core53 parent `79b0b1fe2d0fac93227c28329508553cfec15c4d`; version-1 Player API.
  Depends on the inherited PB59 owner/session ledger, original pointer/queue/effect helpers
  and exact dormant PB integration. New `CapturePB59InventoryStamp()` supports headless bots.
- **Affected files:** PlayerPB59Inventory.cpp, PB59InventoryReservation.h,
  PB59CombatRecoveryState.h and existing PB59 headers; Player.h/Player.cpp/PlayerStorage.cpp/
  PlayerUpdates.cpp; Item.h/Item.cpp/Container/Bag.cpp; Unit/CharmInfo.h; Map.h/Map.cpp;
  WorldSession.cpp/WorldSessionMgr.cpp; ScriptMgrMacros.h, Spell.cpp/SpellScript.cpp and read-only
  recovery observations in SpellAuras.h/SpellAuraEffects.h. Existing custom hooks still run.
- **Risks/tests:** shared producer guards activate only for an opted-in reservation/quarantine.
  Uncertain effects remain quarantined, with no automatic replay/reset/disposal. Actual-source
  standalone fixtures pass (986 executor assertions in ordinary/NDEBUG modes, 40 guard checks,
  existing raw/visible/queue/combat regressions); engine/DB doubles are explicit. Full native
  tuple/module compile, independent integration/fault injection and gameplay remain release
  gates. See [native contract and executable evidence](../tests/pb59/CORE74_NATIVE_INVENTORY_BATCH.md).
- **Config/SQL/DBC/client/live:** no changes, no runtime caller, no hosted CI, unmerged and
  undeployed. No F14/Paid or visual-concealment activation. No established feature removed.
- **Rollback:** discard isolated draft or revert coherent executor/dependent adapter commits
  together before activation. Any future deployment requires source/module inventory,
  recoverable DB/config/binary backups, restoration proof and separate owner approval.

## Core dormant AutoGear recovery evidence/proof prototype (24 November 2026)

- **Intent/owner:** CosmicCuddle; ordered Core handback in coordination #81 comment7138488409.
  Bounded value-only recovery inspection, independent native-proof claims and per-owner
  retirement observation. No mutation, reset, unlock, replay or persistence implementation.
- **Source:** isolated `feature/codex-pb59-recovery-evidence-20261124`, exact Core76 parent
  `e90600d4d30c5d5d2a904a5fd4e9df2dc4f321c1`; preserve Core74 c779e7b4 and PB53/PB54 pins.
  Retains Core76 exact-pointer diagnostic separately from the unchanged baseline.
- **Files:** PB59RecoveryEvidence.h, PB59RecoveryProof.h, standalone recovery-evidence tests
  and [contract/source trace](../tests/pb59/RECOVERY_EVIDENCE_PROTOTYPE.md). No runtime consumer.
- **Limits/tests:** fixed 19-item/512-text-byte bounds, fail-closed unknown/stale/missing facts,
  empty-GUID effects uncertainty, pending intents; separate memory and persistence labels.
  Native collector, exact full build/DB/gameplay and safe shutdown remain unproven. See
  source-pinned PR evidence for executable counts; prototype claims cannot authorize Finish.
- **Dependencies/live/rollback:** native operation/lifetime/custody/effects/queue collectors
  and operation-bound DB provenance require contract review. New persistence or global
  shutdown policy requires owner review before wiring. No schema/IO/config/SQL/DBC/client,
  activation, live operation or hosted CI. Discard child to roll back; original branches
  and established custom features remain intact.

## Dormant native recovery collection/provenance adapter (24 November 2026)

- **Owner/assignment:** CosmicCuddle / #51 comment7139518657. Private static Player
  observation helpers, no runtime caller or Playerbots API. No mutation/release authority.
- **Source:** `feature/codex-pb59-native-recovery-collector-20261124`, exact Core78 parent
  `9bfe0d6ffbfe8204b62cd64f2af02eabb638a476`. Existing Core/PB build tuple unchanged.
- **Files/dependencies:** Player.h static declarations; PlayerPB59Recovery.cpp;
  PB59NativeRecoveryCollection.h; PB59RecoverySaveProvenance.h; standalone tests and
  [native collection contract/source trace](../tests/pb59/NATIVE_RECOVERY_COLLECTION.md).
  Depends on actual ledger scope, native stamp/slot observation and existing save records.
- **Behaviour/limits:** pre-reserved bounded scratch, actual Core ticket/revision,
  sentinel/currentness-before-read, creator-thread check, owned value export. Journal/queue
  observations remain Unknown/Uncertain. Save execution/membership never become durable
  recovery proof; missing/stale/ambiguous data remains unverified. No native collector hook.
- **Evidence:** 64 extracted collector checks ordinary/NDEBUG; real save-record constexpr
  contracts and eight static mutants per mode. Explicit engine doubles, no real Worldserver/
  DB/gameplay/durability proof. Source-pinned PR records exact results and open build gates.
- **Capture revision follow-up (25 November 2026):** child of Core90
  `ada575ad023a7d8eb4d1e6bb07407a54f57296bf`. Collection publishes a revision
  only after matching the native stamp at both ends of the scan. Mid-scan drift
  refuses the sample and retains observed save/logout intent. The new fixture
  failed on Core90 and passed on the child, with 484 collector checks per build
  mode and 100 test-only handoff checks. Native build and runtime validation of
  this child remain pending.
- **Inventory writer tracking follow-up (25 November 2026):** isolated Core91
  child, parent `7341b7f73a3bf0be502aa53561d18cb2c7915f11`. The dormant
  `PB59InventoryWriteFence.h` tracks outstanding writes by character GUID
  across session lifetimes. Same-GUID capture refuses until every registered
  writer settles. A lost or uncertain outcome keeps capture closed. Native
  writer registration, exact task binding and a fence held through recovery
  submission and verification are separate dependencies.
- **Prepared inventory write follow-up (25 November 2026):** isolated Core92
  child, parent `ada73c8ec6578c2b9e6598edcce8dd4013899d18`. An unwired holder
  binds one registered writer to its preallocated native task and typed
  completion. The typed queue admission path is shared with existing callers.
  An owner-independent pending store, native writers, owned payload and durable
  verification remain integration dependencies.
- **Pending inventory writes follow-up (25 November 2026):** isolated Core93
  child, parent `b8bedbbd905f5a50338bda30d1e9f7fdf94fbe4c`. A bounded,
  process-local store claims capacity before payload construction and retains
  exact prepared tasks across producer retirement. Closing admission preserves
  already-tracked completions. Lost or ambiguous outcomes hold the relevant GUID
  in the fence. The store is dormant. Native service lifecycle, lossless payload
  construction, competing writer registration and durable verification remain
  implementation dependencies.
- **Item-instance capture follow-up (25 November 2026):** isolated Core94
  child, parent `036a9129c6344dcdca1c84c0b1db8f8f7f54a4fd`. A private const
  serializer appends owned binds for NEW and CHANGED item instances,
  including the changed-wrapped gift-owner update. Ordinary Item::SaveToDB
  shares it. The fixture checks exact bind types and order, capture after the
  source lifetime, allocation failure and ordinary save behavior. Recovery
  integration, inventory links, native build and persisted-state verification
  remain separate dependencies.
- **Inventory-link staging follow-up (25 November 2026):** isolated Core95
  child, parent `b2c7158e9a777d8ae2432cf566491acc5b9b1274`. Stages one
  supported live Item's inventory link and instance after matching the
  expected pointer, native fields, owner and container. Unsupported custody
  or refund/enchant state refuses before append. The existing slot observer
  shares the link reader. Whole-payload admission, runtime wiring and native
  verification remain open.
- **Inventory save batch follow-up (25 November 2026):** isolated Core96
  child, parent `2f39f8d07ac22e6fb438fb26bfcffa0106164476`. The dormant
  batch builder preflights the bounded dirty queue using reachable native
  cells, rejects duplicate persisted counters and unsupported side effects,
  then rechecks the stamp and queue before accepting the staged statements.
  Native owner admission, sealed receipt association, persistence and
  post-commit queue settlement remain open.
- **Recovery payload binding follow-up (25 November 2026):** isolated
  Core97 child, parent `117ea8f3942e2d887e1ad16d6f420243cb53673a`. Pairs the
  exact retained recovery ticket and live owner/session/stamp with a private
  undispatched typed task and one protected receipt reservation. Membership
  comes from the staged batch. Refusal returns the unused reservation.
  Native one-time submission, receipt sealing, persistence and queue
  settlement remain integration dependencies.
- **Protected completion handoff (25 November 2026):** isolated Core98
  child, parent `5d063b65c53484f033bb981f6569205eed543472`. Adds the dormant
  atomic seal-and-transfer step for one prepared future. Ordinary typed
  submission rejects a task after transfer. Discarding an undispatched
  holder publishes and drains its exact cancellation. Native writer
  admission and consumption after a submitted save remain separate.
- **Recovery write hold (25 November 2026):** isolated Core99 child,
  parent `f7ab9402d63facf42e369b8f05983bd473dfc84b`. The dormant fence
  promotes the exact capture reservation without releasing the GUID.
  A definitive no-write settlement clears the hold. Commit, uncertainty
  and lost completion retain it. Native receipt-to-lease binding, writer
  coverage, persisted verification and disposition remain open.
- **Prepared recovery hold binding (25 November 2026):** isolated
  Core100 child, parent `c38a8f194ceff4f87865e01151b6ebd0314096aa`.
  The dormant holder acquires its GUID lease before staging and settles
  it only from its exact protected receipt. Owner identity is rechecked
  before binding or marking. Completion can arrive after owner retirement.
  Native writer registration, dispatch and persisted verification remain
  separate dependencies.
- **Native save build tracking (25 November 2026):** isolated Core101
  child, parent `7b2640dd9eafbf9d9240f80a70289bcd79a59d81`. The dormant
  pending store reserves the exact task and writer before a native builder
  can consume dirty state. An admitted build survives closure to new work.
  Noncommitted or missing outcomes retain uncertainty. Standalone call-site
  wiring, outer transaction registration and native shutdown remain open.
- **Standalone save composition (25 November 2026):** isolated Core102
  child, parent `a63e532cf65496ab38542144a23b348c45659855`. The dormant
  Player adapter uses the native save builder and keeps the optional
  diagnostic membership with its pending task. Polling settles the write
  and reports the original result without a Player or session. Missing
  handoff reports its ticket immediately and keeps recovery blocked.
  The world service, native call sites, and persisted verification still
  need integration evidence.
- **Standalone save shutdown drain (25 November 2026):** isolated Core103
  child, parent `4de148633438eafda6bd7383e1685f3226f17b8f`. A dormant
  drain step closes tracker admission and reports ready completions
  without waiting for active builders. All tracked noncommitted results
  report their ticket and original status even with detailed evidence
  disabled. The pool still owns final queue shutdown and worker joins.
- **Pending protected save ownership (25 November 2026):** isolated Core104
  child, parent `a178e07b005a94b7713d84bf5355770924658d76`. The dormant
  owner retains the protected task receipt and its shared-fence hold after
  the actor retires. One dedicated pool entry accepts the exact task after
  transfer of its future. Definite no-write releases custody; committed
  and uncertain results keep the hold for later verification. The world
  service and verified release path remain open.
- **Shared save shutdown owner (25 November 2026):** isolated Core105
  child, parent `23b9a323e86325081f9295f2d2ae6e31ada42ef3`. The dormant
  service owns one fence for standalone and protected saves and tracks
  producers through cleanup. It reports drained after both trackers and
  their producers finish, retaining settled holds for verification. All
  256 protected slots can remain held, with further requests refused
  before staging. World ownership, logout registration, and persisted
  verification still need integration.
- **Retained terminal recovery receipt (26 November 2026):** isolated
  Core106 child, parent `9480e9020b52045d30da8e3b096b8690e3e71db4`.
  Each retained protected hold keeps its terminal observer record with
  its operation, capture stamp and settlement. The dormant service
  copies a match by exact operation, stamp and observer ticket after
  actor retirement, drain or observer slot reuse. Persistence proof and
  hold release remain separate work.
- **Expected protected inventory values (26 November 2026):** isolated
  Core107 child, parent `68830039d2faf4efda1f8f3fb609f807498b19a9`.
  The staged protected task retains immutable link, item-instance and
  optional gift-owner bindings with its exact receipt. Binding uses
  the existing bounded recovery capture and the ordinary serializer.
  DB row presence, normalization and durability remain future evidence.
- **Dormant inventory value comparison (26 November 2026):** Maintenance
  takes over the paused coding lane from exact Core108 `b38eabcc9e01c9b60a7bbc65f419d58febce54ea`.
  `PB59InventoryReadbackComparison.h` compares captured SQL bindings with independently
  supplied typed link, instance and gift rows. Rejects ambiguous keys, duplicate rows,
  missing or unexpected rows, incorrect binding kinds/types, unrepresentable widths,
  nullable and byte-exact differences. Single-realm item GUID representation is checked.
  Local exact header/test blobs passed 59 checks per GCC/Clang debug and optimized mode,
  plus Clang ASan/UBSan, using declaration adapters. No full Core build or DB evidence.
  Comparison does not prove receipt ownership, full custody, preservation of untouched gift
  fields or durable readback; no caller and no hold release. Discard child to roll back.
- **Dormant readback request ownership (26 November 2026):** Core109 child
  `953d73d4ccfa4bc63edde444ed739b1326bdfbb1`, header
  `PB59InventoryReadbackRequest.h`, matching tests and requirements documentation.
  Single-use request matches exact operation, stamp and receipt, immutable
  expected write set and complete captured item membership. Only terminal
  committed records admitted. 43/43 assertions per four GCC/Clang modes with
  declaration adapters; real-header driver/native build pending. A future
  retained holder must enforce one gate per completion and bind the actual
  query result; no such wiring, durable verification, release or retry yet.
- **Preservation/live/rollback:** no existing executor/custom-rule change, schema/IO/config/
  SQL/DBC/client, reset/replay/unlock, shutdown policy, CI dispatch or live action. Runtime
  binding/full reconciliation and any new persistence/global policy need review before
  wiring. Discard isolated child to roll back; original branches/pins remain preserved.

## Dormant recovery queue identity before-image (24 November 2026)

- **Owner/scope:** CosmicCuddle / continuing #51 native-observation assignment7139518657;
  contract check-in7140735652. Private dormant Core collector only; CS01 owns Playerbots.
- **Source:** `feature/codex-pb59-recovery-queue-beforeimage-20261124`, exact Core79 parent
  `d293650c77a9ce233f6e792c41632daed8e2b167`. No integration tuple repin.
- **Files/dependencies:** PB59NativeRecoveryCollection.h, PlayerPB59Recovery.cpp, existing
  extracted native collector fixture/runner and NATIVE_RECOVERY_COLLECTION.md. No runtime caller.
- **Behaviour:** pre-admission queue identity image (max4096, scratch max256 KiB), exact bind
  validation, recovery comparison including null/removed/replaced entries. First19 differences
  exported with total count; native pointers stay private. Changed queues remain Uncertain,
  unchanged Unknown. This is not full dirty/update-mask, custody, effects or persistence proof.
- **Tests/limits:**175 ordinary +175 optimized NDEBUG extracted-native checks, five runtime
  mutants rejected in NDEBUG. Explicit engine doubles; full Player/Game build and gameplay
  remain unvalidated. No native state writes, unlock, Finish, IO, SQL/schema/config or policy.
- **Live/rollback:** dormant development only; no activation/deployment/CI dispatch or existing
  feature removal. Discard isolated child to roll back; parent and existing executor preserved.

## Dormant recovery update-state observations (24 November 2026)

- **Owner/scope:** CosmicCuddle / #51 assignment7139518657, continuing Core native-observation
  work. Private Item mask reader and dormant Player collector; no runtime call sites or PB change.
- **Source:** `feature/codex-pb59-recovery-update-observation-20261124`, exact Core81 parent
  `62f54266395d4715c34464ff592ec09043f457bf`; no integration tuple repin.
- **Files/dependencies:** PB59RecoveryUpdateObservation.h value copier, Item.h private helper,
  PB59NativeRecoveryCollection.h, PlayerPB59Recovery.cpp, collector fixture/runner and protocol.
  Uses actual UpdateMask GetCount/GetBit, native dirty state and original Map update set.
- **Behaviour:** bounded item/container masks256 fields and owner2048 fields; fixed scratch
  <=512 KiB before admission. Records queue item state/world/pending/map membership and owner
  update state; validates before-image at bind, records changes/invalidity at recovery. Unknown
  unchanged, Uncertain changed/inconsistent; no authorization or native writes/restoration.
- **Tests/limits:**373 ordinary +373 optimized NDEBUG extracted-native checks; nine deliberate
  faults caught in NDEBUG. Actual mask class/private helper, explicit engine/serialization
  doubles; full Player/Game build/gameplay, unqueued objects, field values, effects and persistence
  remain unproven. See source-pinned PR/protocol for exact results and fixture limits.
- **Live/rollback:** dormant only, no migration/config/SQL/DBC/client, IO, unlock, policy,
  CI dispatch, activation or deployment. Discard isolated child to roll back; preserve ancestors.

## Dormant original/bag raw-field recovery observations (24 November 2026)

- **Owner/priority:** CosmicCuddle / CS01 #81 comment7141476499, native recovery completion only.
- **Source:** `feature/codex-pb59-recovery-original-values-20261124`, exact Core82 parent
  `afd670fc7fc2bc8244a67718ca8cc38e975d7556`; existing Core74/PB56 acceptance pins unchanged.
- **Files:** Item.h private field copier; PB59RecoveryUpdateObservation.h value type/copier;
  NativeRecoveryCollection header, Player.h private helper, PlayerPB59Recovery.cpp and native
  collector tests/protocol. NATIVE_RECOVERY_RELEASE_GATES.md records minimum compile/IO path.
- **Behaviour:** bounded raw update-field and update-state before/current observations for
  planned originals and deduplicated affected carried bags, including unqueued instances.
  Re-resolves source/destination/bag links before native instance reads; replacements/missing
  links remain incomplete. CopyWatchedItem exports one owned record; no native pointer export.
- **Evidence/limits:**479 checks ordinary +479 optimized NDEBUG, twelve guard mutants caught
  under NDEBUG; actual collector/Item helper/UpdateMask bodies, explicit remaining engine doubles.
  Real-header exploratory compilation stops at missing Boost flat_map.hpp; no real Player/Game
  build PASS. Quarantine save admission and missing recovery-ticket/revision commit association
  prevent persistence proof; no ordinary save bypass is introduced. Effects/non-field metadata,
  unrelated objects, gameplay and durability remain unproven.
- **Live/rollback:** dormant/no runtime caller, Finish/unlock/replay/IO, schema/config/SQL/DBC/
  client, CI dispatch, activation, merge or deployment. Discard child; custom behavior preserved.

## Native recovery handoff fixture (24 November 2026)

- **Owner/scope:** CosmicCuddle / #81 native recovery completion task7141476499. Tests only.
- **Source:** `test/codex-pb59-executor-recovery-handoff-20261124`, exact Core84 parent
  `8d027154e1d3b1525d5cb867fde44631995e5dd0`. Production source and acceptance tuple unchanged.
- **Files/behaviour:** existing collector runner optional --bridge, executor_recovery_bridge
  checks/hooks and EXECUTOR_RECOVERY_HANDOFF.md. Temporary protocol insertions into the actual
  executor body are reversible; the actual uninstrumented-body control is separately compiled.
- **Tests/limits:**100 ordinary +100 NDEBUG handoff checks, six runtime guard mutants caught
  in NDEBUG; eleven shared control scenarios preserve outcomes/native state/counters. Reuses
  existing effect before-images without authorization. Explicit engine doubles; actual native
  compilation, complete effects/cooldown/persistence/gameplay gates remain open. No PB duplication.
- **Live/rollback:** no runtime wiring or native behavior change, IO/release/schema/config/SQL/
  DBC/client, CI dispatch/activation/merge/deployment. Discard isolated test child to roll back.

## Save observer exceptional-delivery containment (24 November 2026)

- **Owner/assignment:** CosmicCuddle / CS01 #81 comment7142198686, narrow recovery-save contract.
- **Source:** `fix/codex-pb59-recovery-save-contract-20261124`, exact Core85 parent
  `72efa4a5c74e9ad0636f08e561e7a00770db849b`. Core74/PB56 tuple unchanged.
- **Change:** PB59CharacterSaveObserver.h catches any future.get delivery exception as Ambiguous,
  matching existing reclaim handling; report-callback exceptions still propagate without ACK.
  Existing quarantine/save admission and missing recovery operation/revision binding preserved.
- **Tests/status:**186 ordinary +186 optimized NDEBUG contracts pass; four deliberate faults
  compile and fail runtime assertions under NDEBUG. Parent exception escape reproduced;
  std/non-std exceptional delivery, broken promise and report retention tested with actual
  observer types. See RECOVERY_SAVE_BLOCKING_CONTRACT.md for exact evidence and native limits.
- **Dependencies/live/rollback:** actual observer/future types and extracted save guard prefixes;
  remaining engine/DB boundaries doubled. No native build, gameplay or durable recovery proof;
  no runtime recovery-save admission/IO/reset/replay, CI/merge/activation/deployment/live changes.
  Discard isolated child to roll back; full native build and recovery-save binding remain gates.

## Native build and recovery-save admission gate specification (24 November 2026)

- **Owner/assignment:** CosmicCuddle / CS01 #81 comment7146046931; native resource request
  #52 comment7146136903. Specification and existing-policy tests, no runtime implementation.
- **Source:** `docs/codex-pb59-native-build-admission-gates-20261124`, exact Core88 parent
  `d5eee59878bed7991611b0bf6764e73e03885b39`; requested native build target remains that parent.
- **Files:** NATIVE_GAME_BUILD_BRIEF.md, RECOVERY_SAVE_ADMISSION_REQUIREMENTS.md, real-header
  receipt-retention checks/runner, linked release-gate handoff. No production source changes.
- **Contract:** exact native owner/attempt/revision/observer origin, captured-state ownership,
  pre-submit admission/fences and retained typed completion are required. Current diagnostic
  observer's success reclamation/ACK cannot be presumed a recovery receipt handoff. No selected
  new API, bypass, persistence schema or asynchronous borrowed native scope.
- **Evidence/limits:**51 ordinary +51 NDEBUG actual-header checks, synthetic futures/no DB,
  confirm ready-success/Empty and post-report-failure reclaim behavior. Native development
  environment/module pins remain pending tester confirmation; no real Game/Worldserver/DB proof.
- **Live/rollback:** Core74/PB56 acceptance unchanged; no activation, IO/release, environment
  install, CI dispatch, merge/deploy or live/config/SQL/DBC/client change. Discard child to roll back.

## Protected save receipt ownership (24 November 2026)

- **Owner and scope:** CosmicCuddle, CS01 #81 comment 7147882367. Adds a dormant ownership mode to the
  actual save observer. The isolated branch starts at Core89 `892a7ee24ee6f2721618336c77329c658a016156`.
- **Behavior:** a protected reservation shares the observer's fixed capacity and issues a move-only
  handle with an instance identity and exact store ticket. Ordinary reports and pressure reclamation
  skip protected slots. The exact consumer gets the typed terminal record once and frees its slot.
  A dropped handle or missing future leaves the slot held. An unsealed capture can be marked incomplete
  by its exact owner. Receipt consumption copies the record before acknowledging it.
- **Evidence and dependencies:** real observer, evidence store and typed futures exercised by
  `tests/pb59/test_recovery_receipt_retention.py`, 203 checks in each of ordinary and optimized modes.
  Future wiring still needs a trusted native producer, recovery-operation binding and a retained
  consumer. The caller supplies the future, and the receipt carries the observed execution outcome.
- **Live status and rollback:** dormant and unmerged. Core88 remains the native build target. Drop the
  isolated child to roll back this observer and fixture change. Runtime activation and shutdown
  handling for protected receipts require a separate lifecycle decision.

## Retained inventory readback owner (26 November 2026)

- **Owner/scope:** CosmicCuddle; Maintenance takeover of PB59 recovery, coordination #68 and #52.
  Separate development child of Core113 `edf56d0958718c47a5dd34eb0296abf57b66ab56`.
- **Files/behavior:** PB59RetainedInventoryReadback.h, PlayerStorage.cpp and isolated tests/drivers.
  Each prepared recovery save owns one non-reopenable readback object. The pending holder selects
  its own exact retained operation/stamp/receipt and committed NeedsVerification settlement under
  its mutex. Begin refuses closed admission; issued requests can finish after closure. Refusal,
  allocation failure, lost request, mismatch or unavailable result cannot re-arm the object.
  No external diagnostic copy can supply a record to the pending-holder entry point.
- **Tests/limits:** 20/20 targeted component assertions in each of five GCC/Clang configurations
  with real gate/store/comparator headers and declaration adapters. Actual holder fixture changes
  and updated extraction drivers await compilation. No real query result provenance, competing
  writer exclusion, shutdown of future query work, native build or persisted custody proof.
- **Live/rollback:** dormant entry points, no caller or hold-release API, no database/SQL/schema,
  configuration, client/DBC, merge, repin, activation or deployment. Discard this child.

## Strict readback wire value decoding (26 November 2026)

- **Owner/scope:** CosmicCuddle; CS01 PB59 recovery development, isolated child of Core115 `a74c247ccd5e38167108f18e138df4c24d94b45e`.
- **Files:** PB59ReadbackWireValues.h and readback_wire_values_checks.cpp. Canonical decimal parsing with exact range, signedness and full consumption checks; NULL versus empty and byte-preserving bounded owned text. Unsuccessful decoding leaves output unchanged. Allocation failure is not injected; future caller must handle exceptions without releasing the hold. No field/row adapter or native caller.
- **Source review:** existing QueryResult.cpp `00c11b7725663737f8ee0940efb55173c77e9bcf` accepts MYSQL_DATA_TRUNCATED and forwards fetched_length even if larger than buffer_length; no per-field error pointer. General Field/QueryResult API not suitable as unquestioned lossless evidence. Future query producer must explicitly verify complete buffers, exact row count/shape, NULL, conversion and connection/snapshot identity; this primitive does not validate the provenance or length of the input slice. No global DB reader change.
- **Verification:** 41/41 isolated C++ checks per GCC and Clang debug/optimized and Clang ASan+UBSan (205 repeated checks). Six compiled GCC debug mutations rejected by assertions. Exact local and Git blob hashes match. Run from root: `g++ -std=c++20 -Wall -Wextra -Werror -pedantic -I src/server/game/Entities/Player tests/pb59/readback_wire_values_checks.cpp -o /tmp/pb59-wire && /tmp/pb59-wire`. No DB, native server, snapshot or actual hold tested; Core114/115 full fixture remains pending.
- **Live/rollback:** dormant header only, no caller, schema/config/CI/merge/deploy or hold release; drop isolated child.

## Strict readback row decoding (26 November 2026)

- **Owner/scope:** CosmicCuddle; CS01 PB59 recovery, child of Core116 `e9549eb54200985fd67030079344af8a994054de`.
- **Files:** PB59ReadbackRowDecoder.h, readback_row_decoder_checks.cpp, test_readback_row_decoder.py. Decode explicitly ordered 4/14/4-column inventory-link, item-instance and gift projections, require complete row widths, exact column labels, strict numeric widths and nullability, bounded UTF-8 text preserving embedded NUL and trailing bytes. All or nothing; preserve duplicates and empty projections for comparator. Caller supplies a positive row limit and must bound it appropriately. No row filtering or hold release. Source schema facts based on CS02's reviewed Core108 schema record coordination #52 7190553284 and unchanged comparator Core116 490bd42f5cf19949a2d3638c53b34971d16c520b; live schema not verified. Column labels do not authenticate metadata or values.
- **Verification:** 61/61 assertions per GCC 14.2 debug/optimized and Clang 17 debug/optimized plus Clang ASan+UBSan/_GLIBCXX_ASSERTIONS (305 repeated). Six compiled GCC mutations rejected by assertions: omitted column check, omitted row budget, relaxed enchantment/text bounds, omitted UTF-8 validation, swapped creator columns. Exact local/Git blobs matched (header a969e836d255088f03d173ca171d9d823c8f2bce, test 6a2e629f77f7d6b9307dc7612aa611691d844a4a, prior wire header 16174e73641804cd26e42312b2fa4c7ec3931c2f). Row struct and UTF-8 function excerpts plus text-limit constant matched source FNV64 993a23f8850b7e10/712 bytes, b3eed4d35a9739d5/958, 7b4ffd1f5a1c904a/46. Local declaration adapter, not native DB/complete comparator or checkout driver run. Full driver provided for future checkout; use python3 -B tests/pb59/test_readback_row_decoder.py --compiler g++ (also --ndebug / clang++).
- **Limits/rollback:** no trusted complete-buffer query producer, installed schema verification, snapshot identity, competing-writer exclusion, allocation-fault injection, custody or shutdown proof. Core114/115 full fixture remains pending. No caller or production changes; discard isolated child.

## Complete readback result capture (26 November 2026)

- **Owner/scope:** CosmicCuddle; CS01 PB59 development child of Core117 `8d4063cb9cc3a250f92d361a13c1b028ee35af78`.
- **Files:** PB59CompleteReadbackResult.h and complete_readback_result_checks.cpp. Independently bounded field byte lengths and accessible buffer extent, explicit row/EOF/error/truncation handling, exact advertised row count, one-shot extraction only after successful EOF, owned byte copies preserving NULL/empty/NUL/trailing spaces. Failures invalidate the whole capture, including allocation/copy exceptions; no partial result escapes. Null length must be normalized by the future native producer. Template caller provides row and field budgets; those must be justified by the native query plan. No native MySQL adapter or connection/receipt metadata authentication yet.
- **Verification:** actual standalone C++ 65/65 assertions per GCC 14.2 debug/optimized and Clang 17 debug/optimized and Clang ASan+UBSan/_GLIBCXX_ASSERTIONS (325 repeated). Six GCC-compiled mutants fail explicit assertions (status, early EOF, cell error, NULL length, accessible buffer extent, byte budget). First buffer-bound mutant survived due to overlapping test conditions; fixture corrected to a deliberately short span over allocated storage and rerun all five modes and six mutants. Exact local/Git blobs 46771c44668acfa89bdb9469aaca029052d7f47d and 6a020b62c550e9415d4b5557829515d9d05de4b2. Run `g++ -std=c++20 -Wall -Wextra -Werror -pedantic -I src/server/game/Entities/Player tests/pb59/complete_readback_result_checks.cpp -o /tmp/pb59-result && /tmp/pb59-result`. Allocation exceptions not fault-injected. This is synthetic capture evidence, not native fetch or snapshot proof.
- **Remaining:** native producer must map actual fetch return codes, independently sized/initialized buffers and per-cell flags, verify schema and metadata, bind result to the exact one-shot request, handle connection loss/reconnect and shutdown, and establish competing-writer exclusion. No change to shared database reader, schema, config, runtime caller, hold release or live state. Rollback: discard child.

## Complete capture to strict row decoder bridge (26 November 2026)

- **Owner/scope:** CosmicCuddle; CS01 PB59 development child of Core118 `77b81ee2639763fbaba1c3df9bc98afee46aacc6`.
- **Files:** PB59DecodeCompleteReadback.h and decode_complete_readback_checks.cpp. Consume all three completed link/instance/gift captures once even when any fails; bound each table by MaximumOriginals (19), preserve NULL versus empty and exact bytes, pass actual supplied column labels to strict decoder, preserve duplicates/valid empty results, never return partial decoded rows. Owned captures remain alive until decoded copies are returned. Catch decoder exceptions; allocation fault not injected. Independent guard in view constructor rejects over-limit inputs. Diagnostic only; no query producer, snapshot/receipt authentication or hold release. A caller must independently authenticate provenance before trusting values; the views borrow local owned rows and must not escape.
- **Verification:** actual standalone 71/71 per GCC14.2 debug/optimized and Clang17 debug/optimized plus Clang ASan/UBSan/_GLIBCXX_ASSERTIONS (355 repeated checks). Five compiled GCC mutants detected by explicit failed assertions: early return leaving other captures reusable, ignored supplied labels, NULL conflation, link/gift swap, C-string truncation. Exact local/Git new blobs b8bca2d9ac9638b1fd6c862b9f497b6e833ada43 / 0521c94e530bf731ce41a87d92a8892086814ce2, unchanged capture/row/wire headers 46771c44668acfa89bdb9469aaca029052d7f47d / a969e836d255088f03d173ca171d9d823c8f2bce / 16174e73641804cd26e42312b2fa4c7ec3931c2f. Used local declaration adapter for comparator (actual row definitions and UTF8 excerpts matched prior Git-source FNV64/length 993a23f8850b7e10/712 and b3eed4d35a9739d5/958; constants MaximumOriginals=19 and MaximumTextBytes=512). No whole-checkout compile or live DB test. To reproduce in a checkout, adapt existing test_readback_row_decoder.py extraction harness to also copy PB59CompleteReadbackResult.h, PB59DecodeCompleteReadback.h and MaximumOriginals constant and compile decode_complete_readback_checks.cpp; this extended driver is not committed yet.
- **Remaining:** actual native MySQL adapter, authenticated metadata/connection/snapshot and receipt, competing-writer exclusion, actual Core114/115 holder fixture, integrated compile and gameplay. No global reader modification or live state change. Rollback: discard child.

## PB59 durability mutation entry guards (27 November 2026)

- **Owner/scope:** CosmicCuddle; CS01 Core safety fix in isolated child of Core119 `202299d9092b256a91a7c918d5d229cc714730da`; Claude owns separate Playerbots readiness proposal (#80 7204889164 / 7215585139).
- **Defect:** exact Player.cpp parent blob 48b36825773879a0af2e3a2027fc79fced5a3a39, DurabilityPointsLoss and DurabilityRepair wrote ITEM_FIELD_DURABILITY before guarded Item::SetState, with possible stat effects and paid repair charges. Item.cpp 799af91184cbe5762003864a0203c1131df955fe confirms SetState checks player state then item guard; Item.h 01295be967e5ce5ab24d72936969b4931f81113e grants Player friendship for private item guard. Add owner and item mutation admission checks at entry to both methods; repair refuses before payment, item field writes and stat effects. Null item also refuses safely in loss. No new public API, config or schema; ordinary allowed repair/loss paths otherwise unchanged. This is entry-only protection, not an atomic repair/payment transaction or proof of safety if a script callback changes quarantine/context during execution. Existing global quarantine guard semantics and reservation assertions preserved. A zero return for refused repair retains the existing return contract; callers must not interpret zero cost as proof of successful repair.
- **Verification:** exact two patched source methods extracted into isolated synthetic item, owner, payment and effect adapters. FNV64/length match Git source after patch: loss b0a05de1d7ba62e5/1212, repair 5837e2b2c927dde/2948. Actual test driver blob 22f1a3bc0aa890b09eb49954a5edbb10a8dea17e matched local and remote. 89/89 per GCC 14.2 debug/optimized and Clang 17 debug/optimized plus Clang ASan/UBSan/_GLIBCXX_ASSERTIONS (445 repeated checks). Four compiled GCC guard-removal mutants (owner/item loss and owner/item repair) failed explicit assertions. Covers free/paid/guild repair, blocked owner/item, durability break/restore, ordinary effects, aura/no item/insufficient funds/guild refusal. Run from checkout: `python3 tests/pb59/test_durability_entry_guards.py --source src/server/game/Entities/Player/Player.cpp --compiler g++` (also --ndebug, --compiler clang++, --sanitize). This is an extracted-method regression, not native compilation or actual payment, reservation or gameplay verification. Reentrant callbacks and wider direct field writers not yet audited.
- **Live/rollback:** unmerged draft only. Preserve existing live behavior until separately reviewed, compiled and tested; rollback discard child. No new DB operation, configuration, deployment or release authority.

## PB59 buffered native-client readback adapter (27 November 2026)

- **Owner/scope:** CosmicCuddle; CS01 isolated child of Core120 `0767cc3d8af735ea1b3f70807dba30f85ce1a804`. New dormant PB59BufferedReadbackFetch.h consumes a fresh buffered single-result text-protocol binary-projected SELECT supplied by its future trusted exclusive connection owner. Checks connection errors/warnings, result multiplicity, exact field cardinality/labels, binary charset and allowed string/blob metadata types, row budget and exact observed count, NULL lengths, byte limits and EOF. Owns returned bytes, rejects partial/error results. No query generation/execution, result freeing, reconnect, transaction control or hold authority. Caller must establish actual handle/result ownership, initialize only from a bounded SQL result before mysql_store_result, authenticate schema/metadata/snapshot/receipt and clean up handles; lengths use the C API's valid-buffer contract rather than an independent application allocation extent. Does not yet feed the Core119 bridge directly (returns owned rows). No native server changes.
- **Verification:** actual local MariaDB client headers/library (mariadb_config 11.8.6), GCC 14.2 and Clang 17 debug/optimized plus Clang ASan/UBSan/_GLIBCXX_ASSERTIONS. 51/51 each mode (255 repeated assertions) using C API test doubles declared against actual client header; six GCC compiled mutants failed explicit assertions (initial warnings, labels, charset, type, row budget, EOF error). Separate executable linked to actual libmariadb, mysql_init and null-result refusal then mysql_close, exit 0, no connection established. Exact local/Git capture/header/test blobs 46771c44668acfa89bdb9469aaca029052d7f47d / 7fe47665cf2f68234dd5078dd55847e4e8c4f3a3 / b904521f664b38c9b510d4b065928f28c28c55b7. Smoke source blob 28c069eab9332ef03b5ecedae8bc7e4e8adcdebf. Reproduce on this Linux client: g++ -std=c++20 -Wall -Wextra -Werror -pedantic -I/usr/include/mysql -I src/server/game/Entities/Player tests/pb59/buffered_readback_fetch_checks.cpp -o /tmp/pb59-fetch && /tmp/pb59-fetch. For actual library smoke replace source with buffered_readback_fetch_link_smoke.cpp and append -lmariadb. Not actual SQL fetch, installed server connector compatibility or native DB proof. No allocation fault injection. Next bounded binary SQL projections, ownership/connection/snapshot contract, owned-row bridge, competing-writer proof.
- **Core120 follow-up audit:** Player.cpp blob 41c58ec4846064a66010c5c22d69a53df17360d6: ModifyMoney invokes OnPlayerMoneyChanged before SetMoney; _ApplyItemMods has effect-boundary checks and spell/aura/enchant calls. Guild.cpp 97a53c5ab4b75926bdd7e236606774c53bf5fc54: HandleMemberWithdrawMoney invokes OnGuildMemberWitdrawMoney before bank debit, updates withdrawal and log in DB transaction and broadcasts. These are source-confirmed callback boundaries, not a reproduced mid-call quarantine. Entry guard alone cannot establish reentrant atomicity. Do not add a late refusal after payment without an explicit safe payment/effect contract; do not assume callback cannot invalidate context. No speculative global money/guild change. Separate audit of actual installed callback implementations and owner/item lifetime protection required before native release.
- **Rollback/live:** discard isolated child; no shared reader changes, real DB, config, merge, release or deployment.

## PB59 bounded inventory readback query plan (27 November 2026)

- **Owner/scope:** CosmicCuddle; CS01 child of Core121 000d7bd36787fc0de11ac8f402541586f2261f1c. New dormant PB59ReadbackQueryPlan.h builds three read-only SELECT strings from 1–19 distinct canonical full item GUIDs. Uses exact item-key IN lists, never expected-owner filtering, joins or DISTINCT. Fixed projection order and binary cast with substring bound of column byte limit +1; LIMIT item count +1. Overflow sentinels must be rejected by the reader, never trimmed to valid evidence. No arbitrary identifier/string input to public builder. Independently verified installed schema is required: cast conversion and metadata behavior depend on real column types/client/server; ZEROFILL or incompatible schema may refuse. Projection constants must remain synchronized with decoder and recovery limits. Does not check neighboring inventory slots or cross-table custody. No SQL execution, snapshot or receipt authority.
- **Verification:** actual local 31/31 assertions each GCC14.2 debug/optimized and Clang17 debug/optimized plus Clang ASan/UBSan/_GLIBCXX_ASSERTIONS (155 repeated). Six compiled GCC mutants failed explicit assertions: canonical key check, duplicate check, row sentinel, byte sentinel, inventory lookup key, maximum item count. Exact tested and Git blobs 6444462afee1d8d1f23f8795d2b5938852d17868 / ad07dbdd4a8c528b2044e4a8b3871b4870d9a7ba. Reproduce: g++ -std=c++20 -Wall -Wextra -Werror -pedantic -I src/server/game/Entities/Player tests/pb59/readback_query_plan_checks.cpp -o /tmp/pb59-query && /tmp/pb59-query. Tests compare generated strings and rejection, not DB semantics, native compile or actual metadata. Local container has no mariadb/mysqld executable on PATH. No database connection made.
- **Remaining/rollback:** actual disposable SQL validation on supported client/server; exact schema and projection limit contract, owned connection/read-only snapshot, no reconnect, result cleanup, request ownership and competing-writer exclusion, full integration. No runtime caller, live config, schema change, merge or deployment. Drop child to roll back.

## PB59 owned bounded readback execution (27 November 2026)

- **Owner/scope:** CosmicCuddle; CS01 child of Core122 `6751e53969b9f4ed19d7a676d4814c02fb3a91da`. New dormant PB59ExecuteReadbackQueries.h connects the fixed query plan and buffered reader, using mysql_real_query, mysql_store_result and RAII result/connection cleanup. Consumes unique ownership of a dedicated connection, checks nonzero expected server thread ID, client errors/warnings and extra results at query, buffering and result cleanup boundaries; derives exact reader limits/labels from the fixed projections. Stops at first failure, returns only an all-table owned result, closes connection on every path; no return to shared pool, retry or commit. Thread ID continuity is NOT authenticated session identity or snapshot proof. Caller must exclusively transfer an authenticated dedicated connection with reconnect disabled and verified schema, and have established a read-only consistent snapshot. These requirements are not established by this implementation. Never transfer a pooled handle or a connection with unrelated transaction work. Closing an active transaction is not durable commit proof. No runtime caller or hold authority.
- **Verification:** actual local GCC14.2 and Clang17 debug/optimized plus Clang ASan/UBSan/_GLIBCXX_ASSERTIONS, **168/168 each (840 repeated checks)**. C API test doubles declared using actual local client headers; 30 fault combinations across three query stages, invalid initial conditions, exact SQL/order, NULL and embedded NUL and cleanup counts. Six compiled GCC mutants (missing close/free, omitted checks after query/store/free, missing thread comparison) failed explicit assertions. Initial omitted post-store identity check mutant survived because the later check still refused the result; added explicit no-fetch assertion on early failures, reran all five modes and mutants successfully. Real libmariadb link test initializes an unconnected handle, refuses invalid expected thread 0 and closes, exit 0, no network connection. Exact tested/remote blobs header aa42574350f5ec60983f7c02e83d72d1e5b06de3, tests 1782b38154d57e8374e8274fb0654b52578d22a1, smoke a20f787c58e690f4e095fc7b94eef0b312dc2048. Dependencies query 6444462afee1d8d1f23f8795d2b5938852d17868, reader 7fe47665cf2f68234dd5078dd55847e4e8c4f3a3, capture 46771c44668acfa89bdb9469aaca029052d7f47d matched. Reproduce: g++ -std=c++20 -Wall -Wextra -Werror -pedantic -I/usr/include/mysql -I src/server/game/Entities/Player tests/pb59/execute_readback_queries_checks.cpp -o /tmp/pb59-exec && /tmp/pb59-exec; real link smoke uses execute_readback_queries_smoke.cpp and -lmariadb. No actual database query or full server compilation; allocation fault not injected.
- **Remaining/rollback:** dedicated connection factory and authentication/reconnect verification, schema/projection tests on disposable supported server, consistent snapshot setup/validation, request binding, owned rows to decoder and competing-writer/custody/shutdown safeguards. No production changes; discard isolated child.

## PB59 owned execution to decoded inventory rows (27 November 2026)

- **Owner/scope:** CosmicCuddle; CS01 isolated development child of Core123 `d6c185c2ffccfde20310de0639d82607d967bb8b`. New dormant PB59ExecuteDecodedReadback.h combines the bounded native execution path with the strict row decoder. Compiles projection-name/order equality checks across all three tables and text byte-budget equality; rejects empty/oversized request counts, consumes dedicated connection, decodes only the owned result generated by that execution, no externally supplied metadata or row injection at this entry. Preserves valid empty projections, duplicates, wrong owners and NULL/empty distinction; strict row decoding returns no partial output. Result values do not grant hold-release authority. Caller still must authenticate exclusive connection, verified schema, disabled reconnect and read-only consistent snapshot; no request/receipt binding or custody proof yet.
- **Verification:** actual local GCC debug/optimized and Clang debug/optimized plus Clang ASan+UBSan/_GLIBCXX_ASSERTIONS: 55/55 each (275 repeated checks). C API doubles use actual MariaDB client declarations; fixture poisons backing bytes on mysql_free_result to verify owned copies, covers per-column mappings, embedded NUL and signed/null fields, wrong-owner/duplicate/empty observations, malformed values, bad metadata, row/byte sentinels, failed query, cleanup. Four GCC-compiled deliberately changed variants failed runtime assertions (link/gift swap, ignore decoded output, embedded-NUL truncation, NULL conflation); ignored-output mutant initially failed compilation due to unused rowLimit, revised mutant to explicitly consume it, then runtime failure confirmed. Independent projection-name drift mutant rejected by static assertion at compilation. Actual libmariadb link smoke (mysql_init without connection, expected thread 0 refusal, close) exit 0. Exact local/Git new header/test/smoke blobs 1ce597e2cb2c4c89b21e3d139cd5a79354b132ab / 7d423e38ace2fa723f6255c900cf279137a317e1 / b745970840bbba068762fc3b692e6c8c29b44d22. Seven unmodified local dependency blobs matched pinned Git: executor aa42574350f5ec60983f7c02e83d72d1e5b06de3, capture 46771c44668acfa89bdb9469aaca029052d7f47d, reader 7fe47665cf2f68234dd5078dd55847e4e8c4f3a3, query 6444462afee1d8d1f23f8795d2b5938852d17868, wire 16174e73641804cd26e42312b2fa4c7ec3931c2f, row decoder a969e836d255088f03d173ca171d9d823c8f2bce, views b8bca2d9ac9638b1fd6c862b9f497b6e833ada43. Comparator declaration adapter (actual row and UTF8 excerpt hashes/lengths 993a23f8850b7e10/712, b3eed4d35a9739d5/958 matched pinned Git; constants MaximumOriginals=19 and MaximumTextBytes=512 verified). Not a full comparator/whole-checkout compile. For isolated reproduction extend test_readback_row_decoder.py's extraction to include MaximumOriginals and copy the seven dependency headers and new header; compile execute_decoded_readback_checks.cpp with -I/usr/include/mysql and the temporary include directory, -std=c++20 -Wall -Wextra -Werror -pedantic. Smoke uses execute_decoded_readback_smoke.cpp with -lmariadb. Allocation exceptions not fault-injected. No actual SQL sent or server/DB access.
- **Remaining/rollback:** authenticated factory, schema/SQL verification in disposable supported environment, consistent snapshot, request binding, competing-writer and neighboring custody protection, shutdown and native acceptance. Core120 callback limitations remain. No live changes or release authority. Discard isolated child to roll back.

## PB59 read-only consistent snapshot setup (27 November 2026)

- **Owner/scope:** CosmicCuddle; CS01 isolated child of Core124 `e03f743d0a76a652a21d23e8078356c748e46197`. PB59SnapshotReadback.h adds two fixed non-result SQL commands before existing bounded readback: SET TRANSACTION ISOLATION LEVEL REPEATABLE READ, then START TRANSACTION WITH CONSISTENT SNAPSHOT, READ ONLY. Validate canonical item set before control commands, check exact thread continuity/errors/warnings/multiple results, require no result columns and affected rows zero. Any refusal closes exclusively transferred connection and sends no SELECT; no retry/commit. Success uses same dedicated connection for the existing query/decoder chain. Prerequisites remain: independently authenticated, fresh connection with no active transaction, reconnect disabled, verified InnoDB tables and supported SQL semantics. A successful START or thread ID alone cannot establish schema/receipt/custody or exclude competing writers; closing connection does not commit. No runtime caller, permission to release holds or shared reader change.
- **Verification:** actual local standalone C++ 100/100 per GCC debug/optimized, Clang debug/optimized and Clang ASan+UBSan/_GLIBCXX_ASSERTIONS (500 repeated checks). Existing complete execution/decoder tests plus 14 control-command faults (seven at each stage), exact ordering and command strings, initial invalid thread/IDs, direct command refusal. Five compiled GCC mutants failed explicit runtime assertions (skip START, wrong isolation, ignore field count, ignore affected rows, missing immediate post-command continuity check). Last mutant initially survived due to later redundant safeguard; strengthened direct command refusal tests, reran all five modes and five mutants. Real libmariadb link smoke with mysql_init, expected thread 0 and close passed (no connection). Exact local/Git blobs new header ec3f3cb31f5b41414d07db1a63a93dbda071aad3, test df3f09ea7bb3723a38e78da84780f8b3ffa3d3b6, smoke 1594ef598f550c7d1ac5b0b0a64897b2a80c860b; eight unchanged dependency blobs matched Core124 recorded hashes. Same extracted comparator declaration adapter as Core124 (not full comparator or whole checkout). Reproduce using Core124 documented extraction/include procedure, additionally copy snapshot header, build snapshot_readback_checks.cpp against local client headers and smoke against -lmariadb. No allocation fault injection or real database semantics tested.
- **Native/GRM constraints:** local PATH checks found Chromium and MariaDB client headers/library but no Opera, mariadbd or mysqld; getent for github.com/download.opera.com returned no addresses. No full checkout/build or Opera acceptance attempted; cannot substitute Chromium for Opera. Both Grok/native tester lanes paused per owner clarification, CS01 assumes obtainable work. Existing ordinary-only Opera scope and combined acceptance blockers unchanged. No new installs or live data access.
- **Remaining/rollback:** dedicated authenticated connection factory with verified fresh session and reconnect policy, schema/SQL validation on disposable supported MySQL/MariaDB, transaction receipt binding, competing-writer/neighbor custody and shutdown proof, actual native acceptance. Core120 reentrant repair limitation remains. No deployment/merge/repin. Discard child to roll back.
