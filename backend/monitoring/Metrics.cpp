#include "monitoring/Metrics.h"

#include "base/Error.h"
#include "base/I18n.h"
#include "monitoring/SecurityAudit.h"
#include "ssh/Ssh.h"
#include "vault/Vault.h"

#include <QDateTime>
#include <QRegularExpression>
#include <array>
#include <cstdio>
#include <thread>

namespace {
/** After an error, no retry before this, unless by hand: many failed logins trigger fail2ban. */
constexpr qint64 PAUSE_AFTER_ERROR_MS = 60'000;
/**
 * A remote command is closed after this: a df on an unreachable NFS mount never ends, and its thread would hold
 * one of the slots forever.
 */
constexpr int TIMEOUT_S = 30;
/** The longest output accepted: a large configuration fits in a few hundred KB. */
constexpr int MAX_OUTPUT = 4 * 1024 * 1024;
/** Updates and security are read again after this: the package lists change a few times a day at most. */
constexpr qint64 KEEP_MS = 10 * 60'000;
/** A failed connection is not tried again before this: the same error is given. */
constexpr qint64 FAILED_FOR_MS = 10'000;

qint64 now() {
    return QDateTime::currentMSecsSinceEpoch();
}

/** One of the slots, for as long as it lives. */
struct Slot {
    std::counting_semaphore<8> &s;
    explicit Slot(std::counting_semaphore<8> &s) : s(s) { s.acquire(); }
    ~Slot() { s.release(); }
};
}  // namespace

const QString Metrics::COMMAND = QStringList{
    "echo \"N $(nproc 2>/dev/null || grep -c ^processor /proc/cpuinfo)\"",
    "echo \"C $(head -1 /proc/stat)\"",
    "sleep 1",
    "echo \"C $(head -1 /proc/stat)\"",
    "awk '/^MemTotal:|^MemAvailable:/{print \"M\",$1,$2}' /proc/meminfo",
    "echo \"U $(cut -d' ' -f1 /proc/uptime)\"",
    // On the server too df has a deadline, if the timeout command exists: a dead mount leaves no hanging processes.
    "T=$(command -v timeout >/dev/null 2>&1 && echo 'timeout 10')",
    "($T df -P -k -x tmpfs -x devtmpfs -x squashfs -x overlay -x efivarfs 2>/dev/null || $T df -P -k) | awk 'NR>1{print \"D\",$2,$3,$4,$6}'"}.join("; ");

const QString Metrics::UPDATES = QStringList{
    "T=$(command -v timeout >/dev/null 2>&1 && echo 'timeout 25')",
    "if [ -x /usr/lib/update-notifier/apt-check ]; then echo \"A $($T /usr/lib/update-notifier/apt-check 2>&1 | tail -1)\"",
    "elif command -v apt-get >/dev/null 2>&1; then I=$($T apt-get -s -o Debug::NoLocking=true upgrade 2>/dev/null | grep '^Inst');"
    " echo \"A $(printf '%s' \"$I\" | grep -c .);$(printf '%s' \"$I\" | grep -ci security)\"",
    "elif command -v dnf >/dev/null 2>&1; then echo \"A $($T dnf -q --cacheonly check-update 2>/dev/null | grep -c '^[[:alnum:]]');"
    // One line per advisory and version: the packages are counted, by name.
    "$($T dnf -q --cacheonly updateinfo list --security 2>/dev/null | awk '{n=$NF; sub(/-[^-]+-[^-]+$/, \"\", n); print n}' | sort -u | grep -c .)\"",
    "elif command -v yum >/dev/null 2>&1; then echo \"A $($T yum -q -C check-update 2>/dev/null | grep -c '^[[:alnum:]]');\"",
    // openSUSE: "v |" lines are updates, a "needed" security patch counts as one.
    "elif command -v zypper >/dev/null 2>&1; then echo \"A $($T zypper -q -n --no-refresh list-updates 2>/dev/null | grep -c '^v ');"
    "$($T zypper -q -n --no-refresh list-patches --category security 2>/dev/null | grep -ci '| needed')\"",
    "elif command -v apk >/dev/null 2>&1; then echo \"A $($T apk -u list 2>/dev/null | grep -c .);\"",
    "elif command -v pacman >/dev/null 2>&1; then echo \"A $($T pacman -Qu 2>/dev/null | grep -c .);\"",
    "fi",
    "if [ -f /var/run/reboot-required ]; then echo \"R yes $(tr '\\n' ' ' < /var/run/reboot-required.pkgs 2>/dev/null)\"",
    "elif [ -d /var/lib/dpkg ]; then echo 'R no'",
    "elif command -v needs-restarting >/dev/null 2>&1; then if $T needs-restarting -r >/dev/null 2>&1; then echo 'R no'; else [ $? -eq 1 ] && echo 'R yes'; fi",
    // zypper: exit code 102 when a reboot is needed, 0 when not.
    "elif command -v zypper >/dev/null 2>&1; then $T zypper -q needs-rebooting >/dev/null 2>&1; case $? in 0) echo 'R no' ;; 102) echo 'R yes' ;; esac",
    "fi"}.join('\n');

