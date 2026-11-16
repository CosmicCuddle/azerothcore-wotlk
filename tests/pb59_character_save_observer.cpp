#include "PB59CharacterSaveObserver.h"
#include <cassert>
#include <iostream>
#include <vector>
int main()
{
    using Observer = PB59::CharacterSaveObserver<2>;
    using Status = DatabaseTransactionStatus;
    using A = Observer::Store::Admission;
    Observer observer;
    PB59::SaveEvidence evidence;
    evidence.AddQuest(66001);
    evidence.AddQuest(66001);
    for (std::uint32_t id=1; id<=64; ++id) evidence.AddQuest(id);
    evidence.AddItem(42);
    evidence.AddSkill(3);
    assert(evidence.Valid() && evidence.incomplete && evidence.questCount==64 && evidence.quests[0]==66001);
    auto a=observer.Reserve(1,2,3), b=observer.Reserve(1,2,3);
    assert(a.status==A::Accepted && b.status==A::Accepted && a.ticket.identity.sequence < b.ticket.identity.sequence);
    assert(observer.Reserve(1,2,3).status==A::Full);
    assert(observer.Seal(a.ticket,evidence) && observer.Seal(b.ticket,evidence));
    std::promise<DatabaseTransactionResult> first, second;
    assert(observer.Attach(a.ticket,first.get_future()) && observer.Attach(b.ticket,second.get_future()));
    std::vector<Observer::Record> reports;
    auto report=[&](Observer::Record const& r){reports.push_back(r);};
    observer.Drain(report); assert(reports.empty());
    second.set_value(DatabaseTransactionResult::FromExecutionCode(0));
    observer.Drain(report); assert(reports.size()==1 && reports[0].ticket==b.ticket && reports[0].result.IsCommitted());
    auto c=observer.Reserve(1,4,5); assert(c.status==A::Accepted && observer.Seal(c.ticket,evidence));
    assert(!observer.Attach(b.ticket,std::future<DatabaseTransactionResult>{}));
    { std::promise<DatabaseTransactionResult> lost; assert(observer.Attach(c.ticket,lost.get_future())); }
    first.set_value(DatabaseTransactionResult::FromExecutionCode(-2));
    observer.Drain(report); assert(reports.size()==3 && reports[1].ticket==a.ticket && reports[2].ticket==c.ticket);
    assert(reports[1].result.status==Status::Ambiguous && reports[2].result.status==Status::Ambiguous);
    assert(reports[1].evidence.items[0]==42 && reports[1].evidence.skills[0]==3);
    auto d=observer.Reserve(1,2,3), e=observer.Reserve(1,2,3);
    assert(d.status==A::Accepted && e.status==A::Accepted);
    assert(observer.Seal(d.ticket,evidence));
    std::promise<DatabaseTransactionResult> pending;
    assert(observer.Attach(d.ticket,pending.get_future()));
    observer.Drain(report,true);
    assert(reports.size()==5 && reports[3].result.status==Status::Ambiguous && reports[4].evidence.incomplete);
    assert(observer.Reserve(1,2,3).status==A::Accepted);
    // On saturation only, ready successes can be reclaimed without waiting
    // for periodic Drain; every uncertain record must survive to reporting.
    {
        Observer full;
        auto x=full.Reserve(10,11,12), y=full.Reserve(20,21,22);
        assert(x.status==A::Accepted && y.status==A::Accepted);
        assert(full.Seal(x.ticket,evidence) && full.Seal(y.ticket,evidence));
        std::promise<DatabaseTransactionResult> success, uncertain;
        assert(full.Attach(x.ticket,success.get_future()) && full.Attach(y.ticket,uncertain.get_future()));
        success.set_value(DatabaseTransactionResult::FromExecutionCode(0));
        uncertain.set_value(DatabaseTransactionResult::FromExecutionCode(-2));
        auto z=full.Reserve(30,31,32);
        assert(z.status==A::Accepted && z.ticket.slot==x.ticket.slot && z.ticket.serial!=x.ticket.serial);
        assert(!full.Seal(x.ticket,evidence) && !full.Attach(x.ticket,{}));
        assert(full.Reserve(40,41,42).status==A::Full);
        unsigned failed=0;
        full.Drain([&](Observer::Record const& r){
            assert(r.ticket==y.ticket && r.result.status==Status::Ambiguous && r.evidence.quests[0]==66001);
            ++failed;
        });
        assert(failed==1 && full.Reserve(40,41,42).status==A::Accepted);
    }
    {
        Observer full;
        auto x=full.Reserve(10,11,12), y=full.Reserve(20,21,22);
        assert(full.Seal(x.ticket,evidence) && full.Seal(y.ticket,evidence));
        std::promise<DatabaseTransactionResult> empty;
        assert(full.Attach(x.ticket,empty.get_future()));
        { std::promise<DatabaseTransactionResult> broken; assert(full.Attach(y.ticket,broken.get_future())); }
        empty.set_value(DatabaseTransactionResult::FromExecutionCode(DatabaseTransactionResult::EmptyCode));
        auto z=full.Reserve(30,31,32);
        assert(z.status==A::Accepted && z.ticket.slot==x.ticket.slot);
        unsigned failed=0;
        full.Drain([&](Observer::Record const& r){assert(r.ticket==y.ticket && r.result.status==Status::Ambiguous);++failed;});
        assert(failed==1);
    }
    std::cout << "PASS: bounded capture, capacity, delayed/out-of-order completion, broken future, final drain, slot reuse, saturation reclamation\n";
}
