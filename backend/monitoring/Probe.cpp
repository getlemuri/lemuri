#include "monitoring/Probe.h"

#include "base/Handles.h"
#include "base/Net.h"
#include "base/Posix.h"

#include <chrono>
#include <memory>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace {
constexpr int WAIT_MS = 3000;
}

Probe::Probe() = default;

Probe::~Probe() {
    stop();
}

void Probe::launch(Outcomes o) {
    std::lock_guard<std::mutex> l(m);
    if (launched || stopped) return;
    outcomes = std::move(o);
    launched = true;
    ticking = std::thread([this] { clock(); });
    if (minutes > 0) test(targets);
}

void Probe::every(int n) {
    std::lock_guard<std::mutex> l(m);
    minutes = std::max(0, n);
    period++;
    changed.notify_all();
}

void Probe::all() {
    std::lock_guard<std::mutex> l(m);
    if (launched) test(targets);
}

void Probe::list(const QList<Target> &l) {
    std::lock_guard<std::mutex> g(m);
    targets = l;
    if (!launched || minutes == 0) return;
    // Never: only when asked (all()).
    QList<Target> fresh;
    for (const Target &t : l)
        if (tried.value(t.id) != t.address + ":" + QString::number(t.port)) fresh << t;
    test(fresh);
}

void Probe::stop() {
    {
        std::lock_guard<std::mutex> l(m);
        stopped = true;
        changed.notify_all();
    }
    if (ticking.joinable()) ticking.join();
    rounds.join();
}

void Probe::clock() {
    std::unique_lock<std::mutex> l(m);
    while (!stopped) {
        int seen = period;
        if (minutes <= 0) {
            changed.wait(l, [&] { return stopped || period != seen; });
            continue;
        }
        bool again = changed.wait_for(l, std::chrono::minutes(minutes), [&] { return stopped || period != seen; });
        // Woken by a new period: counted from now. Otherwise the time is up: a round.
        if (!again) test(targets);
    }
}

/** With the lock held. One thread per host; the outcomes all together once each is known. */
void Probe::test(const QList<Target> &l) {
    if (l.isEmpty() || stopped) return;
    for (const Target &t : l) tried.insert(t.id, t.address + ":" + QString::number(t.port));
    Outcomes give = outcomes;
    rounds.start([this, l, give] {
        auto states = std::make_shared<QHash<QString, Host::Reach>>();
        auto guard = std::make_shared<std::mutex>();
        std::vector<std::thread> each;
        for (const Target &t : l)
            each.emplace_back([t, states, guard] {
                Host::Reach s = reachable(t.address, t.port) ? Host::Reach::Online : Host::Reach::Offline;
                std::lock_guard<std::mutex> g(*guard);
                states->insert(t.id, s);
            });
        for (auto &t : each) t.join();
        {
            std::lock_guard<std::mutex> g(m);
            if (stopped) return;
        }
        give(*states);
    });
}

bool Probe::reachable(const QString &address, int port) {
    Fd fd(Net::connect(address, port, WAIT_MS));
    if (!fd) return false;
    greetAndClose(fd.get());
    return true;
}

void Probe::greetAndClose(int fd) {
    using Clock = std::chrono::steady_clock;
    auto end = Clock::now() + std::chrono::milliseconds(WAIT_MS);
    QByteArray line;
    // At most 255 bytes (RFC 4253): a line that does not arrive or does not end does not hold the probe.
    while (line.size() < 255) {
        auto left = std::chrono::duration_cast<std::chrono::milliseconds>(end - Clock::now()).count();
        pollfd p{fd, POLLIN, 0};
        if (left <= 0 || ::poll(&p, 1, int(left)) <= 0) return;
        char c;
        if (::read(fd, &c, 1) != 1 || c == '\n') break;
        line += c;
    }
    if (!line.startsWith("SSH-")) return;
    static const char hello[] = "SSH-2.0-Lemuri_probe\r\n";
    (void)!::send(fd, hello, sizeof hello - 1, MSG_NOSIGNAL);
}
