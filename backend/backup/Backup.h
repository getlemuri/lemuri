#pragma once

#include "hosts/list/Host.h"

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

class Activity;
class VaultAccess;
class Hosts;
class Team;
class Router;

/**
 * Everything of a Lemuri, to move it to another PC (the same file as the first Lemuri's): hosts, folders, every entry of
 * the vault and the files of ~/.ssh the user chose. One JSON file, encrypted like the vault (Argon2id and AES-GCM, the
 * plain parameters in the AAD) but with a passphrase chosen for the transfer.
 */
namespace Backup {
constexpr int VERSION = 1;
/** Hosts and a few keys weigh a few hundred KB: a bigger file is not a backup of ours. */
constexpr qint64 MAX_FILE = 16 * 1024 * 1024;
/** A passphrase shorter than this is refused when exporting: the file may end up anywhere. */
constexpr int MIN_PASSPHRASE = 10;
/** The vault entry with the contacts. */
inline const QString CONTACTS = QStringLiteral("lan:trusted");

/** A file of ~/.ssh: its bare name and its content. */
struct SshFile {
    QString name;
    QByteArray bytes;
};
/** What is inside: where and when it was made, and the data. */
struct Contents {
    QString from;
    qint64 created = 0;
    QStringList folders;
    QList<Host> hosts;
    QHash<QString, QString> secrets;
    QList<SshFile> ssh;
};

/** The encrypted file, ready to be written. */
QByteArray seal(const Contents &c, const QString &passphrase);
/**
 * Opens a backup. Throws Fail: with backup.wrongPassphrase's text if the passphrase is wrong (or the file was touched,
 * then *wrong is true), otherwise the reason it is not a backup or its content is not valid.
 */
Contents open(const QByteArray &file, const QString &passphrase, bool *wrong = nullptr);
/**
 * The import that keeps what is here: hosts and vault entries of this PC stay, those of the file are added only if
 * missing. The contacts of the file are never added this way: a contact is a key trusted after comparing its fingerprint.
 */
Contents merge(const Contents &here, const Contents &imported);
}  // namespace Backup

/**
 * The backup requests: backup.sshFiles lists what can go in, backup.export writes the file;
 * backup.open decrypts and describes one (kept here under a token), backup.import applies it. Main thread.
 */
class Backups {
public:
    Backups(Activity &activity, VaultAccess &vaultAccess, Hosts &hosts, Team &team);
    /** Registers its requests. */
    void ops(Router &router);

private:
    /** One row per file of ~/.ssh: the keys (those used by a host ticked already), then known_hosts and config. */
    QJsonObject sshFiles();
    QJsonObject save(const QJsonObject &m);
    QJsonObject open(const QJsonObject &m);
    QJsonObject import(const QJsonObject &m);

    Activity &activity;
    VaultAccess &vaultAccess;
    Hosts &hosts;
    Team &team;
    QHash<QString, Backup::Contents> opened;
};
