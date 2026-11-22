#include "sharing/Lan.h"

#include "base/Error.h"
#include "base/Files.h"
#include "base/I18n.h"
#include "base/Legacy.h"
#include "base/Net.h"
#include "base/Posix.h"
#include "base/Prefs.h"

#include <QDateTime>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QRegularExpression>
#include <QSysInfo>
#include <QUuid>
#include <arpa/inet.h>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <future>
#include <ifaddrs.h>
#include <net/if.h>
#include <netdb.h>
#include <netinet/in.h>
#include <optional>
#include <poll.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

namespace {
using Clock = std::chrono::steady_clock;
constexpr qint64 ANNOUNCE_MS = 5'000;
/** Connections served at once: beyond that, new ones are refused, so a stranger cannot flood the app. */
constexpr int MAX_JOBS = 16;
/** A message fits in a few KB: whoever sends more is not one of ours. */
constexpr int MAX_LINE = 64 * 1024;
/** Unknown PCs remembered at most: a burst of fake announcements does not fill memory. */
constexpr int MAX_UNKNOWN = 64;
/** Other addresses announced for a trusted key, tried only if the known one does not prove itself. */
constexpr int MAX_UNPROVEN = 3;
/** Pairing version: from 2 on, the code is computed with the commitment. */
constexpr int PAIRING_VERSION = 2;
/** From the same address, at most one pairing request in this time. */
constexpr qint64 PAIRING_PAUSE_MS = 60'000;
/** How long this PC waits for a link once the user asked to (Team → Wait for a link). */
constexpr qint64 WAIT_FOR_LINK_MS = 5 * 60'000;
/** Connections at the same time from one address: one PC cannot take every thread. */
constexpr int MAX_JOBS_PER_ADDRESS = 4;
const QString TRUSTED = QStringLiteral("lan:trusted");

qint64 now() {
    return QDateTime::currentMSecsSinceEpoch();
}

QString errorText(int error) {
    return QString::fromLocal8Bit(std::strerror(error));
}

// ── Wire: one JSON object per line ──

QString text(const QJsonObject &o, const QString &field) {
    // Only a plain value: an object or an array in its place (a message shaped by someone else) counts as missing.
    QJsonValue v = o[field];
    if (v.isString()) return v.toString();
    if (v.isBool()) return v.toBool() ? "true" : "false";
    if (v.isDouble()) {
        double d = v.toDouble();
        return d == double(qint64(d)) ? QString::number(qint64(d)) : QString::number(d);
    }
    return QString();
}

/** True only if the field is the boolean true: anything else is a no. */
bool yes(const QJsonObject &o, const QString &field) {
    return o[field].isBool() && o[field].toBool();
}

/** A whole number field, or `otherwise` if it is missing, not a number or out of the int range. */
int number(const QJsonObject &o, const QString &field, int otherwise) {
    QJsonValue v = o[field];
    if (!v.isDouble()) return otherwise;
    double d = v.toDouble();
    return d >= -2147483648.0 && d <= 2147483647.0 ? int(d) : otherwise;
}

QString shorten(const QString &s) {
    if (s.isNull()) return s;
    // Not only the ASCII controls: also zero-width and text direction characters, which would hide what is shown.
    QString r;
    for (char32_t c : s.toUcs4()) {
        QChar::Category k = QChar::category(c);
        if (k == QChar::Other_Control || k == QChar::Other_Format || k == QChar::Separator_Line || k == QChar::Separator_Paragraph) continue;
        r += QString::fromUcs4(&c, 1);
    }
    return r.size() > 80 ? r.left(80) : r;
}

QJsonObject parse(const QByteArray &line) {
    QJsonDocument d = QJsonDocument::fromJson(line);
    if (!d.isObject()) throw Fail{"malformed message"};
    return d.object();
}

void write(int fd, const QJsonObject &m) {
    QByteArray b = QJsonDocument(m).toJson(QJsonDocument::Compact) + "\n";
    qsizetype done = 0;
    while (done < b.size()) {
        ssize_t n = ::send(fd, b.constData() + done, size_t(b.size() - done), MSG_NOSIGNAL);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) throw Fail{errorText(errno)};
        done += n;
    }
}

/**
 * A private address: IPv4 10/8, 172.16/12, 192.168/16, link-local, loopback, IPv6 ULA; 100.64/10 only if
 * `carrierGrade` (a Tailscale network, or a carrier's: there the other "local" PCs are the operator's other customers).
 */
bool isPrivate(const sockaddr *a, bool carrierGrade) {
    if (a->sa_family == AF_INET6) {
        const auto *b = reinterpret_cast<const sockaddr_in6 *>(a)->sin6_addr.s6_addr;
        if (IN6_IS_ADDR_V4MAPPED(reinterpret_cast<const in6_addr *>(b))) {
            sockaddr_in v4{};
            v4.sin_family = AF_INET;
            std::memcpy(&v4.sin_addr, b + 12, 4);
            return isPrivate(reinterpret_cast<const sockaddr *>(&v4), carrierGrade);
        }
        const auto *x = reinterpret_cast<const in6_addr *>(b);
        // loopback, link-local, the old site-local fec0::/10 and ULA fc00::/7
        return IN6_IS_ADDR_LOOPBACK(x) || IN6_IS_ADDR_LINKLOCAL(x) || IN6_IS_ADDR_SITELOCAL(x) || (b[0] & 0xfe) == 0xfc;
    }
    if (a->sa_family != AF_INET) return false;
    const auto *b = reinterpret_cast<const unsigned char *>(&reinterpret_cast<const sockaddr_in *>(a)->sin_addr);
    return b[0] == 127 || b[0] == 10 || (b[0] == 172 && (b[1] & 0xf0) == 16) || (b[0] == 192 && b[1] == 168)
           || (b[0] == 169 && b[1] == 254) || (carrierGrade && b[0] == 100 && (b[1] & 0xc0) == 64);
}

bool isLoopback(const sockaddr *a) {
    if (a->sa_family == AF_INET6) {
        const auto *x = &reinterpret_cast<const sockaddr_in6 *>(a)->sin6_addr;
        if (IN6_IS_ADDR_V4MAPPED(x)) return x->s6_addr[12] == 127;
        return IN6_IS_ADDR_LOOPBACK(x);
    }
    return a->sa_family == AF_INET && reinterpret_cast<const unsigned char *>(&reinterpret_cast<const sockaddr_in *>(a)->sin_addr)[0] == 127;
}

