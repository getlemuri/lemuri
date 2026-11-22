#include "ssh/Ssh.h"

#include "base/core/Error.h"
#include "base/system/Handles.h"
#include "base/config/I18n.h"
#include "base/system/Net.h"
#include "base/system/Posix.h"
#include "base/core/Runtime.h"
#include "monitoring/audit/SecurityAudit.h"

#include <QDir>
#include <QLocalSocket>
#include <clocale>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStringDecoder>
#include <QStringList>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <libssh/libssh.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

// ── Helpers ──

namespace {
using Key = Owned<ssh_key_struct, ssh_key_free>;
using Text = Owned<char, ssh_string_free_char>;
}  // namespace

namespace {
using Clock = std::chrono::steady_clock;
/** At most this much typed or pasted and not yet sent to the server. */
constexpr qsizetype MAX_PENDING = 8 * 1024 * 1024;

/** The exit code of a command or a shell, -1 if the server sent none (libssh 0.11 renamed the call). */
int exitStatus(ssh_channel c) {
#if LIBSSH_VERSION_INT >= SSH_VERSION_INT(0, 11, 0)
    uint32_t code = 0;
    return ssh_channel_get_exit_state(c, &code, nullptr, nullptr) == SSH_OK ? int(code) : -1;
#else
    return ssh_channel_get_exit_status(c);
#endif
}

QString lastError(ssh_session s) {
    return QString::fromUtf8(ssh_get_error(s));
}

SshError failure(const QString &technical) {
    return SshError{Ssh::explain(technical), technical};
}

void writeAll(int fd, const char *p, ssize_t n) {
    while (n > 0) {
        ssize_t w = ::write(fd, p, size_t(n));
        if (w < 0 && errno == EINTR) continue;
        if (w < 0 && errno == EAGAIN) {
            pollfd f{fd, POLLOUT, 0};
            ::poll(&f, 1, 100);
            continue;
        }
        if (w <= 0) return;
        p += w;
        n -= w;
    }
}

/** A private key protected by a passphrase: PEM "ENCRYPTED", or an OpenSSH key whose cipher is not "none". */
bool encrypted(const QString &path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return false;
    QByteArray text = f.read(64 * 1024);
    if (text.contains("ENCRYPTED")) return true;
    if (!text.contains("BEGIN OPENSSH PRIVATE KEY")) return false;
    QByteArray body;
    for (const QByteArray &line : text.split('\n'))
        if (!line.startsWith("-----")) body += line.trimmed();
    QByteArray raw = QByteArray::fromBase64(body);
    const QByteArray magic("openssh-key-v1\0", 15);
    if (!raw.startsWith(magic) || raw.size() < 19) return false;
    quint32 len = (quint8(raw[15]) << 24) | (quint8(raw[16]) << 16) | (quint8(raw[17]) << 8) | quint8(raw[18]);
    return raw.mid(19, len) != "none";
}

/** The key's family, as ssh names it in "ED25519 key fingerprint is ...". */
QString keyKind(ssh_key key) {
    QString t = QString::fromLatin1(ssh_key_type_to_char(ssh_key_type(key)));
    if (t.contains("ed25519")) return "ED25519";
    if (t.contains("ecdsa")) return "ECDSA";
    if (t.contains("rsa")) return "RSA";
    if (t.contains("dss")) return "DSA";
    return t.toUpper();
}

/** The size of a key: RSA from its modulus, the others from their kind. */
int keyBits(ssh_key key) {
    QString t = QString::fromLatin1(ssh_key_type_to_char(ssh_key_type(key)));
    if (t.contains("ed25519")) return 256;
    if (t.contains("nistp256")) return 256;
    if (t.contains("nistp384")) return 384;
    if (t.contains("nistp521")) return 521;
    if (t.contains("dss")) return 1024;
    char *raw = nullptr;
    if (ssh_pki_export_pubkey_base64(key, &raw) != SSH_OK) return 0;
    Text b64(raw);
    return SecurityAudit::rsaBits(QByteArray::fromBase64(b64.get()));
}
}  // namespace

void Ssh::init() {
    // The system's error texts (strerror, inside libssh's messages too) in English: errorKey()
    // recognises them and turns them into the user's language; an Italian "Connessione rifiutata" it would not.
    // libssh gives no finer code than "fatal" or "denied", so its texts are all there is. Called in
    // main(), before the backend starts its threads: setlocale is not safe once threads run.
    std::setlocale(LC_MESSAGES, "C");
    ssh_init();
}

QString Ssh::sshDir() {
    return QDir::homePath() + "/.ssh";
}

QString Ssh::keyFile(const QString &key) {
    return QFileInfo(key).isAbsolute() ? key : sshDir() + "/" + key;
}

bool Ssh::passwordQuestion(const QString &question) {
    static const QRegularExpression password("password|passwort|contraseña|mot de passe|senha|parola d'ordine");
    // A one-time code, a PIN or a password change: even with "password" in it, the saved one does not fit there.
    static const QRegularExpression notPassword(
            "one[- ]?time|\\botp\\b|\\btotp\\b|\\bhotp\\b|oath|token|code|passcode|codice|verifica|verification|\\bpin\\b"
            "|2fa|two[- ]factor|authenticator|yubikey|duo|\\bnew\\b|\\bnuova\\b|retype|re-enter|again|confirm|current");
    // Without "user@host": a server called "code-server" does not make the question suspicious.
    QString d = question.toLower();
    d.replace(QRegularExpression("\\S*@\\S*"), " ");
    return password.match(d).hasMatch() && !notPassword.match(d).hasMatch();
}

