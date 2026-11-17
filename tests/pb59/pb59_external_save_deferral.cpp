// PB59 native ledger policy regression: independent saves may not enter
// while an external owner holds an economic scope. Pure ledger-only test:
// no Player, WorldSession, TradeHandler, real DB commit or gameplay.
#include "PB59TransactionAdmission.h"

#include <iostream>
using Ledger = PB59::TransactionAdmission;
using Work = Ledger::Work;
using Decision = Ledger::Decision;
using Scope = Ledger::ExternalScope;

namespace
{
int checks = 0;
bool Check(bool result, char const* name)
{
    ++checks;
    if (!result)
        std::cerr << "FAIL " << checks << " " << name << '\n';
    return result;
}
}

int main()
{
    bool ok = true;
    auto test = [&](bool success, char const* name) { ok = Check(success, name) && ok; };
    // Idle paths remain unchanged.
    Ledger ordinary;
    {
        Scope scheduled(ordinary, Work::ScheduledSave);
        test(scheduled.Admitted(), "idle scheduled save admitted");
    }
    {
        Scope standalone(ordinary, Work::StandaloneSave);
        test(standalone.Admitted(), "idle standalone save admitted");
    }
    test(!ordinary.PendingSave(), "idle saves do not fabricate deferrals");

    // A previously owned operation holds an economic scope but phase Idle.
    Ledger ongoing;
    {
        Scope trade(ongoing, Work::InventoryAndGold);
        test(trade.Admitted(), "outer economic producer admitted");
        test(ongoing.Request(Work::ScheduledSave) == Decision::DeferScheduledSave,
             "scheduled save deferred behind economic producer");
        test(!ongoing.PendingSave(), "scheduled tick does not manufacture standalone intent");
        {
            Scope scheduled(ongoing, Work::ScheduledSave);
            test(!scheduled.Admitted() && scheduled.Result() == Decision::DeferScheduledSave,
                 "scheduled scope cannot enter a dirty external producer");
        }
        test(ongoing.Request(Work::StandaloneSave) == Decision::DeferStandaloneSave,
             "standalone save coalesces while producer active");
        test(ongoing.PendingSave(), "standalone replay intent survives producer");
        {
            Scope standalone(ongoing, Work::StandaloneSave);
            test(!standalone.Admitted() && standalone.Result() == Decision::DeferStandaloneSave,
                 "standalone scope cannot race an admitted producer");
        }
        test(ongoing.Request(Work::Logout) == Decision::DeferLogout,
             "pending logout remains deferred behind producer");
        test(ongoing.PendingSave() && ongoing.PendingLogout(),
             "both independent native intents survive");
    }
    test(ongoing.PendingSave() && ongoing.PendingLogout(), "unwind does not erase deferred intents");
    {
        Scope settle(ongoing, Work::StandaloneSave);
        test(settle.Admitted(), "standalone replay permitted after prior owner unwinds");
        test(settle.CompleteDeferred(), "only matching save replay clears pending save");
    }
    test(!ongoing.PendingSave() && ongoing.PendingLogout(),
         "replaying save cannot silently erase logout");
    {
        Scope logout(ongoing, Work::Logout);
        test(logout.Admitted(), "explicit logout replay admitted after save");
        test(logout.SealOwner(), "native replay can seal only after settling work");
    }
    test(ongoing.OwnerSealed() && !ongoing.PendingLogout(),
         "settled logout seals owner and consumes intent");
    test(ongoing.Request(Work::ScheduledSave) == Decision::RejectBeforeMutation,
         "sealed owner rejects even scheduled save");

    // All retirement phases disallow an independently starting save.
    Ledger retiring;
    {
        auto lease = retiring.TryAcquireRetirement();
        test(bool(lease), "retirement ownership reserved");
        test(retiring.Request(Work::ScheduledSave) == Decision::DeferScheduledSave,
             "scheduled save deferred while retirement reserved");
        test(retiring.Request(Work::StandaloneSave) == Decision::DeferStandaloneSave,
             "standalone save deferred while retirement reserved");
        test(retiring.PendingSave(), "retirement save coalescing is preserved");
        test(lease.MarkMutation(), "retirement advances to mutation");
        test(retiring.Request(Work::ScheduledSave) == Decision::DeferScheduledSave,
             "scheduled save deferred while retirement mutating");
        test(lease.Complete(), "retirement marks completed but token retained");
        test(retiring.Request(Work::StandaloneSave) == Decision::DeferStandaloneSave,
             "independent save deferred before retirement token release");
    }
    test(!retiring.RetirementInProgress(), "retirement lease released");
    {
        Scope settle(retiring, Work::StandaloneSave);
        test(settle.Admitted(), "retirement save replay admitted after token release");
        test(settle.CompleteDeferred(), "retirement replay clears only its own save");
    }
    test(!retiring.PendingSave(), "retirement deferred save no longer pending");
    std::cout << "checks=" << checks << " status=" << (ok ? "PASS" : "FAIL") << '\n';
    return ok ? 0 : 1;
}