/** The address as text; an IPv4 address reached through an IPv6 socket as plain IPv4. */
QString hostAddress(const sockaddr *a) {
    char buffer[INET6_ADDRSTRLEN + IF_NAMESIZE + 2] = {};
    if (a->sa_family == AF_INET6) {
        const auto *s6 = reinterpret_cast<const sockaddr_in6 *>(a);
        if (IN6_IS_ADDR_V4MAPPED(&s6->sin6_addr)) {
            ::inet_ntop(AF_INET, s6->sin6_addr.s6_addr + 12, buffer, sizeof buffer);
            return QString::fromLatin1(buffer);
        }
        ::inet_ntop(AF_INET6, &s6->sin6_addr, buffer, sizeof buffer);
        QString s = QString::fromLatin1(buffer);
        char name[IF_NAMESIZE] = {};
        if (s6->sin6_scope_id && ::if_indextoname(s6->sin6_scope_id, name)) s += "%" + QString::fromLatin1(name);
        return s;
    }
    ::inet_ntop(AF_INET, &reinterpret_cast<const sockaddr_in *>(a)->sin_addr, buffer, sizeof buffer);
    return QString::fromLatin1(buffer);
}

/** The first address a name resolves to, as text; the name itself if it does not resolve. */
QString resolve(const QString &host) {
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo *found = nullptr;
    if (::getaddrinfo(host.toUtf8().constData(), nullptr, &hints, &found) != 0 || !found) return host;
    QString s = hostAddress(found->ai_addr);
    ::freeaddrinfo(found);
    return s;
}

// ── The firewall of this PC ──

/** firewalld (active service) or ufw (ENABLED=yes in /etc/ufw/ufw.conf); reading the rules would need root. */
QString activeFirewall() {
#ifdef Q_OS_MACOS
    return QString();
#else
    QProcess p;
    p.start("systemctl", {"is-active", "--quiet", "firewalld"});
    if (p.waitForFinished(3000) && p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0) return "firewalld";
    if (p.state() != QProcess::NotRunning) {
        p.kill();
        p.waitForFinished(1000);
    }
    QFile f("/etc/ufw/ufw.conf");
    if (f.open(QIODevice::ReadOnly))
        for (const QByteArray &l : f.readAll().split('\n'))
            if (l.trimmed() == "ENABLED=yes") return "ufw";
    return QString();
#endif
}

QString howToOpen(const QString &firewall) {
    QString a = QString::number(Lan::ANNOUNCE_PORT), t = QString::number(Lan::PORT);
    QString command = firewall == "ufw" ? "sudo ufw allow " + a + "/udp && sudo ufw allow " + t + "/tcp"
                                        : "sudo firewall-cmd --permanent --add-port=" + a + "/udp --add-port=" + t
                                              + "/tcp && sudo firewall-cmd --reload";
    return I18n::t("lan.firewallHere", firewall, a, t, command);
}

QPair<int, int> ports() {
    QStringList l = qEnvironmentVariable("LEMURI_LAN_PORTS").split(',');
    if (l.size() == 2) return {l[0].toInt(), l[1].toInt()};
    return {Lan::PORT, Lan::ANNOUNCE_PORT};
}

/** A socket listening on every interface, IPv6 and IPv4 together where possible. Throws Fail. */
int bound(int type, int port, bool reuse, bool broadcast, int *actualPort) {
    int fd = Posix::socket(AF_INET6, type, 0);
    bool six = fd >= 0;
    if (!six) fd = Posix::socket(AF_INET, type, 0);
    if (fd < 0) throw Fail{errorText(errno)};
    int one = 1, zero = 0;
    if (reuse) ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    if (broadcast) ::setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &one, sizeof one);
    sockaddr_storage s{};
    socklen_t size;
    if (six) {
        ::setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &zero, sizeof zero);
        auto *a = reinterpret_cast<sockaddr_in6 *>(&s);
        a->sin6_family = AF_INET6;
        a->sin6_addr = in6addr_any;
        a->sin6_port = htons(quint16(port));
        size = sizeof(sockaddr_in6);
    } else {
        auto *a = reinterpret_cast<sockaddr_in *>(&s);
        a->sin_family = AF_INET;
        a->sin_addr.s_addr = htonl(INADDR_ANY);
        a->sin_port = htons(quint16(port));
        size = sizeof(sockaddr_in);
    }
    if (::bind(fd, reinterpret_cast<sockaddr *>(&s), size) != 0 || (type == SOCK_STREAM && ::listen(fd, 50) != 0)) {
        int e = errno;
        ::close(fd);
        throw Fail{errorText(e)};
    }
    size = sizeof s;
    ::getsockname(fd, reinterpret_cast<sockaddr *>(&s), &size);
    *actualPort = ntohs(s.ss_family == AF_INET6 ? reinterpret_cast<sockaddr_in6 *>(&s)->sin6_port : reinterpret_cast<sockaddr_in *>(&s)->sin_port);
    return fd;
}

QJsonArray trustedJson(const QList<TrustedPeer> &l) {
    QJsonArray a;
    for (const TrustedPeer &t : l) a.append(QJsonObject{{"key", t.key}, {"name", t.name}});
    return a;
}
}  // namespace

bool Peer::nearby() const {
    return now() - seen < SEEN_MS;
}

/** One start of the local network: sockets, threads, keys. stop() ends it; the threads that still hold it see stopped. */
struct Lan::Run {
    std::atomic<bool> stopped{false};
    /** wake: read end of a pipe that becomes readable when the run stops; every wait polls it too. */
    int tcp = -1, udp = -1, wake = -1, wakeWrite = -1;
    int tcpPort = 0, udpPort = 0;
    Envelope::Keys keys;
    /** 100.64/10 counts as local in this run (the setting when it started: a change restarts it). */
    bool carrierGrade = false;
    /** Connections being served, per address (under Lan::guard). */
    QHash<QString, int> perAddress;
    std::atomic<int> jobs{0};
    std::mutex sleeping;
    std::condition_variable alarm;
    std::thread tcpThread, udpThread, timerThread;

    ~Run() {
        for (int fd : {tcp, udp, wake, wakeWrite})
            if (fd >= 0) ::close(fd);
    }
};

/** A connection, read a line at a time within its deadline; closed with it. */
struct Lan::Conn {
    int fd = -1;
    int wake = -1;
    QByteArray buffer;
    Clock::time_point deadline = Clock::time_point::max();

    ~Conn() {
        if (fd >= 0) ::close(fd);
    }