QString Ssh::errorKey(const QString &technical) {
    QString m = technical.toLower();
    if (m.contains("resolve hostname") || m.contains("name or service not known") || m.contains("nodename nor servname")
        || m.contains("could not resolve") || m.contains("temporary failure in name resolution"))
        return "ssh.err.unknownHost";
    if (m.contains("no route to host") || m.contains("network is unreachable")) return "ssh.err.noRoute";
    if (m.contains("connection refused")) return "ssh.err.refused";
    if (m.contains("timeout") || m.contains("timed out")) return "ssh.err.timeout";
    if (m.contains("reject hostkey") || m.contains("host key has changed")) return "ssh.err.hostKey";
    if (m.contains("auth fail") || m.contains("auth cancel") || m.contains("too many authentication failures")) return "ssh.err.auth";
    if (m.contains("invalid private key")) return "ssh.err.key";
    if (m.contains("kex error") || m.contains("no match for method") || m.contains("could not agree")) return "ssh.err.algorithms";
    if (m.contains("connection reset") || m.contains("broken pipe") || m.contains("remote host closed") || m.contains("disconnected")
        || m.contains("socket error: eof") || m.contains("connection closed"))
        return "ssh.err.closedByServer";
    if (m.contains("packet corrupt") || m.contains("session is down") || m.contains("channel is not open")) return "ssh.err.dropped";
    return QString();
}

QString Ssh::explain(const QString &technical) {
    QString key = errorKey(technical);
    return key.isEmpty() ? technical : I18n::format(key, {});
}

// ── The connection ──

/** A channel of the hop to the next server, carried to a local socket that the next session uses as its own. */
struct SshConnection::Link {
    std::shared_ptr<SshConnection> hop;
    ssh_channel channel = nullptr;
    int local = -1;
    std::atomic<bool> stop{false};
    std::thread pump;

    ~Link() {
        stop = true;
        if (pump.joinable()) pump.join();
        // A disconnect has freed the channel with its session already.
        if (channel)
            hop->ifConnected([this](ssh_session) {
                ssh_channel_close(channel);
                ssh_channel_free(channel);
            });
        if (local >= 0) ::close(local);
    }

    void run() {
        char buffer[32768];
        while (!stop) {
            pollfd p[2] = {{local, POLLIN, 0}, {hop->socket(), POLLIN, 0}};
            ::poll(p, 2, 50);
            // Read under the connection's lock, written to the local end without it: a slow reader there must not
            // hold up the shell and the transfers on the same connection.
            QByteArray got;
            bool ended = false;
            bool connected = hop->ifConnected([&](ssh_session) {
                int n;
                while ((n = ssh_channel_read_nonblocking(channel, buffer, sizeof buffer, 0)) > 0) got.append(buffer, n);
                ended = n == SSH_ERROR || ssh_channel_is_eof(channel) || ssh_channel_is_closed(channel);
            });
            if (!got.isEmpty()) writeAll(local, got.constData(), got.size());
            if (!connected || ended) break;
            if (p[0].revents & (POLLIN | POLLHUP)) {
                ssize_t n = ::read(local, buffer, sizeof buffer);
                if (n == 0 || (n < 0 && errno != EINTR && errno != EAGAIN)) break;
                bool sent = n <= 0 || hop->ifConnected([&](ssh_session) {
                    if (ssh_channel_write(channel, buffer, uint32_t(n)) == SSH_ERROR) n = -1;
                });
                if (!sent || n < 0) break;
            }
        }
        ::shutdown(local, SHUT_RDWR);
    }
};

/** A forward channel of `via` to `to`, carried to a local socket: `fd` is its other end. */
std::shared_ptr<SshConnection::Link> SshConnection::openLink(const std::shared_ptr<SshConnection> &via, const Host &to, int *fd) {
    auto l = std::make_shared<SshConnection::Link>();
    l->hop = via;
    std::lock_guard<std::mutex> g(via->io);
    l->channel = ssh_channel_new(via->session);
    QByteArray address = to.address().toUtf8();
    if (!l->channel || ssh_channel_open_forward(l->channel, address.constData(), to.port(), "127.0.0.1", 22) != SSH_OK) {
        QString why = lastError(via->session);
        if (l->channel) ssh_channel_free(l->channel);
        l->channel = nullptr;
        throw failure(why);
    }
    int pair[2];
    if (Posix::socketpair(AF_UNIX, SOCK_STREAM, 0, pair) != 0) throw failure(systemError());
    l->local = pair[1];
    *fd = pair[0];
    SshConnection::Link *raw = l.get();
    l->pump = std::thread([raw] { raw->run(); });
    return l;
}

