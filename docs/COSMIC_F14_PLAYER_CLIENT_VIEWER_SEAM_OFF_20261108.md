# F14 real per-viewer Blood Elf/Draenei RandomBot character client query — 8 Nov 2026

**Owner:** CosmicCuddle. **Status:** child of Core [DRAFT #5](https://github.com/CosmicCuddle/azerothcore-wotlk/pull/5) exact `5dca9d74bb1b676dedeffaa5aa92c128c9a31778`. **DEFAULT OFF / NOT MERGED, DEPLOYED OR GAME-TESTED.**

## Reason for a separate Player-character query
Owner Core `Player::IsVisibleGloballyFor` returns TRUE for ordinary visible players **before** the original `OnPlayerNotVisibleGloballyFor` hook can run. The earlier Core4 creature CREATE/OUT query covers only creatures; Core5 /who query covers only WHO cache entries. Neither can conceal the actual RNDBOT **player actor** from a Vanilla human's client. This child prepares an opt-in query at both actual `Player::UpdateVisibilityOf` client CREATE/OUT overloads without modifying global `CanSeeOrDetect`, phases, general combat/action targeting or the original Player early return.

## Narrow negative source gate
The separate `COSMIC_F14_PLAYER_CLIENT_VIEWER_QUERY_EXPERIMENT` is **undefined in all ordinary builds**; if compiled ON it asks a default-FALSE `WorldObjectScript`/ScriptMgr callback. No positive listener is installed. The seven booleans allow a **request** only when target is an actual Player, viewer and target differ, viewer is neither GM-mode nor headless, target is headless and race10/11 and not GM-mode. This **does not prove headless genuine RNDBOT identity**; an Altbot may also be headless. It never establishes observer's effective earned Vanilla stage, historic first-birth commit/absence-of-old-data or any human interaction/loot/assist/pet/DoT rights.

Existing Core4 creature and Core5 /who probes remain separately disabled and must eventually share one positively trusted IP identity+observer-era authority. **A client-only hide is NOT permission to remove targetability or hide an NPC**. Active implementation requires full per-viewer interaction controls and previously-hidden client restoration across death/corpse, stop attack, stage switch, logout, respawn, map/instance and errors. Never enable macros without a reviewed unified end-to-end policy and owner's approval.

## Scope, tests, rollback
10 reviewed paths: five modified Core source/register paths, new pure header, new Python source test, new 128-case native C++ test, one source-only workflow, one design note. Existing Core2 six source tests, Core4 five creature tests and Core5 five WHO tests must remain; independent actual five-owner Worldserver compilation and real two-human 3.3.5a acceptance remain required. No SQL/config/DB/DBC/client/MPQ or host modifications.

Rollback only this separate DRAFT; do not reset/clean original checkouts. Before any owner-host operation protect **tracked, untracked AND ignored local custom SQL cleanup/migrations**, effective configs, all installed modules, source/binary/DBC/client assets, database snapshots and tested restoration. Owner approval is mandatory before deployment.