    /** One line, waiting at most `waitMs` for it (and never past the deadline); nothing if the other side closed. */
    std::optional<QByteArray> line(int waitMs) {
        auto end = std::min(Clock::now() + std::chrono::milliseconds(waitMs), deadline);
        for (;;) {
            qsizetype nl = buffer.indexOf('\n');
            if (nl >= 0) {
                QByteArray l = buffer.left(nl);
                buffer.remove(0, nl + 1);
                if (l.size() > MAX_LINE) throw Fail{"message too long"};
                return l;
            }
            if (buffer.size() > MAX_LINE) throw Fail{"message too long"};
            auto left = std::chrono::duration_cast<std::chrono::milliseconds>(end - Clock::now()).count();
            if (left <= 0) throw Fail{"Read timed out"};
            pollfd p[2] = {{fd, POLLIN, 0}, {wake, POLLIN, 0}};
            int n = ::poll(p, wake >= 0 ? 2 : 1, int(std::min<qint64>(left, 1000)));
            if (n < 0 && errno != EINTR) throw Fail{errorText(errno)};
            if (n <= 0) continue;
            if (wake >= 0 && p[1].revents) throw Fail{"stopped"};
            char b[8192];
            ssize_t k = ::recv(fd, b, sizeof b, 0);
            if (k < 0) {
                if (errno == EINTR || errno == EAGAIN) continue;
                throw Fail{errorText(errno)};
            }
            if (k == 0) {
                if (buffer.isEmpty()) return std::nullopt;
                QByteArray l = buffer;
                buffer.clear();
                return l;
            }
            buffer.append(b, k);
        }
    }

    QJsonObject read(int waitMs) {
        auto l = line(waitMs);
        if (!l) throw Fail{I18n::t("lan.closed")};
        return parse(*l);
    }
};

namespace {
/** One request and its reply, on a connection of its own. Throws Fail. */
QJsonObject ask(const QString &host, int port, const QJsonObject &m, int waitMs) {
    int error = 0;
    int fd = Net::connect(host, port, 3000, &error);
    if (fd < 0) throw Fail{error == ETIMEDOUT ? QString("Connect timed out") : errorText(error)};
    QByteArray buffer;
    struct Closer {
        int fd;
        ~Closer() { ::close(fd); }
    } closer{fd};
    write(fd, m);
    auto end = Clock::now() + std::chrono::milliseconds(waitMs);
    for (;;) {
        qsizetype nl = buffer.indexOf('\n');
        if (nl >= 0) return parse(buffer.left(nl));
        if (buffer.size() > MAX_LINE) throw Fail{"message too long"};
        auto left = std::chrono::duration_cast<std::chrono::milliseconds>(end - Clock::now()).count();
        if (left <= 0) throw Fail{"Read timed out"};
        pollfd p{fd, POLLIN, 0};
        int n = ::poll(&p, 1, int(left));
        if (n <= 0) continue;
        char b[8192];
        ssize_t k = ::recv(fd, b, sizeof b, 0);
        if (k < 0 && (errno == EINTR || errno == EAGAIN)) continue;
        if (k < 0) throw Fail{errorText(errno)};
        if (k == 0) {
            if (buffer.isEmpty()) throw Fail{"no answer"};
            return parse(buffer);
        }
        buffer.append(b, k);
    }
}
}  // namespace

// ── Startup ──

QString Lan::name(State s) {
    switch (s) {
    case State::Off: return "OFF";
    case State::VaultLocked: return "VAULT_LOCKED";
    case State::Active: return "ACTIVE";
    case State::Error: return "ERROR";
    }
    return "OFF";
}

Lan::Lan(Environment env)
    : env(std::move(env)), preferredPort(ports().first), announcePort(ports().second),
      testing(preferredPort != PORT || announcePort != ANNOUNCE_PORT) {}

Lan::~Lan() {
    stop();
    threads.join();
}

void Lan::listen(Listener l) {
    listener = std::move(l);
}

bool Lan::isOn() const {
    // Off until the user turns it on: announcing yourself on the network is a choice, not a default.
    return testing ? onInTest.load() : Prefs("lemuri/sharing").getBool("lanOn", false);
}

void Lan::note(const QString &about, const QString &message) {
    {
        std::lock_guard<std::mutex> g(notedGuard);
        if (noted.value(about) == message) return;
        // Bounded: addresses come from the network.
        if (noted.size() > 256) noted.clear();
        noted.insert(about, message);
    }
    std::fprintf(stderr, "Local network: %s\n", qPrintable(message));
}

void Lan::waitForLink(bool yes) {
    waitingUntil = yes ? now() + WAIT_FOR_LINK_MS : 0;
}

bool Lan::waitingForLink() const {
    return now() < waitingUntil.load();
}

bool Lan::carrierGrade() const {
    // Off unless asked: on a phone's hotspot or a carrier's network, 100.64/10 holds strangers, not your other PCs.
    return testing ? carrierGradeInTest.load() : Prefs("lemuri/sharing").getBool("lanCarrierGrade", false);
}

void Lan::allowCarrierGrade(bool yes) {
    if (testing) carrierGradeInTest = yes;
    else Prefs("lemuri/sharing").put("lanCarrierGrade", yes ? "true" : "false");
    restart();
}

void Lan::turnOn(bool yes) {
    if (testing) onInTest = yes;
    else Prefs("lemuri/sharing").put("lanOn", yes ? "true" : "false");
    restart();
}

int Lan::tcpPort() const {
    auto r = active();
    return r ? r->tcpPort : 0;
}

int Lan::udpPort() const {
    auto r = active();
    return r ? r->udpPort : 0;
}

bool Lan::privateAddress(const QString &numeric, bool carrierGrade) {
    sockaddr_storage s{};
    QByteArray a = numeric.toLatin1();
    if (::inet_pton(AF_INET, a.constData(), &reinterpret_cast<sockaddr_in *>(&s)->sin_addr) == 1) s.ss_family = AF_INET;
    else if (::inet_pton(AF_INET6, a.constData(), &reinterpret_cast<sockaddr_in6 *>(&s)->sin6_addr) == 1) s.ss_family = AF_INET6;
    else return false;
    return isPrivate(reinterpret_cast<sockaddr *>(&s), carrierGrade);
}

std::shared_ptr<Lan::Run> Lan::active() const {
    std::lock_guard<std::mutex> l(guard);
    return run;
}

QString Lan::myName() {
    QString user = Files::localUser();
    if (user.isEmpty()) user = "user";
    QString host = QSysInfo::machineHostName();
    return user + "@" + (host.isEmpty() ? QString("pc") : host);
}

