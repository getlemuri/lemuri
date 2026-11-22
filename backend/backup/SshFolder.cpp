#include "backup/SshFolder.h"

#include "base/Error.h"
#include "base/Handles.h"
#include "base/I18n.h"
#include "sharing/ContactNames.h"

#include <QDir>
#include <QRegularExpression>
#include <QSet>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {
/** A key or a config file is a few KB. */
constexpr qint64 MAX_SIZE = 256 * 1024;

QByteArray path(const QString &name) {
    return QFile::encodeName(SshFolder::dir() + "/" + name);
}

/** lstat: what is there, without following a link. */
bool status(const QString &name, struct stat *st) {
    return ::lstat(path(name).constData(), st) == 0;
}

bool regular(const QString &name, struct stat *st) {
    return status(name, st) && S_ISREG(st->st_mode);
}

QByteArray readAll(const QString &name, qint64 limit, bool *ok) {
    *ok = false;
    Fd fd(::open(path(name).constData(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC));
    if (!fd) return {};
    QByteArray b;
    char buffer[16384];
    for (;;) {
        ssize_t n = ::read(fd.get(), buffer, sizeof buffer);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) {
            *ok = n == 0;
            break;
        }
        b.append(buffer, n);
        if (b.size() > limit) break;
    }
    return b;
}

bool looksPrivate(const QString &head) {
    return (head.startsWith("-----BEGIN") && SshFolder::lines(head).value(0).contains("PRIVATE KEY")) || head.startsWith("PuTTY-User-Key-File");
}

bool marker(const QString &line) {
    QString l = ContactNames::strip(line);
    return l.startsWith("@cert-authority") || l.startsWith("@revoked");
}

bool blank(const QString &s) {
    return ContactNames::strip(s).isEmpty();
}

/** A new file, born with its permissions: never an existing one, never through a link. Throws Fail. */
void write(const QString &name, const QByteArray &content, bool everyone) {
    mode_t mode = everyone ? 0644 : 0600;
    Fd fd(::open(path(name).constData(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, mode));
    if (!fd) throw Fail(SshFolder::dir() + "/" + name + ": " + systemError());
    ::fchmod(fd.get(), mode);
    qsizetype done = 0;
    while (done < content.size()) {
        ssize_t n = ::write(fd.get(), content.constData() + done, size_t(content.size() - done));
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) throw Fail(name + ": " + systemError());
        done += n;
    }
}
}  // namespace

QStringList SshFolder::lines(const QString &s) {
    if (s.isEmpty()) return {};
    static const QRegularExpression breaks("\\r\\n|\\r|\\n");
    QStringList l = s.split(breaks);
    if (l.last().isEmpty()) l.removeLast();
    return l;
}

QString SshFolder::dir() {
    return QDir::homePath() + "/.ssh";
}

bool SshFolder::safeName(const QString &name) {
    static const QRegularExpression ok("\\A[A-Za-z0-9_@+-][A-Za-z0-9._@+-]{0,99}\\z");
    return !name.isNull() && ok.match(name).hasMatch();
}

SshFolder::Kind SshFolder::kind(const QString &name, const QByteArray &content) {
    // Files that sshd or ssh read with effects far beyond a key: a login to this PC, commands at every login.
    static const QRegularExpression forbidden("\\A(?:authorized_keys.*|rc|environment|known_hosts2.*)\\z");
    static const QRegularExpression publicKey("\\A(?:ssh-|ecdsa-|sk-)\\S+ [A-Za-z0-9+/=]+(?: .*)?\\z");
    if (!safeName(name) || forbidden.match(name).hasMatch()) return Kind::None;
    if (name == KNOWN_HOSTS) return Kind::KnownHosts;
    if (name == CONFIG) return Kind::Config;
    QString s = QString::fromUtf8(content);
    if (name.endsWith(".pub")) {
        QString t = ContactNames::strip(s);
        return lines(t).size() == 1 && publicKey.match(t).hasMatch() ? Kind::PublicKey : Kind::None;
    }
    return looksPrivate(s) ? Kind::PrivateKey : Kind::None;
}

