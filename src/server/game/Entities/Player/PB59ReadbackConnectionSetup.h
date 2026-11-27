#pragma once

#include "PB59ExecuteReadbackQueries.h"

namespace PB59::Recovery::ReadbackWire
{
// Unix-socket-only setup for a fresh dedicated handle. No option files,
// network fallback, reconnect, multi-statement flags or local file loading.
// This does not open/authenticate a connection or establish endpoint trust.
[[nodiscard]] inline OwnedReadbackConnection PrepareLocalReadbackConnection(unsigned int timeoutSeconds) noexcept
{
    if (!timeoutSeconds || timeoutSeconds > 60)
        return {};
    OwnedReadbackConnection connection(mysql_init(nullptr));
    if (!connection)
        return {};
    auto* handle = connection.get();
    using ClientBool = decltype(mysql_more_results(handle));
    ClientBool reconnect = false;
    unsigned int localFiles = 0;
    unsigned int protocol = MYSQL_PROTOCOL_SOCKET;
    if (mysql_options(handle, MYSQL_OPT_RECONNECT, &reconnect) ||
        mysql_options(handle, MYSQL_OPT_LOCAL_INFILE, &localFiles) ||
        mysql_options(handle, MYSQL_OPT_PROTOCOL, &protocol) ||
        mysql_options(handle, MYSQL_OPT_CONNECT_TIMEOUT, &timeoutSeconds) ||
        mysql_options(handle, MYSQL_OPT_READ_TIMEOUT, &timeoutSeconds) ||
        mysql_options(handle, MYSQL_OPT_WRITE_TIMEOUT, &timeoutSeconds) ||
        mysql_options(handle, MYSQL_SET_CHARSET_NAME, "utf8mb4"))
        return {};
    return connection;
}
}
