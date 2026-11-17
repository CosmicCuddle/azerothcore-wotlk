// Regression for a synchronous native callback deleting a ledger while its
// MutationScope is still on the stack. Source-level safety only, no Player or DB.
#include "PB59TransactionAdmission.h"
#include <cstdint>
#include <iostream>
#include <memory>

using Ledger = PB59::TransactionAdmission;
static int checks = 0;
static bool Good(bool ok, char const* label)
{
    ++checks;
    if (!ok)
        std::cerr << "FAIL " << label << '\n';
    return ok;
}

int main()
{
    bool ok = true;
    auto test = [&](bool result, char const* label) { ok = Good(result, label) && ok; };
    {
        auto owner = std::make_unique<Ledger>();
        auto mutation = std::make_unique<Ledger::MutationScope>(*owner);
        test(mutation->GetTicket() != 0, "begin before owner destruction");
        owner.reset(); // emulates owner deletion by synchronous callback
        test(!mutation->Finish({true,true,true,true}), "cannot finish after owner deletion");
        mutation.reset(); // destructor MUST NOT dereference dead ledger
        test(true, "scope cleanup after owner deletion returned safely");
    }
    {
        Ledger owner;
        Ledger::Ticket ticket = 0;
        {
            Ledger::MutationScope mutation(owner);
            ticket = mutation.GetTicket();
            test(ticket != 0 && owner.State() == Ledger::Phase::Active,
                "ordinary admitted mutation still active");
        }
        test(owner.State() == Ledger::Phase::RecoveryRequired,
             "live owner's unfinished mutation enters recovery-required");
        test(!owner.CanBegin(), "unfinished mutation cannot start another operation");
        test(!owner.Finish(ticket, {false,true,true,true}),
             "incomplete stable proof is refused");
        test(owner.Finish(ticket, {true,true,true,true}),
             "owner can finish after independent stable proof");
        test(owner.State() == Ledger::Phase::Idle,
             "reconciled owner leaves recovery state");
    }
    {
        Ledger owner;
        {
            Ledger::MutationScope mutation(owner);
            test(mutation.GetTicket() != 0, "ordinary second scope admitted");
            test(mutation.Finish({true,true,true,true}), "full stable proof acknowledges finish");
        }
        test(owner.State() == Ledger::Phase::Idle, "finished scope does not quarantine");
        test(owner.CanBegin(), "successful finish keeps owner reusable");
    }
    std::cout << "checks=" << checks << " status=" << (ok?"PASS":"FAIL") << '\n';
    return ok ? 0 : 1;
}
