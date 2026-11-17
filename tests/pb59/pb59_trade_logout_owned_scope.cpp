// PB59 test-only two-participant admission/lifecycle interleaving fixture.
// Includes exact native Core TransactionAdmission, not a transcribed policy.
// This does not modify TradeHandler and does NOT prove Player lifetime,
// inventory rollback, DB durability, cross-realm atomicity or gameplay.
#include "PB59TransactionAdmission.h"

#include <iostream>
#include <memory>

using Ledger = PB59::TransactionAdmission;
using Work = Ledger::Work;
using Decision = Ledger::Decision;
using Scope = Ledger::ExternalScope;

namespace
{
int checks = 0;

bool Check(bool result, char const* description)
{
    ++checks;
    if (!result)
        std::cerr << "FAIL " << checks << " " << description << '\n';
    return result;
}

// Test-only mock producer, not a native trade implementation. The only
// evidence it can provide is that both admission scopes were obtained before
// any simulated side effect, and an already-owned scope survives a deferred
// logout *while both ledger objects remain alive*. No DB write is performed.
struct TwoPartyProbe
{
    Scope initiator;
    Scope partner;
    int simulatedEffects = 0;
    int simulatedAppends = 0;

    TwoPartyProbe(Ledger& first, Ledger& second)
        : initiator(first, Work::InventoryAndGold),
          partner(second, Work::InventoryAndGold)
    {
    }

    bool BothAdmitted() const
    {
        return initiator.Admitted() && partner.Admitted();
    }

    bool SimulateMutation()
    {
        if (!BothAdmitted())
            return false;
        ++simulatedEffects;
        return true;
    }

    // Not a request for NEW CallerOwnedSave admission: only model the exact
    // handoff that a real native adapter would have to own before mutation.
    bool SimulateAlreadyOwnedAppend()
    {
        if (!BothAdmitted() || simulatedEffects != 1 || simulatedAppends)
            return false;
        ++simulatedAppends;
        return true;
    }
};
}

int main()
{
    bool ok = true;
    auto test = [&](bool result, char const* title)
    {
        ok = Check(result, title) && ok;
    };

    // Scenario 1: a callback requests logout AFTER two-party admission and
    // mutation but BEFORE the native caller-owned save/append.
    Ledger first;
    Ledger second;
    {
        TwoPartyProbe trade(first, second);
        test(trade.BothAdmitted(), "both parties admitted before first mutation");
        test(trade.SimulateMutation(), "simulated mutation begins only after two admissions");
        test(first.Request(Work::Callback) == Decision::Admit,
             "callback can run while a previously admitted scope is active");
        test(first.Request(Work::Logout) == Decision::DeferLogout,
             "reentrant logout becomes pending, not completed");
        test(first.PendingLogout(), "pending logout persists inside admitted work");
        test(trade.BothAdmitted(), "already-owned scopes survive deferred intent while owners exist");
        test(first.Request(Work::CallerOwnedSave) == Decision::RejectBeforeMutation,
             "fresh caller-owned save admission is forbidden after deferred logout");
        test(first.Request(Work::InventoryAndGold) == Decision::RejectBeforeMutation,
             "a new economic operation is forbidden after deferred logout");
        test(trade.SimulateAlreadyOwnedAppend(),
             "model can finish the already-owned append without a fresh Request");
        test(trade.simulatedAppends == 1 && !trade.SimulateAlreadyOwnedAppend(),
             "mock append is one-shot and never duplicated");
        test(first.PendingLogout(), "producer completion does not silently consume logout");
    }
    test(first.PendingLogout(), "deferred logout survives both producer scope exits");
    test(first.Request(Work::InventoryMutation) == Decision::RejectBeforeMutation,
         "new mutation refused after admitted producer fully unwinds");
    {
        Scope logout(first, Work::Logout);
        test(logout.Admitted(), "explicit replay can enter only after producer ends");
        test(logout.SealOwner(), "replay can seal after pending work is settled");
    }
    test(first.OwnerSealed() && !first.PendingLogout(), "seal clears intent and rejects future work");
    test(first.Request(Work::InventoryAndGold) == Decision::RejectBeforeMutation,
         "sealed first owner refuses further economic activity");

    // Scenario 2: second party is already logging out. First party's
    // successful admission must NEVER authorize mutation on its own.
    Ledger waiting;
    Ledger innocent;
    {
        Scope existing(waiting, Work::InventoryMutation);
        test(existing.Admitted(), "pre-existing second-party scope admitted");
        test(waiting.Request(Work::Logout) == Decision::DeferLogout,
             "second party has pending logout");
    }
    {
        TwoPartyProbe refused(innocent, waiting);
        test(refused.initiator.Admitted() && !refused.partner.Admitted(),
             "one-party success with partner refusal is not two-party admission");
        test(!refused.SimulateMutation() && !refused.SimulateAlreadyOwnedAppend(),
             "refused partner causes zero mock mutations or appends");
        test(refused.simulatedEffects == 0 && refused.simulatedAppends == 0,
             "no partial simulated economic effects on admission failure");
    }
    test(!innocent.PendingLogout() && waiting.PendingLogout(),
         "refused attempt does not fabricate or erase a logout");

    // Scenario 3: a recovery-required ledger is not a trade participant.
    Ledger recovery;
    {
        Ledger::MutationScope mutation(recovery);
        test(mutation.GetTicket() != 0, "incomplete prior mutation receives a real ticket");
    }
    test(recovery.State() == Ledger::Phase::RecoveryRequired,
         "uncompleted prior mutation remains quarantined");
    {
        TwoPartyProbe refused(innocent, recovery);
        test(!refused.BothAdmitted() && !refused.SimulateMutation(),
             "recovery-required partner denies new trade before side effects");
    }

    // Scenario 4: native owner deletion invalidates the scope. This prevents
    // treating a surviving counter as proof of a surviving Player or session.
    auto temporary = std::make_unique<Ledger>();
    Ledger survivor;
    {
        TwoPartyProbe pending(*temporary, survivor);
        test(pending.BothAdmitted(), "temporary owner initially admitted");
        temporary.reset();
        test(!pending.BothAdmitted(), "destroying ledger invalidates old admission");
        test(pending.initiator.Result() == Decision::RejectBeforeMutation,
             "dead owner is a rejection, not an owned-save permission");
        test(!pending.SimulateMutation() && !pending.SimulateAlreadyOwnedAppend(),
             "no mock native effects after owner disappears");
    }

    std::cout << "checks=" << checks << " status=" << (ok ? "PASS" : "FAIL") << '\n';
    return ok ? 0 : 1;
}
