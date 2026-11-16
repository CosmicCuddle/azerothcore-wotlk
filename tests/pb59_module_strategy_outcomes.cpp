#include "DatabaseTransactionResult.h"
#include <cassert>
#include <cstdint>
#include <memory>
#include <vector>
#include <iostream>
#include <utility>
using uint8 = std::uint8_t;
constexpr int ER_LOCK_DEADLOCK = 1213;
struct TransactionBase { int clean=0; void Cleanup(){++clean;} };
struct MySQLConnection {
    std::vector<int> codes;
    std::size_t pos=0;
    int unlock=0;
    int ExecuteTransaction(std::shared_ptr<TransactionBase> const&){assert(pos<codes.size());return codes[pos++];}
    void Unlock(){++unlock;}
};
struct ModuleDatabasePool {
    enum {IDX_ASYNC,IDX_SYNCH};
    std::vector<MySQLConnection*> _connections[2];
    MySQLConnection* GetFreeConnection(){return _connections[IDX_SYNCH].front();}
    bool TryDirectCommitTransaction(std::shared_ptr<TransactionBase>);
    DatabaseTransactionResult DirectCommitTransactionWithResult(std::shared_ptr<TransactionBase>);
};
#include "native.inc"
int main(){
    using S=DatabaseTransactionStatus;
    auto test=[](std::vector<int> codes,S status,int clean){
        MySQLConnection c{codes}; ModuleDatabasePool p; p._connections[1].push_back(&c);
        auto t=std::make_shared<TransactionBase>(); auto r=p.DirectCommitTransactionWithResult(t);
        assert(r.status==status && c.pos==codes.size() && c.unlock==1 && t->clean==clean);
    };
    test({0},S::Committed,0);
    test({-1},S::Empty,1);
    test({-3},S::NotStarted,1);
    test({-2},S::Ambiguous,1);
    test({1213,-2},S::Ambiguous,1);
    test({1213,-3},S::FailedUnverified,1);
    test({1213,1062},S::FailedUnverified,1);
    test({1213,0},S::Committed,1);
    test({1213,1213,1213,1213,1213,1213},S::FailedUnverified,1);
    ModuleDatabasePool p;
    assert(p.DirectCommitTransactionWithResult(std::make_shared<TransactionBase>()).status==S::NotEnqueued);
    MySQLConnection c{{1213,-2}}; p._connections[1].push_back(&c);
    assert(!p.TryDirectCommitTransaction(std::make_shared<TransactionBase>())&&c.pos==2);
    std::cout<<"PASS 11 module transaction cases\n";
}
