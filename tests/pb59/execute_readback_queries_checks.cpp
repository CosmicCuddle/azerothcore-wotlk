#include "PB59ExecuteReadbackQueries.h"
#include <iostream>
using namespace PB59::Recovery::ReadbackWire;
namespace {
MYSQL connection{};
MYSQL_RES result{};
std::array<MYSQL_FIELD,14> fields{};
std::array<std::string,14> names{};
std::array<char*,14> cells{};
std::array<unsigned long,14> lengths{};
std::array<std::string,3> issued{};
char value[]="7", binary[]={'a','\0','b',' '};
unsigned calls=0, stores=0, frees=0, closes=0, cursor=0, count=0, error=0, warning=0;
unsigned long threadId=41;
unsigned faultAt=0, fault=0;
bool more=false, openResult=false, invalidCleanup=false;
void Reset(unsigned at=0,unsigned kind=0) {
    calls=stores=frees=closes=cursor=count=error=warning=0;
    threadId=41; faultAt=at; fault=kind; more=openResult=invalidCleanup=false; issued={};
}
bool Fault(unsigned kind) {return calls==faultAt && fault==kind;}
template<std::size_t N> void Fields(std::array<QueryColumn,N> const& p) {
    count=N; fields={};
    for(std::size_t i=0;i<N;++i) {
        names[i]=p[i].name; fields[i].name=names[i].data(); fields[i].name_length=names[i].size();
        fields[i].type=MYSQL_TYPE_VAR_STRING; fields[i].charsetnr=63;
        cells[i]=value; lengths[i]=1;
    }
    if(calls==2) {cells[7]=nullptr;lengths[7]=0;cells[13]=binary;lengths[13]=4;}
}
}
extern "C" {
unsigned long STDCALL mysql_thread_id(MYSQL*) {return threadId;}
unsigned int STDCALL mysql_errno(MYSQL*) {return error;}
unsigned int STDCALL mysql_warning_count(MYSQL*) {return warning;}
using MoreResult=decltype(mysql_more_results(static_cast<MYSQL*>(nullptr)));
MoreResult STDCALL mysql_more_results(MYSQL*) {return more;}
int STDCALL mysql_real_query(MYSQL*,char const* sql,unsigned long n) {
    if(openResult || calls>=3) {invalidCleanup=true;return 1;}
    issued[calls++]=std::string(sql,n);cursor=0;
    if(calls==1) Fields(LinkProjection); else if(calls==2) Fields(InstanceProjection); else Fields(GiftProjection);
    if(Fault(1)) return 1;
    if(Fault(2)) ++threadId;
    if(Fault(3)) warning=1;
    if(Fault(4)) more=true;
    return 0;
}
MYSQL_RES* STDCALL mysql_store_result(MYSQL*) {
    ++stores;
    if(Fault(5)) return nullptr;
    openResult=true;
    if(Fault(6)) ++threadId;
    if(Fault(7)) fields[0].charsetnr=45;
    return &result;
}
void STDCALL mysql_free_result(MYSQL_RES*) {
    if(!openResult) invalidCleanup=true;
    openResult=false;++frees;
    if(Fault(10)) ++threadId;
}
void STDCALL mysql_close(MYSQL*) {if(openResult)invalidCleanup=true;++closes;}
unsigned int STDCALL mysql_field_count(MYSQL*) {return count;}
unsigned int STDCALL mysql_num_fields(MYSQL_RES*) {return count;}
my_ulonglong STDCALL mysql_num_rows(MYSQL_RES*) {return Fault(8)?2:1;}
MYSQL_FIELD* STDCALL mysql_fetch_fields(MYSQL_RES*) {return fields.data();}
MYSQL_ROW STDCALL mysql_fetch_row(MYSQL_RES*) {
    if(cursor++) {if(Fault(9)) error=1;return nullptr;}
    return cells.data();
}
unsigned long* STDCALL mysql_fetch_lengths(MYSQL_RES*) {return lengths.data();}
}
unsigned checks=0;
#define CHECK(x) do {++checks;if(!(x)){std::cerr<<"failed line "<<__LINE__<<'\n';return 1;}}while(false)
int main(){
    constexpr std::uint64_t prefix=0x4000ULL<<48;
    std::array<std::uint64_t,1> ids{prefix|7};
    auto run=[&](unsigned long expected=41){return ExecuteReadbackQueries(OwnedReadbackConnection(&connection),expected,ids);};
    Reset();auto got=run();
    CHECK(got && got->links.size()==1 && got->instances.size()==1 && got->gifts.size()==1);
    CHECK(got->links[0][0]=="7" && !got->instances[0][7] && got->instances[0][13]==std::string("a\0b ",4));
    CHECK(calls==3 && stores==3 && frees==3 && closes==1 && !invalidCleanup);
    auto plan=BuildReadbackQueryPlan(ids);CHECK(plan && issued[0]==plan->links && issued[1]==plan->instances && issued[2]==plan->gifts);
    Reset();CHECK(!ExecuteReadbackQueries({},41,ids));CHECK(calls==0 && closes==0);
    Reset();CHECK(!run(0));CHECK(calls==0 && closes==1);
    Reset();CHECK(!run(42));CHECK(calls==0 && closes==1);
    Reset();warning=1;CHECK(!run());CHECK(calls==0 && closes==1);
    Reset();error=1;CHECK(!run());CHECK(calls==0 && closes==1);
    Reset();more=true;CHECK(!run());CHECK(calls==0 && closes==1);
    Reset();ids[0]=7;CHECK(!run());CHECK(calls==0 && closes==1);ids[0]=prefix|7;
    for(unsigned at=1;at<=3;++at) for(unsigned kind=1;kind<=10;++kind){
        Reset(at,kind);CHECK(!run());CHECK(calls==at && closes==1 && !invalidCleanup);
        CHECK(stores==(kind<=4?at-1:at));
        CHECK(frees==(kind<=5?at-1:at));
        CHECK(cursor==(kind<=8?0u:2u));
    }
    std::cout<<checks<<'/'<<checks<<" execute checks passed\n";
}
