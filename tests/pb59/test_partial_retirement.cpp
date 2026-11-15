#include "PB59TransactionAdmission.h"
#include <cassert>
#include <memory>
#include <stdexcept>
using A=PB59::TransactionAdmission;
int main()
{
    A a;
    {auto lease=a.TryAcquireRetirement();assert(lease);assert(!lease.Complete());}
    assert(a.CanRetire());
    {auto lease=a.TryAcquireRetirement();assert(lease.MarkMutation());assert(!lease.MarkMutation());
     assert(lease.Complete());assert(!lease.Complete());assert(!a.CanBegin());}
    assert(a.CanRetire()&&!a.RetirementRecoveryRequired());
    {auto lease=a.TryAcquireRetirement();assert(lease.MarkMutation());}
    assert(a.RetirementRecoveryRequired()&&a.RetirementInProgress());
    assert(!a.Begin()&&!a.TryAcquireRetirement());
    assert(a.Request(A::Work::Destroy)==A::Decision::RetainOwner);
    assert(a.Request(A::Work::Logout)==A::Decision::RejectBeforeMutation);
    assert(a.Request(A::Work::Redirect)==A::Decision::RejectBeforeMutation);
    // A partial producer outcome is not proof of an unsafe inventory save.
    // Do not introduce a blanket suppression of ordinary saves.
    assert(a.Request(A::Work::StandaloneSave)==A::Decision::Admit);
    A b;
    try {auto lease=b.TryAcquireRetirement();assert(lease.MarkMutation());throw std::runtime_error("after effect");}
    catch(std::runtime_error const&){}
    assert(b.RetirementRecoveryRequired()&&!b.TryAcquireRetirement());
    A c,d;
    {auto one=c.TryAcquireRetirement(), two=d.TryAcquireRetirement();assert(one.MarkMutation());
     one=std::move(two);assert(c.RetirementRecoveryRequired()&&one&&!two);}
    assert(d.CanRetire());
    {auto owner=std::make_unique<A>();auto lease=owner->TryAcquireRetirement();assert(lease.MarkMutation());
     owner.reset();assert(lease.Complete());}
    {auto owner=std::make_unique<A>();auto lease=owner->TryAcquireRetirement();assert(lease.MarkMutation());
     owner.reset();} // token may outlive deleted Player even on partial outcome
}
