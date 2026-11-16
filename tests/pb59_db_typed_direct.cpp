#include "DatabaseTransactionResult.h"
#include <cassert>
#include <cstdint>
#include <initializer_list>
#include <iostream>
#include <memory>
#include <vector>
using uint8 = std::uint8_t;
constexpr int ER_LOCK_DEADLOCK = 1213;
struct Transaction { int cleaned = 0; void Cleanup() { ++cleaned; } };
template<class T> using SQLTransaction = std::shared_ptr<Transaction>;
struct Connection {
    std::vector<int> codes;
    std::size_t cursor = 0;
    int unlocks = 0;
    explicit Connection(std::initializer_list<int> c) : codes(c) {}
    int ExecuteTransaction(SQLTransaction<Connection>&) { assert(cursor < codes.size()); return codes[cursor++]; }
    void Unlock() { ++unlocks; }
};
template<class T> struct DatabaseWorkerPool {
    T connection;
    explicit DatabaseWorkerPool(std::initializer_list<int> codes) : connection(codes) {}
    T* GetFreeConnection() { return &connection; }
    void DirectCommitTransaction(SQLTransaction<T>&);
    DatabaseTransactionResult DirectCommitTransactionWithResult(SQLTransaction<T>&);
};
#include "native.inc"
int main() {
    using S = DatabaseTransactionStatus;
    auto check = [](std::initializer_list<int> codes, S status, int code, int cleaned) {
        DatabaseWorkerPool<Connection> pool(codes);
        auto t = std::make_shared<Transaction>();
        auto r = pool.DirectCommitTransactionWithResult(t);
        assert(r.status == status && r.executionCode == code);
        assert(r.IsCommitted() == (status == S::Committed));
        assert(pool.connection.cursor == codes.size() && pool.connection.unlocks == 1 && t->cleaned == cleaned);
    };
    check({0}, S::Committed, 0, 0);
    check({-1}, S::Empty, -1, 1);
    check({-3}, S::NotStarted, -3, 1);
    check({-2}, S::Ambiguous, -2, 1);
    check({1062}, S::FailedUnverified, 1062, 1);
    check({1213, 0}, S::Committed, 0, 1);
    check({1213, -2}, S::Ambiguous, -2, 1);
    check({1213, -3}, S::FailedUnverified, -4, 1);
    check({1213, 1062}, S::FailedUnverified, 1062, 1);
    check({1213, 1213, 1213, 1213, 1213, 1213}, S::FailedUnverified, 1213, 1);
    DatabaseWorkerPool<Connection> legacy({1213, -2});
    auto t = std::make_shared<Transaction>();
    legacy.DirectCommitTransaction(t);
    assert(legacy.connection.cursor == 2 && legacy.connection.unlocks == 1 && t->cleaned == 1);
    std::cout << "PASS: 11 typed direct transaction scenarios\n";
}