namespace {
void checkHostKey(ssh_session s, const Host &h, const SshPrompts &p, const std::function<void(const QString &)> &warnings) {
    ssh_key raw = nullptr;
    if (ssh_get_server_publickey(s, &raw) != SSH_OK) throw failure(lastError(s));
    Key server(raw);
    unsigned char *hash = nullptr;
    size_t length = 0;
    QString fingerprint, kind = keyKind(server.get());
    if (ssh_get_publickey_hash(server.get(), SSH_PUBLICKEY_HASH_SHA256, &hash, &length) == 0) {
        Text f(ssh_get_fingerprint_hash(SSH_PUBLICKEY_HASH_SHA256, hash, length));
        fingerprint = QString::fromLatin1(f.get());
        ssh_clean_pubkey_hash(&hash);
    }
    QString item = h.port() == 22 ? h.address() : QString("[%1]:%2").arg(h.address()).arg(h.port());
    enum ssh_known_hosts_e state = ssh_session_is_known_server(s);
    if (state == SSH_KNOWN_HOSTS_OK) return;
    // CHANGED: another key where one is remembered. OTHER: the server shows a key of another kind than the remembered
    // one, which is what a fake server in the middle does when it has not got the real one (libssh: "a possible
    // attack"). Both refused, as OpenSSH does, and known_hosts is left as it is.
    if (state == SSH_KNOWN_HOSTS_CHANGED || state == SSH_KNOWN_HOSTS_OTHER) {
        warnings("\x1b[1;31m" + I18n::t("ssh.changed.title", h.destination()) + "\x1b[0m\r\n" + I18n::t("ssh.changed.refuse") + "\r\n"
                 + QString("%1 key fingerprint is %2.").arg(kind, fingerprint) + "\r\n" + I18n::t("ssh.changed.fix") + "\r\n"
                 + "    ssh-keygen -R '" + item + "'\r\n");
        throw failure("reject HostKey: " + h.address());
    }
    if (state == SSH_KNOWN_HOSTS_ERROR) throw failure(lastError(s));
    // A new host: the user decides, as with ssh.
    QString question = QString("The authenticity of host '%1' can't be established.\n%2 key fingerprint is %3.\n"
                               "Are you sure you want to continue connecting?").arg(item, kind, fingerprint);
    if (!p.confirm(question)) throw failure("reject HostKey: " + h.address());
    // Not remembered (known_hosts not writable): the connection goes on, but the next one asks again.
    if (ssh_session_update_known_hosts(s) != SSH_OK)
        warnings("Warning: the host key could not be saved in " + Ssh::sshDir() + "/known_hosts: " + lastError(s) + "\r\n");
}

enum class Tried { Yes, No, Cancelled };

bool securityKey(ssh_key key) {
    switch (ssh_key_type(key)) {
    case SSH_KEYTYPE_SK_ECDSA:
    case SSH_KEYTYPE_SK_ECDSA_CERT01:
    case SSH_KEYTYPE_SK_ED25519:
    case SSH_KEYTYPE_SK_ED25519_CERT01:
        return true;
    default:
        return false;
    }
}

/** A big-endian uint32 of the agent protocol (RFC draft-miller-ssh-agent), or -1 past the end. */
qint64 u32(const QByteArray &b, qsizetype at) {
    if (at < 0 || at + 4 > b.size()) return -1;
    auto c = [&](qsizetype i) { return quint32(quint8(b[at + i])); };
    return qint64(c(0) << 24 | c(1) << 16 | c(2) << 8 | c(3));
}
}  // namespace

QStringList Ssh::agentKeyTypes(const QByteArray &answer) {
    // SSH_AGENT_IDENTITIES_ANSWER: byte 12, uint32 count, then per key: string blob (starting with string type), string comment.
    QStringList types;
    if (answer.isEmpty() || quint8(answer[0]) != 12) return types;
    qint64 count = u32(answer, 1);
    qsizetype at = 5;
    for (qint64 i = 0; i < count && i < 1000; i++) {
        qint64 blob = u32(answer, at);
        if (blob < 4 || at + 4 + blob > answer.size()) break;
        qint64 type = u32(answer, at + 4);
        if (type < 0 || type > blob - 4) break;
        types << QString::fromLatin1(answer.mid(at + 8, type));
        at += 4 + blob;
        qint64 comment = u32(answer, at);
        if (comment < 0 || at + 4 + comment > answer.size()) break;
        at += 4 + comment;
    }
    return types;
}

bool Ssh::agentHoldsSecurityKeys() {
    QString path = qEnvironmentVariable("SSH_AUTH_SOCK");
    if (path.isEmpty()) return false;
    QLocalSocket agent;
    agent.connectToServer(path);
    // Not asked, not known: as if it held one, the background stays away from it.
    if (!agent.waitForConnected(2000)) return true;
    agent.write(QByteArray("\0\0\0\x01\x0b", 5));  // SSH_AGENTC_REQUEST_IDENTITIES
    if (!agent.waitForBytesWritten(2000)) return true;
    QByteArray in;
    while (in.size() < 4 || in.size() < 4 + u32(in, 0)) {
        if (in.size() > 4 + 256 * 1024 || !agent.waitForReadyRead(2000)) return true;
        in += agent.readAll();
    }
    for (const QString &t : agentKeyTypes(in.mid(4, u32(in, 0))))
        if (t.startsWith("sk-")) return true;
    return false;
}

