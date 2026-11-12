#ifndef COSMIC_F14_PLAYER_CLIENT_VISIBILITY_QUERY_H
#define COSMIC_F14_PLAYER_CLIENT_VISIBILITY_QUERY_H

// Pure negative preconditions only: NOT playerbot identity, paid tier,
// effective observer era, gameplay rights, DB receipt or map restoration.
namespace CosmicF14PlayerClientVisibilityQuery
{
struct Candidate
{
    bool targetIsPlayer;
    bool sameIdentity;
    bool gmModeViewer;
    bool viewerHeadless;
    bool targetHeadless;
    bool selectedTbcRace;
    bool gmModeTarget;
};
constexpr bool MayAskScript(Candidate const& c)
{
    return c.targetIsPlayer && !c.sameIdentity && !c.gmModeViewer &&
        !c.viewerHeadless && c.targetHeadless && c.selectedTbcRace &&
        !c.gmModeTarget;
}
}
#endif
