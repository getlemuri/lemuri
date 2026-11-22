#include "base/Net.h"

#include "base/Posix.h"

#include <chrono>
#include <cerrno>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

int Net::connect(const QString &host, int port, int ms, int *error) {
    using Clock = std::chrono::steady_clock;
    auto end = Clock::now() + std::chrono::milliseconds(ms);
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo *found = nullptr;
    QByteArray name = host.toUtf8(), service = QByteArray::number(port);
    int why = EHOSTUNREACH;
    if (error) *error = why;
    if (host.isEmpty() || port <= 0 || port > 65535 || ::getaddrinfo(name.constData(), service.constData(), &hints, &found) != 0) return -1;
    int fd = -1;
    for (addrinfo *a = found; a && fd < 0; a = a->ai_next) {
        auto left = std::chrono::duration_cast<std::chrono::milliseconds>(end - Clock::now()).count();
        if (left <= 0) {
            why = ETIMEDOUT;
            break;
        }
        int s = Posix::socket(a->ai_family, a->ai_socktype, a->ai_protocol, true);
        if (s < 0) continue;
        int rc = ::connect(s, a->ai_addr, a->ai_addrlen);
        if (rc != 0 && errno != EINPROGRESS) why = errno;
        if (rc != 0 && errno == EINPROGRESS) {
            pollfd p{s, POLLOUT, 0};
            int failed = 0;
            socklen_t size = sizeof failed;
            int ready = ::poll(&p, 1, int(left));
            if (ready == 0) why = ETIMEDOUT;
            else if (ready == 1 && ::getsockopt(s, SOL_SOCKET, SO_ERROR, &failed, &size) == 0 && failed == 0) rc = 0;
            else why = failed ? failed : errno;
        }
        if (rc == 0) {
            ::fcntl(s, F_SETFL, ::fcntl(s, F_GETFL) & ~O_NONBLOCK);
            fd = s;
        } else {
            ::close(s);
        }
    }
    ::freeaddrinfo(found);
    if (error) *error = fd >= 0 ? 0 : why;
    return fd;
}