// ── Reading the answers ──

QJsonObject Metrics::parse(const QString &text) {
    int cpu = 0;
    QList<std::array<qint64, 8>> rounds;
    qint64 memTotal = 0, memFree = 0, on = 0;
    QJsonArray disks;
    QStringList dots;
    static const QRegularExpression spaces("\\s+");
    for (const QString &line : text.split('\n')) {
        QStringList p = line.trimmed().split(spaces);
        if (p.size() < 2) continue;
        // Odd lines (a shell with a welcome message, a different df) are skipped.
        bool ok = true;
        if (p[0] == "N") {
            int n = p[1].toInt(&ok);
            if (ok) cpu = n;
        } else if (p[0] == "C") {
            // "C cpu user nice system idle iowait irq softirq steal ..."
            std::array<qint64, 8> v{};
            for (int i = 0; i < 8 && i + 2 < p.size() && ok; i++) v[size_t(i)] = p[i + 2].toLongLong(&ok);
            if (ok) rounds.append(v);
        } else if (p[0] == "M" && p.size() >= 3) {
            qint64 n = p[2].toLongLong(&ok);
            if (ok && p[1] == "MemTotal:") memTotal = n;
            else if (ok && p[1] == "MemAvailable:") memFree = n;
        } else if (p[0] == "U") {
            double s = p[1].toDouble(&ok);
            if (ok) on = qint64(s);
        } else if (p[0] == "D" && p.size() >= 5 && !dots.contains(p[4])) {
            bool a, b, c;
            qint64 total = p[1].toLongLong(&a), used = p[2].toLongLong(&b), free = p[3].toLongLong(&c);
            if (a && b && c) {
                dots << p[4];
                disks.append(QJsonObject{{"dot", p[4]}, {"totalKb", total}, {"usedKb", used}, {"freeKb", free}});
            }
        }
    }
    QJsonValue usage;
    if (rounds.size() == 2) {
        qint64 total = 0;
        for (size_t i = 0; i < 8; i++) total += rounds[1][i] - rounds[0][i];
        qint64 idle = rounds[1][3] + rounds[1][4] - rounds[0][3] - rounds[0][4];
        if (total > 0) usage = std::clamp(1 - double(idle) / double(total), 0.0, 1.0);
    }
    if (memTotal == 0 && disks.isEmpty() && usage.isNull()) throw Fail{I18n::t("metrics.err.notLinux")};
    return {{"cpu", cpu}, {"cpuUsage", usage}, {"memTotalKb", memTotal}, {"memFreeKb", memFree}, {"disks", disks}, {"onForS", on}};
}

