#include "CrossRealmTradeHandoff.h"
#include <cstdlib>
#include <iostream>
#include <memory>
using namespace CosmicTrade;
using Ledger = PB59::TransactionAdmission;
static Receipt R(unsigned realm, SideState state) {
    Receipt r; r.tradeId=987; r.realm=realm; r.owner=realm+1000;
    r.payloadHash=realm+100; r.state=state; r.durable=true; return r;
}
int main() {
    int checks=0;
    auto check=[&](bool ok){ ++checks; if(!ok){std::cerr<<"FAILED #"<<checks<<"\n";std::abort();}};
    {
        Ledger a,b;
        auto gate=DualOwnerHandoff::Begin(a,b);
        check(bool(gate)); check(gate->Admitted());
        check(gate->LeftTicket()!=0 && gate->RightTicket()!=0);
        check(a.Request(Ledger::Work::ScheduledSave)==Ledger::Decision::DeferScheduledSave);
        check(b.Request(Ledger::Work::InventoryMutation)==Ledger::Decision::RejectBeforeMutation);
        check(a.Request(Ledger::Work::Logout)==Ledger::Decision::DeferLogout);
        check(a.PendingLogout());
        check(gate->Observe(R(1,SideState::Prepared),R(2,SideState::Committed))==Decision::Hold);
        check(gate->Observe(R(1,SideState::Committed),R(2,SideState::Committed))==Decision::Hold);
        auto coordinator=std::move(gate);
        check(!gate && bool(coordinator));
        check(a.State()==Ledger::Phase::Active && b.State()==Ledger::Phase::Active);
        coordinator.reset();
        check(a.State()==Ledger::Phase::RecoveryRequired);
        check(b.State()==Ledger::Phase::RecoveryRequired);
        check(a.Request(Ledger::Work::Destroy)==Ledger::Decision::RetainOwner);
    }
    {
        Ledger a,b;
        auto other=b.Begin(); check(other!=0);
        auto gate=DualOwnerHandoff::Begin(a,b);
        check(!gate); check(a.State()==Ledger::Phase::Idle);
        check(b.RequireRecovery(other));
    }
    {
        Ledger a;
        auto gate=DualOwnerHandoff::Begin(a,a);
        check(!gate); check(a.State()==Ledger::Phase::Idle);
    }
    {
        // ASan regression for the prior raw Ledger& use-after-free.
        auto a=std::make_unique<Ledger>();
        auto b=std::make_unique<Ledger>();
        auto gate=DualOwnerHandoff::Begin(*a,*b);
        check(bool(gate));
        a.reset(); b.reset();
        gate.reset(); // must not dereference destroyed owner
        check(true);
    }
    {
        Ledger a,b;
        auto gate=DualOwnerHandoff::Begin(a,b);
        check(bool(gate));
        auto bad=R(1,SideState::Committed); bad.tradeId=0;
        check(gate->Observe(bad,R(2,SideState::Committed))==Decision::ManualRecovery);
        gate.reset();
        check(a.State()==Ledger::Phase::RecoveryRequired);
        check(b.State()==Ledger::Phase::RecoveryRequired);
    }
    std::cout<<checks<<" dual-owner handoff/lifetime assertions passed\n";
}
