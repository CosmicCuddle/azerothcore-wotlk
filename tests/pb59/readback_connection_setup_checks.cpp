#include "PB59ReadbackConnectionSetup.h"
#include <iostream>
using namespace PB59::Recovery::ReadbackWire;
namespace {
MYSQL handle{};
unsigned init, calls, closes, failure;
bool initFail, wrong;
unsigned int timeout = 9;
constexpr std::array<mysql_option, 7> order{MYSQL_OPT_RECONNECT, MYSQL_OPT_LOCAL_INFILE, MYSQL_OPT_PROTOCOL,
    MYSQL_OPT_CONNECT_TIMEOUT, MYSQL_OPT_READ_TIMEOUT, MYSQL_OPT_WRITE_TIMEOUT, MYSQL_SET_CHARSET_NAME};
void Reset(unsigned fault=0) {init=calls=closes=0;failure=fault;initFail=wrong=false;}
}
extern "C" {
MYSQL* STDCALL mysql_init(MYSQL* p) {++init; if(p) wrong=true;return initFail?nullptr:&handle;}
void STDCALL mysql_close(MYSQL* p) {++closes;if(p!=&handle) wrong=true;}
int STDCALL mysql_options(MYSQL* p, mysql_option option, void const* data) {
    if(p!=&handle || calls>=order.size() || option!=order[calls] || !data) {wrong=true;return 1;}
    ++calls;
    if(option==MYSQL_OPT_RECONNECT) wrong|=*static_cast<decltype(mysql_more_results(p)) const*>(data)!=0;
    else if(option==MYSQL_SET_CHARSET_NAME) wrong|=std::string_view(static_cast<char const*>(data))!="utf8mb4";
    else {auto value=*static_cast<unsigned int const*>(data);wrong|=value!=(option==MYSQL_OPT_PROTOCOL?unsigned(MYSQL_PROTOCOL_SOCKET):option==MYSQL_OPT_LOCAL_INFILE?0u:timeout);}
    return calls==failure || wrong;
}
}
unsigned checks=0;
#define CHECK(x) do {++checks;if(!(x)){std::cerr<<"failed "<<__LINE__<<'\n';return 1;}}while(false)
int main(){
    for(unsigned n:{0u,61u,~0u}){Reset();CHECK(!PrepareLocalReadbackConnection(n));CHECK(init==0&&calls==0&&closes==0);}
    Reset();initFail=true;CHECK(!PrepareLocalReadbackConnection(timeout));CHECK(init==1&&calls==0&&closes==0);
    for(unsigned n=1;n<=7;++n){Reset(n);CHECK(!PrepareLocalReadbackConnection(timeout));CHECK(init==1&&calls==n&&closes==1&&!wrong);}
    for(unsigned n:{1u,9u,60u}){Reset();timeout=n;{auto p=PrepareLocalReadbackConnection(timeout);CHECK(p.get()==&handle);CHECK(init==1&&calls==7&&closes==0&&!wrong);}CHECK(closes==1);}
    std::cout<<checks<<'/'<<checks<<" connection setup checks passed\n";
}
