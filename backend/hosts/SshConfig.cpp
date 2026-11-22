#include "hosts/SshConfig.h"

#include "base/Files.h"
#include "hosts/Hosts.h"
#include "ssh/Ssh.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>
#include <QSet>

namespace {
/** Git hosting services: reachable over SSH but not servers to open a shell on. */
const QSet<QString> GIT_SERVICES{"github.com", "gitlab.com", "bitbucket.org", "ssh.dev.azure.com", "vs-ssh.visualstudio.com",
                                 "codeberg.org", "git.sr.ht"};
constexpr int MAX_INCLUDE_DEPTH = 16;
/** Files read for one config in all, whatever the includes: a config including itself must not read it forever. */
constexpr int MAX_FILES = 256;

QString localUser() {
    return Files::localUser();
}

/** One block of the file: the lines before the first Host apply to every host. */
struct Block {
    bool all = true, match = false;
    QStringList patterns;
    QHash<QString, QString> options;

    bool applies(const QString &alias) const {
        if (match) return false;
        if (all) return true;
        bool any = false;
        for (const QString &p : patterns) {
            bool negated = p.startsWith('!');
            if (SshConfig::glob(negated ? p.mid(1) : p, alias)) {
                if (negated) return false;
                any = true;
            }
        }
        return any;
    }
};

/** Space-separated words, where a quoted part is a single word. */
QStringList words(const QString &value) {
    QStringList out;
    QString w;
    bool quoted = false, any = false;
    for (QChar c : value) {
        if (c == '"') {
            quoted = !quoted;
            any = true;
        } else if (c.isSpace() && !quoted) {
            if (any) out << w;
            w.clear();
            any = false;
        } else {
            w += c;
            any = true;
        }
    }
    if (any) out << w;
    return out;
}

QString expandHome(const QString &p) {
    return p.startsWith("~/") || p == "~" ? QDir::homePath() + p.mid(1) : p;
}

/** The files of an Include: relative paths are in ~/.ssh, and wildcards are allowed in the file name. */
QStringList include(const QString &pattern) {
    QString p = expandHome(pattern);
    if (!QFileInfo(p).isAbsolute()) p = Ssh::sshDir() + "/" + p;
    QFileInfo f(p);
    QString name = f.fileName();
    if (!name.contains('*') && !name.contains('?')) return f.isFile() ? QStringList{p} : QStringList();
    QDir dir = f.dir();
    QStringList out;
    for (const QFileInfo &x : dir.entryInfoList({name}, QDir::Files, QDir::Name)) out << x.absoluteFilePath();
    return out;
}

/** What load() keeps across the includes of one config. */
struct Loading {
    QList<Block> blocks{Block{}};
    QStringList aliases;
    /** The files being read now, the include chain: one of them included again is a loop, skipped. */
    QSet<QString> chain;
    int files = 0;
};

void load(const QString &path, Loading &l, int depth) {
    QString canonical = QFileInfo(path).canonicalFilePath();
    if (depth > MAX_INCLUDE_DEPTH || l.files >= MAX_FILES || canonical.isEmpty() || l.chain.contains(canonical)) return;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return;
    l.files++;
    l.chain.insert(canonical);
    QList<Block> &blocks = l.blocks;
    QStringList &aliases = l.aliases;
    static const QRegularExpression split("\\s*=\\s*|\\s+");
    for (const QString &raw : QString::fromUtf8(f.readAll()).split('\n')) {
        QString line = raw.trimmed();
        if (line.isEmpty() || line.startsWith('#')) continue;
        // "Keyword value", "Keyword=value" or "Keyword = value".
        QRegularExpressionMatch m = split.match(line);
        QString key = (m.hasMatch() ? line.left(m.capturedStart()) : line).toLower();
        QString value = m.hasMatch() ? line.mid(m.capturedEnd()).trimmed() : QString();
        if (key == "host") {
            Block b;
            b.all = false;
            b.patterns = words(value);
            blocks << b;
            for (const QString &p : b.patterns)
                if (!p.startsWith('!') && !p.contains('*') && !p.contains('?') && !aliases.contains(p)) aliases << p;
        } else if (key == "match") {
            Block b;
            b.match = true;
            blocks << b;
        } else if (key == "include") {
            for (const QString &pattern : words(value))
                for (const QString &p : include(pattern)) load(p, l, depth + 1);
        } else {
            QStringList w = words(value);
            if (!w.isEmpty() && !blocks.last().options.contains(key)) blocks.last().options.insert(key, w.first());
        }
    }
    l.chain.remove(canonical);
}

QString expand(const QString &path, const QString &alias, const QString &hostName, const QString &user) {
    QString p = expandHome(path)
                        .replace("%d", QDir::homePath())
                        .replace("%u", localUser())
                        .replace("%r", user.isEmpty() ? localUser() : user)
                        .replace("%h", hostName)
                        .replace("%n", alias)
                        .replace("%%", "%");
    return QDir::cleanPath(QFileInfo(p).isAbsolute() ? p : Ssh::sshDir() + "/" + p);
}
}  // namespace

bool SshConfig::Entry::gitService() const {
    return GIT_SERVICES.contains(hostName.toLower());
}

QString SshConfig::Entry::destination() const {
    return QString("%1@%2:%3").arg(user.isEmpty() ? localUser() : user, hostName).arg(port);
}

Host SshConfig::Entry::toHost(const QString &group) const {
    QJsonObject o{{"id", Hosts::uid()}, {"name", alias}, {"address", hostName}, {"port", port},
                  {"user", user.isEmpty() ? localUser() : user}, {"group", group}};
    if (!proxyJump.isEmpty()) o["jump"] = proxyJump;
    if (!identityFile.isEmpty()) {
        o["authMethod"] = "KEY";
        QFileInfo k(identityFile);
        o["key"] = k.absolutePath() == QDir::cleanPath(Ssh::sshDir()) ? k.fileName() : identityFile;
    } else {
        o["authMethod"] = "AGENT";
    }
    return Host::from(o);
}

QString SshConfig::file() {
    return Ssh::sshDir() + "/config";
}

bool SshConfig::glob(const QString &pattern, const QString &text) {
    QString rx;
    for (QChar c : pattern) rx += c == '*' ? QString(".*") : c == '?' ? QString(".") : QRegularExpression::escape(QString(c));
    return QRegularExpression("^" + rx + "$", QRegularExpression::CaseInsensitiveOption).match(text).hasMatch();
}

QList<SshConfig::Entry> SshConfig::read(const QString &path) {
    if (!QFileInfo(path).isFile()) return {};
    Loading l;
    load(path, l, 0);
    const QList<Block> &blocks = l.blocks;
    const QStringList &aliases = l.aliases;
    QList<Entry> out;
    for (const QString &alias : aliases) {
        QHash<QString, QString> o;
        // As ssh does: for each option the first value obtained wins.
        for (const Block &b : blocks)
            if (b.applies(alias))
                for (auto it = b.options.begin(); it != b.options.end(); ++it)
                    if (!o.contains(it.key())) o.insert(it.key(), it.value());
        Entry e;
        e.alias = alias;
        e.hostName = o.value("hostname", alias).replace("%h", alias).replace("%%", "%");
        bool ok = false;
        int p = o.value("port", "22").toInt(&ok);
        e.port = ok ? p : 22;
        e.user = o.value("user");
        if (o.contains("identityfile")) e.identityFile = expand(o.value("identityfile"), alias, e.hostName, e.user);
        e.proxyJump = o.value("proxyjump");
        if (e.proxyJump.compare("none", Qt::CaseInsensitive) == 0) e.proxyJump.clear();
        out << e;
    }
    return out;
}
