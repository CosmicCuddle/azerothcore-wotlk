# F14 Core fork — first default-FALSE hook port, intentionally UNWIRED (7 November 2026)

**Owner:** CosmicCuddle. **Repository:** `CosmicCuddle/azerothcore-wotlk`. **Development base:** [owner Core paid receipt DRAFT #1](https://github.com/CosmicCuddle/azerothcore-wotlk/pull/1) exact `1e37f3343cf08618101b625601cd299f0a1770ca`, itself based on pinned original Grimfeather Core `0d754d1921261dfd939f91060ebc022eae280f71`. **Status:** child review-only draft; NO merge, no installed/runtime feature, no SQL/DBC, config, MySQL or client change.

## Why these three small Core source edits

Original [IP #123](https://github.com/CosmicCuddle/mod-individual-progression/pull/123) `tests/core_patches/apply_f14_viewer_suppression_attribution.py` introduced a more extensive **disposable CI-only** Core F14 suppression interface plus observer/visibility changes and expected multiple earlier overlays. Replaying that patch on a new owner Core fork directly would be unsafe: original owners' real positive RNDBOT actor proof, full human rights and permanent previously-hidden viewer/creature restoration protocol are not yet connected or accepted. This draft ports **ONLY** the default-false callback *declaration/dispatch*, with **zero callers in Player visibility**.

## Exact affected Core runtime files

1. `src/server/game/Scripting/ScriptMgr.h`: add `[[nodiscard]] bool ShouldF14SuppressCreatureForViewer(Player const*, WorldObject const*)` declaration in existing WorldObjectScript section.
2. `src/server/game/Scripting/ScriptDefines/WorldObjectScript.h`: append one `WORLDOBJECTHOOK_ON_F14_VIEWER_CREATURE_SUPPRESSION_QUERY` before existing `WORLDOBJECTHOOK_END`, retain all five existing lifecycle hooks, add default-FALSE virtual `OnF14ShouldSuppressCreatureForViewer` to WorldObjectScript.
3. `src/server/game/Scripting/ScriptDefines/WorldObjectScript.cpp`: define dispatch using pinned existing Core `CALL_ENABLED_BOOLEAN_HOOKS_WITH_DEFAULT_FALSE`. **No registered positive listener.** This function is **not invoked anywhere by this draft**. It changes no `PlayerUpdates.cpp`, `Player::CanSeeOrDetect`, `IsVisibleGloballyFor`, `WorldSession`, `Map::Update` or client packet path.

**No user-facing effect by construction**, even if a future module were to override this callback: a positive override is inert until a separate reviewed call site and rights/identity/restoration safety package is added. For now it is an API foothold only, not F14 implementation, no attack/quest/loot eligibility and no staged hide. Do not assert it makes the original ten disposable overlays portable or preserves correct observer-specific visibility by itself.

## Required future integration gates (NOT authorised by this draft)

- **Core/Playerbots:** positively verified and safely maintained current *genuine* race10/11 headless **RandomBot-only** actor, not merely race/name/account/session; test new versus saved bot GUID, lifecycle/restart and map-worker-safe immutable classification. Owner PB #3 is a default-OFF **same-process diagnostic**, not durable birth/DB commit proof.
- **IP:** single authoritative observer-specific per-character earned/paid era policy, TBC eligibility, starting floor without mutating saved0–7 or earned8–18 silently; exact previous-hidden viewer/creature matching record from original Claude [IP #94](https://github.com/CosmicCuddle/mod-individual-progression/pull/94). No positive provider or concealment enabled before these proofs.
- **Core visibility:** both direct and batched updates, full human targeting/tap/quest-loot/group/auras/pets/DoTs/corpse safeguards, trustworthy serialized map owner, pre-unlink handoff, no stale hide after restore; default **VISIBLE** when uncertain. Never hide real Blood Elf/Draenei humans or Altbots.
- **WoW client:** F01–F20 tests including /who, NPC attack/disengage/death/corpse/respawn, quest855 two-human loot sparkle and visibility, client rendering/targeting (a server packet send attempt is not a client acknowledgment).
- **Releases:** separately build **exact Core/IP/PB/Naxx/Services** tuple at native S4, compare actual installed owner Core & other modules/config/DBC/SQL, get recoverable backups and explicit owner approval for an offline test before S5–S7. No production deployment/merge from this draft.

## Evidence, safety and rollback

- Add `tests/cosmic_f14_unwired_hook/test_f14_unwired_hook_source.py`, source-only mutation/invariant tests, and one workflow limiting changed paths to those three Core files plus tests/docs. It explicitly verifies lack of invocation in both PlayerUpdates and baseline WorldObject object visibility, and no existing original WorldObject lifecycle hook deleted.
- **Expected proof:** source checks alone; no C++ ABI/full Worldserver verification until a separate exact-head native build. This 3-file Core API change is not in the older clean five-fork green native tuple.
- **Rollback:** discard this isolated child branch/DRAFT and retain parent owner Core #1 unchanged; no database changes to reverse. If ever separately approved for installation, prepare a known-good server binary/config/DB recovery path before deployment and follow owner change control.

**Existing shared issues:** [F14 #9](https://github.com/CosmicCuddle/Naxxramas-Server-Coordination/issues/9), [combined test readiness #4](https://github.com/CosmicCuddle/Naxxramas-Server-Coordination/issues/4), [five-repository routing DRAFT #40](https://github.com/CosmicCuddle/Naxxramas-Server-Coordination/pull/40). Never overwrite or duplicate owner Naxx master #28.