void Lan::restart() {
    std::lock_guard<std::recursive_mutex> running(life);
    stop();
    // The contacts are needed even with the local network off: hosts go to them as text too.
    loadTrusted();
    if (!isOn()) {
        set(State::Off, I18n::t("lan.msg.off"));
        return;
    }
    Envelope::Keys k;
    try {
        k = env.keys();
    } catch (const std::exception &e) {
        set(State::Error, I18n::t("lan.msg.keys", errorText(e)));
        return;
    }
    if (k.isNull()) {
        set(State::VaultLocked, I18n::t("lan.msg.vault"));
        return;
    }
    auto r = std::make_shared<Run>();
    r->keys = k;
    r->carrierGrade = carrierGrade();
    try {
        try {
            r->tcp = bound(SOCK_STREAM, preferredPort, true, false, &r->tcpPort);
        } catch (const Fail &) {
            // Another app (or another copy of Lemuri) holds the port: take a free one and announce it.
            r->tcp = bound(SOCK_STREAM, 0, true, false, &r->tcpPort);
        }
        r->udp = bound(SOCK_DGRAM, announcePort, true, true, &r->udpPort);
        int fds[2];
        if (Posix::pipe(fds, true) != 0) throw Fail{errorText(errno)};
        r->wake = fds[0];
        r->wakeWrite = fds[1];
    } catch (const Fail &f) {
        set(State::Error, I18n::t("lan.msg.ports", f.why));
        return;
    }
    QString firewall = activeFirewall();
    {
        std::lock_guard<std::mutex> l(guard);
        run = r;
        firewallHere = firewall;
    }
    // The listeners have their own threads; each incoming connection gets one too, up to MAX_JOBS.
    r->tcpThread = std::thread([this, r] { listenTcp(r); });
    r->udpThread = std::thread([this, r] { listenUdp(r); });
    r->timerThread = std::thread([this, r] { timers(r); });
    set(State::Active, I18n::t("lan.msg.on", r->tcpPort, r->udpPort, myName()));
}

void Lan::timers(std::shared_ptr<Run> r) {
    // The rounds the other Lemuri apps expect: announce every 5 s, call the typed addresses and try the PCs nearby every
    // 15 s, the lists for the interface every 2 s. Each round runs after the previous one has ended.
    qint64 start = now();
    qint64 announceAt = start, addressesAt = start + 1000, nearbyAt = start + 3000, publishAt = start + 2000;
    while (!r->stopped) {
        qint64 t = now();
        auto survive = [](const char *what, const std::function<void()> &f) {
            try {
                f();
            } catch (const std::exception &e) {
                std::fprintf(stderr, "Local network, %s: %s\n", what, qPrintable(errorText(e)));
            }
        };
        if (t >= announceAt) {
            survive("announce", [&] { announce(*r); });
            announceAt = now() + ANNOUNCE_MS;
        }
        if (!r->stopped && t >= addressesAt) {
            survive("call addresses", [&] { callAddresses(); });
            addressesAt = now() + 15'000;
        }
        if (!r->stopped && t >= nearbyAt) {
            survive("try nearby", [&] { tryNearby(); });
            nearbyAt = now() + 15'000;
        }
        if (!r->stopped && t >= publishAt) {
            survive("publish", [&] { publish(); });
            publishAt = now() + 2000;
        }
        qint64 next = std::min({announceAt, addressesAt, nearbyAt, publishAt});
        std::unique_lock<std::mutex> l(r->sleeping);
        r->alarm.wait_for(l, std::chrono::milliseconds(std::max<qint64>(10, next - now())), [&] { return r->stopped.load(); });
    }
}

void Lan::stop() {
    std::lock_guard<std::recursive_mutex> running(life);
    std::shared_ptr<Run> r;
    bool hadPeers;
    {
        std::lock_guard<std::mutex> l(guard);
        r.swap(run);
        seen.clear();
        unprovenAddresses.clear();
        hadPeers = !published.isEmpty();
        published.clear();
    }
    if (r) {
        r->stopped = true;
        char one = 1;
        if (::write(r->wakeWrite, &one, 1) < 0) { /* the threads also wake up within a second */ }
        {
            std::lock_guard<std::mutex> l(r->sleeping);
        }
        r->alarm.notify_all();
        // When stopped, the ports are free at once for the next start; the key goes with the last thread that uses it.
        for (std::thread *t : {&r->tcpThread, &r->udpThread, &r->timerThread}) {
            if (!t->joinable()) continue;
            // Stopped from one of its own threads: that one ends right after, and is joined with the others.
            if (t->get_id() == std::this_thread::get_id()) threads.adopt(std::move(*t));
            else t->join();
        }
        ::close(r->tcp);
        ::close(r->udp);
        r->tcp = r->udp = -1;
    }
    if (hadPeers && listener.peers) listener.peers({});
}

void Lan::refresh() {
    auto r = active();
    if (!r) {
        restart();
        return;
    }
    threads.start([this, r] {
        try {
            QString f = activeFirewall();
            {
                std::lock_guard<std::mutex> l(guard);
                firewallHere = f;
            }
            announce(*r);
            callAddresses();
            tryNearby();
        } catch (const std::exception &e) {
            // the next rounds try again
            note("round", QString("a round of announcements failed: ") + e.what());
        }
    });
}

void Lan::set(State s, const QString &message) {
    if (s == State::Error || s != current) std::fprintf(stderr, "Local network sharing: %s\n", qPrintable(message));
    current = s;
    if (listener.state) listener.state(s, message);
}

/** Every 2 seconds the lists for the interface, dropping whoever has not been seen for long. */
void Lan::publish() {
    std::lock_guard<std::mutex> one(publishing);
    qint64 t = now();
    QList<Peer> l;
    bool peersChanged = false, firewallChanged = false;
    QString warning;
    {
        std::lock_guard<std::mutex> g(guard);
        requests.forget(t);
        for (auto it = alreadyArrived.begin(); it != alreadyArrived.end();) it = t - it.value() > 20 * 60'000 ? alreadyArrived.erase(it) : ++it;
        warning = firewallHere.isEmpty() || t - lastEntry < 10 * 60'000 ? QString() : howToOpen(firewallHere);
        if (!firewallSent || warning != firewallWarning) {
            firewallSent = true;
            firewallWarning = warning;
            firewallChanged = true;
        }
        l = peers(t);
        if (l != published) {
            published = l;
            peersChanged = true;
        }
    }
    if (firewallChanged && listener.firewall) listener.firewall(warning);
    if (peersChanged && listener.peers) listener.peers(l);
}

