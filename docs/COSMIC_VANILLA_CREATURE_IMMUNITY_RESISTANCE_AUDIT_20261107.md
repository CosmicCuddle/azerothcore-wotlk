# CosmicCuddle — original Vanilla creature immunity/resistance source contract (7 Nov 2026)

**Owner:** CosmicCuddle. **Status:** DRAFT/UNMERGED, review-only CI and documentation, **no production source or data changes**. **Parent:** owner [Core default-false F14 API DRAFT #2](https://github.com/CosmicCuddle/azerothcore-wotlk/pull/2) exact `d2066943e19cd959de2a0e9897135573e4124219`, which builds with owner PB/Naxx/IP/Services but is **not** verified as installed. Related two-way Information requests [mechanics mask #38](https://github.com/CosmicCuddle/Naxxramas-Server-Coordination/issues/38) and [negative resistances #39](https://github.com/CosmicCuddle/Naxxramas-Server-Coordination/issues/39) have already been **acknowledged** and remain OPEN/BLOCKED pending actual installed data and gameplay. No re-request or new policy is created.

## Risk 1 — confusion of legacy/wiki immunity bits with current runtime indexed bits

Exact owner Core #2 source (`SharedDefines.h`, `SpellMgr.cpp/.h`, `Creature.cpp`) defines the spell mechanic enum and loads SQL `creature_immunities.MechanicsMask` **directly** as an unsigned `uint64` into `std::bitset<MAX_MECHANIC>`, then applies each set bit at the SAME numeric mechanic index. Therefore BLEED (15) uses `1<<15 = 0x8000`, BANDAGE (16) uses `0x10000`, INFECTED (22) uses `0x400000`; wiki/old legacy mask `0x4000` is mechanic **KNOCKOUT (14)** in this loader, not Bleed. Existing spell/effect and pet/owner immunity exceptions must remain intact.

The original IP `vanilla_creature_immunities.sql` documents legacy masks beside converted stored ones (e.g., `0x6206 << 1 == 0xC40C == 50188`). Server-Side-Information's existing **136/136** Classic-era Bleed set/clear audit supports the already converted archive. **Do not double-shift, delete or bulk update current DB rows** based on wiki label values. A source-level match does NOT verify actual loaded SQL, old migration history or installed host.

## Risk 2 — archived negative magic resistance rows and generic stat zero-clamp

Core owner #2 source `ObjectMgr.cpp` reads `creature_template_resistance.Resistance` as `int16` (negative values survive initial DB decode). `Creature.cpp` stores a signed flat per-school base; `StatSystem.cpp` computes and calls `SetResistance(..., int32(value))`; `Unit.h` forwards to `Object::SetStatInt32Value`, which clamps any negative value **to zero** before publishing the unit resistance field. `Unit::GetEffectiveResistChance` then also applies `std::max(victimResistance, 0.0f)` after penetration calculations. No extra spell-damage bonus can be *inferred from this generic path alone*. Creature-specific spells, weakness auras, scripts and true historical 1.x damage numbers remain separate unknowns.

Archived owner-world evidence includes **19 negative resistance rows across 16 creature IDs**, e.g. Tar Beast (6517 Fire -51), Warpwood Treant (11462 Fire -55), Molten War Golem (8908 Frost -140). **Preserve these rows and do not globally remove negative clamping**, which would impact combat calculations for player, pet, bot and creature across the entire 3.3.5a Core. There is no approved numerical Vanilla elemental vulnerability formula or live-gameplay measurement.

## Exact read-only verification

On 7 Nov, CS01 compared the specified read-only public source ranges of Grim Core `0d754d1921261dfd939f91060ebc022eae280f71` with owner Core #2 `d2066943e19cd959de2a0e9897135573e4124219`. Ten relevant ranges (enum, bitset, SQL immunity loader, creature immunity consumer, signed resistance data decode and base, creature resistance update, stat forwarding/zero clamp, and effective resistance calculation) were **byte-for-byte equal**. This source-only CI child converts those facts into **nine direct regression tests** that intentionally fail if future upstream integration changes their assumptions.

`tests/cosmic_vanilla_creature_stats/test_source_mechanics_and_negative_resistance.py` opens only locally checked-out **source text** on the GitHub-hosted runner, asserts exact enum numeric values, direct bitset and exception semantics, signed data path, explicit negative clamp, and preservation of old-mask arithmetic as *a number, not a SQL operation*. It does not load arbitrary files, run SQL, inspect user-provided DB credentials, start Worldserver or run client spells. CI allows **FOUR** paths: Python test, GitHub workflow, this explanatory note, and append-only Custom Modifications Register note. **Check actual Actions results before claiming GREEN.**

## Next safe work before any gameplay correction

1. On an explicitly approved OFFLINE server copy, record exact installed Core+IP+PB+Naxx+Services and **other** active modules (Autobalance/AHBot/Challenge), dirty/untracked custom code and loaded configuration without exposing credentials.
2. Preserve exact world SQL dump and prove which converted creature immunity and signed resistance rows are really loaded.
3. Test correct Bleed-vs-Bandage and Rend/Garrote mechanics with matching spell attributes and bypass exceptions in an offline character/client.
4. Compare repeated noncritical, controlled Fire/Frost spells against representative Vanilla creatures and careful level/resistance controls. Do not treat a single hit/crit/partial resist as a numerical weakness bonus.
5. If gameplay evidence proves a genuine defect, separately propose an owner-approved **narrow, opt-in** content or spell-specific change with a full affected-creature audit, native five-fork compatibility, database/DB backup and rollback plan and explicit CosmicCuddle production approval. **Do not merge any global stat clamp rewrite or SQL mask shift.**

**Confidence:** pinned owner source checks only. **Not installed, not compiled Core Worldserver in this test-only child, not effective SQL, not in-game historical proof.** The separate owner five-fork Services #20 native build green does NOT run these tests and says nothing about creature data correctness.

**Rollback:** discard this DRAFT child without affecting any Core source/gameplay feature. No host or database rollback required because nothing on them was changed.
