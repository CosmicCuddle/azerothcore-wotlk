#include <cassert>
#include <cstdlib>
#include <cstdint>
#include <memory>
#include <string>
#include <variant>
#include <vector>
#include <utility>
#include <iostream>
#include <initializer_list>
using uint8 = std::uint8_t;
constexpr int CR_SERVER_GONE_ERROR=2006;
template<class... T> void log(T&&...) {}
#define LOG_ERROR(...) log(__VA_ARGS__)
#define LOG_WARN(...) log(__VA_ARGS__)
#define LOG_FATAL(...) log(__VA_ARGS__)
#define ASSERT(x) assert(x)
#define ABORT(...) std::abort()
struct PreparedStatementBase {};
enum { SQL_ELEMENT_PREPARED, SQL_ELEMENT_RAW };
struct SQLElementData { int type; std::variant<PreparedStatementBase*, std::string> element; };
struct TransactionBase { std::vector<SQLElementData> m_queries; };
struct Step { std::string sql; bool ok; int error=0; bool lost=false; };
struct MySQLConnection {
    static constexpr int TransactionBoundaryFailure=-2;
    bool m_inTransaction=false;
    int m_lostTransactionError=0, error=0, resets=0, reconnects=0;
    std::size_t cursor=0;
    std::vector<Step> steps;
    explicit MySQLConnection(std::initializer_list<Step> s): steps(s) {}
    bool Execute(std::string const& sql) {
        assert(cursor<steps.size()); auto const& s=steps[cursor++]; assert(s.sql==sql);
        if(sql!="START TRANSACTION") assert(m_inTransaction);
        error=s.error;
        if(s.lost) { assert(m_inTransaction); m_inTransaction=false; m_lostTransactionError=s.error; ++reconnects; }
        return s.ok;
    }
    bool Execute(PreparedStatementBase*) { return Execute("PREPARED"); }
    int GetLastError() const { return error; }
    bool _HandleMySQLErrno(int code, char const*) { assert(code==CR_SERVER_GONE_ERROR && !m_inTransaction); ++resets; return true; }
    int ExecuteTransaction(std::shared_ptr<TransactionBase>);
    int ExecuteTransactionAttempt(std::shared_ptr<TransactionBase> const&, bool&);
    int AbortTransaction(std::size_t,bool&);
    void ResetTransactionConnection();
};
#include "native.inc"
int main(){
    PreparedStatementBase stmt;
    auto t=std::make_shared<TransactionBase>();
    t->m_queries={{SQL_ELEMENT_RAW,std::string("BODY")},{SQL_ELEMENT_PREPARED,&stmt}};
    auto check=[&](std::initializer_list<Step> s,int result,int resets,int reconnects=0){
        MySQLConnection c(s); assert(c.ExecuteTransaction(t)==result); assert(c.cursor==c.steps.size()); assert(c.resets==resets); assert(c.reconnects==reconnects); assert(!c.m_inTransaction && !c.m_lostTransactionError);
    };
    check({{"START TRANSACTION",true},{"BODY",true},{"PREPARED",true},{"COMMIT",true}},0,0);
    check({{"START TRANSACTION",false,123}},-2,1);
    check({{"START TRANSACTION",true},{"BODY",true},{"PREPARED",true},{"COMMIT",false,1213}},-2,1);
    check({{"START TRANSACTION",true},{"BODY",true},{"PREPARED",true},{"COMMIT",false,2006,true}},-2,0,1);
    check({{"START TRANSACTION",true},{"BODY",false,1213},{"ROLLBACK",true}},1213,0);
    check({{"START TRANSACTION",true},{"BODY",false,1213},{"ROLLBACK",false,123}},-2,1);
    check({{"START TRANSACTION",true},{"BODY",false,1213},{"ROLLBACK",false,2006,true}},-2,0,1);
    check({{"START TRANSACTION",true},{"BODY",false,2006,true},{"START TRANSACTION",true},{"BODY",true},{"PREPARED",true},{"COMMIT",true}},0,0,1);
    check({{"START TRANSACTION",true},{"BODY",true},{"PREPARED",false,0},{"ROLLBACK",true}},-2,0);
    check({{"START TRANSACTION",true},{"BODY",false,2006,true},{"START TRANSACTION",true},{"BODY",false,2006,true},{"START TRANSACTION",true},{"BODY",false,2006,true}},2006,0,3);
    MySQLConnection empty({}); assert(empty.ExecuteTransaction(std::make_shared<TransactionBase>())==-1);
    std::cout<<"PASS: 11 transaction boundary scenarios (mocked connection)\n";
}
