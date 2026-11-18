// PB59 ledger-only preflight tests. No WorldSession, Player lifetime, items or DB.
#include "PB59TwoOwnerTradePreflight.h"
#include <iostream>
#include <memory>
#include <string_view>

using Ledger = PB59::TransactionAdmission;
using Preflight = PB59::TwoOwnerTradePreflight;
using Work = Ledger::Work;
using Decision = Ledger::Decision;

int main()
{
    int checks = 0;
    bool success = true;
    auto check = [&](bool cond, std::string_view label) {
        ++checks;
        if (!cond) { success = false; std::cerr << "FAIL " << checks << " " << label << '\n'; }
    };
    Ledger first, second;
    check(first.OwnerIncarnation() != 0 && second.OwnerIncarnation() != 0,
        "nonzero owner incarnation");
    check(first.OwnerIncarnation() != second.OwnerIncarnation(), "distinct owner incarnation");
    {
        Preflight pair(first, second);
        check(pair.Admitted() && !pair.Revoked(), "pair admitted");
        check(first.Request(Work::InventoryMutation) == Decision::RejectBeforeMutation,
              "first owner cannot admit a nested new mutation");
        check(second.Request(Work::InventoryAndGold) == Decision::RejectBeforeMutation,
              "second owner cannot admit a nested new economic scope");
        check(first.Request(Work::StandaloneSave) == Decision::DeferStandaloneSave,
              "first concurrent standalone save deferred");
        check(second.Request(Work::ScheduledSave) == Decision::DeferScheduledSave,
              "second concurrent timer save deferred");
        check(pair.Admitted(), "both admitted while owned and unchanged");
    }
    check(first.PendingSave(), "first deferred save intent survived unwind");
    check(!second.PendingSave(), "timer deferral did not fabricate a pending save");
    check(first.Request(Work::InventoryMutation) == Decision::RejectBeforeMutation,
          "new economic request cannot leapfrog deferred save");
    {
        Ledger::ExternalScope replay(first, Work::StandaloneSave);
        check(replay.Admitted(), "save replay after pair unwinds");
        check(replay.CompleteDeferred(), "replay explicitly clears pending save");
    }
    check(first.Request(Work::InventoryMutation) == Decision::Admit,
          "new operation eligible after replay");
    {
        Preflight same(first, first);
        check(!same.Admitted() && !same.Revoked(), "same ledger rejected");
        check(first.Request(Work::InventoryMutation) == Decision::Admit,
              "same-ledger denial did not retain an external scope");
    }
    {
        Ledger blocked, freeOwner;
        Ledger::ExternalScope held(blocked, Work::InventoryAndGold);
        check(held.Admitted(), "preexisting first owner scope admitted");
        Preflight pair(blocked, freeOwner);
        check(!pair.Admitted(), "busy first owner blocks pair");
        check(freeOwner.Request(Work::InventoryMutation) == Decision::Admit,
              "busy-first denial did not touch second owner");
    }
    {
        Ledger freeOwner, blocked;
        Ledger::ExternalScope held(blocked, Work::InventoryAndGold);
        check(held.Admitted(), "preexisting second owner scope admitted");
        Preflight pair(freeOwner, blocked);
        check(!pair.Admitted(), "busy second owner blocks pair");
        check(freeOwner.Request(Work::InventoryMutation) == Decision::Admit,
              "busy-second denial released first owner scope");
    }
    {
        Ledger deferred, freeOwner;
        {
            Ledger::ExternalScope original(deferred, Work::InventoryAndGold);
            check(original.Admitted(), "original deferred-save producer admitted");
            check(deferred.Request(Work::StandaloneSave) == Decision::DeferStandaloneSave,
                  "original producer defers standalone save");
        }
        Preflight pair(deferred, freeOwner);
        check(!pair.Admitted(), "unsettled save blocks pair before first mutation");
        check(freeOwner.Request(Work::InventoryMutation) == Decision::Admit,
              "denied deferred-save pair leaves second owner free");
    }
    {
        Ledger reserved, freeOwner;
        auto lease = reserved.TryAcquireRetirement();
        check(bool(lease), "retirement lease acquired");
        Preflight pair(freeOwner, reserved);
        check(!pair.Admitted(), "retired owner blocks pair");
        check(freeOwner.Request(Work::InventoryMutation) == Decision::Admit,
              "retirement denial releases initial owner");
    }
    {
        Ledger keep, logOut;
        // Distinct pending logout must block independent economic preflight.
        Ledger::ExternalScope held(logOut, Work::InventoryAndGold);
        check(held.Admitted(), "logout scenario scope admitted");
        check(logOut.Request(Work::Logout) == Decision::DeferLogout,
              "logout deferred under existing owner scope");
        // Can't start a new pair when the old owner still exists.
        Preflight pair(keep, logOut);
        check(!pair.Admitted(), "pending logout rejects a new pair");
    }
    {
        // A separate lifetime sentinel test is deliberately only evidence
        // that a revoked scope is not advertised as currently admitted.
        auto dying = std::make_unique<Ledger>();
        Ledger stable;
        Preflight pair(*dying, stable);
        check(pair.Admitted(), "preflight admitted before callback destruction");
        dying.reset();
        check(!pair.Admitted() && pair.Revoked(),
              "destroyed ledger makes preflight irrevocably not admitted");
    }
    {
        Ledger stable;
        auto dying = std::make_unique<Ledger>();
        Preflight pair(stable, *dying);
        check(pair.Admitted(), "second-owner preflight admitted before destruction");
        dying.reset();
        check(!pair.Admitted() && pair.Revoked(),
              "destroyed second owner invalidates preflight");
    }
    std::cout << "checks=" << checks << " status=" << (success ? "PASS" : "FAIL") << '\n';
    return success ? 0 : 1;
}
