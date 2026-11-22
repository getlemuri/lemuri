#pragma once

#include "hosts/Host.h"
#include "sessions/Transport.h"

#include <QList>
#include <QString>
#include <QStringList>
#include <atomic>
#include <functional>
#include <stdexcept>
#include <memory>
#include <mutex>
#include <thread>

typedef struct ssh_session_struct *ssh_session;
typedef struct ssh_channel_struct *ssh_channel;

/** A connection that failed: the sentence for the user, and what libssh said (shown in grey under it). */
struct SshError : std::runtime_error {
    SshError(const QString &message, const QString &detail) : std::runtime_error(message.toStdString()), message(message), detail(detail) {}
    QString message, detail;
};

/** The questions of a sign-in, for one server of the connection (the jump hosts included). A null answer is "cancel". */
struct SshPrompts {
    /** A new host key: accept it? */
    std::function<bool(const QString &message)> confirm = [](const QString &) { return false; };
    std::function<QString(const QString &message)> password = [](const QString &) { return QString(); };
    std::function<QString(const QString &message)> passphrase = [](const QString &) { return QString(); };
    /** A server question that is not the password (a 2FA code and the like): the answer is not saved. */
    std::function<QString(const QString &question)> reply = [](const QString &) { return QString(); };
    /** Signed in: a password just typed can now be remembered. */
    std::function<void()> authenticated = [] {};
    /**
     * Nobody is watching (the background readings): no key that wants a touch (FIDO, "sk-" keys) and no ssh-agent
     * holding one, so that a security key never blinks out of nowhere.
     */
    bool unattended = false;
};

/**
 * A signed-in SSH connection (libssh), through its jump hosts if it has any: each hop is connected first and the
 * next server is reached over a channel of it, as ssh -J does. Host keys are checked against ~/.ssh/known_hosts
 * (shared with ssh and the first Lemuri): a new key is asked about, a changed one refused, as OpenSSH does.
 *
 * libssh lets one thread at a time use a session: whoever uses it goes through withSession(), which holds the
 * connection's lock. Shell, commands and SFTP share the one connection, so there is no second login.
 */
class SshConnection {
public:
    /** Throws SshError. `warnings`: lines for the terminal (a key file not found, a changed host key). */
    static std::shared_ptr<SshConnection> open(const Host &host, const QList<Host> &hops, const QString &routeError,
                                               const std::function<SshPrompts(const Host &)> &prompts,
                                               const std::function<void(const QString &)> &warnings, int timeoutSeconds = 15);
    ~SshConnection();
    void disconnect();
    /** The socket to wait on, read under the lock (disconnect frees the session from another thread); -1 once gone. */
    int socket();

    /**
     * Runs `f(session)` holding the connection: libssh lets one thread at a time use a session. Throws SshError if
     * it is not connected (any more). `f` must not use the connection again: the lock is not recursive.
     */
    template <typename F>
    auto withSession(F &&f) -> decltype(f(ssh_session{})) {
        std::lock_guard<std::mutex> l(io);
        if (!session) throw notConnected();
        return f(session);
    }
    /** As withSession, but only if still connected: then true. For cleaning up, which must never throw. */
    template <typename F>
    bool ifConnected(F &&f) {
        std::lock_guard<std::mutex> l(io);
        if (!session) return false;
        f(session);
        return true;
    }

    /** Others using the session (SFTP, commands): the shell's thread then looks at the connection more often. */
    std::atomic<int> users{0};
    /** Runs a command and returns its output (at most `max` bytes); throws SshError. */
    QByteArray run(const QString &command, int max, int timeoutSeconds, int *exitCode = nullptr);
    /** Still connected (the server may have closed an idle connection). */
    bool alive();
    /** As alive(), without waiting: a connection busy with a command is in use, so it is alive. */
    bool seemsAlive();

    /** What was agreed with the server, for the security checks. */
    struct Negotiated {
        QString banner, kex, cipher, mac;
        /** As libssh names the key: ssh-ed25519, ecdsa-sha2-nistp256, ssh-rsa... */
        QString hostKeyType;
        int hostKeyBits = 0;
    };
    Negotiated negotiated();
    /**
     * The algorithm lists the server offers (its KEXINIT), read on a new connection that stops there, through the
     * same jump hosts; libssh does not tell them. Empty lists if they could not be read.
     */
    void serverOffer(int timeoutSeconds, QStringList *kex, QStringList *hostKeys);

    /** The server it is connected to (the last one, past the jump hosts). */
    Host target;

    /** How it reaches its server through a jump host (see Ssh.cpp). */
    struct Link;
    void setLink(std::shared_ptr<Link> l);

private:
    friend class SshShell;
    static SshError notConnected();
    static std::shared_ptr<SshConnection> connectOne(const Host &h, const SshPrompts &p, const std::shared_ptr<SshConnection> &via,
                                                     const std::function<void(const QString &)> &warnings, int timeout);
    static std::shared_ptr<Link> openLink(const std::shared_ptr<SshConnection> &via, const Host &to, int *fd);

    ssh_session session = nullptr;
    std::mutex io;
    QList<std::shared_ptr<SshConnection>> through;
    std::shared_ptr<Link> link;
};

/** A terminal on a server: the shell of an SSH connection, with a pseudo-terminal xterm-256color. */
class SshShell : public Transport {
public:
    SshShell(Host host, QList<Host> hops, QString routeError, std::function<SshPrompts(const Host &)> prompts);
    ~SshShell() override;
    void launch(ThreadGroup &threads, int columns, int lines) override;
    void send(const QByteArray &bytes) override;
    void resize(int columns, int lines) override;
    void close() override;
    /** The operating system read from the server (PRETTY_NAME of /etc/os-release), from the shell's thread. */
    std::function<void(const QString &)> onOs = [](const QString &) {};
    /** The connection once signed in (null before, and after the end): SFTP goes over it. */
    std::shared_ptr<SshConnection> connection() const;

private:
    void run();
    void loop(ssh_channel channel);
    void wake();

    Host host;
    QList<Host> hops;
    QString routeError;
    std::function<SshPrompts(const Host &)> prompts;
    mutable std::mutex m;
    std::shared_ptr<SshConnection> conn;
    QByteArray pending;
    int columns = 80, lines = 24;
    bool resized = false;
    int wakeFds[2] = {-1, -1};
    std::atomic<bool> closed{false};
};

namespace Ssh {
/** Once, at startup. */
void init();
/** Whether the ssh-agent holds a key that wants a touch ("sk-" types); true if the agent does not answer. */
bool agentHoldsSecurityKeys();
/** The key types in the agent's answer to SSH_AGENTC_REQUEST_IDENTITIES (the message, without its length). */
QStringList agentKeyTypes(const QByteArray &answer);
/** A keyboard-interactive question that asks for the account password and nothing else. When in doubt, no. */
bool passwordQuestion(const QString &question);
/** From libssh's message to the sentence for the user (the key of the messages), or null if not one we know. */
QString errorKey(const QString &technical);
QString explain(const QString &technical);
/** The key file of a host as written in it: an absolute path, or a name inside ~/.ssh. */
QString keyFile(const QString &key);
QString sshDir();
/** A private key file: its kind (ED25519, ECDSA, RSA, DSA), bits, whether a passphrase protects it. */
struct KeyInfo {
    QString type;
    int bits = 0;
    bool encrypted = false;
};
/** False if the file cannot be read or is not a key. Never asks for the passphrase. */
bool keyInfo(const QString &path, KeyInfo *info);
/** The libssh in use offers strict kex (0.10.6 and later): with a server that offers it too, it is used. */
bool strictKexOffered();
}  // namespace Ssh