QJsonObject Metrics::parseUpdates(const QString &text, qint64 when) {
    QJsonValue packages, security, reboot, because;
    for (const QString &line : text.split('\n')) {
        QString l = line.trimmed();
        if (l.startsWith("A ")) {
            // apt-check may print a warning instead of the numbers: that line is skipped.
            QStringList p = l.mid(2).split(';');
            bool ok;
            int n = p[0].trimmed().toInt(&ok);
            if (!ok) continue;
            packages = n;
            if (p.size() > 1 && !p[1].trimmed().isEmpty()) {
                int s = p[1].trimmed().toInt(&ok);
                if (ok) security = s;
            }
        } else if (l == "R no") {
            reboot = false;
        } else if (l.startsWith("R yes")) {
            reboot = true;
            // The packages that asked for it, as the server wrote them: shown in a label, so without controls or
            // invisible characters, and cut (the file can be as long as the output).
            QString rest;
            rest.reserve(qMin(l.size(), qsizetype(4 * MAX_BECAUSE)));
            for (qsizetype i = 5; i < l.size() && rest.size() <= 2 * MAX_BECAUSE; i++) {
                QChar::Category c = l[i].category();
                if (c != QChar::Other_Control && c != QChar::Other_Format && c != QChar::Separator_Line && c != QChar::Separator_Paragraph)
                    rest += l[i];
            }
            rest = rest.trimmed();
            if (rest.size() > MAX_BECAUSE) rest = rest.left(MAX_BECAUSE).trimmed() + QString::fromUtf8("…");
            because = rest.isEmpty() ? QJsonValue() : QJsonValue(rest);
        }
    }
    return {{"packages", packages}, {"security", security}, {"reboot", reboot}, {"rebootBecause", because}, {"error", QJsonValue()}, {"when", when}};
}

QString Metrics::reason(const SshError &e) {
    QString d = e.detail.isEmpty() ? e.message : e.detail;
    if (d.contains("reject HostKey") || d.contains("HostKey has been changed")) return I18n::t("metrics.err.fingerprint");
    if (d.contains("Auth fail") || d.contains("Auth cancel")) return I18n::t("metrics.err.auth");
    return e.message;
}

// ── Readings ──

Metrics::Metrics(Vault *vault) : vault(vault) {}

Metrics::~Metrics() {
    stop();
}

void Metrics::start(std::function<void()> job) {
    if (stopped) return;
    jobs.start(std::move(job));
}

bool Metrics::join(QHash<QString, QList<Outcome>> &waiting, const QString &id, Outcome outcome) {
    std::lock_guard<std::mutex> l(m);
    bool first = !waiting.contains(id);
    waiting[id].append(std::move(outcome));
    return first;
}

void Metrics::deliver(QHash<QString, QList<Outcome>> &waiting, const QString &id, const QJsonObject &result) {
    QList<Outcome> l;
    {
        std::lock_guard<std::mutex> g(m);
        l = waiting.take(id);
    }
    for (const Outcome &o : l) o(result);
}

void Metrics::refresh(const Host &h, const Jumps::Route &route, bool force, Outcome outcome) {
    if (stopped || h.reach() == Host::Reach::Offline) return;
    QString id = h.id();
    {
        std::lock_guard<std::mutex> l(m);
        QJsonObject before = lastOnes.value(id);
        if (!force && before["error"].isString() && now() - qint64(before["when"].toDouble()) < PAUSE_AFTER_ERROR_MS) return;
        if (reading.contains(id)) return;
        reading.insert(id);
    }
    start([this, h, route, id, force, outcome] {
        QJsonObject e{{"resources", QJsonValue()}, {"error", QJsonValue()}, {"when", now()}};
        try {
            Slot s(room);
            e["resources"] = parse(QString::fromUtf8(run(h, route, COMMAND, force)));
        } catch (const SshError &x) {
            e["error"] = reason(x);
        } catch (const Fail &f) {
            // The output comes from the server: reading it must survive anything.
            e["error"] = I18n::t("metrics.err.other", f.why);
        }
        e["when"] = now();
        {
            std::lock_guard<std::mutex> l(m);
            QJsonValue usage = e["resources"].toObject()["cpuUsage"];
            if (usage.isDouble()) {
                QList<double> &d = history[id];
                d.append(usage.toDouble());
                while (d.size() > SAMPLES) d.removeFirst();
            }
            // An error does not erase the last good reading, it only marks it as old.
            QJsonObject old = lastOnes.value(id);
            if (e["resources"].isNull() && old["resources"].isObject()) e["resources"] = old["resources"];
            lastOnes.insert(id, e);
            reading.remove(id);
        }
        outcome(e);
    });
}

