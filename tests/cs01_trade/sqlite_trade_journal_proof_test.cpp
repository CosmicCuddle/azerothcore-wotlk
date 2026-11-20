#include "SqliteTradeJournalProof.h"
#include <cassert>
#include <cstdlib>
#include <iostream>
#include <string>
#include <unistd.h>
#include <sys/stat.h>
using namespace CosmicTrade;

static JournalEvent Event(std::uint32_t realm, std::uint64_t seq, EventKind kind, std::uint64_t hash=999) {
    JournalEvent e;
    e.identity.tradeId=67;
    e.identity.realm=realm;
    e.identity.owner=1000+realm;
    e.identity.payloadHash=hash;
    e.sequence=seq;
    e.kind=kind;
    return e;
}
int main(){
    char dirname[]="/tmp/cosmic-trade-journal-XXXXXX";
    char* folder=mkdtemp(dirname);
    if(!folder) { std::cerr<<"mkdtemp failed\n";return 2; }
    std::string a=std::string(folder)+"/realm_a.sqlite3";
    std::string b=std::string(folder)+"/realm_b.sqlite3";
    int assertions=0;
    auto check=[&](bool ok){++assertions;if(!ok){std::cerr<<"FAILED #"<<assertions<<"\n";std::abort();}};
    {
        SqliteTradeJournalProof left(a,1),right(b,2);
        check(left.Ready() && right.Ready());
        check(left.Persist(Event(1,2,EventKind::Commit),7)==StoreResult::Rejected);
        check(left.EventCount(67)==0);
        check(left.Persist(Event(1,1,EventKind::Prepare))==StoreResult::Persisted);
        check(left.Persist(Event(1,1,EventKind::Prepare))==StoreResult::Duplicate);
        check(left.Persist(Event(2,1,EventKind::Prepare))==StoreResult::Rejected);
        check(left.Persist(Event(1,1,EventKind::Prepare,1001))==StoreResult::ManualRecovery);
        check(right.Persist(Event(2,1,EventKind::Prepare))==StoreResult::Persisted);
        check(left.EventCount(67)==1 && right.EventCount(67)==1);
        check(left.Persist(Event(1,3,EventKind::Commit),44)==StoreResult::Rejected); // no sequence gaps
        check(left.Persist(Event(1,2,EventKind::Commit),44,InjectFailure::AfterEffectBeforeReceipt)==StoreResult::IOError);
        check(left.EffectCount(67)==0 && left.EventCount(67)==1); // atomic rollback
        check(left.Persist(Event(1,2,EventKind::Commit),44,InjectFailure::AfterReceiptBeforeCommit)==StoreResult::IOError);
        check(left.EffectCount(67)==0 && left.EventCount(67)==1); // atomic rollback
        check(left.Persist(Event(1,2,EventKind::Commit),44)==StoreResult::Persisted);
        check(left.Persist(Event(1,2,EventKind::Commit),44)==StoreResult::Duplicate);
        check(left.Persist(Event(1,2,EventKind::Commit),45)==StoreResult::ManualRecovery);
        check(left.EffectCount(67)==1 && left.EventCount(67)==2);
        JournalReplay live;
        check(left.Replay(67,live) && right.Replay(67,live));
        check(live.Status()==Decision::Hold); // one realm has not committed
    }
    { // process-like restart: reconstruct the in-memory replay from independently durable databases
        SqliteTradeJournalProof left(a,1),right(b,2);
        check(left.Ready() && right.Ready());
        JournalReplay restarted;
        check(left.Replay(67,restarted) && right.Replay(67,restarted));
        check(restarted.Status()==Decision::Hold);
        check(right.Persist(Event(2,2,EventKind::Commit),95)==StoreResult::Persisted);
        check(right.Persist(Event(2,2,EventKind::Commit),95)==StoreResult::Duplicate);
        check(right.EventCount(67)==2 && right.EffectCount(67)==1);
        check(right.Persist(Event(2,3,EventKind::Refund))==StoreResult::ManualRecovery);
    }
    {
        SqliteTradeJournalProof left(a,1),right(b,2);
        JournalReplay afterSecondRestart;
        check(left.Replay(67,afterSecondRestart));
        check(right.Replay(67,afterSecondRestart));
        check(afterSecondRestart.Status()==Decision::Complete); // PROOF GATE ONLY, not actual item/gold safety
        check(left.EffectCount(67)==1 && right.EffectCount(67)==1);
        check(left.Persist(Event(1,2,EventKind::Commit),44)==StoreResult::Duplicate);
        check(left.Persist(Event(1,2,EventKind::Commit),98)==StoreResult::ManualRecovery);
    }
    // No attempt to clean up live data. Only remove our randomized test files.
    for(auto base:{a,b}){
        unlink(base.c_str()); unlink((base+"-wal").c_str()); unlink((base+"-shm").c_str());
    }
    rmdir(dirname);
    std::cout<<assertions<<" SQLite durable-journal proof assertions passed\n";
}
