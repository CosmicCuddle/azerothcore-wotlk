#include "PB59TransactionAdmission.h"
#include <cassert>
#include <memory>
#include <stdexcept>
#include <type_traits>
using A=PB59::TransactionAdmission;
using W=A::Work;
using D=A::Decision;
constexpr A::StableProof stable{true,true,true,true};
int main()
{
    static_assert(!std::is_copy_constructible_v<A::RetirementLease>);
    static_assert(std::is_nothrow_move_constructible_v<A::RetirementLease>);
    A a, b;
    assert(a.CanRetire());
    {
        auto l=a.TryAcquireRetirement();
        assert(l && a.RetirementInProgress() && !a.CanBegin() && !a.Begin());
        assert(!a.TryAcquireRetirement());
        assert(a.Request(W::Logout)==D::RejectBeforeMutation);
        assert(a.Request(W::Redirect)==D::RejectBeforeMutation);
        assert(a.Request(W::Destroy)==D::RetainOwner);
        assert(!a.PendingLogout());
        { A::ExternalScope save(a,W::StandaloneSave); assert(save.Admitted()); assert(!a.Begin()); }
        { A::ExternalScope save(a,W::InventoryAndGold); assert(save.Admitted()); assert(!a.TryAcquireRetirement()); }
        auto other=b.TryAcquireRetirement();
        assert(other);
        auto moved=std::move(l);
        assert(!l && moved && a.RetirementInProgress());
    }
    assert(a.CanRetire() && b.CanRetire());
    auto t=a.Begin();
    assert(t && !a.TryAcquireRetirement());
    assert(a.RequireRecovery(t) && !a.TryAcquireRetirement());
    assert(a.Finish(t,stable) && a.CanRetire());
    { A::ExternalScope save(a,W::StandaloneSave); assert(!a.TryAcquireRetirement()); }
    t=a.Begin();
    assert(a.Request(W::StandaloneSave)==D::DeferStandaloneSave);
    assert(a.Request(W::Logout)==D::DeferLogout);
    assert(a.Finish(t,stable) && !a.TryAcquireRetirement());
    { A::ExternalScope save(a,W::StandaloneSave); assert(save.CompleteDeferred()); }
    assert(!a.TryAcquireRetirement());
    { A::ExternalScope logout(a,W::Logout); assert(logout.SealOwner()); }
    assert(!a.TryAcquireRetirement());
    // Token is independent of Player lifetime; deleting the ledger first is safe.
    {
        auto state=std::make_unique<A>();
        auto lease=state->TryAcquireRetirement();
        assert(lease);
        state.reset();
        assert(lease);
    }
    A c;
    try { auto lease=c.TryAcquireRetirement(); assert(lease); throw std::runtime_error("producer failure"); }
    catch (std::runtime_error const&) {}
    assert(c.CanRetire());
}
