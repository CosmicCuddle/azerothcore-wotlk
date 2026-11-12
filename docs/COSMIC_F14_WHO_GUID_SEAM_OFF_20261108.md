# CosmicCuddle F14 WHO GUID source boundary — 8 November 2026

**Status: DEVELOPMENT DRAFT, OFF by default.** Exact Core parent: `4e0a3452ea50299e9a80cb250927417d3cc4b652` ([Core #4](https://github.com/CosmicCuddle/azerothcore-wotlk/pull/4)). Core's existing creature client-visibility experiment and paid receipt protections remain unchanged.

## Change

The 3.3.5a `WorldSession::HandleWhoOpcode` uses entries from `sWhoListCacheMgr->GetWhoList()`. Each entry has a cached GUID and race. This child adds a **compile-time OFF** optional query before `matchCount++`. If a *future approved* script returns true, the filtered row is neither counted nor serialized; the existing maximum and all original search/permission filters remain unchanged.

The new `WorldObjectScript` default-FALSE method accepts **viewer Player pointer + cached target GUID + cached race**. A cached GUID is only a lookup key; it is NOT positive proof that the entry was first-created as an authentic headless RandomBot, nor that its save transaction was committed. No one currently overrides this method, so **even compiling the experiment ON has no positive hiding effect**. It is **not** permission to enable the macro.

## Safety boundary

- Do not hide real Blood Elf/Draenei human players, Altbots, GM/self, or approved TBC+ observers. Do not hide legacy race-10/11 saved characters based on appearance.
- Core only asks about race 10/11 entries for normal non-GM human sessions and non-self/non-empty target GUIDs. A future approved IP/Playerbots listener must independently prove the genuinely eligible RNDBOT GUID + viewer effective era. This cache cannot prove those facts.
- **All normal builds leave `COSMIC_F14_WHO_GUID_QUERY_EXPERIMENT` undefined**. This branch registers no positive consumer, does not write stage 8, change Playerbots, alter `CanSeeOrDetect`, mutate SQL or hide hostile creatures.
- Future first-birth durable DB receipt, Core object visibility, independent human rights, combat/corpse/respawn restoration and actual 3.3.5a two-human gameplay remain blocked.
- Source tests only show placement/defaults. A separately reviewed full five-owner worldserver native build and owner-approved isolated client test are needed before any activation.

## Rollback / preservation

Discard this separate child DRAFT; Core #4 remains unmodified. No production backup is claimed. Before any owner-PC updates, inventory and preserve **all** tracked, untracked and ignored local custom SQL/cleanup files, configs, binaries, DBC/client patches, DB snapshots and a recoverable restoration path. No merge, config, database, host or gameplay operation is authorised here.
