#include "PB59BufferedReadbackFetch.h"
int main() {
    MYSQL* connection = mysql_init(nullptr);
    if (!connection) return 2;
    auto result = PB59::Recovery::ReadbackWire::ReadBufferedResult(connection, nullptr,
        std::array<std::string_view,1>{"guid"}, std::array<std::size_t,1>{10}, 1);
    mysql_close(connection);
    return result ? 1 : 0;
}
