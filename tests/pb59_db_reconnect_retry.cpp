#include <cassert>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <vector>
using uint32 = std::uint32_t;
using uint8 = std::uint8_t;
using namespace std::chrono_literals;
constexpr int CR_SERVER_GONE_ERROR=2006, CR_SERVER_LOST=2013, CR_SERVER_LOST_EXTENDED=2055,
    CR_CONN_HOST_ERROR=2003, CR_CONNECTION_ERROR=2002, CR_UNKNOWN_HOST=2005,
    ER_LOCK_DEADLOCK=1213, ER_WRONG_VALUE_COUNT=1058, ER_DUP_ENTRY=1062,
    ER_BAD_FIELD_ERROR=1054, ER_NO_SUCH_TABLE=1146, ER_PARSE_ERROR=1064, CONNECTION_ASYNC=1;
struct Handle { std::string error="original error"; int statements=0; };
struct Statement { Handle* handle; explicit Statement(Handle* h):handle(h){++h->statements;} ~Statement(){--handle->statements;} };
static void mysql_close(Handle* h) { assert(h && h->statements==0); delete h; }
static int slept=0;
namespace mockstd { using std::string; namespace this_thread { void sleep_for(std::chrono::seconds s){slept+=static_cast<int>(s.count());} } }
struct FailStop { uint32 code; std::string text; };
template<class... T> void log(T&&...) {}
#define LOG_INFO(...) log(__VA_ARGS__)
#define LOG_ERROR(...) log(__VA_ARGS__)
#define LOG_WARN(...) log(__VA_ARGS__)
#define LOG_FATAL(...) log(__VA_ARGS__)
#define ABORT(fmt,str,code,error) throw FailStop{code,error}
struct MySQLConnection {
    Handle* m_Mysql=nullptr;
    std::vector<std::unique_ptr<Statement>> m_stmts{2};
    std::vector<uint32> opens;
    std::size_t cursor=0;
    bool m_reconnecting=false, m_inTransaction=false, prepareOk=true;
    uint32 m_lostTransactionError=0;
    int prepares=0, m_connectionFlags=1;
    struct { std::string database="test",host="local",port_or_socket="0"; } m_connectionInfo;
    explicit MySQLConnection(std::initializer_list<uint32> results, bool old=true):opens(results){if(old){m_Mysql=new Handle; m_stmts[0]=std::make_unique<Statement>(m_Mysql);}}
    ~MySQLConnection(){for(auto& s:m_stmts)s.reset();if(m_Mysql)mysql_close(m_Mysql);}
    uint32 Open(){assert(m_reconnecting && !m_Mysql && cursor<opens.size()); auto code=opens[cursor++];if(!code)m_Mysql=new Handle;return code;}
    bool PrepareStatements(){assert(m_Mysql && m_reconnecting && m_stmts.size()==2);++prepares;if(prepareOk)m_stmts[0]=std::make_unique<Statement>(m_Mysql);return prepareOk;}
    bool _HandleMySQLErrno(uint32,char const*,uint8=5);
};
#define std mockstd
#include "native.inc"
#undef std
int main(){
    { MySQLConnection c({2002,2005,0}); slept=0; assert(c._HandleMySQLErrno(2013,c.m_Mysql->error.c_str())); assert(c.cursor==3 && c.prepares==1 && slept==6 && !c.m_reconnecting && c.m_Mysql); }
    { MySQLConnection c({2002,0}); c.m_inTransaction=true; slept=0; assert(!c._HandleMySQLErrno(2013,c.m_Mysql->error.c_str())); assert(c.m_lostTransactionError==2013 && !c.m_inTransaction && c.cursor==2 && slept==3); }
    { MySQLConnection c({0},false); assert(c._HandleMySQLErrno(2005,nullptr) && c.prepares==1); }
    { MySQLConnection c({2002,2005,2003}); slept=0; bool stopped=false; try{c._HandleMySQLErrno(2006,c.m_Mysql->error.c_str(),3);}catch(FailStop const& e){stopped=true;assert(e.code==2006 && e.text=="original error");} assert(stopped && c.cursor==3 && !c.m_Mysql && slept==16); }
    { MySQLConnection c({0}); c.prepareOk=false; slept=0; bool stopped=false; try{c._HandleMySQLErrno(2006,"prepare failure");}catch(FailStop const& e){stopped=true;assert(e.code==2006);} assert(stopped && c.prepares==1 && slept==10); }
    { MySQLConnection c({}); assert(!c._HandleMySQLErrno(1213,"deadlock")); assert(c.cursor==0 && c.m_Mysql); }
    { MySQLConnection c({}); bool stopped=false; try{c._HandleMySQLErrno(2006,"zero attempts",0);}catch(FailStop const&){stopped=true;} assert(stopped && c.cursor==0); }
    std::cout<<"PASS: 7 reconnect scenarios (mocked handles and time)\n";
}
