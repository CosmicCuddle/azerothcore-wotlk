// Tests only: no WoW Core includes, no packets, no DB and no runtime listener.
// A model send-attempt witness is NEVER a verified network/client ACK.
#include "F14CorePassScopeProvenanceModel.h"
#include <cstdint>
#include <iostream>
#include <limits>
#include <string>

using namespace CosmicF14CorePassScopeModel;

int main()
{
    int cases = 0;
    int fails = 0;
    auto check = [&](bool condition, char const* message)
    {
        ++cases;
        if (!condition)
        {
            ++fails;
            std::cerr << "FAIL: " << message << '\n';
        }
    };

    Viewer const alice{101, 2001};
    Viewer const bob{102, 2002};

    // All three ACTUAL batched Core entrances modelled separately.
    Tracker t;
    std::uint64_t previous = 0;
    for (Route route : {Route::NormalFullScan, Route::PlayerRelocation,
                        Route::SharedVisionRelocation})
    {
        std::uint64_t token = 0;
        {
            ScopedPass pass(t, alice, route, true);
            token = pass.Token();
            check(token != 0 && token > previous, "batched route mints fresh token");
            check(pass.ObserveTaggedSendAttempt(true, true), "model tagged send call within pass");
            check(!pass.ObserveTaggedSendAttempt(false, true), "no source callback: no evidence");
            check(!pass.ObserveTaggedSendAttempt(true, false), "empty batch: no evidence");
        }
        check(t.ActiveCount() == 0, "destructor closes batched pass");
        check(!t.ObserveTaggedSendAttempt(alice, token, true, true),
              "closed token rejects late same-tag send call");
        previous = token;
    }

    // A single viewer never borrows token provenance from another viewer.
    {
        ScopedPass first(t, alice, Route::SharedVisionRelocation, true);
        ScopedPass second(t, bob, Route::SharedVisionRelocation, true);
        check(first.Token() != 0 && second.Token() != 0 && first.Token() != second.Token(),
              "independent shared-vision viewers have distinct pass tokens");
        check(!t.ObserveTaggedSendAttempt(bob, first.Token(), true, true),
              "cross viewer send is not attributed");
        check(!t.ObserveTaggedSendAttempt(Viewer{alice.guid, 42}, first.Token(), true, true),
              "wrong login epoch is not attributed");
        check(t.ActiveCount() == 2, "simultaneous distinct viewers remain independent");
    }
    check(t.ActiveCount() == 0, "both shared vision scopes close");

    {
        ScopedPass immediate(t, alice, Route::ImmediateDirect, true);
        check(immediate.Token() == 0, "unbatched direct visibility has no notifier token");
        check(!immediate.ObserveTaggedSendAttempt(true, true), "immediate path cannot borrow batch evidence");
    }
    {
        ScopedPass disabled(t, alice, Route::NormalFullScan, false);
        check(disabled.Token() == 0, "manual/default OFF path is inert");
    }
    {
        ScopedPass skipped(t, alice, Route::PlayerRelocation, true, false);
        check(skipped.Token() == 0, "early skipped movement has no initiated pass");
    }
    {
        ScopedPass empty(t, alice, Route::NormalFullScan, true);
        check(empty.Token() != 0, "empty batch can have initiated scope");
        check(!empty.ObserveTaggedSendAttempt(false, false), "no-data does not produce send evidence");
    }
    check(t.ActiveCount() == 0, "zero-data pass closes and does not leak identity");

    // Re-entrant same-viewer pass must fail closed, including the outer scope.
    {
        ScopedPass outer(t, alice, Route::NormalFullScan, true);
        ScopedPass nested(t, alice, Route::PlayerRelocation, true);
        check(outer.Token() != 0 && nested.Token() == 0,
              "nested same-viewer pass rejected");
        check(!outer.ObserveTaggedSendAttempt(true, true),
              "nested ambiguity poisons outer evidence");
        check(!nested.ObserveTaggedSendAttempt(true, true),
              "nested rejected pass has no send evidence");
    }
    check(t.ActiveCount() == 0, "nested cancellation cannot poison the next pass");
    {
        ScopedPass next(t, alice, Route::NormalFullScan, true);
        check(next.Token() > previous, "new pass can restart with strictly new token");
        check(next.ObserveTaggedSendAttempt(true, true), "new independent pass recovers");
    }

    // An old end callback must not close a newer pass of same viewer.
    {
        std::uint64_t const old = t.Begin(alice, Route::NormalFullScan, true, true);
        t.Close(alice, old);
        std::uint64_t const fresh = t.Begin(alice, Route::NormalFullScan, true, true);
        check(old != fresh && fresh != 0, "new token after close");
        t.Close(alice, old);
        check(t.ObserveTaggedSendAttempt(alice, fresh, true, true),
              "stale end does not revoke fresh pass");
        t.Close(Viewer{alice.guid, alice.loginEpoch+1}, fresh);
        check(t.ObserveTaggedSendAttempt(alice, fresh, true, true),
              "wrong epoch close does not revoke fresh pass");
        t.Close(alice, fresh);
        check(!t.ObserveTaggedSendAttempt(alice, fresh, true, true),
              "fresh close invalidates its own token");
    }

    {
        std::uint64_t const token = t.Begin(alice, Route::NormalFullScan, true, true);
        t.RetireViewer(Viewer{alice.guid, alice.loginEpoch + 1});
        check(t.ObserveTaggedSendAttempt(alice, token, true, true),
              "old/wrong viewer retirement must not erase new epoch");
        t.RetireViewer(alice);
        check(!t.ObserveTaggedSendAttempt(alice, token, true, true),
              "matching viewer logout revokes evidence");
        Viewer nextLogin{alice.guid, alice.loginEpoch + 1};
        std::uint64_t const token2 = t.Begin(nextLogin, Route::PlayerRelocation, true, true);
        check(token2 > token, "relogin never reuses old token");
        check(!t.ObserveTaggedSendAttempt(alice, token2, true, true),
              "old epoch denied on new pass");
        t.Close(nextLogin, token2);
    }

    // Cross-epoch nested observer is also ambiguous and fails closed.
    {
        ScopedPass old(t, alice, Route::NormalFullScan, true);
        ScopedPass newer(t, Viewer{alice.guid, alice.loginEpoch+1},
                         Route::NormalFullScan, true);
        check(newer.Token() == 0, "cross-epoch nested scope is not borrowed");
        check(!old.ObserveTaggedSendAttempt(true, true), "cross-epoch nesting poisons old provenance");
    }

    // No token may wrap to zero or repeat after UINT64_MAX; exhaustion is permanent.
    {
        Tracker nearWrap(std::numeric_limits<std::uint64_t>::max());
        std::uint64_t const last = nearWrap.Begin(alice, Route::NormalFullScan, true, true);
        check(last == std::numeric_limits<std::uint64_t>::max(),
              "last nonzero token may be used once");
        nearWrap.Close(alice, last);
        check(nearWrap.Begin(alice, Route::NormalFullScan, true, true) == 0,
              "token exhaustion refuses wrap and later passes");
    }
    {
        Tracker invalid(0);
        check(invalid.Begin(alice, Route::NormalFullScan, true, true) == 0,
              "uninitialised/invalid token sequence refuses all");
    }

    // Unknown viewer, epoch or no opt-in never produces a pass.
    check(t.Begin(Viewer{0, 2001}, Route::NormalFullScan, true, true) == 0,
          "zero viewer GUID denied");
    check(t.Begin(Viewer{101, 0}, Route::NormalFullScan, true, true) == 0,
          "zero login epoch denied");
    check(t.Begin(alice, Route::NormalFullScan, false, true) == 0,
          "default OFF refuses mint");

    std::cout << "F14 CORE PASS TOKEN PURE MODEL: " << cases
              << " expected cases, " << fails << " failures; no runtime/client evidence\n";
    return fails == 0 ? 0 : 1;
}
