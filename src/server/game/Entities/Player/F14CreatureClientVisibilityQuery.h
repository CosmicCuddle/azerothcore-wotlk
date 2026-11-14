/*
 * CosmicCuddle F14 — preliminary per-viewer CREATURE client update seam.
 *
 * Pure, side-effect-free preconditions only. This does not identify a
 * RandomBot, confer an era, grant loot, hide any creature, or refresh a
 * previously hidden client pair. Never enable a positive decision without
 * the complete audited IP/PB identity, human rights and restore ledger.
 */
#ifndef COSMIC_F14_CREATURE_CLIENT_VISIBILITY_QUERY_H
#define COSMIC_F14_CREATURE_CLIENT_VISIBILITY_QUERY_H

namespace CosmicF14CreatureClientVisibilityQuery
{
    struct Candidate
    {
        bool targetIsCreature = false;
        bool gmModeViewer = false;
        bool ownedOrCharmed = false;
        bool pet = false;
        bool summon = false;
        bool guardian = false;
        bool totem = false;
    };

    // Returning true means "the read-only script MAY be asked", not
    // "the creature may be hidden". The actual script defaults to FALSE.
    [[nodiscard]] constexpr bool MayAskScript(Candidate const& c)
    {
        return c.targetIsCreature && !c.gmModeViewer &&
            !c.ownedOrCharmed && !c.pet && !c.summon &&
            !c.guardian && !c.totem;
    }
}

#endif
