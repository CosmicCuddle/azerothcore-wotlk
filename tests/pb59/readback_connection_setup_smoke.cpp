#include "PB59ReadbackConnectionSetup.h"
#include <iostream>
int main(){
    using namespace PB59::Recovery::ReadbackWire;
    auto connection=PrepareLocalReadbackConnection(9);
    if(!connection)return 1;
    decltype(mysql_more_results(connection.get())) reconnect=1;
    unsigned int protocol=0, timeout=0;
    if(mysql_get_option(connection.get(),MYSQL_OPT_RECONNECT,&reconnect)||reconnect||
       mysql_get_option(connection.get(),MYSQL_OPT_PROTOCOL,&protocol)||protocol!=MYSQL_PROTOCOL_SOCKET||
       mysql_get_option(connection.get(),MYSQL_OPT_CONNECT_TIMEOUT,&timeout)||timeout!=9)return 2;
    std::cout<<"Unconnected client options verified\n";
}
