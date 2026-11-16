#include "PB59OriginalCombatEffects.h"
#include <cassert>
#include <iostream>
#include <stdexcept>

int main()
{
    using Journal = PB59::OriginalCombatEffects;
    using R = Journal::Result;
    using P = Journal::Phase;
    unsigned checks = 0;
    auto check = [&](bool ok) { assert(ok); ++checks; };
    for (bool restoring : {false, true})
        for (int fault = -1; fault <= 5; ++fault)
        {
            Journal j;
            int setCount = 2, stat = 30, calls = 0, observations = 0;
            auto verify = [&]() { return true; };
            auto set = [&](bool apply) { setCount += apply ? 1 : -1; };
            auto mods = [&](bool apply) { stat += apply ? 10 : -10; };
            if (restoring)
                check(j.Run(1, 42, false, verify, set, mods) == R::Complete);
            int const oldSet = setCount, oldStat = stat;
            auto checked = [&]() { ++observations; return !(fault == 0 && observations == 1) && !(fault == 3 && observations == 2) && !(fault == 5 && observations == 3); };
            auto changeSet = [&](bool apply) { ++calls; if (fault == 1) throw std::runtime_error("before set"); set(apply); if (fault == 2) throw std::runtime_error("after set"); };
            auto changeMods = [&](bool apply) { ++calls; mods(apply); if (fault == 4) throw std::runtime_error("after mods"); };
            auto result = j.Run(1, 42, restoring, checked, changeSet, changeMods);
            check(result == (fault == -1 ? R::Complete : fault == 0 ? R::Rejected : R::RecoveryRequired));
            if (fault == 0)
            {
                check(calls == 0 && setCount == oldSet && stat == oldStat);
                check(j.State() == (restoring ? P::Removed : P::Fresh));
                check(j.Run(1, 42, restoring, verify, set, mods) == R::Complete);
            }
            else if (fault == -1)
            {
                check(calls == 2 && setCount == oldSet + (restoring ? 1 : -1) && stat == oldStat + (restoring ? 10 : -10));
                check(j.State() == (restoring ? P::Restored : P::Removed));
                check(j.Run(1, 42, restoring, verify, set, mods) == R::Rejected);
            }
            else
            {
                check(j.State() == P::Uncertain);
                int const a = setCount, b = stat;
                check(j.Run(1, 42, false, verify, set, mods) == R::Rejected);
                check(j.Run(1, 42, true, verify, set, mods) == R::Rejected);
                check(setCount == a && stat == b);
            }
        }
    Journal j;
    auto yes = [] { return true; };
    auto noOp = [](bool) {};
    check(j.Run(0, 42, false, yes, noOp, noOp) == R::Rejected);
    check(j.Run(1, 0, false, yes, noOp, noOp) == R::Rejected);
    check(j.Run(1, 42, true, yes, noOp, noOp) == R::Rejected);
    check(j.Run(1, 42, false, yes, noOp, noOp) == R::Complete);
    check(j.Run(2, 42, true, yes, noOp, noOp) == R::Rejected);
    check(j.Run(1, 43, true, yes, noOp, noOp) == R::Rejected);
    check(j.Run(1, 42, true, yes, noOp, noOp) == R::Complete);
    // Normal completed rollback is reusable only after an independent
    // stable native proof, never merely because the journal says Restored.
    check(j.State() == P::Restored);
    check(!j.RearmAfterVerifiedRestore(2, 42, yes));
    check(!j.RearmAfterVerifiedRestore(1, 43, yes));
    check(!j.RearmAfterVerifiedRestore(1, 42, [] { return false; }));
    check(!j.RearmAfterVerifiedRestore(1, 42, []() -> bool {
        throw std::runtime_error("not stable");
    }));
    check(j.State() == P::Restored);
    check(j.RearmAfterVerifiedRestore(1, 42, yes));
    check(j.State() == P::Fresh);
    check(!j.Matches(1, 42));
    check(!j.RearmAfterVerifiedRestore(1, 42, yes));
    check(j.Run(2, 99, false, yes, noOp, noOp) == R::Complete);
    check(j.Run(2, 99, true, yes, noOp, noOp) == R::Complete);
    check(j.RearmAfterVerifiedRestore(2, 99, yes));
    // Removal-only and ambiguous state can never be re-armed by a guess.
    Journal removed;
    check(removed.Run(3, 77, false, yes, noOp, noOp) == R::Complete);
    check(!removed.RearmAfterVerifiedRestore(3, 77, yes));
    check(removed.State() == P::Removed);
    Journal uncertain;
    auto fail = [](bool) { throw std::runtime_error("partial set"); };
    check(uncertain.Run(4, 88, false, yes, fail, noOp) == R::RecoveryRequired);
    check(!uncertain.RearmAfterVerifiedRestore(4, 88, yes));
    check(uncertain.State() == P::Uncertain);
    check(uncertain.Run(4, 88, true, yes, noOp, noOp) == R::Rejected);
    std::cout << checks << " combat effect journal checks passed\n";
}