// ── Trust ──

bool Lan::trusted(const QString &key) const {
    std::lock_guard<std::mutex> l(guard);
    return !key.isEmpty() && trustedKeys.contains(key);
}

Peer Lan::reachable(const QString &key) const {
    std::lock_guard<std::mutex> l(guard);
    auto it = seen.constFind(key);
    return it != seen.constEnd() && it->nearby() && trustedKeys.contains(key) ? *it : Peer();
}

QString Lan::myKey() const {
    auto r = active();
    return r ? r->keys.publicKey : QString();
}

QList<TrustedPeer> Lan::contacts() const {
    std::lock_guard<std::mutex> l(guard);
    return trustedList;
}

QString Lan::trustedName(const QString &key) const {
    std::lock_guard<std::mutex> l(guard);
    for (const TrustedPeer &t : trustedList)
        if (t.key == key) return t.name;
    return QString();
}

void Lan::loadTrusted() {
    QList<TrustedPeer> l;
    QString j = env.read(TRUSTED);
    if (!j.isNull()) {
        // Unreadable: start again without pairings. The first versions wrote "chiave" and "nome".
        for (const QJsonValue &v : QJsonDocument::fromJson(j.toUtf8()).array()) {
            QJsonObject o = v.toObject();
            QString key = Legacy::field(o, "key", "chiave").toString();
            QString name = Legacy::field(o, "name", "nome").toString();
            if (!key.isEmpty()) l.append({key, name});
        }
    }
    std::lock_guard<std::mutex> g(guard);
    changeTrusted(l);
}

/** With m held. */
void Lan::changeTrusted(const QList<TrustedPeer> &l) {
    trustedList = l;
    trustedKeys.clear();
    for (const TrustedPeer &t : l) trustedKeys.insert(t.key);
    if (listener.trusted) listener.trusted(trustedList);
}

void Lan::trust(const QString &key, const QString &name) {
    QString n = ContactNames::clean(name);
    QString problem = ContactNames::problem(n);
    if (!problem.isNull()) throw Fail{I18n::format(problem, {})};
    std::lock_guard<std::mutex> g(guard);
    QString other = ContactNames::takenBy(trustedList, n, key);
    if (!other.isNull()) throw Fail{I18n::t("contact.nameTaken", other)};
    QList<TrustedPeer> l = trustedList;
    l.removeIf([&](const TrustedPeer &t) { return t.key == key; });
    l.append({key, n});
    env.write(TRUSTED, QString::fromUtf8(QJsonDocument(trustedJson(l)).toJson(QJsonDocument::Compact)));
    changeTrusted(l);
}

void Lan::forget(const QString &key) {
    std::lock_guard<std::mutex> g(guard);
    QList<TrustedPeer> l = trustedList;
    l.removeIf([&](const TrustedPeer &t) { return t.key == key; });
    env.write(TRUSTED, QString::fromUtf8(QJsonDocument(trustedJson(l)).toJson(QJsonDocument::Compact)));
    changeTrusted(l);
}

QJsonObject Lan::identity(const Run &r, const QString &type) const {
    // No email: anyone on the network would read who you are. Contacts are recognised by the key.
    return {{"app", "lemuri"}, {"v", 2}, {"type", type}, {"name", myName()}, {"key", r.keys.publicKey}, {"port", r.tcpPort}};
}

QJsonObject Lan::reply(bool ok, const QString &reason) const {
    QJsonObject o{{"ok", ok}};
    if (!reason.isNull()) o["reason"] = reason;
    QString k = myKey();
    if (!k.isEmpty()) o["key"] = k;
    return o;
}

// ── Addresses typed by hand ──

QStringList Lan::addresses() const {
    if (testing) return {};
    QStringList l;
    for (const QString &x : Prefs("lemuri/sharing").get("lanAddresses").split(','))
        if (!x.trimmed().isEmpty()) l << x.trimmed();
    return l;
}

void Lan::saveAddresses(const QStringList &l) {
    if (!testing) Prefs("lemuri/sharing").put("lanAddresses", l.join(','));
}

Peer Lan::addAddress(const QString &address) {
    try {
        Peer p = hello(address);
        QStringList l = addresses();
        if (!l.contains(address)) l << address;
        saveAddresses(l);
        publish();
        return p;
    } catch (const Fail &f) {
        throw Fail{I18n::t("lan.noAnswer", address, f.why)};
    }
}

void Lan::removeAddress(const QString &address) {
    QStringList l = addresses();
    l.removeAll(address);
    saveAddresses(l);
}

void Lan::outgoing(std::function<void()> f) {
    if (!active()) throw Fail{I18n::t("lan.notActive")};
    threads.start(std::move(f));
}

// ── Discovery ──

void Lan::announce(const Run &r) {
    if (r.stopped || r.keys.isNull()) return;
    QByteArray data = QJsonDocument(identity(r, "announce")).toJson(QJsonDocument::Compact);
    // Only on private networks: never on an interface with a public address.
    QList<in_addr> where;
    ifaddrs *all = nullptr;
    if (::getifaddrs(&all) == 0) {
        for (ifaddrs *i = all; i; i = i->ifa_next) {
            if (!i->ifa_addr || i->ifa_addr->sa_family != AF_INET || !(i->ifa_flags & IFF_UP) || (i->ifa_flags & IFF_LOOPBACK)) continue;
            if (!(i->ifa_flags & IFF_BROADCAST) || !i->ifa_broadaddr || !isPrivate(i->ifa_addr, r.carrierGrade)) continue;
            in_addr b = reinterpret_cast<sockaddr_in *>(i->ifa_broadaddr)->sin_addr;
            bool known = false;
            for (const in_addr &x : where) known = known || x.s_addr == b.s_addr;
            if (!known) where << b;
        }
        ::freeifaddrs(all);
    }
    if (announcePort == 0) return;  // no announcements in tests
    for (const in_addr &b : where) {
        sockaddr_storage s{};
        socklen_t size;
        sockaddr_storage mine{};
        socklen_t mineSize = sizeof mine;
        ::getsockname(r.udp, reinterpret_cast<sockaddr *>(&mine), &mineSize);
        if (mine.ss_family == AF_INET6) {
            auto *a = reinterpret_cast<sockaddr_in6 *>(&s);
            a->sin6_family = AF_INET6;
            a->sin6_port = htons(quint16(announcePort));
            a->sin6_addr.s6_addr[10] = 0xff;
            a->sin6_addr.s6_addr[11] = 0xff;
            std::memcpy(a->sin6_addr.s6_addr + 12, &b, 4);
            size = sizeof(sockaddr_in6);
        } else {
            auto *a = reinterpret_cast<sockaddr_in *>(&s);
            a->sin_family = AF_INET;
            a->sin_port = htons(quint16(announcePort));
            a->sin_addr = b;
            size = sizeof(sockaddr_in);
        }
        // network down or interface without broadcast: retry at the next round
        ::sendto(r.udp, data.constData(), size_t(data.size()), MSG_NOSIGNAL, reinterpret_cast<sockaddr *>(&s), size);
    }
}

