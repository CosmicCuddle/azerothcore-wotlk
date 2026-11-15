#include "PB59TransactionAdmission.h"
#include <array>
#include <cassert>
#include <stdexcept>
using A = PB59::TransactionAdmission;
using W = A::Work;
using D = A::Decision;
using P = A::Phase;
constexpr A::StableProof stable{true, true, true, true};

int main()
{
    constexpr std::array work{W::StandaloneSave, W::CallerOwnedSave, W::InventoryAndGold,
        W::GoldOnly, W::InventoryMutation, W::Logout, W::Redirect, W::Destroy};
    constexpr std::array blocked{D::DeferStandaloneSave, D::RejectBeforeMutation,
        D::RejectBeforeMutation, D::RejectBeforeMutation, D::RejectBeforeMutation,
        D::DeferLogout, D::RejectBeforeMutation, D::RetainOwner};
    for (auto w : work)
    {
        A a;
        assert(a.Request(w) == D::Admit);
        {
            A::ExternalScope outer(a, w);
            assert(outer.Admitted() && !a.CanBegin() && a.Begin() == 0);
            A::ExternalScope inner(a, W::InventoryAndGold);
            assert(inner.Admitted() && a.Begin() == 0);
            assert(a.Request(W::Destroy) == D::RetainOwner);
            assert(a.Request(W::Redirect) == D::RejectBeforeMutation);
        }
        assert(a.CanBegin());
    }
    for (bool recovery : {false, true})
    {
        for (std::size_t i = 0; i < work.size(); ++i)
        {
            A a;
            auto t = a.Begin();
            assert(t != 0 && a.IsActive(t));
            if (recovery) assert(a.RequireRecovery(t));
            {
                A::ExternalScope external(a, work[i]);
                assert(!external.Admitted() && external.Result() == blocked[i]);
                assert(a.Begin() == 0 && !a.CanBegin());
                assert(!external.CompleteDeferred());
            }
            assert(a.PendingSave() == (work[i] == W::StandaloneSave));
            assert(a.PendingLogout() == (work[i] == W::Logout));
            for (unsigned mask = 0; mask < 15; ++mask)
            {
                A::StableProof partial{bool(mask & 1), bool(mask & 2), bool(mask & 4), bool(mask & 8)};
                assert(!a.Finish(t, partial));
            }
            assert(!a.Finish(0, stable) && !a.Finish(t + 1, stable));
            assert(!a.RequireRecovery(0) && !a.RequireRecovery(t + 1));
            assert(a.Finish(t, stable) && a.State() == P::Idle);
            if (a.PendingSave() || a.PendingLogout())
            {
                assert(!a.CanBegin());
                assert(a.Request(W::Destroy) == D::RetainOwner);
                { A::ExternalScope replay(a, work[i]); assert(replay.Admitted());
                  { A::ExternalScope nested(a, W::StandaloneSave); assert(!nested.CompleteDeferred()); }
                  if (work[i] == W::Logout)
                  {
                      assert(!replay.CompleteDeferred() && replay.SealOwner());
                      assert(!replay.SealOwner());
                  }
                  else { assert(replay.CompleteDeferred()); assert(!replay.CompleteDeferred()); } }
                if (work[i] == W::Logout)
                {
                    assert(a.OwnerSealed() && !a.CanBegin());
                    assert(a.Request(W::Destroy) == D::Admit && a.Request(W::StandaloneSave) != D::Admit);
                    continue;
                }
            }
            assert(a.CanBegin());
            auto next = a.Begin();
            assert(next != t && !a.Finish(t, stable) && !a.RequireRecovery(t));
            assert(a.Finish(next, stable));
        }
    }
    A a;
    A::Ticket abandoned = 0;
    try
    {
        A::MutationScope transaction(a);
        abandoned = transaction.GetTicket();
        assert(abandoned && a.IsActive(abandoned));
        assert(a.Request(W::StandaloneSave) == D::DeferStandaloneSave);
        assert(a.Request(W::StandaloneSave) == D::DeferStandaloneSave);
        assert(a.Request(W::Logout) == D::DeferLogout);
        throw std::runtime_error("effect callback failed");
    }
    catch (std::runtime_error const&) { }
    assert(a.State() == P::RecoveryRequired && !a.IsActive(abandoned));
    assert(!a.Begin() && a.PendingSave() && a.PendingLogout());
    assert(a.Request(W::Destroy) == D::RetainOwner);
    assert(a.Finish(abandoned, stable));
    assert(!a.CanBegin() && a.Request(W::Logout) == D::DeferLogout);
    assert(a.Request(W::Redirect) == D::RejectBeforeMutation);
    assert(a.Request(W::Destroy) == D::RetainOwner);
    { A::ExternalScope save(a, W::StandaloneSave); assert(save.CompleteDeferred()); }
    assert(!a.CanBegin() && a.Request(W::Destroy) == D::RetainOwner);
    { A::ExternalScope logout(a, W::Logout); assert(logout.SealOwner()); }
    assert(a.OwnerSealed() && !a.CanBegin());
    A fresh;
    {
        A::MutationScope transaction(fresh);
        assert(transaction.GetTicket());
        assert(!transaction.Finish({true, true, false, true}));
        assert(transaction.Finish(stable));
    }
    assert(fresh.CanBegin());
    {
        A::ExternalScope save(fresh, W::StandaloneSave);
        A::MutationScope refused(fresh);
        assert(!refused.GetTicket() && !refused.Finish(stable));
    }
    assert(fresh.CanBegin());
    // Logout requested during an ordinary save is not forgotten.
    {
        A::ExternalScope save(fresh, W::StandaloneSave);
        assert(fresh.Request(W::Logout) == D::DeferLogout && fresh.PendingLogout());
    }
    assert(!fresh.CanBegin() && fresh.Request(W::Destroy) == D::RetainOwner);
    { A::ExternalScope logout(fresh, W::Logout); assert(logout.SealOwner()); }
    assert(fresh.OwnerSealed() && !fresh.CanBegin());
    A invalid;
    assert(invalid.Request(static_cast<W>(99)) == D::RejectBeforeMutation);
    {
        A::ExternalScope save(invalid, W::StandaloneSave);
        assert(!save.SealOwner());
    }
    {
        A::ExternalScope redirect(invalid, W::Redirect);
        assert(redirect.Admitted() && redirect.SealOwner());
        assert(invalid.Request(W::Destroy) != D::Admit);
    }
    assert(invalid.OwnerSealed() && !invalid.CanBegin() && invalid.Request(W::Destroy) == D::Admit);
}
