// Regression for independently reported F59-1 and F60-1. Disposable files only.
// Run from tests/cs01_trade with exact Core database header include directory.
#include "SqliteTradeJournalProof.h"
#include "TradeDbOutcomeFence.h"
#include <sqlite3.h>
#include <cstdlib>
#include <future>
#include <iostream>
#include <string>
#include <unistd.h>
using namespace CosmicTrade;

static JournalEvent E(unsigned realm, unsigned seq, EventKind kind) {
    JournalEvent e;
    e.identity.tradeId=9001;
    e.identity.realm=realm;
    e.identity.owner=100+realm;
    e.identity.payloadHash=200+realm;
    e.sequence=seq;
    e.kind=kind;
    return e;
}
int main() {
    int checks=0;
    auto check=[&](bool good) { ++checks; if(!good) { std::cerr<<"FAILED "<<checks<<"\n";std::abort(); } };
    char tmp[]="/tmp/cs01-proof-hardening-XXXXXX";
    int fd=mkstemp(tmp);
    check(fd>=0);
    close(fd);
    {
        SqliteTradeJournalProof store(tmp,1);
        check(store.Ready());
        check(store.Persist(E(1,1,EventKind::Prepare))==StoreResult::Persisted);
        check(store.Persist(E(1,2,EventKind::Commit),7)==StoreResult::Persisted);
    }
    sqlite3* db=nullptr;
    check(sqlite3_open(tmp,&db)==SQLITE_OK);
    check(sqlite3_exec(db,"DELETE FROM effect_marker WHERE trade_id=9001 AND realm=1;",nullptr,nullptr,nullptr)==SQLITE_OK);
    check(sqlite3_close(db)==SQLITE_OK);
    {
        SqliteTradeJournalProof store(tmp,1);
        JournalReplay replay;
        check(!store.Replay(9001,replay));
        // The PREPARED state is admissible; a missing effect marker MUST NOT
        // promote the second row to COMMITTED in the replay state.
        check(replay.Side(0).state==SideState::Prepared);
        check(replay.Status()==Decision::Hold);
    }
    unlink(tmp);
    unlink((std::string(tmp)+"-wal").c_str());
    unlink((std::string(tmp)+"-shm").c_str());
    std::future<DatabaseTransactionResult> deferred=std::async(std::launch::deferred, [] {
        return DatabaseTransactionResult{DatabaseTransactionStatus::Committed,0};
    });
    NonBlockingOutcome outcome(1,std::move(deferred));
    RealmCommitEvidence evidence=outcome.Poll();
    check(evidence.outcomeReceived);
    check(!evidence.outcome.IsCommitted());
    check(EvaluateOutcomes(evidence,RealmCommitEvidence{2,true,{DatabaseTransactionStatus::Committed,0}})
          ==OutcomeDecision::NeedsReconciliation);
    std::cout<<checks<<" hardening checks passed\n";
}