QStringList SshFolder::riskyDirectives(const QByteArray &content) {
    // Settings of a connection that run nothing, load nothing and open nothing: the rest is shown to the user.
    static const QSet<QString> harmless{
        "host", "match", "hostname", "user", "port", "identityfile", "identitiesonly", "proxyjump", "certificatefile",
        "addkeystoagent", "ciphers", "macs", "kexalgorithms", "hostkeyalgorithms", "pubkeyacceptedalgorithms",
        "pubkeyacceptedkeytypes", "casignaturealgorithms", "hostbasedacceptedalgorithms", "preferredauthentications",
        "passwordauthentication", "pubkeyauthentication", "kbdinteractiveauthentication", "challengeresponseauthentication",
        "gssapiauthentication", "hostbasedauthentication", "numberofpasswordprompts", "batchmode", "serveraliveinterval",
        "serveralivecountmax", "tcpkeepalive", "connecttimeout", "connectionattempts", "compression", "rekeylimit",
        "stricthostkeychecking", "hashknownhosts", "updatehostkeys", "verifyhostkeydns", "checkhostip", "visualhostkey",
        "fingerprinthash", "hostkeyalias", "loglevel", "logverbose", "addressfamily", "ipqos", "requesttty", "escapechar",
        "exitonforwardfailure", "clearallforwardings", "canonicalizehostname", "canonicaldomains",
        "canonicalizefallbacklocal", "canonicalizemaxdots", "canonicalizepermittedcnames", "obscurekeystroketiming",
        "requiredrsasize", "enableescapecommandline", "sessiontype", "stdinnull", "forkafterauthentication", "tag"};
    static const QRegularExpression directive("^\\s*([A-Za-z][A-Za-z0-9]*)(?:\\s*=\\s*|\\s+|$)(.*)$");
    static const QRegularExpression exec("\\bexec\\b", QRegularExpression::CaseInsensitiveOption);
    QStringList l;
    for (const QString &line : lines(QString::fromUtf8(content))) {
        if (blank(line) || line.trimmed().startsWith('#')) continue;
        auto m = directive.match(line);
        if (!m.hasMatch()) continue;
        QString name = m.captured(1), lower = name.toLower();
        QString shown = lower == "match" && exec.match(m.captured(2)).hasMatch() ? QString("Match exec") : name;
        if ((lower == "match" && shown == name) || (harmless.contains(lower) && lower != "match")) continue;
        bool known = false;
        for (const QString &x : l) known = known || x.compare(shown, Qt::CaseInsensitive) == 0;
        if (!known) l << shown;
    }
    return l;
}

QStringList SshFolder::newKnownHosts(const QByteArray &arrived) {
    struct stat st;
    bool ok = false;
    QByteArray here = regular(KNOWN_HOSTS, &st) ? readAll(KNOWN_HOSTS, 64LL * 1024 * 1024, &ok) : QByteArray();
    QStringList l;
    for (const QString &x : lines(QString::fromUtf8(missingLines(here, arrived))))
        if (!blank(x)) l << x;
    return l;
}

int SshFolder::markerLines(const QByteArray &arrived) {
    int n = 0;
    for (const QString &l : lines(QString::fromUtf8(arrived)))
        if (marker(l)) n++;
    return n;
}

QStringList SshFolder::privateKeys() {
    QStringList l;
    for (const QString &n : QDir(dir()).entryList(QDir::Files | QDir::Hidden | QDir::System)) {
        if (!safeName(n) || n.endsWith(".pub")) continue;
        struct stat st;
        if (!regular(n, &st) || st.st_size > MAX_SIZE) continue;
        bool ok = false;
        QByteArray head = readAll(n, 64, &ok).left(64);
        if (looksPrivate(QString::fromLatin1(head))) l << n;
    }
    std::sort(l.begin(), l.end(), [](const QString &a, const QString &b) { return a.compare(b, Qt::CaseInsensitive) < 0; });
    return l;
}

