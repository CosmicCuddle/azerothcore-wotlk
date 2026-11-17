// Source-level admission policy regression. Includes the actual Core header
// rather than a transcribed mock of TransactionAdmission. This does NOT
// exercise WorldSession, DB, Player lifetime, gameplay or item disposal.
#include "PB59TransactionAdmission.h"
#include <iostream>
#include <initializer_list>

using PB59::TransactionAdmission;
using Work = TransactionAdmission::Work;
using Decision = TransactionAdmission::Decision;
using Scope = TransactionAdmission::ExternalScope;

namespace
{
int checks = 0;
bool Verify(bool valid, char const* scenario)
{
    ++checks;
    if (!valid)
        std::cerr << "FAIL " << checks << " " << scenario << '\n';
    return valid;
}

bool RejectNewEconomicWrites(TransactionAdmission& ledger)
{
    bool all = true;
    for (Work work : {Work::CallerOwnedSave, Work::InventoryAndGold,
                      Work::GoldOnly, Work::InventoryMutation})
    {
        Scope refused(ledger, work);
        all = Verify(!refused.Admitted(), "no new economic scope after deferred logout") && all;
        all = Verify(refused.Result() == Decision::RejectBeforeMutation,
                     "economic work refused before native producer mutation") && all;
    }
    return all;
}
}

int main()
{
    bool ok = true;
    auto check = [&](bool good, char const* why) { ok = Verify(good, why) && ok; };

    // No pending logout: pre-existing admitted economic operations are intact.
    for (Work work : {Work::CallerOwnedSave, Work::InventoryAndGold,
                      Work::GoldOnly, Work::InventoryMutation})
    {
        TransactionAdmission ledger;
        Scope ordinary(ledger, work);
        check(ordinary.Admitted(), "idle admission preserved without pending logout");
    }

    TransactionAdmission ledger;
    check(!ledger.PendingLogout(), "logout initially absent");
    {
        Scope producer(ledger, Work::InventoryMutation);
        check(producer.Admitted(), "initial producer admitted");
        check(ledger.Request(Work::Logout) == Decision::DeferLogout,
              "logout deferred behind active producer");
        check(ledger.PendingLogout(), "deferred logout is latched");
        check(RejectNewEconomicWrites(ledger), "nested economic operations refused");
    }
    check(ledger.PendingLogout(), "logout intent survives producer scope exit");
    check(RejectNewEconomicWrites(ledger), "economic operations refused after scope ends");
    check(ledger.Request(Work::Callback) == Decision::RejectBeforeMutation,
          "callback remains rejected while logout pending");
    check(ledger.Request(Work::Redirect) == Decision::RejectBeforeMutation,
          "redirect remains rejected while logout pending");
    check(ledger.Request(Work::Destroy) == Decision::RetainOwner,
          "destroy retains owner until pending logout is handled");
    check(!ledger.TryAcquireRetirement(), "retirement remains gated by pending logout");

    // Replay is a distinct native-owner responsibility; an idle save is not
    // automatically rejected, so earlier dirty state may still be persisted.
    {
        Scope save(ledger, Work::StandaloneSave);
        check(save.Admitted(), "standalone save remains available to settle prior work");
    }
    {
        Scope replay(ledger, Work::Logout);
        check(replay.Admitted(), "native replay scope can enter");
        check(replay.SealOwner(), "replay scope seals after prior external scope ends");
        check(ledger.OwnerSealed() && !ledger.PendingLogout(), "seal consumes pending intent");
    }
    check(ledger.Request(Work::InventoryMutation) == Decision::RejectBeforeMutation,
          "sealed owner rejects later mutation");

    // A pending save must be acknowledged before the final logout seal.
    {
        TransactionAdmission waiting;
        TransactionAdmission::MutationScope mutation(waiting);
        check(mutation.GetTicket() != 0, "begin admitted before any logout");
        check(waiting.Request(Work::StandaloneSave) == Decision::DeferStandaloneSave,
              "standalone save coalesced during active transaction");
        check(waiting.Request(Work::Logout) == Decision::DeferLogout,
              "logout also deferred during active transaction");
        check(mutation.Finish({true, true, true, true}), "independent stable-proof bits allow finish");
        check(waiting.PendingSave() && waiting.PendingLogout(),
              "both intents persist after transaction finish");
        check(RejectNewEconomicWrites(waiting), "economic operations still refused");
        {
            Scope save(waiting, Work::StandaloneSave);
            check(save.Admitted(), "native save replay admitted");
            check(save.CompleteDeferred(), "save replay acknowledges its own intent");
        }
        check(!waiting.PendingSave() && waiting.PendingLogout(),
              "save completion does not erase pending logout");
        {
            Scope logout(waiting, Work::Logout);
            check(logout.Admitted(), "final logout can enter");
            check(logout.SealOwner(), "final logout seals after save");
        }
    }

    std::cout << "checks=" << checks << " status=" << (ok ? "PASS" : "FAIL") << '\n';
    return ok ? 0 : 1;
}
