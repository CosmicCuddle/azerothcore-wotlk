// Exact native TransactionAdmission policy regression for external reentry.
// This is a ledger-only fixture: no Player, WorldSession, TradeHandler or DB.
#include "PB59TransactionAdmission.h"

#include <array>
#include <iostream>

using Ledger = PB59::TransactionAdmission;
using Work = Ledger::Work;
using Decision = Ledger::Decision;
using Scope = Ledger::ExternalScope;

namespace
{
int checks = 0;
bool Verify(bool good, char const* description)
{
    ++checks;
    if (!good)
        std::cerr << "FAIL " << checks << ": " << description << '\n';
    return good;
}

constexpr std::array<Work, 4> Economic
{
    Work::CallerOwnedSave, Work::InventoryAndGold,
    Work::GoldOnly, Work::InventoryMutation
};

bool AllNewEconomicRefused(Ledger& owner)
{
    bool ok = true;
    for (Work work : Economic)
    {
        Scope attempted(owner, work);
        ok = Verify(!attempted.Admitted(), "no new economic admission") && ok;
        ok = Verify(attempted.Result() == Decision::RejectBeforeMutation,
                    "rejected before native side effect") && ok;
    }
    return ok;
}
}

int main()
{
    bool ok = true;
    auto check = [&](bool result, char const* message)
    {
        ok = Verify(result, message) && ok;
    };

    // Stock idle behaviour must not be broken: each category remains available
    // as an independent new request once its previous scope has unwound.
    Ledger idle;
    for (Work work : Economic)
    {
        Scope normal(idle, work);
        check(normal.Admitted(), "idle economic request still admitted");
        check(normal.Result() == Decision::Admit, "idle result remains Admit");
    }

    // A callback may be nested inside an admitted external producer, but a
    // second trade, gold mutation, inventory mutation or caller-owned save is
    // a NEW operation, never the original caller's already-owned DB append.
    Ledger active;
    {
        Scope first(active, Work::InventoryAndGold);
        check(first.Admitted(), "first economic scope admitted");
        check(active.Request(Work::Callback) == Decision::Admit,
              "non-economic callback remains admitted while first scope active");
        check(AllNewEconomicRefused(active), "nested economic scopes all refused");
        check(first.Admitted(), "outer scope remains admitted after nested refusals");
        check(active.Request(Work::Logout) == Decision::DeferLogout,
              "logout requested in admitted scope remains deferred");
        check(active.PendingLogout(), "pending logout intent retained");
        check(AllNewEconomicRefused(active), "new work still refused with logout pending");
    }
    check(active.PendingLogout(), "deferred logout survives scope unwind");
    check(AllNewEconomicRefused(active), "later economic operation cannot leapfrog logout");

    // Owner retirement has three non-released phases. None permits an
    // unrelated economic request, even if there is no external scope.
    Ledger retiring;
    {
        auto retirement = retiring.TryAcquireRetirement();
        check(bool(retirement), "retirement was reserved");
        check(AllNewEconomicRefused(retiring), "economic request denied during reserved retirement");
        check(retirement.MarkMutation(), "retirement moves to mutating");
        check(AllNewEconomicRefused(retiring), "economic request denied during mutating retirement");
        check(retirement.Complete(), "producer completes retirement token");
        check(AllNewEconomicRefused(retiring), "economic request denied until lease releases");
    }
    check(!retiring.RetirementInProgress(), "completed retirement lease released");
    for (Work work : Economic)
    {
        Scope permitted(retiring, work);
        check(permitted.Admitted(), "fresh economic request admitted after release");
    }

    // Ledger's existing recovery-required rule remains a stronger exclusion.
    Ledger recovering;
    {
        Ledger::MutationScope mutation(recovering);
        check(mutation.GetTicket() != 0, "separate live mutation ticket admitted");
        check(AllNewEconomicRefused(recovering), "active mutation blocks external economy");
    }
    check(recovering.State() == Ledger::Phase::RecoveryRequired,
          "unfinished mutation quarantined");
    check(AllNewEconomicRefused(recovering), "quarantined economy remains blocked");

    std::cout << "checks=" << checks << " status=" << (ok ? "PASS" : "FAIL") << '\n';
    return ok ? 0 : 1;
}