namespace {

/** Signs in with a key file; `query`: only if the server accepts its public key (then the passphrase is asked). */
bool keyFileWorks(ssh_session s, const QString &path, const SshPrompts &p, bool query) {
    QByteArray file = QFile::encodeName(path);
    if (query) {
        ssh_key raw = nullptr;
        if (ssh_pki_import_pubkey_file((file + ".pub").constData(), &raw) != SSH_OK) return false;
        Key pub(raw);
        if (ssh_userauth_try_publickey(s, nullptr, pub.get()) != SSH_AUTH_SUCCESS) return false;
    }
    ssh_key raw = nullptr;
    if (ssh_pki_import_privkey_file(file.constData(), nullptr, nullptr, nullptr, &raw) != SSH_OK) {
        if (!encrypted(path)) return false;
        for (int attempt = 0; attempt < 3 && !raw; attempt++) {
            QString pass = p.passphrase("Passphrase for " + path);
            if (pass.isNull()) return false;
            QByteArray b = pass.toUtf8();
            if (ssh_pki_import_privkey_file(file.constData(), b.constData(), nullptr, nullptr, &raw) != SSH_OK) raw = nullptr;
            b.fill(0);
        }
        if (!raw) return false;
    }
    Key key(raw);
    // Signing with a security key asks for a touch: not while nobody is watching.
    if (p.unattended && securityKey(key.get())) return false;
    return ssh_userauth_publickey(s, nullptr, key.get()) == SSH_AUTH_SUCCESS;
}

bool agentWorks(ssh_session s, const SshPrompts &p) {
    if (qEnvironmentVariableIsEmpty("SSH_AUTH_SOCK")) return false;
    if (p.unattended && Ssh::agentHoldsSecurityKeys()) return false;
    return ssh_userauth_agent(s, nullptr) == SSH_AUTH_SUCCESS;
}

/** The default keys and then every other private key in ~/.ssh that has its .pub next to it, as ssh does. */
bool localKeysWork(ssh_session s, const SshPrompts &p) {
    QStringList keys;
    for (const char *n : {"id_ed25519", "id_ecdsa", "id_rsa"}) keys << Ssh::sshDir() + "/" + n;
    QStringList pubs = QDir(Ssh::sshDir()).entryList({"*.pub"}, QDir::Files, QDir::Name);
    for (const QString &pub : pubs) {
        QString k = Ssh::sshDir() + "/" + pub.chopped(4);
        if (!keys.contains(k)) keys << k;
    }
    for (const QString &k : keys)
        if (QFileInfo(k).isFile() && keyFileWorks(s, k, p, QFileInfo::exists(k + ".pub"))) return true;
    return false;
}

/** Keyboard-interactive: the password question gets the password, any other (a 2FA code) is the user's. */
Tried keyboard(ssh_session s, const SshPrompts &p) {
    int denied = 0;
    for (int round = 0; round < 20; round++) {
        int rc = ssh_userauth_kbdint(s, nullptr, nullptr);
        if (rc == SSH_AUTH_SUCCESS) return Tried::Yes;
        if (rc == SSH_AUTH_DENIED) {
            // A wrong answer: asked again, as ssh does (the saved password is not given twice).
            if (++denied >= 3) return Tried::No;
            continue;
        }
        if (rc != SSH_AUTH_INFO) return Tried::No;
        int n = ssh_userauth_kbdint_getnprompts(s);
        if (n == 0) continue;
        if (n != 1) return Tried::No;
        char echo = 0;
        QString question = QString::fromUtf8(ssh_userauth_kbdint_getprompt(s, 0, &echo)).trimmed();
        if (echo) return Tried::No;
        QString a = Ssh::passwordQuestion(question) ? p.password(question) : p.reply(question);
        if (a.isNull()) return Tried::Cancelled;
        QByteArray b = a.toUtf8();
        ssh_userauth_kbdint_setanswer(s, 0, b.constData());
        b.fill(0);
    }
    return Tried::No;
}

void authenticate(ssh_session s, const Host &h, const SshPrompts &p, const std::function<void(const QString &)> &warnings) {
    int rc = ssh_userauth_none(s, nullptr);
    if (rc == SSH_AUTH_SUCCESS) return;
    if (rc == SSH_AUTH_ERROR) throw failure(lastError(s));
    int methods = ssh_userauth_list(s, nullptr);
    Host::Auth method = h.auth();
    if (method != Host::Auth::Password && (methods & SSH_AUTH_METHOD_PUBLICKEY)) {
        if (method == Host::Auth::Key && !h.key().trimmed().isEmpty()) {
            QString k = Ssh::keyFile(h.key());
            if (QFileInfo(k).isFile()) {
                if (keyFileWorks(s, k, p, false)) return;
            } else {
                warnings("\x1b[33mssh: " + I18n::t("ssh.keyNotFound", k) + "\x1b[0m\r\n");
                if (agentWorks(s, p) || localKeysWork(s, p)) return;
            }
        } else if (agentWorks(s, p) || localKeysWork(s, p)) {
            return;
        }
    }
    methods = ssh_userauth_list(s, nullptr);
    if (methods & SSH_AUTH_METHOD_INTERACTIVE) {
        Tried t = keyboard(s, p);
        if (t == Tried::Yes) return;
        if (t == Tried::Cancelled) throw failure("Auth cancel");
    }
    if (methods & SSH_AUTH_METHOD_PASSWORD) {
        for (int attempt = 0; attempt < 3; attempt++) {
            QString pw = p.password(QString("Password for %1@%2").arg(h.user(), h.address()));
            if (pw.isNull()) throw failure("Auth cancel");
            QByteArray b = pw.toUtf8();
            rc = ssh_userauth_password(s, nullptr, b.constData());
            b.fill(0);
            if (rc == SSH_AUTH_SUCCESS) return;
            if (rc == SSH_AUTH_ERROR) throw failure(lastError(s));
        }
    }
    throw failure("Auth fail");
}
}  // namespace