void Metrics::updates(const Host &h, const Jumps::Route &route, bool force, Outcome outcome) {
    QString id = h.id();
    QJsonObject known;
    {
        std::lock_guard<std::mutex> l(m);
        known = updatesKnown.value(id);
    }
    if (!force && !known.isEmpty() && now() - qint64(known["when"].toDouble()) < KEEP_MS) {
        outcome(known);
        return;
    }
    if (stopped || h.reach() == Host::Reach::Offline) {
        outcome({{"packages", QJsonValue()}, {"security", QJsonValue()}, {"reboot", QJsonValue()}, {"rebootBecause", QJsonValue()},
                 {"error", I18n::t("summary.unreachable")}, {"when", now()}});
        return;
    }
    if (!join(updating, id, std::move(outcome))) return;
    start([this, h, route, id, force] {
        QJsonObject u;
        QString error;
        try {
            Slot s(room);
            u = parseUpdates(QString::fromUtf8(run(h, route, UPDATES, force)), now());
        } catch (const SshError &x) {
            error = reason(x);
        } catch (const Fail &f) {
            error = I18n::t("metrics.err.other", f.why);
        }
        if (!error.isNull()) {
            u = {{"packages", QJsonValue()}, {"security", QJsonValue()}, {"reboot", QJsonValue()}, {"rebootBecause", QJsonValue()},
                 {"error", error}, {"when", now()}};
        } else {
            std::lock_guard<std::mutex> l(m);
            updatesKnown.insert(id, u);
        }
        deliver(updating, id, u);
    });
}

namespace {
/** The private key file of the host, if it has its own. None with ssh-agent or the keys in ~/.ssh. */
std::optional<SecurityAudit::ClientKey> clientKey(const Host &h) {
    if (h.auth() != Host::Auth::Key || h.key().trimmed().isEmpty()) return std::nullopt;
    Ssh::KeyInfo k;
    if (!Ssh::keyInfo(Ssh::keyFile(h.key()), &k)) return std::nullopt;
    return SecurityAudit::ClientKey{k.type, k.bits, k.encrypted};
}

/** The negotiated algorithms and the server's key of an open connection. */
SecurityAudit::Connection facts(const std::shared_ptr<SshConnection> &c) {
    SshConnection::Negotiated n = c->negotiated();
    SecurityAudit::Connection k{n.banner, n.kex, n.cipher, n.mac, false, n.hostKeyType, n.hostKeyBits};
    QString cipher = n.cipher.toLower(), mac = n.mac.toLower();
    // Strict kex matters only where Terrapin reaches: then the server's offer is read, libssh does not tell it.
    bool exposed = cipher.contains("chacha20-poly1305") || (cipher.contains("cbc") && mac.contains("-etm"));
    if (exposed && Ssh::strictKexOffered()) {
        QStringList kex, keys;
        c->serverOffer(5, &kex, &keys);
        k.strictKex = kex.contains("kex-strict-s-v00@openssh.com");
    }
    // With an RSA key libssh signs with SHA-2 (ssh-rsa, SHA-1, is off by default): the first it prefers.
    if (n.hostKeyType == "ssh-rsa") k.hostKeyAlgorithm = "rsa-sha2-512";
    return k;
}
}  // namespace

void Metrics::security(const Host &h, const Jumps::Route &route, bool force, Outcome outcome) {
    QString id = h.id();
    QJsonObject known;
    {
        std::lock_guard<std::mutex> l(m);
        known = securityKnown.value(id);
    }
    if (!force && !known.isEmpty() && now() - qint64(known["when"].toDouble()) < KEEP_MS) {
        outcome(known);
        return;
    }
    if (stopped || h.reach() == Host::Reach::Offline) {
        outcome(SecurityAudit::Report{{}, I18n::t("summary.unreachable"), now()}.json());
        return;
    }
    if (!join(auditing, id, std::move(outcome))) return;
    start([this, h, route, id, force] {
        std::optional<SecurityAudit::ClientKey> key = clientKey(h);
        std::optional<SecurityAudit::Server> server;
        QString error;
        std::shared_ptr<SshConnection> c;
        try {
            Slot s(room);
            server = SecurityAudit::parse(QString::fromUtf8(run(h, route, SecurityAudit::COMMAND, force, &c)));
        } catch (const SshError &x) {
            error = reason(x);
        } catch (const Fail &f) {
            error = I18n::t("metrics.err.other", f.why);
        }
        std::optional<SecurityAudit::Connection> conn;
        if (c && c->alive()) conn = facts(c);
        // Without even the connection there is nothing worth keeping: asked again next time.
        QJsonObject report = conn ? SecurityAudit::audit(h.auth(), key, conn, server, error, now()).json()
                                  : SecurityAudit::Report{{}, error, now()}.json();
        if (conn) {
            std::lock_guard<std::mutex> l(m);
            securityKnown.insert(id, report);
        }
        deliver(auditing, id, report);
    });
}

