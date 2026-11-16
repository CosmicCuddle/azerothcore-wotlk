#include "PB59CharacterSaveEvidence.h"
#include <cassert>
#include <iostream>
#include <thread>

int main()
{
    using Store = PB59::CharacterSaveEvidence<2>;
    using A = Store::Admission;
    using S = Store::State;
    Store store;
    PB59::SaveIdentity first{1, 2, 3, 1}, next{1, 2, 3, 2}, replacement{1, 4, 5, 1};
    assert(store.Reserve({}).status == A::Invalid);
    auto a = store.Reserve(first);
    assert(a.status == A::Accepted && store.Reserve(first).status == A::Duplicate);
    auto b = store.Reserve(next);
    assert(b.status == A::Accepted && store.Reserve(replacement).status == A::Full);
    assert(!store.Acknowledge(a.ticket) && !store.Complete(a.ticket, DatabaseTransactionResult::FromExecutionCode(0)));
    PB59::SaveEvidence evidence;
    evidence.questCount = 65;
    assert(!store.Seal(a.ticket, evidence));
    evidence.questCount = 1;
    assert(!store.Seal(a.ticket, evidence));
    evidence.quests[0] = 66001;
    evidence.itemCount = 1;
    evidence.items[0] = 42;
    assert(store.Seal(a.ticket, evidence));
    evidence.quests[0] = 66018;
    assert(!store.Seal(a.ticket, evidence));
    assert(store.Inspect(a.ticket)->evidence.quests[0] == 66001);
    assert(store.Seal(b.ticket, evidence));
    auto wrong = a.ticket;
    wrong.identity.session = 99;
    assert(!store.Complete(wrong, DatabaseTransactionResult::FromExecutionCode(0)));
    assert(store.Complete(b.ticket, DatabaseTransactionResult::FromExecutionCode(0)));
    assert(store.Complete(a.ticket, DatabaseTransactionResult::FromExecutionCode(-2)));
    assert(!store.Complete(a.ticket, DatabaseTransactionResult::FromExecutionCode(0)));
    assert(store.Inspect(a.ticket)->result.status == DatabaseTransactionStatus::Ambiguous);
    assert(store.Inspect(b.ticket)->result.IsCommitted());
    assert(store.Reserve(replacement).status == A::Full);
    assert(store.Acknowledge(a.ticket));
    auto c = store.Reserve(replacement);
    assert(c.status == A::Accepted && c.ticket.slot == a.ticket.slot && c.ticket.serial != a.ticket.serial);
    assert(!store.Acknowledge(a.ticket) && !store.Inspect(a.ticket));
    assert(store.Abandon(c.ticket) && !store.Abandon(c.ticket));
    auto record = store.InspectSlot(c.ticket.slot);
    assert(record && record->state == S::Terminal && record->evidence.incomplete);
    assert(!store.InspectSlot(2));
    assert(store.Acknowledge(record->ticket) && store.Acknowledge(b.ticket));
    assert(store.Counts()[0] == 2);
    for (int i = 0; i < 200; ++i)
    {
        auto r = store.Reserve(first);
        assert(r.status == A::Accepted && store.Seal(r.ticket, evidence));
        bool won1 = false, won2 = false;
        std::thread x([&] { won1 = store.Complete(r.ticket, DatabaseTransactionResult::FromExecutionCode(0)); });
        std::thread y([&] { won2 = store.Complete(r.ticket, DatabaseTransactionResult::FromExecutionCode(-2)); });
        x.join(); y.join();
        assert(won1 != won2 && store.Inspect(r.ticket)->state == S::Terminal);
        assert(store.Acknowledge(r.ticket));
    }
    std::cout << "PASS: reservation, immutability, identity, saturation, late completion, abandonment, drain, 200 completion races\n";
}