std::shared_ptr<SshConnection> SshConnection::open(const Host &host, const QList<Host> &hops, const QString &routeError,
                                                   const std::function<SshPrompts(const Host &)> &prompts,
                                                   const std::function<void(const QString &)> &warnings, int timeout) {
    if (!routeError.isEmpty()) throw SshError{I18n::t("jump.err.route", routeError), routeError};
    QList<std::shared_ptr<SshConnection>> chain;
    std::shared_ptr<SshConnection> previous;
    try {
        for (const Host &hop : hops) {
            try {
                previous = connectOne(hop, prompts(hop), previous, warnings, timeout);
            } catch (const SshError &e) {
                // Which hop failed: "Auth fail" alone would point at the server at the end.
                throw SshError{I18n::t("jump.err.hop", hop.destination(), e.message), e.detail};
            }
            chain << previous;
        }
        auto c = connectOne(host, prompts(host), previous, warnings, timeout);
        c->through = chain;
        c->target = host;
        return c;
    } catch (...) {
        for (auto it = chain.rbegin(); it != chain.rend(); ++it) (*it)->disconnect();
        throw;
    }
}

std::shared_ptr<SshConnection> SshConnection::connectOne(const Host &h, const SshPrompts &p, const std::shared_ptr<SshConnection> &via,
                                                         const std::function<void(const QString &)> &warnings, int timeout) {
    auto c = std::make_shared<SshConnection>();
    int fd = -1;
    std::shared_ptr<SshConnection::Link> through;
    if (via) through = openLink(via, h, &fd);
    ssh_session s = ssh_new();
    c->session = s;
    c->setLink(through);
    // As ssh does: accepted host keys go into ~/.ssh/known_hosts; a new ~/.ssh is the user's only, as ssh makes it.
    if (!QFileInfo::exists(Ssh::sshDir())) {
        QDir().mkpath(Ssh::sshDir());
        QFile::setPermissions(Ssh::sshDir(), QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    }
    QString known = Ssh::sshDir() + "/known_hosts";
    if (!QFileInfo::exists(known)) {
        QFile f(known);
        if (f.open(QIODevice::WriteOnly)) f.close();
        QFile::setPermissions(known, QFile::ReadOwner | QFile::WriteOwner);
    }
    QByteArray address = h.address().toUtf8(), user = h.user().toUtf8(), knownFile = QFile::encodeName(known);
    unsigned int port = unsigned(h.port());
    long seconds = timeout;
    int no = 0;
    // libssh would read a "user@host" typed in the address as a user too: only the host is passed, and the user is
    // always the host's own field (hosts from contacts with an @ are refused before: Parcel::problem).
    if (qsizetype at = address.lastIndexOf('@'); at >= 0) address = address.mid(at + 1);
    auto set = [s](ssh_options_e option, const void *value) {
        if (ssh_options_set(s, option, value) != SSH_OK) throw failure(lastError(s));
    };
    set(SSH_OPTIONS_HOST, address.constData());
    set(SSH_OPTIONS_PORT, &port);
    set(SSH_OPTIONS_USER, user.constData());
    set(SSH_OPTIONS_TIMEOUT, &seconds);
    set(SSH_OPTIONS_KNOWNHOSTS, knownFile.constData());
    // Only what the host says: ~/.ssh/config is imported by hand.
    set(SSH_OPTIONS_PROCESS_CONFIG, &no);
    if (fd >= 0) set(SSH_OPTIONS_FD, &fd);
    if (ssh_connect(s) != SSH_OK) throw failure(lastError(s));
    checkHostKey(s, h, p, warnings);
    authenticate(s, h, p, warnings);
    p.authenticated();
    return c;
}

SshConnection::~SshConnection() {
    disconnect();
}

void SshConnection::setLink(std::shared_ptr<Link> l) {
    link = std::move(l);
}

SshError SshConnection::notConnected() {
    return SshError{I18n::t("ssh.notReady"), "not connected"};
}

int SshConnection::socket() {
    std::lock_guard<std::mutex> l(io);
    return session ? ssh_get_fd(session) : -1;
}

void SshConnection::disconnect() {
    {
        std::lock_guard<std::mutex> l(io);
        if (session) {
            ssh_disconnect(session);
            ssh_free(session);
            session = nullptr;
        }
    }
    link.reset();
    for (auto it = through.rbegin(); it != through.rend(); ++it) (*it)->disconnect();
    through.clear();
}

bool SshConnection::alive() {
    std::lock_guard<std::mutex> l(io);
    return session && ssh_is_connected(session);
}

bool SshConnection::seemsAlive() {
    std::unique_lock<std::mutex> l(io, std::try_to_lock);
    if (!l.owns_lock()) return true;
    return session && ssh_is_connected(session);
}

SshConnection::Negotiated SshConnection::negotiated() {
    std::lock_guard<std::mutex> l(io);
    Negotiated n;
    if (!session) return n;
    auto text = [](const char *c) { return c ? QString::fromLatin1(c) : QString(); };
    n.banner = text(ssh_get_serverbanner(session));
    n.kex = text(ssh_get_kex_algo(session));
    n.cipher = text(ssh_get_cipher_out(session));
    n.mac = text(ssh_get_hmac_out(session));
    ssh_key raw = nullptr;
    if (ssh_get_server_publickey(session, &raw) == SSH_OK) {
        Key k(raw);
        n.hostKeyType = text(ssh_key_type_to_char(ssh_key_type(k.get())));
        n.hostKeyBits = keyBits(k.get());
    }
    return n;
}

namespace {
/** Exactly `n` bytes before `end`. */
bool readFull(int fd, char *p, size_t n, Clock::time_point end) {
    while (n > 0) {
        auto left = std::chrono::duration_cast<std::chrono::milliseconds>(end - Clock::now()).count();
        if (left <= 0) return false;
        pollfd f{fd, POLLIN, 0};
        if (::poll(&f, 1, int(left)) <= 0) return false;
        ssize_t r = ::read(fd, p, n);
        if (r < 0 && errno == EINTR) continue;
        if (r <= 0) return false;
        p += r;
        n -= size_t(r);
    }
    return true;
}

/** The server's version line, ours, then its KEXINIT (sent in clear, before any key): the first two lists. */
bool readOffer(int fd, Clock::time_point end, QStringList *kex, QStringList *hostKeys) {
    QByteArray line;
    // Before its version a server may send other lines (RFC 4253, 4.2).
    for (int lines = 0;;) {
        char c;
        if (!readFull(fd, &c, 1, end)) return false;
        if (c == '\n') {
            if (line.startsWith("SSH-")) break;
            line.clear();
            if (++lines > 20) return false;
        } else if (line.size() < 255) {
            line += c;
        } else {
            return false;
        }
    }
    static const char hello[] = "SSH-2.0-Lemuri_audit\r\n";
    writeAll(fd, hello, sizeof hello - 1);
    unsigned char head[5];
    if (!readFull(fd, reinterpret_cast<char *>(head), 5, end)) return false;
    quint32 length = (quint32(head[0]) << 24) | (quint32(head[1]) << 16) | (quint32(head[2]) << 8) | head[3];
    if (length < 22 || length > 35000 || head[4] >= length) return false;
    QByteArray rest(int(length - 1), 0);
    if (!readFull(fd, rest.data(), size_t(rest.size()), end)) return false;
    QByteArray payload = rest.left(rest.size() - head[4]);
    // SSH_MSG_KEXINIT, a 16 byte cookie, then the name lists.
    if (payload.size() < 17 || payload[0] != 20) return false;
    qsizetype at = 17;
    auto list = [&](QStringList *out) {
        if (at + 4 > payload.size()) return false;
        quint32 n = (quint32(quint8(payload[at])) << 24) | (quint32(quint8(payload[at + 1])) << 16) | (quint32(quint8(payload[at + 2])) << 8)
                    | quint8(payload[at + 3]);
        if (n > quint32(payload.size() - at - 4)) return false;
        *out = QString::fromLatin1(payload.mid(at + 4, n)).split(',', Qt::SkipEmptyParts);
        at += 4 + n;
        return true;
    };
    return list(kex) && list(hostKeys);
}
}  // namespace

void SshConnection::serverOffer(int timeoutSeconds, QStringList *kex, QStringList *hostKeys) {
    kex->clear();
    hostKeys->clear();
    auto end = Clock::now() + std::chrono::seconds(timeoutSeconds);
    int raw = -1;
    std::shared_ptr<Link> l;
    try {
        if (!through.isEmpty()) l = openLink(through.last(), target, &raw);
        else raw = Net::connect(target.address(), target.port(), timeoutSeconds * 1000);
    } catch (const SshError &) {
        return;
    }
    // Closed first when this ends (it is declared after the link): the link's pump sees its end closed and stops.
    Fd fd(raw);
    if (!fd) return;
    if (!readOffer(fd.get(), end, kex, hostKeys)) {
        kex->clear();
        hostKeys->clear();
    }
}

bool Ssh::strictKexOffered() {
    return ssh_version(SSH_VERSION_INT(0, 10, 6)) != nullptr;
}

bool Ssh::keyInfo(const QString &path, KeyInfo *info) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return false;
    QByteArray text = f.read(64 * 1024);
    if (!text.contains("PRIVATE KEY")) return false;
    info->encrypted = encrypted(path);
    QByteArray file = QFile::encodeName(path);
    // The public half: the .pub next to it, or the one an OpenSSH key carries in clear, or the key itself if open.
    ssh_key pub = nullptr;
    if (ssh_pki_import_pubkey_file((file + ".pub").constData(), &pub) != SSH_OK) pub = nullptr;
    if (!pub && text.contains("BEGIN OPENSSH PRIVATE KEY")) {
        QByteArray body;
        for (const QByteArray &line : text.split('\n'))
            if (!line.startsWith("-----")) body += line.trimmed();
        QByteArray raw = QByteArray::fromBase64(body);
        qsizetype at = 15;  // "openssh-key-v1\0"
        auto field = [&](QByteArray *out) {
            if (at + 4 > raw.size()) return false;
            quint32 n = (quint32(quint8(raw[at])) << 24) | (quint32(quint8(raw[at + 1])) << 16) | (quint32(quint8(raw[at + 2])) << 8)
                        | quint8(raw[at + 3]);
            if (n > quint32(raw.size() - at - 4)) return false;
            if (out) *out = raw.mid(at + 4, n);
            at += 4 + n;
            return true;
        };
        QByteArray blob;
        // cipher, kdf, kdf options, the number of keys (4 bytes), the first public key
        if (raw.startsWith(QByteArray("openssh-key-v1\0", 15)) && field(nullptr) && field(nullptr) && field(nullptr) && (at += 4) <= raw.size()
            && field(&blob)) {
            QByteArray type = blob.size() > 4 ? blob.mid(4, (quint8(blob[2]) << 8) | quint8(blob[3])) : QByteArray();
            if (ssh_pki_import_pubkey_base64(blob.toBase64().constData(), ssh_key_type_from_name(type.constData()), &pub) != SSH_OK) pub = nullptr;
        }
    }
    if (!pub && !info->encrypted && ssh_pki_import_privkey_file(file.constData(), nullptr, nullptr, nullptr, &pub) != SSH_OK) pub = nullptr;
    if (Key owned{pub}) {
        info->type = keyKind(owned.get());
        info->bits = keyBits(owned.get());
        return true;
    }
    // A protected PEM key without its .pub: only the kind, from the first line.
    if (text.contains("BEGIN RSA")) info->type = "RSA";
    else if (text.contains("BEGIN EC")) info->type = "ECDSA";
    else if (text.contains("BEGIN DSA")) info->type = "DSA";
    else return false;
    return true;
}

