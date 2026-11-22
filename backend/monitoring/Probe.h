#pragma once

#include "base/Runtime.h"
#include "hosts/Host.h"

#include <QHash>
#include <QList>
#include <QString>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>

/**
 * Periodically checks whether the hosts answer on the SSH port: enough for the TCP connection to open, no sign-in.
 * Every few minutes (as the user chose, at least MIN_MINUTES, or never) and at once for new hosts or hosts with a
 * changed address. A host with an SSH connection already open (see `connected`) is not knocked on: it answers.
 *
 * Every probe leaves a line in the server's log. Before closing it exchanges the version line with sshd
 * (greetAndClose): a connection closed without it ("Did not receive identification string") is what fail2ban, in
 * the "ddos" and "aggressive" modes of the sshd filter, counts as an attempt. At most POOL hosts at a time.
 */
class Probe {
public:
    /** Minutes between two rounds: fewer lines in the servers' logs than every minute. */
    static constexpr int DEFAULT_MINUTES = 5;
    /** Less often than this never: a round every minute fills the logs of every server, and wakes fail2ban. */
    static constexpr int MIN_MINUTES = 5;
    /** Hosts tried at the same time. */
    static constexpr int POOL = 16;
    struct Target {
        QString id, address;
        int port;
    };
    using Outcomes = std::function<void(const QHash<QString, Host::Reach> &states)>;
    /** Whether the host already has an open SSH connection; asked from the probe's threads. */
    using Connected = std::function<bool(const QString &hostId)>;

    Probe();
    ~Probe();
    /** Starts the rounds; the outcomes (host id -> online or offline) reach `outcomes` from another thread. */
    void launch(Outcomes outcomes, Connected connected = nullptr);
    /** The host list changed: new hosts or hosts with a changed address are tried at once (unless "never"). */
    void list(const QList<Target> &targets);
    /** Minutes between two rounds, at least MIN_MINUTES; 0: never on its own, only with all(). */
    void every(int minutes);
    /** Tries every host again, now. */
    void all();
    void stop();

    static bool reachable(const QString &address, int port);
    /** Reached: reads the server's version line and answers with ours. Whatever goes wrong here does not matter. */
    static void greetAndClose(int fd);

private:
    void clock();
    void test(const QList<Target> &targets);

    std::mutex m;
    std::condition_variable changed;
    std::thread ticking;
    /** The rounds under way: a thread each, which waits for its POOL workers. */
    ThreadGroup rounds;
    Outcomes outcomes;
    Connected connected;
    QList<Target> targets;
    /** host id -> "address:port" already tried, to recognise new or moved hosts. */
    QHash<QString, QString> tried;
    int minutes = DEFAULT_MINUTES;
    bool launched = false, stopped = false;
    /** Bumped when the period changes: the clock starts counting again. */
    int period = 0;
};
