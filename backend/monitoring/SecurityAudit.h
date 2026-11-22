#pragma once

#include "hosts/Host.h"

#include <QByteArray>
#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QPair>
#include <QString>
#include <QStringList>
#include <optional>

/**
 * How safe the SSH access to a host is: one check per aspect, from the connection itself (algorithms, keys,
 * version) and from the server (sshd settings, brute force protection). Pure: no network, no files; Metrics gathers
 * the facts. Only judges, never changes anything. The checks and weights are the first Lemuri's: both apps give a
 * server the same score.
 */
namespace SecurityAudit {
enum class Level { Ok, Warn, Bad };

/**
 * One aspect. `id` names its texts (security.<id> and security.<id>.advice); `value` is what was found
 * (null: nothing to show); `fix` what to do, or null: a command for this computer for the client.* checks, a
 * line of sshd_config for the others. `weight`: how much it counts in the score.
 */
struct Check {
    QString id;
    Level level;
    QString value, fix;
    int weight;
};

/** The checks, and why the server could not be read (null if it could). */
struct Report {
    QList<Check> checks;
    QString error;
    qint64 when = 0;
    /** 0-1: OK counts in full, WARN half, BAD nothing. None without checks. */
    std::optional<double> score() const;
    const Check *find(const QString &id) const;
    /** As the frontend reads it: {checks: [{id, level, value, fix, weight}], error, when, score}. */
    QJsonObject json() const;
};

/** What the SSH connection itself shows: the negotiated algorithms and the server's key and version. */
struct Connection {
    QString serverVersion, kex, cipher, mac;
    bool strictKex = false;
    QString hostKeyAlgorithm;
    int hostKeyBits = 0;
};

/** The private key file used to sign in: kind (ED25519, ECDSA, RSA, DSA), bits, protected by a passphrase. */
struct ClientKey {
    QString type;
    int bits = 0;
    bool encrypted = false;
};

/** What the server told: user id, the sshd settings by lower case name (none if unreadable), the guards running. */
struct Server {
    std::optional<int> uid;
    std::optional<QHash<QString, QString>> sshd;
    QStringList guards;
};

/**
 * What the server is asked, read only and without sudo: the user id, the sshd settings (all of them from sshd -T
 * when logged in as root, otherwise the configuration files, which anyone can read) and the brute force protections
 * running. The brackets in the process names keep the command itself from matching.
 */
extern const QString COMMAND;

Server parse(const QString &text);

/** Files of sshd_config, in the order the server listed them: path and lines. */
using Files = QList<QPair<QString, QStringList>>;
/**
 * The settings as sshd reads them: the first value of each wins, Include brings in the files in their place (in
 * name order), Match blocks are skipped (they apply to some logins only). Only the files given are known.
 */
QHash<QString, QString> sshdConfig(const Files &files);

/**
 * All the checks that can be judged: those of the connection when there is one, those of the key only with a key
 * file, those of the server when it answered. `auth`: how the host signs in.
 */
Report audit(Host::Auth auth, const std::optional<ClientKey> &key, const std::optional<Connection> &c,
             const std::optional<Server> &s, const QString &serverError, qint64 when);

/** Bits of the modulus of an ssh-rsa public key blob; 0 if it is not one. */
int rsaBits(const QByteArray &blob);
}  // namespace SecurityAudit