QByteArray SshConnection::run(const QString &command, int max, int timeoutSeconds, int *exitCode) {
    users++;
    struct Done {
        std::atomic<int> &n;
        ~Done() { n--; }
    } done{users};
    ssh_channel c;
    {
        std::lock_guard<std::mutex> l(io);
        if (!session) throw failure(I18n::t("ssh.notReady"));
        c = ssh_channel_new(session);
        QByteArray cmd = command.toUtf8();
        if (!c || ssh_channel_open_session(c) != SSH_OK || ssh_channel_request_exec(c, cmd.constData()) != SSH_OK) {
            QString why = lastError(session);
            if (c) ssh_channel_free(c);
            throw failure(why);
        }
    }
    QByteArray out;
    char buffer[8192];
    auto end = Clock::now() + std::chrono::seconds(timeoutSeconds);
    bool tooLarge = false, timedOut = false;
    for (;;) {
        int n;
        bool eof;
        {
            std::lock_guard<std::mutex> l(io);
            if (!session) throw failure(I18n::t("ssh.notReady"));
            while ((n = ssh_channel_read_nonblocking(c, buffer, sizeof buffer, 0)) > 0) {
                if (out.size() + n > max) {
                    tooLarge = true;
                    break;
                }
                out.append(buffer, n);
            }
            eof = ssh_channel_is_eof(c) || ssh_channel_is_closed(c);
        }
        if (n == SSH_ERROR || tooLarge || eof) break;
        if (Clock::now() > end) {
            timedOut = true;
            break;
        }
        pollfd f{socket(), POLLIN, 0};
        ::poll(&f, 1, 50);
    }
    {
        std::lock_guard<std::mutex> l(io);
        // Disconnected meanwhile: the channel went with the session.
        if (!session) throw failure(I18n::t("ssh.notReady"));
        // The exit code comes with the end of the channel, a moment after the last data.
        for (int i = 0; i < 20 && exitCode && !ssh_channel_is_closed(c); i++) ssh_channel_poll_timeout(c, 25, 0);
        if (exitCode) *exitCode = exitStatus(c);
        ssh_channel_close(c);
        ssh_channel_free(c);
    }
    if (tooLarge) throw SshError{I18n::t("ssh.err.outputTooLarge", max / 1024), "output too large"};
    if (timedOut) throw failure("Command timed out: " + command);
    return out;
}

