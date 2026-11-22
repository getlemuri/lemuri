#pragma once

#include "sessions/Transport.h"

#include <QByteArray>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <sys/types.h>

/**
 * The shell of this computer (the user's $SHELL, else bash, or zsh on a Mac) as a login shell, in a real
 * pseudo-terminal: colours, full-screen programs and resizing work as in any terminal.
 */
class LocalShell : public Transport {
public:
    ~LocalShell() override;
    void launch(ThreadGroup &threads, int columns, int lines) override;
    void send(const QByteArray &bytes) override;
    void resize(int columns, int lines) override;
    void close() override;
    bool isLocal() const override { return true; }
    /** The folder the shell is in now (for the double click on a name), or empty. */
    QString folder() const;

private:
    void run();
    void write(int fd);
    std::mutex m;
    std::condition_variable wake;
    ThreadGroup *threads = nullptr;
    int master = -1;
    std::atomic<pid_t> child{-1};
    /** Typed and pasted bytes not yet taken by the shell: the writer thread gives them, never the caller. */
    QByteArray pending;
    bool ended = false;
    int columns = 80, lines = 24;
    std::atomic<bool> closed{false};
};
