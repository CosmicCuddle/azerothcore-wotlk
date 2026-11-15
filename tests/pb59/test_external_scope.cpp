#include "PB59TransactionAdmission.h"
#include <cassert>
#include <memory>
#include <new>
#include <stdexcept>
using A=PB59::TransactionAdmission;
using W=A::Work;
using D=A::Decision;
constexpr A::StableProof stable{true,true,true,true};
int main()
{
    A a;
    { A::ExternalScope outer(a,W::InventoryMutation);
      assert(outer.Admitted()&&!a.Begin()&&!a.TryAcquireRetirement());
      { A::ExternalScope nested(a,W::InventoryAndGold); assert(nested.Admitted()&&!a.Begin()); }
      assert(!a.Begin()); }
    assert(a.CanBegin());
    for(bool recovering:{false,true})
    {
        auto t=a.Begin(); assert(t); if(recovering)assert(a.RequireRecovery(t));
        A::ExternalScope blocked(a,W::InventoryMutation);
        assert(!blocked.Admitted()&&blocked.Result()==D::RejectBeforeMutation);
        assert(a.Finish(t,stable));
    }
    { auto retirement=a.TryAcquireRetirement(); assert(retirement);
      A::ExternalScope blocked(a,W::InventoryMutation); assert(!blocked.Admitted());
      A::ExternalScope save(a,W::InventoryAndGold); assert(save.Admitted()); }
    { auto retirement=a.TryAcquireRetirement(); assert(retirement.MarkMutation()); }
    assert(a.RetirementRecoveryRequired());
    { A::ExternalScope blocked(a,W::InventoryMutation); assert(!blocked.Admitted()); }
    // Owner deletion before external scope destruction must not touch freed memory.
    { auto owner=std::make_unique<A>(); auto outer=std::make_unique<A::ExternalScope>(*owner,W::StandaloneSave);
      auto nested=std::make_unique<A::ExternalScope>(*owner,W::InventoryAndGold);
      assert(outer->Admitted()&&nested->Admitted()); owner.reset();
      assert(!outer->Admitted()&&outer->Result()==D::RejectBeforeMutation);
      assert(!outer->CompleteDeferred()&&!outer->SealOwner()&&!nested->Admitted());
      outer.reset(); nested.reset(); }
    // Address reuse cannot turn an old scope into a capability on a new ledger.
    alignas(A) unsigned char storage[sizeof(A)];
    auto* first=new(storage) A;
    auto old=std::make_unique<A::ExternalScope>(*first,W::Logout);
    assert(old->Admitted()); first->~A();
    auto* replacement=new(storage) A;
    assert(!old->Admitted()&&!old->SealOwner()&&replacement->CanBegin());
    old.reset(); assert(replacement->CanBegin()); replacement->~A();
    A b;
    try { A::ExternalScope scope(b,W::InventoryMutation); assert(scope.Admitted()); throw std::runtime_error("callback"); }
    catch(std::runtime_error const&){}
    assert(b.CanBegin());
}
