#pragma once

// The few POSIX calls that Linux has in a handier form (accept4, pipe2, SOCK_CLOEXEC, MSG_NOSIGNAL): the same on a
// Mac, where they are put together from the plain calls. SIGPIPE is ignored by the whole backend (main.cpp), so a
// peer that goes away is an error on send, never the end of the process.
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

namespace Posix {
inline void closeOnExec(int fd, bool nonblock = false) {
    if (fd < 0) return;
    ::fcntl(fd, F_SETFD, FD_CLOEXEC);
    if (nonblock) ::fcntl(fd, F_SETFL, ::fcntl(fd, F_GETFL) | O_NONBLOCK);
}

inline int socket(int domain, int type, int protocol, bool nonblock = false) {
#ifdef SOCK_CLOEXEC
    return ::socket(domain, type | SOCK_CLOEXEC | (nonblock ? SOCK_NONBLOCK : 0), protocol);
#else
    int fd = ::socket(domain, type, protocol);
    closeOnExec(fd, nonblock);
    return fd;
#endif
}

inline int accept(int fd, sockaddr *a, socklen_t *size) {
#ifdef __linux__
    return ::accept4(fd, a, size, SOCK_CLOEXEC);
#else
    int c = ::accept(fd, a, size);
    closeOnExec(c);
    return c;
#endif
}

inline int pipe(int fds[2], bool nonblock = false) {
#ifdef __linux__
    return ::pipe2(fds, O_CLOEXEC | (nonblock ? O_NONBLOCK : 0));
#else
    if (::pipe(fds) != 0) return -1;
    closeOnExec(fds[0], nonblock);
    closeOnExec(fds[1], nonblock);
    return 0;
#endif
}

inline int socketpair(int domain, int type, int protocol, int fds[2]) {
#ifdef SOCK_CLOEXEC
    return ::socketpair(domain, type | SOCK_CLOEXEC, protocol, fds);
#else
    if (::socketpair(domain, type, protocol, fds) != 0) return -1;
    closeOnExec(fds[0]);
    closeOnExec(fds[1]);
    return 0;
#endif
}
}  // namespace Posix
