#include "CrossRealmTradeAdmission.h"
#include <cstdlib>
#include <iostream>
using namespace CosmicTrade;
using Ledger = PB59::TransactionAdmission;
static Receipt R(unsigned realm, SideState state) {
    Receipt r;
    r.tradeId=987; r.realm=realm; r.owner=realm+1000;
    r.payloadHash=realm+100; r.state=state; r.durable=true;
    return r;
}
int main() {
    int checks=0;
    auto check=[&](bool ok) { ++checks; if(!ok) {std::cerr<<"FAILED "<<checks<<"\n";std::abort();} };
    {
        Ledger a,b;
        check(a.CanBegin() && b.CanBegin());
        {
            DualOwnerAdmission gate(a,b);
            check(gate.Admitted());
            check(gate.LeftTicket()!=0 && gate.RightTicket()!=0);
            check(a.Request(Ledger::Work::ScheduledSave)==Ledger::Decision::DeferScheduledSave);
            check(b.Request(Ledger::Work::InventoryMutation)==Ledger::Decision::RejectBeforeMutation);
            check(a.Request(Ledger::Work::Logout)==Ledger::Decision::DeferLogout);
            check(a.PendingLogout());
            check(gate.Observe(R(1,SideState::Prepared),R(2,SideState::Committed))==Decision::Hold);
            check(gate.Admitted()); // HOLD never releases tickets
            check(gate.Observe(R(1,SideState::Committed),R(2,SideState::Committed))==Decision::Complete);
            check(gate.Admitted()); // even COMPLETE cannot auto-Finish; no durable native readback
        }
        check(a.State()==Ledger::Phase::RecoveryRequired);
        check(b.State()==Ledger::Phase::RecoveryRequired);
        check(a.Request(Ledger::Work::ScheduledSave)==Ledger::Decision::DeferScheduledSave);
        check(a.Request(Ledger::Work::Destroy)==Ledger::Decision::RetainOwner);
        check(!a.CanBegin() && !b.CanBegin());
    }
    {
        Ledger a,b;
        auto other=b.Begin();
        check(other!=0);
        {
            DualOwnerAdmission gate(a,b);
            check(!gate.Admitted());
            check(a.State()==Ledger::Phase::Idle); // failed precheck never grabs first owner
        }
        check(a.CanBegin());
        check(b.State()==Ledger::Phase::Active);
        check(b.RequireRecovery(other));
    }
    {
        Ledger a;
        DualOwnerAdmission gate(a,a);
        check(!gate.Admitted());
        check(a.State()==Ledger::Phase::Idle);
    }
    {
        Ledger a,b;
        {
            DualOwnerAdmission gate(a,b);
            check(gate.Admitted());
            auto bad=R(1,SideState::Committed);
            bad.tradeId=0;
            check(gate.Observe(bad,R(2,SideState::Committed))==Decision::ManualRecovery);
            check(a.State()==Ledger::Phase::RecoveryRequired);
            check(b.State()==Ledger::Phase::RecoveryRequired);
            check(!gate.Admitted());
        }
    }
    {
        Ledger a,b;
        auto t=a.Begin();
        check(t!=0);
        check(a.RequireRecovery(t));
        DualOwnerAdmission gate(a,b);
        check(!gate.Admitted());
        check(b.State()==Ledger::Phase::Idle);
    }
    std::cout<<checks<<" dual-owner admission checks passed\n";
}
