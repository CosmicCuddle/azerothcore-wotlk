#include "TradeJournalReplay.h"
#include <cassert>
#include <iostream>
using namespace CosmicTrade;
static JournalEvent E(unsigned realm, std::uint64_t seq, EventKind kind, std::uint64_t hash=100) {
    JournalEvent e; e.identity.tradeId=42; e.identity.realm=realm; e.identity.owner=realm+100;
    e.identity.payloadHash=hash; e.sequence=seq; e.kind=kind; return e;
}
int main() {
    int tests=0;
    auto check=[&](bool v){++tests; if(!v){std::cerr<<"FAILED #"<<tests<<"\n"; std::abort();}};
    JournalReplay j;
    check(j.Status()==Decision::Hold);
    check(j.Apply(E(1,1,EventKind::Prepare))==ApplyResult::Applied);
    check(j.Apply(E(1,1,EventKind::Prepare))==ApplyResult::Duplicate);
    check(j.Apply(E(1,1,EventKind::Commit))==ApplyResult::ManualRecovery);
    check(j.Apply(E(2,1,EventKind::Prepare))==ApplyResult::Applied);
    check(j.Apply(E(1,2,EventKind::Commit))==ApplyResult::Applied);
    check(j.Status()==Decision::Hold);
    check(j.Apply(E(2,2,EventKind::Commit))==ApplyResult::Applied);
    check(j.Status()==Decision::Complete);
    check(j.Apply(E(2,2,EventKind::Commit))==ApplyResult::Duplicate);
    check(j.Apply(E(2,3,EventKind::Refund))==ApplyResult::ManualRecovery);
    check(j.Apply(E(2,3,EventKind::Commit))==ApplyResult::Rejected);
    check(j.Apply(E(1,3,EventKind::Prepare))==ApplyResult::ManualRecovery);
    check(j.Apply(E(3,1,EventKind::Prepare))==ApplyResult::ManualRecovery);
    check(j.Apply(E(1,4,EventKind::Commit,101))==ApplyResult::ManualRecovery);
    check(j.Apply(E(1,0,EventKind::Commit))==ApplyResult::Rejected);
    JournalReplay k;
    check(k.Apply(E(1,1,EventKind::Commit))==ApplyResult::Rejected);
    check(k.Apply(E(1,1,EventKind::Prepare))==ApplyResult::Applied);
    check(k.Apply(E(2,1,EventKind::Prepare))==ApplyResult::Applied);
    check(k.Apply(E(2,2,EventKind::Refund))==ApplyResult::Applied);
    check(k.Status()==Decision::ManualRecovery);
    check(k.Apply(E(2,3,EventKind::Commit))==ApplyResult::ManualRecovery);
    JournalReplay replay;
    check(replay.Apply(E(2,1,EventKind::Prepare))==ApplyResult::Applied);
    check(replay.Apply(E(1,1,EventKind::Prepare))==ApplyResult::Applied);
    check(replay.Apply(E(2,2,EventKind::Commit))==ApplyResult::Applied);
    check(replay.Apply(E(1,2,EventKind::Commit))==ApplyResult::Applied);
    check(replay.Status()==Decision::Complete);
    std::cout<<tests<<" replay assertions passed\n";
}
