#include "sessions/LocalShell.h"

#include "base/Error.h"
#include "base/I18n.h"
#include "base/Runtime.h"

#include <QDir>
#include <QFileInfo>
#include <QStringDecoder>
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <termios.h>
#include <thread>
#include <unistd.h>
#include <vector>
#if defined(__APPLE__)
#include <util.h>
#else
#include <pty.h>
#endif

extern char **environ;

namespace {
constexpr qsizetype MAX_PENDING = 8 * 1024 * 1024;
}  // namespace

LocalShell::~LocalShell() = default;

void LocalShell::launch(ThreadGroup &group, int c, int l) {
    threads = &group;
    columns = c;
    lines = l;
    onState(State::Connecting, {});
    auto self = std::static_pointer_cast<LocalShell>(shared_from_this());
    group.start([self] { self->run(); });
}

void LocalShell::run() {
    QByteArray own = qgetenv("SHELL");
#if defined(__APPLE__)
    QByteArray shell = !own.isEmpty() && ::access(own.constData(), X_OK) == 0 ? own : QByteArray("/bin/zsh");
#else
    QByteArray shell = !own.isEmpty() && ::access(own.constData(), X_OK) == 0 ? own : QByteArray("/bin/bash");
#endif
    QByteArray home = QFile::encodeName(QDir::homePath());
    // The child's environment is made here: after fork, in a process with other threads, only exec is safe (setenv
    // may wait forever on a lock another thread held at the fork).
    QList<QByteArray> env{"TERM=xterm-256color", "COLORTERM=truecolor"};
    for (char **e = environ; *e; e++)
        if (std::strncmp(*e, "TERM=", 5) != 0 && std::strncmp(*e, "COLORTERM=", 10) != 0) env << QByteArray(*e);
    std::vector<char *> envp;
    for (QByteArray &e : env) envp.push_back(e.data());
    envp.push_back(nullptr);
    char *argv[] = {shell.data(), const_cast<char *>("-l"), nullptr};
    struct winsize size {};
    int fd = -1;
    pid_t pid;
    {
        std::lock_guard<std::mutex> l(m);
        size.ws_col = static_cast<unsigned short>(columns);
        size.ws_row = static_cast<unsigned short>(lines);
        pid = forkpty(&fd, nullptr, nullptr, &size);
        if (pid == 0) {
            // The child: a login shell in the home folder, as a terminal window opens it.
            if (::chdir(home.constData()) != 0) {}
            ::execve(shell.constData(), argv, envp.data());
            ::_exit(127);
        }
        master = fd;
        child = pid;
    }
    if (pid < 0) {
        QString why = systemError();
        onData("\r\n\x1b[31m" + I18n::t("terminal.localError", why) + "\x1b[0m\r\n");
        closed = true;
        onState(State::Error, why);
        return;
    }
    // Its own copy of the terminal: the writer closes it when it ends, whatever the reader does with the master.
    // Neither copy goes into the shells started later.
    ::fcntl(fd, F_SETFD, FD_CLOEXEC);
    int out = ::fcntl(fd, F_DUPFD_CLOEXEC, 0);
    if (out >= 0) {
        auto self = std::static_pointer_cast<LocalShell>(shared_from_this());
        threads->start([self, out] { self->write(out); });
    }
    onState(State::Connected, {});
    QStringDecoder utf8(QStringDecoder::Utf8);
    char buffer[8192];
    for (;;) {
        ssize_t n = ::read(fd, buffer, sizeof buffer);
        if (n > 0) {
            onData(utf8.decode(QByteArrayView(buffer, n)));
            continue;
        }
        if (n < 0 && errno == EINTR) continue;
        break;  // the shell has ended (EIO on Linux once the child's side closes)
    }
    {
        std::lock_guard<std::mutex> l(m);
        if (child > 0) ::waitpid(child, nullptr, 0);
        child = -1;
        if (master >= 0) ::close(master);
        master = -1;
        ended = true;
    }
    wake.notify_all();
    if (!closed.exchange(true)) onState(State::Closed, {});
}

/**
 * Gives the shell what was typed, in order. Apart from the caller: a shell that does not read (stopped with Ctrl-S,
 * suspended) holds this thread only, not the backend with all its other sessions.
 */
void LocalShell::write(int fd) {
    for (;;) {
        QByteArray bytes;
        {
            std::unique_lock<std::mutex> l(m);
            wake.wait(l, [this] { return !pending.isEmpty() || ended || closed; });
            if (ended || closed) break;
            bytes.swap(pending);
        }
        for (qsizetype done = 0; done < bytes.size();) {
            ssize_t n = ::write(fd, bytes.constData() + done, size_t(bytes.size() - done));
            if (n < 0 && errno == EINTR) continue;
            if (n <= 0) {
                ::close(fd);
                return;
            }
            done += n;
        }
    }
    ::close(fd);
}

void LocalShell::send(const QByteArray &bytes) {
    {
        std::lock_guard<std::mutex> l(m);
        if (master < 0 || closed) return;
        // A shell that has stopped reading does not make the backend keep everything pasted into it.
        if (pending.size() + bytes.size() > MAX_PENDING) return;
        pending += bytes;
    }
    wake.notify_one();
}

void LocalShell::resize(int c, int l) {
    std::lock_guard<std::mutex> g(m);
    columns = c;
    lines = l;
    if (master < 0) return;
    struct winsize size {};
    size.ws_col = static_cast<unsigned short>(c);
    size.ws_row = static_cast<unsigned short>(l);
    ::ioctl(master, TIOCSWINSZ, &size);
}

void LocalShell::close() {
    if (closed.exchange(true)) return;
    {
        std::lock_guard<std::mutex> l(m);
        // As closing a terminal window: the shell and its jobs get a hang-up.
        if (child > 0) ::kill(child, SIGHUP);
    }
    wake.notify_all();
    onState(State::Closed, {});
}

QString LocalShell::folder() const {
    pid_t pid = child;
    if (pid <= 0) return {};
#if defined(__APPLE__)
    return {};
#else
    return QFileInfo(QString("/proc/%1/cwd").arg(pid)).symLinkTarget();
#endif
}
