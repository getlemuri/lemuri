#include "backup/Backup.h"

#include "backup/SshFolder.h"
#include "base/core/Error.h"
#include "base/system/Files.h"
#include "base/config/I18n.h"
#include "base/core/Params.h"
#include "base/core/Runtime.h"
#include "hosts/list/Hosts.h"
#include "sharing/team/Team.h"
#include "vault/Crypto.h"
#include "vault/Vault.h"
#include "vault/VaultAccess.h"

#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QRegularExpression>
#include <QSysInfo>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>

namespace {
const QString FORMAT = QStringLiteral("lemuri-backup");

QByteArray aad(int version, const Vault::Kdf &k, const QString &salt) {
    return QString("%1|%2|%3|%4|%5|%6|%7").arg(FORMAT).arg(version).arg(k.name).arg(k.iterations).arg(k.memoryKb).arg(k.lanes).arg(salt).toUtf8();
}

Fail notABackup() {
    return Fail{I18n::t("backup.notABackup")};
}

/** Gson reads a number where an int goes, and a missing one as 0. */
int integer(const QJsonValue &v) {
    if (v.isDouble()) return v.toInt();
    if (v.isString()) return v.toString().toInt();
    return 0;
}

QString whoAmI() {
    QString host = QSysInfo::machineHostName();
    QString user = Files::localUser();
    return (user.isEmpty() ? QString("?") : user) + "@" + (host.isEmpty() ? QString("?") : host);
}

/** The file holds every password, even if encrypted: readable by the user only, from the start. Throws Fail. */
void writePrivate(const QString &file, const QByteArray &content) {
    QByteArray p = QFile::encodeName(file);
    ::unlink(p.constData());
    int fd = ::open(p.constData(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (fd < 0) throw Fail{file + ": " + systemError()};
    qsizetype done = 0;
    while (done < content.size()) {
        ssize_t n = ::write(fd, content.constData() + done, size_t(content.size() - done));
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) {
            int e = errno;
            ::close(fd);
            throw Fail{file + ": " + systemError(e)};
        }
        done += n;
    }
    if (::fsync(fd) != 0 || ::close(fd) != 0) throw Fail{file + ": " + systemError()};
}

QString kindLabel(SshFolder::Kind kind) {
    switch (kind) {
    case SshFolder::Kind::KnownHosts: return I18n::t("backup.kind.knownHosts");
    case SshFolder::Kind::Config: return I18n::t("backup.kind.config");
    case SshFolder::Kind::PublicKey: return I18n::t("backup.kind.public");
    case SshFolder::Kind::PrivateKey: return I18n::t("backup.kind.private");
    case SshFolder::Kind::None: break;
    }
    return "?";
}

QJsonObject row(const QString &name, const QString &kind, const QString &detail, bool chosen, bool withPub) {
    return {{"name", name}, {"kind", kind}, {"detail", detail.isNull() ? QJsonValue() : QJsonValue(detail)}, {"chosen", chosen}, {"withPub", withPub}};
}

/** The keys of ~/.ssh used by the hosts, with the names of those hosts. */
QList<QPair<QString, QStringList>> keysUsedByHosts(const QList<Host> &hosts) {
    QList<QPair<QString, QStringList>> m;
    QString ssh = SshFolder::dir();
    for (const Host &h : hosts) {
        if (h.auth() != Host::Auth::Key || h.key().trimmed().isEmpty()) continue;
        QString k = h.key();
        while (k.size() > 1 && k.endsWith('/')) k.chop(1);
        qsizetype slash = k.lastIndexOf('/');
        QString file = slash < 0 ? k : k.mid(slash + 1), parent = slash <= 0 ? QString(slash == 0 ? "/" : "") : k.left(slash);
        if ((k.startsWith('/') && parent != ssh) || file.isEmpty()) continue;
        if (!SshFolder::safeName(file)) continue;
        bool found = false;
        for (auto &e : m)
            if (e.first == file) {
                e.second << h.name();
                found = true;
            }
        if (!found) m.append({file, {h.name()}});
    }
    return m;
}
}  // namespace

QByteArray Backup::seal(const Contents &c, const QString &passphrase) {
    QByteArray salt = Crypto::random(16), iv = Crypto::random(12);
    const Vault::Kdf &kdf = Vault::TODAY;
    QString saltText = QString::fromLatin1(salt.toBase64());
    QJsonArray hosts, ssh;
    for (const Host &h : c.hosts) hosts.append(h.saved());
    for (const SshFile &s : c.ssh) ssh.append(QJsonObject{{"name", s.name}, {"data", QString::fromLatin1(s.bytes.toBase64())}});
    QJsonObject secrets;
    for (auto it = c.secrets.begin(); it != c.secrets.end(); ++it) secrets[it.key()] = it.value();
    QJsonObject inside{{"from", c.from}, {"created", c.created}, {"folders", QJsonArray::fromStringList(c.folders)},
                       {"hosts", hosts}, {"secrets", secrets}, {"ssh", ssh}};
    QByteArray plain = QJsonDocument(inside).toJson(QJsonDocument::Compact);
    QByteArray key = Vault::derive(passphrase, salt, kdf);
    QByteArray data = Crypto::sealGcm(key, iv, plain, aad(VERSION, kdf, saltText));
    Crypto::wipe(key);
    Crypto::wipe(plain);
    QJsonObject f{{"format", FORMAT}, {"version", VERSION}, {"kdf", kdf.name}, {"iterations", kdf.iterations}, {"memory", kdf.memoryKb},
                  {"lanes", kdf.lanes}, {"salt", saltText}, {"iv", QString::fromLatin1(iv.toBase64())},
                  {"data", QString::fromLatin1(data.toBase64())}};
    return QJsonDocument(f).toJson(QJsonDocument::Compact);
}

Backup::Contents Backup::open(const QByteArray &file, const QString &passphrase, bool *wrong) {
    if (wrong) *wrong = false;
    if (file.size() > MAX_FILE) throw notABackup();
    QJsonDocument d = QJsonDocument::fromJson(file);
    if (!d.isObject()) throw notABackup();
    QJsonObject f = d.object();
    if (f["format"].toString() != FORMAT || !f["salt"].isString() || !f["iv"].isString() || !f["data"].isString()) throw notABackup();
    int version = integer(f["version"]);
    if (version > VERSION) throw Fail{I18n::t("backup.newer")};
    Vault::Kdf kdf{f["kdf"].toString(), integer(f["iterations"]), integer(f["memory"]), integer(f["lanes"])};
    Vault::checkArgon2(kdf);
    auto decode = [](const QJsonValue &v) {
        auto r = QByteArray::fromBase64Encoding(v.toString().toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
        if (!r) throw notABackup();
        return r.decoded;
    };
    QByteArray salt = decode(f["salt"]), iv = decode(f["iv"]), data = decode(f["data"]);
    if (iv.isEmpty()) throw notABackup();
    QByteArray key = Vault::derive(passphrase, salt, kdf);
    bool ok = false;
    QByteArray plain = Crypto::openGcm(key, iv, data, aad(version, kdf, f["salt"].toString()), &ok);
    Crypto::wipe(key);
    if (!ok) {
        if (wrong) *wrong = true;
        throw Fail{I18n::t("backup.wrongPassphrase")};
    }
    QJsonDocument inside = QJsonDocument::fromJson(plain);
    Crypto::wipe(plain);
    if (!inside.isObject()) throw notABackup();
    QJsonObject o = inside.object();
    Contents c;
    // Missing lists (a file made by hand) become empty; anything of the wrong shape is a damaged file.
    c.from = o["from"].isString() ? o["from"].toString() : QString("?");
    c.created = qint64(o["created"].toDouble());
    auto list = [&](const char *k) {
        QJsonValue v = o[k];
        if (!v.isUndefined() && !v.isNull() && !v.isArray()) throw notABackup();
        return v.toArray();
    };
    for (const QJsonValue &v : list("folders")) {
        if (!v.isString()) throw notABackup();
        c.folders << v.toString();
    }
    for (const QJsonValue &v : list("hosts")) {
        if (!v.isObject()) throw notABackup();
        Host h = Host::from(v.toObject());
        // Hosts without an id are refused.
        if (h.id().trimmed().isEmpty()) throw notABackup();
        c.hosts << h;
    }
    QJsonValue s = o["secrets"];
    if (!s.isUndefined() && !s.isNull() && !s.isObject()) throw notABackup();
    const QJsonObject secrets = s.toObject();
    for (auto it = secrets.begin(); it != secrets.end(); ++it) {
        if (!it.value().isString()) throw notABackup();
        c.secrets.insert(it.key(), it.value().toString());
    }
    for (const QJsonValue &v : list("ssh")) {
        QJsonObject x = v.toObject();
        QString name = x["name"].toString();
        // Only keys, known_hosts and config: whoever made the file must not plant authorized_keys, rc or environment.
        if (!v.isObject() || !x["data"].isString() || !SshFolder::safeName(name)) throw Fail{I18n::t("backup.badSshName", name)};
        auto bytes = QByteArray::fromBase64Encoding(x["data"].toString().toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
        if (!bytes) throw notABackup();
        if (SshFolder::kind(name, bytes.decoded) == SshFolder::Kind::None) throw Fail{I18n::t("backup.sshNotAllowed", name)};
        c.ssh << SshFile{name, bytes.decoded};
    }
    return c;
}

Backup::Contents Backup::merge(const Contents &here, const Contents &imported) {
    Contents r{here.from, here.created, here.folders, here.hosts, imported.secrets, imported.ssh};
    QSet<QString> ids;
    for (const Host &h : here.hosts) ids.insert(h.id());
    for (const Host &h : imported.hosts)
        if (!ids.contains(h.id())) {
            ids.insert(h.id());
            r.hosts << h;
        }
    for (const QString &f : imported.folders)
        if (!r.folders.contains(f)) r.folders << f;
    r.secrets.remove(CONTACTS);
    for (auto it = here.secrets.begin(); it != here.secrets.end(); ++it) r.secrets.insert(it.key(), it.value());
    return r;
}

// ── Ops ──

Backups::Backups(Activity &activity, VaultAccess &vaultAccess, Hosts &hosts, Team &team)
    : activity(activity), vaultAccess(vaultAccess), hosts(hosts), team(team) {}

void Backups::ops(Router &router) {
    router.op("backup.sshFiles", [this](const QJsonObject &) { return sshFiles(); });
    router.op("backup.export", [this](const QJsonObject &m) { return save(m); });
    router.op("backup.open", [this](const QJsonObject &m) { return open(m); });
    router.op("backup.import", [this](const QJsonObject &m) { return import(m); });
    router.op("backup.close", [this](const QJsonObject &m) { return QJsonValue(opened.remove(Params::need(m, "token")) > 0); });
}

/** One row per file of ~/.ssh: the keys (those used by a host ticked already), then known_hosts and config. */
QJsonObject Backups::sshFiles() {
    auto usedBy = keysUsedByHosts(hosts.all());
    QStringList keys = SshFolder::privateKeys();
    for (const auto &e : usedBy)
        if (!keys.contains(e.first) && SshFolder::exists(e.first)) keys << e.first;
    QJsonArray rows;
    for (const QString &k : keys) {
        QStringList users;
        bool used = false;
        for (const auto &e : usedBy)
            if (e.first == k) {
                users = e.second;
                used = true;
            }
        bool pub = SshFolder::exists(k + ".pub");
        rows.append(row(k, I18n::t(pub ? "backup.kind.privateWithPub" : "backup.kind.private"), used ? users.join(", ") : QString(), used, pub));
    }
    if (SshFolder::exists(SshFolder::KNOWN_HOSTS)) rows.append(row(SshFolder::KNOWN_HOSTS, I18n::t("backup.kind.knownHosts"), QString(), true, false));
    if (SshFolder::exists(SshFolder::CONFIG)) rows.append(row(SshFolder::CONFIG, I18n::t("backup.kind.config"), QString(), true, false));
    return QJsonObject{{"files", rows},
                       {"dir", SshFolder::dir()},
                       {"minPassphrase", Backup::MIN_PASSPHRASE},
                       {"hosts", hosts.all().size()},
                       {"folders", hosts.created().size()},
                       {"secrets", vaultAccess.vault().entries().size()}};
}

/** path, passphrase, files: the names of ~/.ssh chosen (a key brings its .pub). */
QJsonObject Backups::save(const QJsonObject &m) {
    vaultAccess.requireOpen();
    QString pass = Params::need(m, "passphrase");
    if (pass.size() < Backup::MIN_PASSPHRASE) throw Fail{I18n::t("backup.passphraseShort", Backup::MIN_PASSPHRASE)};
    QString to = Params::need(m, "path");
    QList<Backup::SshFile> ssh;
    QStringList left;
    for (const QString &name : Params::strings(m, "files")) {
        QStringList names{name};
        if (SshFolder::exists(name + ".pub") && SshFolder::kind(name, SshFolder::read(name)) == SshFolder::Kind::PrivateKey)
            names << name + ".pub";
        for (const QString &n : names) {
            // Only what an import accepts: anything else would make the whole file unreadable.
            QByteArray b = SshFolder::read(n);
            if (SshFolder::kind(n, b) != SshFolder::Kind::None) ssh << Backup::SshFile{n, b};
            else left << n;
        }
    }
    Backup::Contents c{whoAmI(), QDateTime::currentMSecsSinceEpoch(), hosts.created(), hosts.all(),
                       vaultAccess.vault().entries(), ssh};
    QByteArray file = Backup::seal(c, pass);
    pass.fill(QChar(0));
    writePrivate(to, file);
    int saved = int(c.hosts.size()), files = int(ssh.size());
    activity.done(QFileInfo(to).fileName(), I18n::t("task.backupSaved", saved, files));
    QString message = I18n::t("backup.export.done", to, saved, files);
    if (!left.isEmpty()) message += "\n" + I18n::t("backup.export.left", left.join(", "));
    return QJsonObject{{"message", message}};
}

/** Decrypts path with passphrase: what is inside and, for each ~/.ssh file, what importing it would do here. */
QJsonObject Backups::open(const QJsonObject &m) {
    QString from = Params::need(m, "path");
    QFile f(from);
    if (QFileInfo(from).size() > Backup::MAX_FILE) throw notABackup();
    if (!f.open(QIODevice::ReadOnly)) throw Fail{from + ": " + f.errorString()};
    QByteArray file = f.read(Backup::MAX_FILE + 1);
    QString secret = Params::need(m, "passphrase");
    Backup::Contents in = Backup::open(file, secret);
    secret.fill(QChar(0));
    QString token = Hosts::uid();
    opened.insert(token, in);
    QJsonArray rows;
    for (const Backup::SshFile &s : in.ssh) {
        SshFolder::Kind kind = SshFolder::kind(s.name, s.bytes);
        SshFolder::Outcome outcome = SshFolder::preview(s.name, s.bytes);
        QString what;
        switch (outcome) {
        case SshFolder::Outcome::Added: what = I18n::t("backup.state.new"); break;
        case SshFolder::Outcome::Same: what = I18n::t("backup.state.same"); break;
        case SshFolder::Outcome::Merged: what = I18n::t("backup.state.merge"); break;
        case SshFolder::Outcome::Beside: what = I18n::t("backup.state.beside", s.name + ".imported"); break;
        }
        QString tip = what;
        bool tick = true;
        if (kind == SshFolder::Kind::KnownHosts && outcome != SshFolder::Outcome::Same && outcome != SshFolder::Outcome::Beside) {
            QStringList lines;
            for (const QString &l : SshFolder::newKnownHosts(s.bytes)) {
                QStringList p = l.split(QRegularExpression("\\s+"));
                lines << (p.size() >= 2 ? p[0] + "  " + p[1] : l);
            }
            int markers = SshFolder::markerLines(s.bytes);
            what = I18n::t("backup.state.servers", lines.size()) + (markers > 0 ? " · " + I18n::t("backup.state.markers", markers) : QString());
            tip = what + "\n" + lines.join("\n");
        } else if (kind == SshFolder::Kind::Config) {
            // A config can run commands at the next connection: never ticked for you, and what in it is more than a
            // plain setting is named.
            tick = false;
            QStringList risky = SshFolder::riskyDirectives(s.bytes);
            if (!risky.isEmpty()) what = I18n::t("backup.state.risky", risky.join(", ")) + " · " + what;
            tip = what;
        }
        QJsonObject o = row(s.name, kindLabel(kind), what, tick, false);
        o["tip"] = tip;
        rows.append(o);
    }
    return QJsonObject{{"token", token},
                       {"from", in.from},
                       {"created", in.created},
                       {"hosts", in.hosts.size()},
                       {"folders", in.folders.size()},
                       {"secrets", in.secrets.size()},
                       {"replaceByDefault", hosts.all().isEmpty()},
                       {"hostsHere", hosts.all().size()},
                       {"files", rows}};
}

/** token from backup.open, replace or merge, files: the ~/.ssh files to install. */
QJsonObject Backups::import(const QJsonObject &m) {
    vaultAccess.requireOpen();
    QString token = Params::need(m, "token");
    if (!opened.contains(token)) throw Fail{"Backup not open"};
    Backup::Contents in = opened.take(token);
    bool replace = Params::flag(m, "replace");
    QStringList chosen = Params::strings(m, "files");
    Backup::Contents here{whoAmI(), QDateTime::currentMSecsSinceEpoch(), hosts.created(), hosts.all(),
                          vaultAccess.vault().entries(), {}};
    Backup::Contents result = replace ? in : Backup::merge(here, in);
    try {
        // The vault first: if it cannot be written, the hosts stay as they were too.
        vaultAccess.vault().replaceAll(result.secrets);
        hosts.replaceAll(result.folders, result.hosts);
    } catch (const Fail &f) {
        throw Fail{I18n::t("backup.import.failed", f.why)};
    }
    int added = std::max(0, int(result.hosts.size()) - (replace ? 0 : int(here.hosts.size())));
    QString report = I18n::t(replace ? "backup.import.replaced" : "backup.import.added", added);
    if (!replace && in.secrets.contains(Backup::CONTACTS)) report += "\n" + I18n::t("backup.import.contactsSkipped");
    for (const Backup::SshFile &f : in.ssh) {
        if (!chosen.contains(f.name)) continue;
        try {
            SshFolder::Installed i = SshFolder::install(f.name, f.bytes);
            switch (i.outcome) {
            case SshFolder::Outcome::Added: report += "\n" + I18n::t("backup.ssh.added", i.name); break;
            case SshFolder::Outcome::Same: report += "\n" + I18n::t("backup.ssh.same", i.name); break;
            case SshFolder::Outcome::Merged: report += "\n" + I18n::t("backup.ssh.merged", i.name); break;
            case SshFolder::Outcome::Beside: report += "\n" + I18n::t("backup.ssh.beside", i.name, i.savedAs); break;
            }
        } catch (const Fail &e) {
            report += "\n" + I18n::t("backup.ssh.failed", f.name, e.why);
        }
    }
    // The sharing key and the contacts may have changed with the vaultAccess.vault().
    team.restart();
    QString source = Params::str(m, "source");
    activity.done(source.isNull() ? QString("backup") : source, I18n::t("task.backupImported", added));
    return QJsonObject{{"message", report}};
}