// ── The shell ──

SshShell::SshShell(Host host, QList<Host> hops, QString routeError, std::function<SshPrompts(const Host &)> prompts)
    : host(std::move(host)), hops(std::move(hops)), routeError(std::move(routeError)), prompts(std::move(prompts)) {
    if (Posix::pipe(wakeFds, true) != 0) wakeFds[0] = wakeFds[1] = -1;
}

SshShell::~SshShell() {
    for (int fd : wakeFds)
        if (fd >= 0) ::close(fd);
}

std::shared_ptr<SshConnection> SshShell::connection() const {
    std::lock_guard<std::mutex> l(m);
    return closed ? nullptr : conn;
}

void SshShell::launch(ThreadGroup &threads, int c, int l) {
    {
        std::lock_guard<std::mutex> g(m);
        columns = c;
        lines = l;
    }
    onState(State::Connecting, {});
    auto self = std::static_pointer_cast<SshShell>(shared_from_this());
    threads.start([self] { self->run(); });
}

void SshShell::wake() {
    if (wakeFds[1] >= 0) {
        char b = 1;
        ssize_t r = ::write(wakeFds[1], &b, 1);
        (void)r;
    }
}

void SshShell::send(const QByteArray &bytes) {
    {
        std::lock_guard<std::mutex> l(m);
        if (closed) return;
        // A server that has stopped reading does not make the backend keep everything pasted into it.
        if (pending.size() + bytes.size() > MAX_PENDING) return;
        pending += bytes;
    }
    wake();
}

void SshShell::resize(int c, int l) {
    {
        std::lock_guard<std::mutex> g(m);
        columns = c;
        lines = l;
        resized = true;
    }
    wake();
}

void SshShell::close() {
    if (closed.exchange(true)) return;
    wake();
    onState(State::Closed, {});
}

