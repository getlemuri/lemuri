#include "base/Log.h"

#include "base/Files.h"
#include "base/Posix.h"

#include <QDateTime>
#include <QFile>
#include <cstdio>
#include <fcntl.h>
#include <poll.h>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>

namespace {
constexpr qint64 LIMIT = 1'000'000;
constexpr int FILES = 3;

QString name(int n) {
    return QString("%1/logs/lemuri-%2.log").arg(Files::data()).arg(n);
}

/** lemuri-1 becomes -2, -0 becomes -1: the oldest goes. */
void rotate() {
    QFile::remove(name(FILES - 1));
    for (int i = FILES - 2; i >= 0; i--) QFile::rename(name(i), name(i + 1));
}

std::thread copier;
int console = -1;
/** Written by stop(): children may hold standard error open, so its end may never come. */
int wakeRead = -1, wakeWrite = -1;

int open() {
    return ::open(QFile::encodeName(name(0)).constData(), O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC | O_NOFOLLOW, 0600);
}
}  // namespace

void Log::start() {
    Files::privateFolder(Files::data() + "/logs");
    struct stat st;
    if (::stat(QFile::encodeName(name(0)).constData(), &st) == 0 && st.st_size > LIMIT / 2) rotate();
    int file = open();
    int pipes[2];
    int wake[2];
    if (file < 0 || Posix::pipe(pipes) != 0) return;
    if (Posix::pipe(wake) != 0) return;
    wakeRead = wake[0];
    wakeWrite = wake[1];
    // The real standard error stays for whoever started the backend (the app forwards it to its own).
    console = ::dup(STDERR_FILENO);
    ::dup2(pipes[1], STDERR_FILENO);
    ::close(pipes[1]);
    std::setvbuf(stderr, nullptr, _IOLBF, 0);
    copier = std::thread([reader = pipes[0], console = ::dup(console), file]() mutable {
        QByteArray pending;
        char buffer[8192];
        qint64 size = ::lseek(file, 0, SEEK_END);
        bool stopping = false;
        for (;;) {
            if (!stopping) {
                pollfd p[2] = {{reader, POLLIN, 0}, {wakeRead, POLLIN, 0}};
                if (::poll(p, 2, -1) < 0 && errno == EINTR) continue;
                if (p[1].revents & POLLIN) {
                    // What is already in the pipe is still copied, without waiting for more.
                    stopping = true;
                    ::fcntl(reader, F_SETFL, ::fcntl(reader, F_GETFL) | O_NONBLOCK);
                }
            }
            ssize_t n = ::read(reader, buffer, sizeof buffer);
            if (n == 0) break;
            if (n < 0) {
                if (errno == EINTR) continue;
                break;
            }
            if (console >= 0 && ::write(console, buffer, size_t(n)) < 0) console = -1;
            pending.append(buffer, n);
            for (qsizetype nl; (nl = pending.indexOf('\n')) >= 0;) {
                QByteArray line = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss ").toUtf8() + pending.left(nl).replace('\r', "\\r") + '\n';
                pending.remove(0, nl + 1);
                if (size + line.size() > LIMIT) {
                    ::close(file);
                    rotate();
                    file = open();
                    size = 0;
                }
                if (file >= 0 && ::write(file, line.constData(), size_t(line.size())) > 0) size += line.size();
            }
        }
        ::close(reader);
        ::close(file);
        if (console >= 0) ::close(console);
    });
}

void Log::stop() {
    if (!copier.joinable()) return;
    std::fflush(stderr);
    ::dup2(console, STDERR_FILENO);
    ::close(console);
    console = -1;
    char one = 1;
    (void)!::write(wakeWrite, &one, 1);
    copier.join();
    ::close(wakeRead);
    ::close(wakeWrite);
}
