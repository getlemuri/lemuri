#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>

/**
 * The files of ~/.ssh that a backup can carry: private keys with their .pub, known_hosts
 * and config, nothing else. Only bare file names, never a path; nothing existing is overwritten when they are put
 * back, links are never followed, and known_hosts never gets @cert-authority or @revoked lines from a backup.
 */
namespace SshFolder {
inline const QString KNOWN_HOSTS = QStringLiteral("known_hosts"), CONFIG = QStringLiteral("config");

/** What a file of ~/.ssh can be in a backup; NONE if a backup may not carry it. */
enum class Kind { None, PrivateKey, PublicKey, KnownHosts, Config };
/** What happened (or would happen) to a file put back in ~/.ssh. */
enum class Outcome { Added, Same, Beside, Merged };
struct Installed {
    QString name, savedAs;
    Outcome outcome;
};

/** ~/.ssh (tests point HOME elsewhere). */
QString dir();
/** A bare file name, safe to join to ~/.ssh: no folders, no "..", not hidden. */
bool safeName(const QString &name);
/** What this file is, from its name and content: the forbidden files (authorized_keys, rc...) and anything else are NONE. */
Kind kind(const QString &name, const QByteArray &content);
/**
 * The config directives in `content` that are not plain connection settings, shown before importing it: an allowlist,
 * so whatever is not known to be harmless counts (commands such as ProxyCommand and "Match exec", libraries such as
 * PKCS11Provider, ForwardAgent, Include, control sockets, port forwards, other known_hosts or agents...).
 */
QStringList riskyDirectives(const QByteArray &content);
/** The known_hosts lines an import would add here: the missing ones, without @cert-authority or @revoked. */
QStringList newKnownHosts(const QByteArray &arrived);
/** How many @cert-authority or @revoked lines a known_hosts from a backup has: they are left out. */
int markerLines(const QByteArray &arrived);
/** The private keys in ~/.ssh, recognised by their content (not by the name), sorted by name. */
QStringList privateKeys();
/** True if ~/.ssh has this file (a regular one, not a link). */
bool exists(const QString &name);
/** The content of a file of ~/.ssh: only regular files, not links, and not too big. Throws Fail. */
QByteArray read(const QString &name);
/** Puts a file back in ~/.ssh: a missing one is written, an existing one kept and the new one written beside it. Throws Fail. */
Installed install(const QString &name, const QByteArray &content);
/** What install() would do with this file, without writing anything. */
Outcome preview(const QString &name, const QByteArray &content);
/** The lines of `arrived` that `here` does not have, ready to be appended. */
QByteArray missingLines(const QByteArray &here, const QByteArray &arrived);
/** The lines of a text: split at \n, \r\n or \r, without a last empty line. */
QStringList lines(const QString &s);
}  // namespace SshFolder