void Lan::listenUdp(std::shared_ptr<Run> r) {
    char buffer[8192];
    while (!r->stopped) {
        pollfd p[2] = {{r->udp, POLLIN, 0}, {r->wake, POLLIN, 0}};
        if (::poll(p, 2, 1000) <= 0 || p[1].revents) continue;
        sockaddr_storage from{};
        socklen_t size = sizeof from;
        ssize_t n = ::recvfrom(r->udp, buffer, sizeof buffer, 0, reinterpret_cast<sockaddr *>(&from), &size);
        if (n <= 0 || !isPrivate(reinterpret_cast<sockaddr *>(&from), r->carrierGrade)) continue;
        // Anyone on the network can send any bytes here.
        QJsonDocument d = QJsonDocument::fromJson(QByteArray(buffer, n));
        if (d.isObject()) notice(d.object(), hostAddress(reinterpret_cast<sockaddr *>(&from)), false);
    }
}

/**
 * Records an app that was seen (an announcement, a request, a reply to "hello"); null if it is not one. `proven`
 * when the message was sealed with the key it claims: anyone can announce a trusted key with their own address, so for
 * a trusted PC already seen an unproven address does not replace the known one, it is kept aside.
 */
Peer Lan::notice(const QJsonObject &o, const QString &where, bool manual, bool proven) {
    if (text(o, "app") != "lemuri") return {};
    QString key = text(o, "key");
    QString mine = myKey();
    if (key.isEmpty() || mine.isEmpty() || key == mine || !Envelope::isKey(key)) return {};
    int port = number(o, "port", PORT);
    QString address = where;
    std::lock_guard<std::mutex> g(guard);
    auto it = seen.constFind(key);
    bool known = it != seen.constEnd();
    Peer before = known ? *it : Peer();
    bool isTrusted = trustedKeys.contains(key);
    // A new, untrusted PC is remembered only if there is room.
    if (!known && !isTrusted && seen.size() >= MAX_UNKNOWN) return {};
    bool elsewhere = known && (before.address != address || before.port != port);
    QString name = shorten(text(o, "name"));
    if (elsewhere && !proven && isTrusted) {
        name = before.name;
        QStringList &aside = unprovenAddresses[key];
        QString a = address + " " + QString::number(port);
        if (aside.size() < MAX_UNPROVEN && !aside.contains(a)) aside << a;
        address = before.address;
        port = before.port;
    }
    if (proven) unprovenAddresses.remove(key);
    Peer p{key, name, QString(), address, port, now(), manual || (known && before.manual),
           known && before.address == address && before.port == port ? before.warning : QString()};
    seen.insert(key, p);
    return p;
}

QList<QPair<QString, int>> Lan::unproven(const QString &key) const {
    std::lock_guard<std::mutex> g(guard);
    QList<QPair<QString, int>> l;
    for (const QString &a : unprovenAddresses.value(key)) {
        qsizetype space = a.lastIndexOf(' ');
        l.append({a.left(space), a.mid(space + 1).toInt()});
    }
    return l;
}

void Lan::proven(const QString &key, const QString &address, int port) {
    std::lock_guard<std::mutex> g(guard);
    unprovenAddresses.remove(key);
    auto it = seen.find(key);
    if (it == seen.end() || (it->address == address && it->port == port)) return;
    *it = Peer{it->key, it->name, it->email, address, port, now(), it->manual, QString()};
}

/** With m held: drops whoever has not been seen for long (trusted PCs stay), the others sorted by name. */
QList<Peer> Lan::peers(qint64 t) {
    for (auto it = seen.begin(); it != seen.end();)
        it = t - it->seen > 10 * Peer::SEEN_MS && !trustedKeys.contains(it->key) ? seen.erase(it) : ++it;
    QList<Peer> l = seen.values();
    std::sort(l.begin(), l.end(), [](const Peer &x, const Peer &y) { return x.who().compare(y.who(), Qt::CaseInsensitive) < 0; });
    return l;
}

/**
 * Tries to open the TCP port of every PC seen recently: if announcements arrive but the port does not, there is a
 * firewall in between, and the kind of error tells which.
 */
void Lan::tryNearby() {
    QList<Peer> l;
    {
        std::lock_guard<std::mutex> g(guard);
        l = seen.values();
    }
    // Each on its own thread: a PC that does not answer makes us wait 3 seconds.
    for (const Peer &p : l)
        if (p.nearby()) threads.start([this, p] { tryPeer(p); });
}

void Lan::tryPeer(const Peer &p) {
    int error = 0;
    int fd = Net::connect(p.address, p.port, 3000, &error);
    QString a;
    if (fd >= 0) ::close(fd);
    else if (error == EHOSTUNREACH) a = I18n::t("lan.probe.rejected", p.port);
    else if (error == ETIMEDOUT) a = I18n::t("lan.probe.timeout", p.port);
    else if (error == ECONNREFUSED) a = I18n::t("lan.probe.refused", p.port);
    else a = I18n::t("lan.probe.other", errorText(error));
    {
        std::lock_guard<std::mutex> g(guard);
        auto it = seen.find(p.key);
        if (it != seen.end()) it->warning = a;
    }
    publish();
}

void Lan::callAddresses() {
    for (const QString &a : addresses()) {
        try {
            hello(a);
        } catch (const std::exception &e) {
            // off, unreachable, not a Lemuri, a mistyped port: retry soon, and the other addresses are still called
            note("address " + a, a + ": " + e.what());
        }
    }
}

Peer Lan::hello(const QString &address) {
    auto r = active();
    if (!r) throw Fail{I18n::t("lan.notActive")};
    QString host = address;
    int port = PORT;
    qsizetype i = address.lastIndexOf(':');
    if (i > 0 && address.indexOf(':') == i) {
        host = address.left(i);
        bool ok = false;
        port = address.mid(i + 1).toInt(&ok);
        if (!ok) throw Fail{"For input string: \"" + address.mid(i + 1) + "\""};
    }
    QJsonObject reply = ask(host, port, identity(*r, "hello"), 5000);
    Peer p = notice(reply, resolve(host), true);
    if (p.isNull()) throw Fail{I18n::t("lan.notLemuri", address)};
    return p;
}

