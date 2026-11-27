#include "PB59ExecuteDecodedReadback.h"
int main()
{
    using namespace PB59::Recovery::ReadbackWire;
    OwnedReadbackConnection connection(mysql_init(nullptr));
    if (!connection)
        return 2;
    return ExecuteDecodedReadback(std::move(connection), 0, std::array<std::uint64_t, 1>{0x4000000000000007ULL}) ? 1 : 0;
}