bool Metrics::running(const QString &hostId) {
    std::lock_guard<std::mutex> l(m);
    return reading.contains(hostId);
}

QJsonArray Metrics::cpuHistory(const QString &hostId) {
    std::lock_guard<std::mutex> l(m);
    QJsonArray a;
    for (double d : history.value(hostId)) a.append(d);
    return a;
}

bool Metrics::connected(const QString &hostId) {
    std::shared_ptr<SshConnection> c;
    {
        std::lock_guard<std::mutex> l(m);
        c = connections.value(hostId).connection;
    }
    return c && c->seemsAlive();
}

void Metrics::forget(const QString &hostId) {
    std::shared_ptr<SshConnection> c;
    {
        std::lock_guard<std::mutex> l(m);
        c = connections.take(hostId).connection;
        lastOnes.remove(hostId);
        history.remove(hostId);
        failed.remove(hostId);
        updatesKnown.remove(hostId);
        securityKnown.remove(hostId);
    }
    if (c) c->disconnect();
}

void Metrics::stop() {
    stopped = true;
    QList<Open> all;
    {
        std::lock_guard<std::mutex> l(m);
        all = connections.values();
        connections.clear();
    }
    for (const Open &o : all) o.connection->disconnect();
    // The readings under way end with their connections (or their time limits).
    jobs.join();
}

// ── The silent connection ──

QByteArray Metrics::run(const Host &h, const Jumps::Route &route, const QString &command, bool force, std::shared_ptr<SshConnection> *used) {
    QString id = h.id();
    QString where = QStringList{h.address(), QString::number(h.port()), h.user(), h.jump(), route.error}.join('\n');
    std::shared_ptr<std::mutex> hostLock;
    {
        std::lock_guard<std::mutex> l(m);
        auto &p = hostLocks[id];
        if (!p) p = std::make_shared<std::mutex>();
        hostLock = p;
    }
    std::shared_ptr<SshConnection> c;
    {
        // One host at a time: two readings at once must not open two connections.
        std::lock_guard<std::mutex> g(*hostLock);
        Open o;
        std::optional<Failure> f;
        {
            std::lock_guard<std::mutex> l(m);
            o = connections.value(id);
            if (failed.contains(id)) f = failed.value(id);
        }
        if (o.connection && o.route == where && o.connection->alive()) {
            c = o.connection;
        } else {
            if (o.connection) o.connection->disconnect();
            if (f && !force && now() - f->when < FAILED_FOR_MS) throw SshError{f->message, f->detail};
            // Answers without dialogs: new host keys refused, passwords only from the vault and only once.
            Vault *v = vault;
            auto prompts = [v](const Host &server) {
                SshPrompts p;
                p.unattended = true;
                auto tried = std::make_shared<bool>(false);
                QString sid = server.id();
                p.password = [v, tried, sid](const QString &) {
                    if (*tried || sid.isEmpty()) return QString();
                    *tried = true;
                    return v->password(sid);
                };
                return p;
            };
            try {
                c = SshConnection::open(h, route.hops, route.error, prompts, [](const QString &) {}, 10);
            } catch (const SshError &e) {
                std::lock_guard<std::mutex> l(m);
                failed.insert(id, {e.message, e.detail, now()});
                connections.remove(id);
                throw;
            }
            std::lock_guard<std::mutex> l(m);
            failed.remove(id);
            if (stopped) {
                c->disconnect();
                throw Fail{I18n::t("ssh.notReady")};
            }
            connections.insert(id, {c, where});
        }
    }
    if (used) *used = c;
    try {
        return c->run(command, MAX_OUTPUT, TIMEOUT_S);
    } catch (const SshError &e) {
        if (!c->alive()) {
            std::lock_guard<std::mutex> l(m);
            if (connections.value(id).connection == c) connections.remove(id);
        }
        // Closed at the deadline the output is cut in half: better an error than wrong data.
        if (e.detail.startsWith("Command timed out")) throw Fail{I18n::t("metrics.err.timeout", TIMEOUT_S)};
        throw;
    }
}