// ── Incoming connections ──

void Lan::listenTcp(std::shared_ptr<Run> r) {
    while (!r->stopped) {
        pollfd p[2] = {{r->tcp, POLLIN, 0}, {r->wake, POLLIN, 0}};
        if (::poll(p, 2, 1000) <= 0 || p[1].revents) continue;
        sockaddr_storage from{};
        socklen_t size = sizeof from;
        int c = Posix::accept(r->tcp, reinterpret_cast<sockaddr *>(&from), &size);
        if (c < 0) continue;
        auto *a = reinterpret_cast<sockaddr *>(&from);
        // Local network only: the port is open on all interfaces, but nobody outside gets an answer.
        if (!isPrivate(a, r->carrierGrade)) {
            ::close(c);
            continue;
        }
        if (!isLoopback(a)) {
            std::lock_guard<std::mutex> g(guard);
            lastEntry = now();
        }
        if (r->jobs >= MAX_JOBS) {
            ::close(c);
            continue;
        }
        QString address = hostAddress(a);
        {
            std::lock_guard<std::mutex> g(guard);
            if (r->perAddress.value(address) >= MAX_JOBS_PER_ADDRESS) {
                ::close(c);
                continue;
            }
            r->perAddress[address]++;
        }
        r->jobs++;
        threads.start([this, r, c, address] {
            serve(r, c, address);
            r->jobs--;
            std::lock_guard<std::mutex> g(guard);
            if (--r->perAddress[address] <= 0) r->perAddress.remove(address);
        });
    }
}