void SshShell::run() {
    std::shared_ptr<SshConnection> c;
    try {
        c = SshConnection::open(host, hops, routeError, prompts, [this](const QString &w) { onData(w); });
        {
            std::lock_guard<std::mutex> l(m);
            if (closed) {
                c->disconnect();
                return;
            }
            conn = c;
        }
        ssh_channel channel;
        int asked, high;
        {
            std::lock_guard<std::mutex> io(c->io);
            {
                std::lock_guard<std::mutex> l(m);
                asked = columns;
                high = lines;
                resized = false;
            }
            channel = ssh_channel_new(c->session);
            if (!channel || ssh_channel_open_session(channel) != SSH_OK
                || ssh_channel_request_pty_size(channel, "xterm-256color", asked, high) != SSH_OK
                || ssh_channel_request_shell(channel) != SSH_OK) {
                QString why = lastError(c->session);
                if (channel) ssh_channel_free(channel);
                throw failure(why);
            }
        }
        onState(State::Connected, {});
        // On a separate channel of the same connection: no second login.
        try {
            QByteArray text = c->run("cat /etc/os-release 2>/dev/null", 64 * 1024, 5);
            for (const QByteArray &line : text.split('\n')) {
                if (!line.startsWith("PRETTY_NAME=")) continue;
                QString v = QString::fromUtf8(line.mid(12)).trimmed();
                v.remove(QRegularExpression("^[\"']|[\"']$"));
                if (!v.isEmpty()) onOs(v);
            }
        } catch (const SshError &) {
            // Optional: without /etc/os-release the one typed by hand stays.
        }
        loop(channel);
    } catch (const SshError &e) {
        if (closed) {
            if (c) c->disconnect();
            return;
        }
        onData("\r\n\x1b[31mssh: " + e.message + "\x1b[0m\r\n");
        // What libssh said, in grey: the sentence above may hide a detail that matters.
        if (!e.detail.isEmpty() && e.detail != e.message) onData("\x1b[90m" + I18n::t("ssh.err.details", e.detail) + "\x1b[0m\r\n");
        closed = true;
        {
            std::lock_guard<std::mutex> l(m);
            conn.reset();
        }
        onState(State::Error, e.message);
        if (c) c->disconnect();
    }
}

void SshShell::loop(ssh_channel channel) {
    std::shared_ptr<SshConnection> c = conn;
    QStringDecoder utf8(QStringDecoder::Utf8);
    char buffer[16384];
    auto lastActivity = Clock::now();
    bool ended = false;
    QString failed;
    while (!closed) {
        int timeout = c->users > 0 ? 20 : 15000;
        pollfd p[2] = {{c->socket(), POLLIN, 0}, {wakeFds[0], POLLIN, 0}};
        ::poll(p, 2, timeout);
        if (p[1].revents & POLLIN) {
            char drain[64];
            while (::read(wakeFds[0], drain, sizeof drain) > 0) {}
        }
        if (closed) break;
        QByteArray out;
        bool resize = false;
        int cols, rows;
        {
            std::lock_guard<std::mutex> l(m);
            out.swap(pending);
            resize = resized;
            resized = false;
            cols = columns;
            rows = lines;
        }
        std::lock_guard<std::mutex> io(c->io);
        // Disconnected from another thread: the channel went with the session.
        if (!c->session) {
            ended = true;
            break;
        }
        if (resize) ssh_channel_change_pty_size(channel, cols, rows);
        if (!out.isEmpty()) {
            if (ssh_channel_write(channel, out.constData(), uint32_t(out.size())) == SSH_ERROR) {
                failed = I18n::t("ssh.err.sendFailed", Ssh::explain(lastError(c->session)));
                break;
            }
            lastActivity = Clock::now();
        }
        int n = 0;
        for (int stream : {0, 1}) {
            while ((n = ssh_channel_read_nonblocking(channel, buffer, sizeof buffer, stream)) > 0) {
                onData(utf8.decode(QByteArrayView(buffer, n)));
                lastActivity = Clock::now();
            }
            if (n == SSH_ERROR) break;
        }
        if (n == SSH_ERROR) {
            failed = Ssh::explain(lastError(c->session));
            break;
        }
        if (ssh_channel_is_eof(channel) || ssh_channel_is_closed(channel)) {
            ended = true;
            break;
        }
        // As ServerAliveInterval 30: a connection that silently dropped is noticed, and NATs keep it open.
        if (Clock::now() - lastActivity > std::chrono::seconds(30)) {
            ssh_send_ignore(c->session, "");
            lastActivity = Clock::now();
        }
    }
    int code = -1;
    {
        std::lock_guard<std::mutex> io(c->io);
        if (c->session) {
            if (ended)
                for (int i = 0; i < 20 && !ssh_channel_is_closed(channel); i++) ssh_channel_poll_timeout(channel, 25, 0);
            if (ended) code = exitStatus(channel);
            ssh_channel_close(channel);
            ssh_channel_free(channel);
        }
    }
    if (!failed.isEmpty() && !closed) {
        onData("\r\n\x1b[31mssh: " + failed + "\x1b[0m\r\n");
        closed = true;
        onState(State::Error, failed);
    } else if (!closed) {
        // The server ended the shell (exit, logout, a dropped connection): the terminal says so, as ssh does, and
        // hides the cursor, since nothing typed goes anywhere any more.
        QString msg = code >= 0 ? I18n::t("ssh.ended.code", QString::number(code)) : I18n::t("ssh.ended");
        onData("\r\n\x1b[90m" + msg + "\x1b[0m\r\n\x1b[?25l");
        close();
    }
    {
        std::lock_guard<std::mutex> l(m);
        conn.reset();
    }
    c->disconnect();
}
