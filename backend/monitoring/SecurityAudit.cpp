#include "monitoring/SecurityAudit.h"

#include <QJsonArray>
#include <QRegularExpression>
#include <algorithm>

namespace SecurityAudit {

const QString COMMAND = QStringList{
    "echo \"I $(id -u)\"",
    "T=$(command -v timeout >/dev/null 2>&1 && echo 'timeout 10')",
    "D=$(command -v sshd 2>/dev/null || echo /usr/sbin/sshd)",
    "if [ \"$(id -u)\" = 0 ] && [ -x \"$D\" ]; then $T \"$D\" -T 2>/dev/null | sed 's/^/S /'; fi",
    "for f in /etc/ssh/sshd_config /etc/ssh/sshd_config.d/*.conf; do [ -r \"$f\" ] && { echo \"F $f\"; sed 's/^/L /' \"$f\"; }; done",
    "(systemctl is-active --quiet fail2ban 2>/dev/null || pgrep -f '[f]ail2ban-server' >/dev/null 2>&1) && echo 'G fail2ban'",
    "(systemctl is-active --quiet crowdsec 2>/dev/null || pgrep -x '[c]rowdsec' >/dev/null 2>&1) && echo 'G crowdsec'",
    "(systemctl is-active --quiet sshguard 2>/dev/null || pgrep -x '[s]shguard' >/dev/null 2>&1) && echo 'G sshguard'",
    "true"}.join('\n');

// ── Report ──

std::optional<double> Report::score() const {
    double got = 0, all = 0;
    for (const Check &c : checks) {
        all += c.weight;
        got += c.weight * (c.level == Level::Ok ? 1 : c.level == Level::Warn ? 0.5 : 0);
    }
    if (all == 0) return std::nullopt;
    return got / all;
}

const Check *Report::find(const QString &id) const {
    for (const Check &c : checks)
        if (c.id == id) return &c;
    return nullptr;
}

QJsonObject Report::json() const {
    QJsonArray l;
    auto orNull = [](const QString &s) { return s.isNull() ? QJsonValue() : QJsonValue(s); };
    for (const Check &c : checks)
        l.append(QJsonObject{{"id", c.id},
                             {"level", c.level == Level::Ok ? "OK" : c.level == Level::Warn ? "WARN" : "BAD"},
                             {"value", orNull(c.value)},
                             {"fix", orNull(c.fix)},
                             {"weight", c.weight}});
    auto s = score();
    return {{"checks", l}, {"error", orNull(error)}, {"when", when}, {"score", s ? QJsonValue(*s) : QJsonValue()}};
}

// ── Server answer ──

Server parse(const QString &text) {
    Server out;
    QHash<QString, QString> effective;
    Files files;
    QStringList *current = nullptr;
    static const QRegularExpression spaces("\\s+");
    for (const QString &line : text.split('\n')) {
        if (line.startsWith("I ")) {
            bool ok;
            int uid = line.mid(2).trimmed().toInt(&ok);
            // A welcome message in the way: unknown.
            if (ok) out.uid = uid;
        } else if (line.startsWith("S ")) {
            QString rest = line.mid(2).trimmed();
            qsizetype space = rest.indexOf(spaces);
            // sshd -T writes every setting once, in lower case; some (port, listenaddress) more than once: the first.
            if (space > 0) {
                QString key = rest.left(space).toLower();
                if (!effective.contains(key)) effective.insert(key, rest.mid(space).trimmed());
            }
        } else if (line.startsWith("F ")) {
            files.append({line.mid(2).trimmed(), {}});
            current = &files.last().second;
        } else if (line.startsWith("L ") || line == "L") {
            if (current) current->append(line.size() > 2 ? line.mid(2) : QString());
        } else if (line.startsWith("G ")) {
            out.guards << line.mid(2).trimmed();
        }
    }
    if (!effective.isEmpty()) out.sshd = effective;
    else if (!files.isEmpty()) out.sshd = sshdConfig(files);
    return out;
}

namespace {
QString unquote(const QString &v) {
    return v.size() >= 2 && v.startsWith('"') && v.endsWith('"') ? v.mid(1, v.size() - 2) : v;
}

void read(const QString &file, const Files &files, QHash<QString, QString> &out, int depth) {
    if (depth > 8) return;
    static const QRegularExpression separator("[\\s=]+"), spaces("\\s+");
    bool inMatch = false;
    for (const auto &f : files) {
        if (f.first != file) continue;
        for (const QString &raw : f.second) {
            QString line = raw.trimmed();
            if (line.isEmpty() || line.startsWith('#')) continue;
            QRegularExpressionMatch m = separator.match(line);
            QString key = (m.hasMatch() ? line.left(m.capturedStart()) : line).toLower();
            QString value = m.hasMatch() ? unquote(line.mid(m.capturedEnd()).trimmed()) : QString("");
            if (key == "match") {
                inMatch = value.compare("all", Qt::CaseInsensitive) != 0;
                continue;
            }
            if (inMatch) continue;
            if (key == "include") {
                for (const QString &pattern : value.split(spaces, Qt::SkipEmptyParts)) {
                    QString full = pattern.startsWith('/') ? pattern : "/etc/ssh/" + pattern;
                    QRegularExpression glob(QRegularExpression::wildcardToRegularExpression(full));
                    QStringList matching;
                    for (const auto &g : files)
                        if (glob.match(g.first).hasMatch()) matching << g.first;
                    matching.sort();
                    for (const QString &g : matching) read(g, files, out, depth + 1);
                }
                continue;
            }
            // An old name, as sshd reads it.
            if (key == "challengeresponseauthentication") key = "kbdinteractiveauthentication";
            if (!out.contains(key)) out.insert(key, value);
        }
        return;
    }
}
}  // namespace

QHash<QString, QString> sshdConfig(const Files &files) {
    QHash<QString, QString> out;
    if (files.isEmpty()) return out;
    QString main = files.first().first;
    for (const auto &f : files)
        if (f.first == "/etc/ssh/sshd_config") main = f.first;
    read(main, files, out, 0);
    return out;
}

// ── Checks ──

namespace {
bool yes(const QString &v) {
    return v.trimmed().toLower().startsWith("yes");
}

void connection(const Connection &c, QList<Check> &l) {
    const QString &v = c.serverVersion;
    QString shown = v.startsWith("SSH-2.0-") ? v.mid(8) : v;
    static const QRegularExpression openssh("OpenSSH_(\\d+)\\.(\\d+)");
    QRegularExpressionMatch m = openssh.match(v);
    if (m.hasMatch()) {
        int major = m.captured(1).toInt(), minor = m.captured(2).toInt();
        // Before 7.4 (2016) it no longer gets fixes anywhere; newer ones are kept fixed by the distributions
        // without changing the number, so the number alone says nothing more.
        bool old = major < 7 || (major == 7 && minor < 4);
        l.append({"conn.version", old ? Level::Bad : Level::Ok, shown, {}, 2});
    } else if (!shown.isEmpty()) {
        l.append({"conn.version", Level::Ok, shown, {}, 2});
    }

    QString kex = c.kex.toLower();
    l.append({"conn.kex", kex.contains("sha1") || kex.contains("group1-") ? Level::Bad : Level::Ok, c.kex, {}, 2});

    QString cipher = c.cipher.toLower();
    bool weakCipher = cipher.contains("cbc") || cipher.contains("3des") || cipher.contains("arcfour") || cipher.contains("blowfish");
    l.append({"conn.cipher", weakCipher ? Level::Bad : Level::Ok, c.cipher, {}, 2});

    // With an AEAD cipher the MAC is part of the cipher: the separate one is not used.
    bool aead = cipher.contains("gcm") || cipher.contains("chacha20-poly1305");
    QString mac = c.mac.toLower();
    Level macLevel = aead ? Level::Ok : mac.contains("md5") || mac.endsWith("-96") ? Level::Bad : mac.contains("sha1") ? Level::Warn : Level::Ok;
    l.append({"conn.mac", macLevel, aead ? c.cipher : c.mac, {}, 1});

    // Terrapin (CVE-2023-48795) hits ChaCha20-Poly1305 and CBC with Encrypt-then-MAC, unless both sides use strict kex.
    bool exposed = cipher.contains("chacha20-poly1305") || (cipher.contains("cbc") && mac.contains("-etm"));
    l.append({"conn.terrapin", c.strictKex || !exposed ? Level::Ok : Level::Bad, c.strictKex ? QString("strict kex") : QString(), {}, 2});

    QString hk = c.hostKeyAlgorithm.toLower();
    Level hkLevel;
    if (hk == "ssh-rsa" || hk.contains("dss")) hkLevel = Level::Bad;
    else if (hk.contains("rsa")) hkLevel = c.hostKeyBits >= 3072 || c.hostKeyBits == 0 ? Level::Ok : c.hostKeyBits >= 2048 ? Level::Warn : Level::Bad;
    else hkLevel = Level::Ok;
    l.append({"conn.hostkey", hkLevel,
              c.hostKeyAlgorithm + (hk.contains("rsa") && c.hostKeyBits > 0 ? QString(" %1 bit").arg(c.hostKeyBits) : QString()), {}, 2});
}

void server(const Server &s, QList<Check> &l) {
    bool passwords = false;
    if (s.sshd) {
        const QHash<QString, QString> &d = *s.sshd;
        // Defaults of OpenSSH when a setting is not written.
        passwords = yes(d.value("passwordauthentication", "yes"));
        bool kbd = yes(d.value("kbdinteractiveauthentication", "yes")) && yes(d.value("usepam", "no"));
        l.append(passwords ? Check{"server.password", Level::Bad, "PasswordAuthentication yes", "PasswordAuthentication no", 3}
                 : kbd     ? Check{"server.password", Level::Warn, "KbdInteractiveAuthentication yes", "KbdInteractiveAuthentication no", 3}
                           : Check{"server.password", Level::Ok, "PasswordAuthentication no", {}, 3});

        QString root = d.value("permitrootlogin", "prohibit-password").toLower();
        Level rootLevel = root == "no" ? Level::Ok : root == "yes" ? Level::Bad : Level::Warn;
        l.append({"server.root", rootLevel, "PermitRootLogin " + root, rootLevel == Level::Ok ? QString() : QString("PermitRootLogin no"), 2});

        static const QRegularExpression spaces("\\s+");
        QString port = d.value("port", "22").split(spaces).first();
        l.append({"server.port", port == "22" ? Level::Warn : Level::Ok, "Port " + port, {}, 1});
    }
    QStringList on;
    for (const QString &g : s.guards)
        if ((g == "fail2ban" || g == "crowdsec" || g == "sshguard") && !on.contains(g)) on << g;
    // Without passwords a brute force attack cannot succeed: the protection only keeps the logs quieter.
    l.append(on.isEmpty() ? Check{"server.bruteforce", passwords ? Level::Bad : Level::Warn, {}, {}, 1}
                          : Check{"server.bruteforce", Level::Ok, on.join(", "), {}, 1});
    if (s.uid) l.append({"server.user", *s.uid == 0 ? Level::Warn : Level::Ok, *s.uid == 0 ? QString("root") : QString("uid %1").arg(*s.uid), {}, 1});
}
}  // namespace

Report audit(Host::Auth auth, const std::optional<ClientKey> &key, const std::optional<Connection> &c,
             const std::optional<Server> &s, const QString &serverError, qint64 when) {
    QList<Check> l;
    if (c) connection(*c, l);
    l.append(auth == Host::Auth::Password ? Check{"client.auth", Level::Bad, "password", "ssh-copy-id", 3}
                                : Check{"client.auth", Level::Ok, auth == Host::Auth::Agent ? "ssh-agent" : "key", {}, 3});
    if (key) {
        QString t = key->type.toLower();
        Level lv = t.contains("ed25519") || t.contains("ecdsa") ? Level::Ok
                   : t.contains("rsa") ? (key->bits >= 3072 ? Level::Ok : key->bits >= 2048 ? Level::Warn : Level::Bad)
                                       : Level::Bad;
        l.append({"client.key", lv, key->type + (t.contains("rsa") ? QString(" %1 bit").arg(key->bits) : QString()),
                  lv == Level::Ok ? QString() : QString("ssh-keygen -t ed25519"), 2});
        l.append(key->encrypted ? Check{"client.passphrase", Level::Ok, {}, {}, 1} : Check{"client.passphrase", Level::Warn, {}, "ssh-keygen -p", 1});
    }
    if (s) server(*s, l);
    return {l, serverError, when};
}

int rsaBits(const QByteArray &blob) {
    qsizetype at = 0;
    auto field = [&](QByteArray *out) {
        if (at + 4 > blob.size()) return false;
        quint32 n = (quint32(quint8(blob[at])) << 24) | (quint32(quint8(blob[at + 1])) << 16) | (quint32(quint8(blob[at + 2])) << 8)
                    | quint8(blob[at + 3]);
        if (n > quint32(blob.size() - at - 4)) return false;
        *out = blob.mid(at + 4, n);
        at += 4 + n;
        return true;
    };
    QByteArray type, e, n;
    if (!field(&type) || type != "ssh-rsa" || !field(&e) || !field(&n)) return 0;
    qsizetype i = 0;
    while (i < n.size() && n[i] == 0) i++;
    if (i == n.size()) return 0;
    int bits = int(n.size() - i - 1) * 8;
    for (quint8 b = quint8(n[i]); b; b >>= 1) bits++;
    return bits;
}

}  // namespace SecurityAudit