void Lan::serve(std::shared_ptr<Run> r, int fd, const QString &from) {
    Conn c;
    c.fd = fd;
    c.wake = r->wake;
    // One deadline for the whole connection, not for every read: whoever sends a byte now and then does not hold a thread.
    c.deadline = Clock::now() + std::chrono::seconds(15);
    try {
        auto line = c.line(10'000);
        if (!line) return;
        QJsonObject m = parse(*line);
        // Pairing goes on over the same connection; everything else is one line and its answer.
        write(c.fd, text(m, "type") == "pair" ? answerPairing(*r, m, from, c) : answer(*r, m, from));
    } catch (const std::exception &e) {
        // connection dropped or expired, or a malformed request: anyone on the network can send anything
        note("from " + from, "request from " + from + " dropped: " + e.what());
    }
}

QJsonObject Lan::answer(const Run &r, const QJsonObject &m, const QString &from) {
    QString type = text(m, "type");
    if (type == "hello") {
        notice(m, from, false);
        return identity(r, "me");
    }
    if (type == "delivery") return arrival(r, m, from);
    return reply(false, "lan.wire.unknown");
}

QJsonObject Lan::answerLine(const QByteArray &line, const QString &from) {
    std::shared_ptr<Run> r = active();
    if (!r) throw Fail(I18n::t("lan.notActive"));
    QJsonObject m = parse(line);
    // Pairing needs the connection it came on: not here.
    if (text(m, "type") == "pair") return reply(false, "lan.wire.unknown");
    return answer(*r, m, from);
}

QJsonObject Lan::arrival(const Run &r, const QJsonObject &m, const QString &from) {
    QString sender = text(m, "key");
    if (!trusted(sender)) return reply(false, "lan.wire.notLinked");
    QString who = trustedName(sender);
    bool ok = false;
    QString opened = Envelope::openFrom({text(m, "ephemeral"), text(m, "iv"), text(m, "data")}, r.keys, sender, sender, r.keys.publicKey, &ok);
    QJsonDocument d = QJsonDocument::fromJson(opened.toUtf8());
    // Not sealed for us, or fields missing or of the wrong type.
    if (!ok || !d.isObject() || !d.object()["when"].isDouble()) return reply(false, "lan.wire.cannotOpen");
    QJsonObject inside = d.object();
    QString id = text(inside, "id");
    qint64 when = qint64(inside["when"].toDouble());
    // An old or already seen envelope is a replay: whoever listens to the network could send it again.
    if (std::abs(now() - when) > 10 * 60'000) return reply(false, "lan.wire.tooOld");
    bool again;
    {
        std::lock_guard<std::mutex> g(guard);
        again = id.isNull() || alreadyArrived.contains(id);
        if (!again) alreadyArrived.insert(id, now());
    }
    if (again) return reply(false, "lan.wire.already");
    bool parsed = false;
    Parcel p = Parcel::fromJson(text(inside, "parcel"), &parsed);
    if (!parsed || !p.admit().isNull()) return reply(false, "lan.wire.invalid");
    // The envelope opened with the sender's key: this is really where that PC is.
    notice(m, from, false, true);
    if (env.arrival) env.arrival({"lan:" + id, who.isNull() ? QString("?") : who, sender, when, p, p.name, QString()});
    // The receipt is sealed with our key: whoever only announces our public key cannot write it.
    QJsonObject answer = reply(true, QString());
    try {
        Envelope::Sealed q = Envelope::sealFrom(id, r.keys, sender, r.keys.publicKey, sender);
        answer["receipt"] = QJsonObject{{"ephemeral", q.ephemeral}, {"iv", q.iv}, {"data", q.data}};
    } catch (const std::exception &) {
        return reply(false, "lan.wire.cannotOpen");
    }
    return answer;
}

// ── Pairing ──

bool RequestLimit::allow(const QString &from, qint64 t) {
    forget(t);
    bool ok = !last.contains(from) && all.size() < PER_MINUTE;
    last.insert(from, t);
    // Only the answered ones count for everybody: one address insisting must not use up the others' requests.
    if (ok) all.append(t);
    return ok;
}

void RequestLimit::forget(qint64 t) {
    for (auto it = last.begin(); it != last.end();) it = t - it.value() >= PAUSE_MS ? last.erase(it) : ++it;
    while (!all.isEmpty() && t - all.first() >= PAUSE_MS) all.removeFirst();
}

/**
 * Answers a pairing request: commitment to our random number, the other's number, reveal of ours; then the code, the
 * same as the one shown over there, and the user's choice.
 */
QJsonObject Lan::answerPairing(const Run &r, const QJsonObject &msg, const QString &from, Conn &c) {
    Peer p = notice(msg, from, false);
    if (p.isNull()) return reply(false, "lan.wire.invalid");
    if (number(msg, "v", 0) < PAIRING_VERSION) return reply(false, "lan.wire.older");
    // Nothing shows up on this screen unless its user is waiting for a link: a name and an email chosen by anyone on
    // the network are not put in front of someone who did not ask.
    if (!waitingForLink()) return reply(false, "lan.wire.notWaiting");
    bool tooSoon;
    {
        std::lock_guard<std::mutex> g(guard);
        tooSoon = !requests.allow(from, now());
    }
    // reply() takes the lock itself.
    if (tooSoon) return reply(false, "lan.wire.tooMany");
    bool expected = false;
    if (!pairingBusy.compare_exchange_strong(expected, true)) return reply(false, "lan.wire.busy");
    struct Free {
        std::atomic<bool> &busy;
        ~Free() { busy = false; }
    } free{pairingBusy};
    QString mine = Envelope::nonce();
    QJsonObject commitment = identity(r, "commitment");
    commitment["commitment"] = Envelope::commitment(r.keys.publicKey, p.key, mine);
    write(c.fd, commitment);
    QString theirs = text(c.read(10'000), "nonce");
    if (theirs.isNull()) return reply(false, "lan.wire.invalid");
    write(c.fd, QJsonObject{{"nonce", mine}});
    // Pairing waits for the user: more time, but only now that the random numbers are exchanged.
    c.deadline = Clock::now() + std::chrono::seconds(160);
    QString code = Envelope::code(p.key, r.keys.publicKey, theirs, mine);
    // Asked away from this thread, so that an answer that never comes ends after two minutes.
    auto box = std::make_shared<std::promise<QString>>();
    std::future<QString> answer = box->get_future();
    auto ask = listener.request;
    threads.start([ask, p, code, box] {
        QString name;
        try {
            if (ask) name = ask(p, code);
        } catch (const std::exception &e) {
            std::fprintf(stderr, "Pairing question: %s\n", e.what());
        }
        box->set_value(name);
    });
    bool accepted = false;
    if (answer.wait_for(std::chrono::seconds(120)) == std::future_status::ready) {
        QString name = answer.get();
        if (!name.isNull()) {
            try {
                trust(p.key, name);
                accepted = true;
            } catch (const std::exception &e) {
                // the contact could not be saved (name taken, vault locked)
                note("trust", QString("the linked PC could not be saved as a contact: ") + e.what());
            }
        }
    }
    return reply(accepted, accepted ? QString() : QString("lan.wire.rejected"));
}

/**
 * Asks `p` to pair, on the same connection: it commits to its random number, we send ours, it reveals its own and
 * we check the commitment. Only then is the code computed and given to `onCode`; then we wait for the user over
 * there. On this side the PC is remembered only after the user here has confirmed the same code too.
 */
bool Lan::pair(const Peer &p, const std::function<void(const QString &code)> &onCode) {
    auto r = active();
    if (!r) throw Fail{I18n::t("lan.notActive")};
    int error = 0;
    Conn c;
    c.fd = Net::connect(p.address, p.port, 3000, &error);
    if (c.fd < 0) throw Fail{error == ETIMEDOUT ? QString("Connect timed out") : errorText(error)};
    QJsonObject request = identity(*r, "pair");
    request["v"] = PAIRING_VERSION;
    write(c.fd, request);
    QJsonObject a = c.read(15'000);
    if (text(a, "type") != "commitment") {
        QString reason = text(a, "reason");
        throw Fail{!reason.isNull() ? I18n::ifKey(reason) : I18n::t("lan.wire.older")};
    }
    if (p.key != text(a, "key")) throw Fail{I18n::t("lan.differentKey")};
    QString commitment = text(a, "commitment");
    QString mine = Envelope::nonce();
    write(c.fd, QJsonObject{{"nonce", mine}});
    QString theirs = text(c.read(15'000), "nonce");
    if (theirs.isNull() || commitment.isNull() || Envelope::commitment(p.key, r->keys.publicKey, theirs) != commitment)
        throw Fail{I18n::t("lan.brokenProtocol")};
    onCode(Envelope::code(r->keys.publicKey, p.key, mine, theirs));
    // Now wait for the user over there to compare the code.
    QJsonObject end = c.read(130'000);
    if (!yes(end, "ok")) {
        QString reason = text(end, "reason");
        if (!reason.isNull() && reason != "lan.wire.rejected") throw Fail{I18n::ifKey(reason)};
        return false;
    }
    return true;
}

// ── Deliveries ──

void Lan::send(const Peer &p, const QString &parcel) {
    auto r = active();
    if (!r) throw Fail{I18n::t("lan.notActive")};
    if (!trusted(p.key)) throw Fail{I18n::t("lan.notLinkedTo", p.who())};
    QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QJsonObject inside{{"id", id}, {"when", now()}, {"parcel", parcel}};
    Envelope::Sealed b = Envelope::sealFrom(QString::fromUtf8(QJsonDocument(inside).toJson(QJsonDocument::Compact)), r->keys, p.key,
                                            r->keys.publicKey, p.key);
    QJsonObject msg = identity(*r, "delivery");
    msg["ephemeral"] = b.ephemeral;
    msg["iv"] = b.iv;
    msg["data"] = b.data;
    QList<QPair<QString, int>> where{{p.address, p.port}};
    where += unproven(p.key);
    std::optional<Fail> first;
    for (const auto &[address, port] : where) {
        try {
            QJsonObject a = ask(address, port, msg, 15'000);
            if (!yes(a, "ok")) {
                QString reason = text(a, "reason");
                throw Fail{I18n::t("lan.refused", p.who(), !reason.isNull() ? I18n::ifKey(reason) : I18n::t("lan.unknownReason"))};
            }
            if (receipt(*r, a, p.key) != id) throw Fail{I18n::t("lan.noReceipt", p.who())};
            proven(p.key, address, port);
            return;
        } catch (const Fail &f) {
            if (!first) first = f;
        }
    }
    throw *first;
}

/** What the receipt in `reply` says, if it was sealed by `sender` for us; null otherwise. */
QString Lan::receipt(const Run &r, const QJsonObject &reply, const QString &sender) const {
    if (!reply["receipt"].isObject()) return QString();
    QJsonObject q = reply["receipt"].toObject();
    bool ok = false;
    QString s = Envelope::openFrom({text(q, "ephemeral"), text(q, "iv"), text(q, "data")}, r.keys, sender, sender, r.keys.publicKey, &ok);
    return ok ? s : QString();
}