bool SshFolder::exists(const QString &name) {
    struct stat st;
    return safeName(name) && regular(name, &st);
}

QByteArray SshFolder::read(const QString &name) {
    if (!safeName(name)) throw Fail{I18n::t("backup.badSshName", name)};
    struct stat st;
    bool ok = false;
    QByteArray b;
    if (regular(name, &st) && st.st_size <= MAX_SIZE) b = readAll(name, MAX_SIZE, &ok);
    if (!ok || b.size() > MAX_SIZE) throw Fail{I18n::t("backup.sshNotReadable", name)};
    return b;
}

SshFolder::Installed SshFolder::install(const QString &name, const QByteArray &content) {
    if (!safeName(name)) throw Fail{I18n::t("backup.badSshName", name)};
    // Checked again here, not only when the backup is opened: this is where the file is written.
    if (kind(name, content) == Kind::None) throw Fail{I18n::t("backup.sshNotAllowed", name)};
    QByteArray d = QFile::encodeName(dir());
    struct stat st;
    if (::stat(d.constData(), &st) != 0) {
        if (!QDir().mkpath(dir())) throw Fail{dir() + ": " + systemError()};
        ::chmod(d.constData(), 0700);
    }
    bool pub = name.endsWith(".pub");
    if (!status(name, &st)) {
        // A new known_hosts too gets only plain host keys, never @cert-authority or @revoked lines.
        write(name, name == KNOWN_HOSTS ? missingLines({}, content) : content, pub);
        return {name, name, Outcome::Added};
    }
    if (S_ISREG(st.st_mode)) {
        bool ok = false;
        QByteArray here = readAll(name, 64LL * 1024 * 1024, &ok);
        if (!ok) throw Fail{I18n::t("backup.sshNotReadable", name)};
        if (here == content) return {name, name, Outcome::Same};
        if (name == KNOWN_HOSTS) {
            QByteArray missing = missingLines(here, content);
            if (missing.isEmpty()) return {name, name, Outcome::Same};
            Fd fd(::open(path(name).constData(), O_WRONLY | O_APPEND | O_NOFOLLOW | O_CLOEXEC));
            if (!fd || ::write(fd.get(), missing.constData(), size_t(missing.size())) != missing.size())
                throw Fail(name + ": " + systemError());
            return {name, name, Outcome::Merged};
        }
    }
    for (int i = 1;; i++) {
        QString beside = name + ".imported" + (i > 1 ? "-" + QString::number(i) : QString());
        if (status(beside, &st)) continue;
        write(beside, content, pub);
        return {name, beside, Outcome::Beside};
    }
}

SshFolder::Outcome SshFolder::preview(const QString &name, const QByteArray &content) {
    struct stat st;
    if (!safeName(name) || !status(name, &st)) return Outcome::Added;
    if (!S_ISREG(st.st_mode)) return Outcome::Beside;
    bool ok = false;
    QByteArray here = readAll(name, 64LL * 1024 * 1024, &ok);
    if (!ok) return Outcome::Beside;
    if (here == content) return Outcome::Same;
    if (name == KNOWN_HOSTS) return missingLines(here, content).isEmpty() ? Outcome::Same : Outcome::Merged;
    return Outcome::Beside;
}

QByteArray SshFolder::missingLines(const QByteArray &here, const QByteArray &arrived) {
    QString h = QString::fromUtf8(here);
    QSet<QString> have;
    for (const QString &l : lines(h)) have.insert(l);
    QString b;
    if (!h.isEmpty() && !h.endsWith('\n')) b += '\n';
    qsizetype start = b.size();
    for (const QString &l : lines(QString::fromUtf8(arrived))) {
        if (blank(l) || marker(l) || have.contains(l)) continue;
        have.insert(l);
        b += l + '\n';
    }
    return b.size() == start ? QByteArray() : b.toUtf8();
}
