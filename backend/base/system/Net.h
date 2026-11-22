#pragma once

#include <QString>

namespace Net {
/**
 * A TCP connection to `host`:`port` (a name, IPv4 or IPv6), each address tried in turn within
 * `ms` in all. Returns the socket (blocking, close-on-exec) or -1, with the reason in `error` (an errno:
 * ETIMEDOUT when time ran out, EHOSTUNREACH, ECONNREFUSED...).
 */
int connect(const QString &host, int port, int ms, int *error = nullptr);
}  // namespace Net
