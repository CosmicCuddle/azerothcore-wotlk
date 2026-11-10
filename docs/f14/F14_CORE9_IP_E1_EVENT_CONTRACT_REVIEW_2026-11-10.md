# F14 Core9 ↔ Individual Progression E1: proposed visibility-event contract

**Owner:** CosmicCuddle. **Authoring workstream:** Server-Side-Maintenance / CS01.
**Status:** REVIEW ONLY / NOT IMPLEMENTED / NOT APPROVED FOR RUNTIME OR RELEASE.
**Date:** 10 November 2026. **Scope:** verified selected Blood Elf/Draenei **RandomBots ONLY**, hidden from positively verified Vanilla-stage human observers, subject to complete independent rights and restoration safety. Never hide real human characters, Altbots, SelfBots, unknown identity, GM viewers, or ineligible observers.

This document is an **engineering proposal**, not a new hook, a listener, a gameplay-rule change, a configuration value, a merge, a test result or proof of client rendering. No code below is wired. The source contract must be reviewed before any attempt to compile-opt in or return TRUE to a concealment callback.

## Exact historical pins and independent work to preserve

| Evidence / ownership | Pinned source and limit |
| --- | --- |
| Actual current proposed Core producer | [Owner Core9 DRAFT #9](https://github.com/CosmicCuddle/azerothcore-wotlk/pull/9), `865724cee8d0fb94d7496854c0f426f60bac2232`; original branch **must not be edited**. `VisibleNotifier` owns a per-pass global nonzero token, while per-creature batch GUID/direction callbacks run only **after** a genuine `SendDirectMessage` CALL; neither network ACK nor F14 cause. |
| Core source-only coverage | [Core DRAFT #10](https://github.com/CosmicCuddle/azerothcore-wotlk/pull/10), `2d8de253228b33a3b4e3930b3559c70090447821`; 17 source/negative checks, [independent review](https://github.com/CosmicCuddle/Mod-Naxxramas-Core/issues/48#issuecomment-6769326799) PASS **test-only**. |
| Integration / paid / identity | [IP142](https://github.com/CosmicCuddle/mod-individual-progression/pull/142), `f259c8593f7a8ac5521bee343c40d5a2a35fd76b`; existing compiled-off older Core hooks do **not** match all Core9 APIs. |
| Human viewer login epoch candidate | [IP105](https://github.com/CosmicCuddle/mod-individual-progression/pull/105), `48c0e3613473b2b48cbea4830fdba68b968bdc74`; optional `PlayerScript` + `Player::CustomData` stamp exists in a **separate unmerged older branch**, NOT in IP142 or IP143. Do not invent a new human epoch instead of inspecting and preserving this candidate. Its epoch is distinct from the existing **RandomBot actor** login epoch. |
| Existing original restore debt | [IP94](https://github.com/CosmicCuddle/mod-individual-progression/pull/94), `61302474b2c0e2acfcc6b5c32036dde1517e43c5`: stages F14 intent, server link, handed-to-session packet, unknowable client presentation **separately**; never clear debt on packet send alone. |
| E1 correlation / lifetime | [IP143](https://github.com/CosmicCuddle/mod-individual-progression/pull/143), `e92f6e0e4ecc81d0656d5e66e468a9b13c467478`; pure bounded table only, no actual listener. Per-viewer epoch, pass token, creature incarnation, map/instance and prior F14 suppression are prerequisites; unknown Ends can poison a 256-viewer table; table not thread safe. |
| Negative-only executable safety | [IP144](https://github.com/CosmicCuddle/mod-individual-progression/pull/144), `2a07d27fc1f17ec157964f0598f7e94b74d0503c`; synthetic Core9 post-send cases must NOT match; focused 4/4 compiler jobs previously PASS, inherited other jobs have failures; Grok review pending at this draft. |
| Separate source analysis | [Grok Core9→E1 report](https://github.com/CosmicCuddle/Naxxramas-Server-Coordination/issues/11#issuecomment-6768876148) and [per-viewer route mapping](https://github.com/CosmicCuddle/Mod-Naxxramas-Core/issues/48#issuecomment-6766798584). |

## Current Core9 reality (verified sources, not a prototype claim)

- `Player::UpdateVisibilityOf(T*, UpdateData&, visibleNow)` has BOTH batched OUT and CREATE decisions and receives `UpdateData&` but no explicit notifier pass token; this means the **actual F14-positive decision** cannot yet nominate into E1 with a proven token. [Source](https://github.com/CosmicCuddle/azerothcore-wotlk/blob/865724cee8d0fb94d7496854c0f426f60bac2232/src/server/game/Entities/Player/PlayerUpdates.cpp#L1785-L1841)
- `VisibleNotifier` has an exact pass-token lifetime around batched visits and `SendToSelf`. Its post-send GUID sidecar reports **all eligible uncontrolled creature CREATE/OUT blocks** it observed, including normal Core visibility changes, not only F14 actions. The out-of-sight sweep independently writes ordinary OUT blocks. [Notifier](https://github.com/CosmicCuddle/azerothcore-wotlk/blob/865724cee8d0fb94d7496854c0f426f60bac2232/src/server/game/Grids/Notifiers/GridNotifiers.cpp#L81-L151)
- The **direct** `Player::UpdateVisibilityOf(WorldObject*)` uses `target->DestroyForPlayer` and `target->SendUpdateToPlayer`; the latter/underlying object methods make their own `SendDirectMessage` calls. The direct route is **outside** the batch pass and has no Core9 GUID/pass signal. [Direct path](https://github.com/CosmicCuddle/azerothcore-wotlk/blob/865724cee8d0fb94d7496854c0f426f60bac2232/src/server/game/Entities/Player/PlayerUpdates.cpp#L1849-L1883) · [packet calls](https://github.com/CosmicCuddle/azerothcore-wotlk/blob/865724cee8d0fb94d7496854c0f426f60bac2232/src/server/game/Entities/Object/Object.cpp#L244-L294)
- A withheld CREATE emits **no CREATE block at all**. The only relevant evidence is the independent positive F14 query and withheld link, NOT a fabricated packet send. Never count ordinary OUT (range, phase, despawn or relocation) as F14 suppression.
- Existing `OnF14ShouldSuppressCreatureForViewer` is compile-OFF/default FALSE with no positive listener. The F14 consumer's missing lifecycle macro and Core9 hook mismatch must **not** be hidden by defining an absent macro. [Core API](https://github.com/CosmicCuddle/azerothcore-wotlk/blob/865724cee8d0fb94d7496854c0f426f60bac2232/src/server/game/Scripting/ScriptDefines/WorldObjectScript.h#L91-L135)

## Proposed contract — fields, producer, consumer, ordering

These are **conceptual records**, not API symbols implemented in source. Use exact full GUIDs; never infer session or incarnation from account, race, a pointer, packet order, timestamps or an existing pass token.

| Record | Core source/dispatch point | Required immutable values | Consumer action (only after separate review) |
| --- | --- | --- | --- |
| A. `ViewerSessionBorn` | IP login after independently verifying real, non-headless, non-GM human session | full viewer GUID, unique monotonic nonzero **human** login epoch; current map/instance if known | IP owns per-session authority; do not reuse actor RNDBOT epochs; use/refine already prototyped IP105 after native compatibility review |
| B. `BatchPassBegin` | `VisibleNotifier` construction **before scanning** on owning map update | viewer full GUID, true human epoch if available, map/instance, **Core-created** nonzero pass token | Only register a current eligible viewer context. Do not create 256 table tombstones for every arbitrary GM/unknown viewer begin/end |
| C. `F14DecisionBeforeBatchChange` | exact per-target `Player::UpdateVisibilityOf(T*,UpdateData&,..)` decision, **before** F14-caused OUT unlink or withholding CREATE | same viewer epoch, map+instance, creature GUID+current incarnation, verified RNDBOT combat source, rights snapshot, actual Core batch token carried by an explicit scoped notifier/update-data context; direction; was-linked; positive **F14-only** cause | Refuse unless verified policy, human rights and #94 debt can atomically record stage-2 suppression; never nominate based on generic OUT/CREATE sidecar alone |
| D. `BatchCreatureSendCall` | existing post-`SendDirectMessage` Core9 GUID direction/token callback | same viewer, exact real pass token, GUID, **send-call**, kind=create/out; no new epoch/incarnation falsely stamped here | May attempt E1 Correlate **only** against an already-recorded, identical pre-send F14 nomination and current epoch/creature life; packet transport ACK and client presentation remain unknown |
| E. `BatchPassEnd` | `VisibleNotifier` destructor, including empty and no-send passes | real pass token, viewer session identity; no deferred raw pointer | Close candidate pass without mutating #94 restore debt. Nested pass/end handling must be explicit; unexpected or 257th unknown viewer Ends must never cause a silent positive proof |
| F. `DirectF14Transition` | exact direct `UpdateVisibilityOf(WorldObject*)`, after independently proving the F14 cause | viewer epoch, map/instance, creature GUID+incarnation, rights, direction/was-linked, **kind=direct** | **Initially reject for E1 and fail visible.** A separate Core direct-call provenance design is mandatory; never borrow the nearest batch token or fabricate a BatchCreatureSendCall |
| G. `RestoreRequest/NormalRecheck` | owning map-thread worker on a validated viewer-specific callback/queued task | original hidden pair, current viewer epoch, original/current creature incarnation, map+instance, rights and lifecycle reason | One viewer/one creature only; call a reviewed Core per-viewer re-evaluation route, not global `creature->UpdateObjectVisibility(true)`. Restore debt remains until independently valid proof; stale pair may require global fail-visible and testable fallback |
| H. `ViewerSessionEnd/CreatureRetire` | current IP human logout/map exit and authoritative creature death, corpse-ready, respawn, despawn/map-unload (distinct events) | old session epoch, creature incarnation/map where relevant, owning thread and causality | Invalidate pending E1 nominations before reuse; mark prior-hidden obligations, do not clear debt; no stale callback may affect new session/incarnation |

**Provenance classes MUST remain different:** a positive F14 decision, a server-side link/unlink, a packet **build**, a `SendDirectMessage` **call**, and actual client **presentation** are distinct. Core3.3.5a does not expose a reliable ordinary object-create/destroy receipt ACK. A callback cannot invent one.

### Explicit sequencing decision for proposal review

1. Define Core's owning-map **batch context** with a real token and exact viewer session. The existing `UpdateData&` is an available carrier candidate, but this document does not choose a specific public ABI/field. No process-global “current token” guess; verify nested notifier safety.
2. Before any F14-positive decision, obtain IP's **immutable** verified actor and human-rights snapshot, authoritative creature incarnation and stage. If any fact is missing, expired, unsafe or circular, return **visible** and do not nominate. Original unrelated Core visibility denial still applies normally.
3. If a positive F14-only suppression decision is permissible and #94 can record it, emit F14-attributed server event at the actual OUT/withheld-CREATE transition. For OUT, require a matching genuine send-call later; for withheld CREATE, record zero packet evidence rather than promoting a non-existent CREATE packet.
4. When Core9's existing GUID send-call arrives, only correlate a matching prior nomination. If there is no exact pre-send record, **NoCandidate/Unattributed**, even if a GUID/pass token otherwise matches.
5. For direct-route transitions, first require a separately reviewed packet-call evidence route at its actual packet sending functions. E1's existing batch-token API is not evidence for the direct path.
6. Any invalidated session, map transfer, new incarnation, rights change, GM override, config OFF, capacity poison or unwind must fail visible, enqueue justified per-viewer restoration and preserve original restore debt. Do not call Core visibility from inside an E1 lock or reentrant ScriptMgr callback.
7. Establish a defensible ledger policy for lack of client ACK before *any* positive hiding is permitted. A successful source link and send call alone cannot close #94 obligations.

### Minimal implementation boundaries (NOT permission to implement)

- **Core owns:** exact pass and direct send-call provenance, actual packet/update and link transitions, map-thread single-viewer refresh and unambiguous event ordering. Candidate source sites: `GridNotifiers.*`, `PlayerUpdates.cpp`, and reviewed script interfaces. No global `CanSeeOrDetect` change, global phase mask, ordinary combat suppression, or unrelated AI relocation.
- **IP owns:** observer individual era, human viewer epoch from verified login, RNDBOT actor proof, rights veto, creature incarnation, prior-hidden ledger and fail-visible decision. Never require a synchronous Auth DB call on a map worker.
- **Playerbots owns:** reliable selected **RandomBot** identity (not merely headless, race, account nickname or presence of a bot AI). Do not silently scope in Altbots.
- **Naxx / Services:** no duplicate concealment policy or stage/paid bypass; retain established custom gameplay and hooks. Cross-module versions require exact five-owner Worldserver link plus independently reviewed extra installed modules.

## Mandatory negative and positive tests (future, not run by this document)

| Scenario | Minimum acceptance |
| --- | --- |
| Vanilla human observes verified BE/Draenei RNDBOT | Only an owner-approved positive stage/race/rights case may qualify; both character and related NPC handling require full consented gameplay tests |
| Human BE/Draenei, Altbot, SelfBot, unknown identity; GM ON/OFF; TBC+ observer | Remains visible and normally targetable; no stage grant by checking visibility |
| Ordinary range/phase/despawn OUT, even with identical GUID/token | **Never** recorded as F14-caused suppression |
| F14-withheld CREATE (no packet) | No invented send call / correlation; original invisible debt tracked if and only if future policy genuinely hid it |
| Batch OUT then send call; packet missing, poisoned, merged or callback order reversed | No false client evidence, no debt clear |
| Direct CREATE/DESTROY outside notifier | No borrowed batch token; direct-kind proof separate or fail visible |
| Empty pass, nested pass, late End after logout, epoch reuse, 257 unknown viewers | No stale match, bounded liveness, failure visible; #94 debt unchanged |
| Creature killed/evades, corpse-ready/respawn, actor stops attack, player rights emerge during fight | Restore and no awkward combat-with-invisible-target; quest/tap/loot and corpse sparkle preserved |
| Targeting, pet/minions, healer/assist, damage/DoT, group and quest-credit rights | Never hide a creature the Vanilla human still interacts with or needs |
| Stationary observer awaiting restore | Actual **one-viewer** map-thread recheck; no movement prerequisite, no global AI relocation |
| Two real human clients + verified headless RandomBot, logins/map transitions/GM switch | 3.3.5a in-game comparison and observer-side rendering/targetability verified with full installed module stack |
| Disabled config, over-capacity or failed restore | Fail visible, preserve/resolve original hidden debt via an independently approved protocol; no silent empty-ledger reset |

## Review, release and rollback gates

1. **Review document first** with CS02-IP/Grok (Core9→E1) and CS02 Module source reviewer (viewer refresh) in existing Issues; inspect potential nesting and per-map-thread invalidation before API names are chosen. Claude's #131 ownership and Grok's #143 remain protected.
2. Any implementation must use **new isolated, narrow DRAFT children** in the correct source repositories, independent review, exact pinned test/CI and real Worldserver compile **and link**. Full native S4 success for older Core9/PB15/IP142/Naxx292/Services [#53](https://github.com/CosmicCuddle/mod-character-services/pull/53) does **not** cover E1 #143/#144, this proposal or a later bridge.
3. **S5 blocked:** owner-installed actual Core+all modules, effective config, database/schema, tracked/untracked/**ignored** custom SQL and old cleanup files, DBC/client patches, filesystem/DB backups with **isolated verified restoration**. Never use git clean/reset, change live files or apply database migrations during inventory.
4. **S6 blocked:** owner-approved disposable 3.3.5a two-human + selected RNDBOT/Altbot/GM/gameplay test with rollback and separate positive hide activation approval. No live/server/DB/SQL/feature-flag changes by this document.
5. **Rollback for this draft:** close/discard this documentation-only branch; there is no installed runtime, configuration, database or client state to revert.

**Request for reviewer:** identify the smallest safe Core event/ownership ABI that satisfies rows A–H without manufacturing F14 suppression or client ACK, decide whether to reuse the exact IP105 human epoch implementation after independent branch comparison, and enumerate any Core direct-route and map-thread corner case that would force a fail-visible response.
