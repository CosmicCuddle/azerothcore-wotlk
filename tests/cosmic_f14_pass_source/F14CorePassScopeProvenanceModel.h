#ifndef COSMIC_F14_CORE_PASS_SCOPE_PROVENANCE_MODEL_H
#define COSMIC_F14_CORE_PASS_SCOPE_PROVENANCE_MODEL_H

// TESTS ONLY. Not included by any Core runtime compilation unit.
// Model a future source-owned batched visibility traversal. Do not claim that
// the test caller's send-attempt flag is a real Core packet or client ACK.
#include <cstdint>
#include <limits>
#include <map>

namespace CosmicF14CorePassScopeModel
{
enum class Route : std::uint8_t
{
    NormalFullScan,
    PlayerRelocation,
    SharedVisionRelocation,
    ImmediateDirect // Does not traverse VisibleNotifier and cannot borrow its token.
};

struct Viewer
{
    std::uint64_t guid = 0;
    std::uint64_t loginEpoch = 0;
};

class Tracker
{
public:
    explicit Tracker(std::uint64_t nextToken = 1) :
        _nextToken(nextToken), _exhausted(nextToken == 0) {}

    std::uint64_t Begin(Viewer viewer, Route route, bool optIn, bool traversalStarted)
    {
        if (!optIn || !traversalStarted || route == Route::ImmediateDirect ||
            viewer.guid == 0 || viewer.loginEpoch == 0 || _exhausted)
            return 0;

        auto it = _active.find(viewer.guid);
        if (it != _active.end())
        {
            // Never attribute nested or cross-epoch work to the outer pass.
            // The caller must unwind the old scope before opening a new one.
            it->second.poisoned = true;
            return 0;
        }

        std::uint64_t const token = _nextToken;
        if (token == std::numeric_limits<std::uint64_t>::max())
            _exhausted = true;
        else
            ++_nextToken;

        _active.emplace(viewer.guid, Active{viewer.loginEpoch, token, false});
        return token;
    }

    // A future producer must pass the actual post-SendDirectMessage tagged
    // callback fact; booleans in this pure model do NOT prove a packet send.
    // The only possible positive result is a SEND ATTEMPT, not delivery.
    bool ObserveTaggedSendAttempt(Viewer viewer, std::uint64_t token,
                                  bool actualTaggedSendCall, bool batchHadData)
    {
        if (!actualTaggedSendCall || !batchHadData || !token)
            return false;

        auto const it = _active.find(viewer.guid);
        return it != _active.end() && !it->second.poisoned &&
               it->second.epoch == viewer.loginEpoch && it->second.token == token;
    }

    void Close(Viewer viewer, std::uint64_t token)
    {
        auto it = _active.find(viewer.guid);
        if (it != _active.end() && it->second.epoch == viewer.loginEpoch &&
            it->second.token == token)
            _active.erase(it);
    }

    void RetireViewer(Viewer viewer)
    {
        auto it = _active.find(viewer.guid);
        if (it != _active.end() && it->second.epoch == viewer.loginEpoch)
            _active.erase(it);
    }

    std::size_t ActiveCount() const { return _active.size(); }

private:
    struct Active
    {
        std::uint64_t epoch;
        std::uint64_t token;
        bool poisoned;
    };

    std::map<std::uint64_t, Active> _active;
    std::uint64_t _nextToken;
    bool _exhausted;
};

// The future Core owner needs this destructor-like *closing guarantee*.
// This standalone test type NEVER touches a real Player, packet or map.
class ScopedPass
{
public:
    ScopedPass(Tracker& tracker, Viewer viewer, Route route, bool enabled,
               bool traversalStarted = true) :
        _tracker(tracker), _viewer(viewer),
        _token(tracker.Begin(viewer, route, enabled, traversalStarted)) {}

    ScopedPass(ScopedPass const&) = delete;
    ScopedPass& operator=(ScopedPass const&) = delete;

    ~ScopedPass() { _tracker.Close(_viewer, _token); }

    std::uint64_t Token() const { return _token; }

    bool ObserveTaggedSendAttempt(bool actualTaggedSendCall, bool batchHadData)
    {
        return _tracker.ObserveTaggedSendAttempt(
            _viewer, _token, actualTaggedSendCall, batchHadData);
    }

private:
    Tracker& _tracker;
    Viewer _viewer;
    std::uint64_t _token;
};
} // namespace CosmicF14CorePassScopeModel

#endif
