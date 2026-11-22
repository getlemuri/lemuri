#pragma once

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QMutex>
#include <QString>
#include <functional>

/**
 * Host passwords and the other secrets, encrypted in ~/.lemuri/vault.json with AES-GCM, in the first Lemuri's format,
 * same file. The key comes from the master password with Argon2id (64 MiB, 3 passes, 4 lanes) and stays only in
 * memory while open. The plain parameters (version, derivation, salt) are the AES-GCM AAD. Versions 1-3 (PBKDF2)
 * and 4 (the former AAD label) are opened and rewritten as version 5. Thread-safe.
 */
class Vault {
public:
    static constexpr int VERSION = 5;
    struct Kdf {
        QString name;  // "argon2id" or "pbkdf2"
        int iterations = 0, memoryKb = 0, lanes = 0;
        bool weakerThan(const Kdf &o) const;
    };
    static const Kdf TODAY;

    Vault();
    static QString dir();
    static QString file();
    bool exists() const;
    bool isOpen() const;
    /** Creates an empty vault, replacing the existing one. Throws Fail if it cannot be written. */
    void create(const QString &master);
    /** False if the master password is wrong (or the file was tampered with); throws Fail if it is unreadable. */
    bool open(const QString &master);
    /** False if `current` is wrong. */
    bool changePassword(const QString &current, const QString &fresh);
    void lock();
    /** Null if there is none (or the vault is locked). */
    QString password(const QString &id) const;
    void remember(const QString &id, const QString &secret);
    void forget(const QString &id);
    QHash<QString, QString> entries() const;
    /** Every entry replaced in one save (an import). */
    void replaceAll(const QHash<QString, QString> &entries);

    void onUnlock(std::function<void()> f) { unlocked.append(std::move(f)); }
    void onLock(std::function<void()> f) { locked.append(std::move(f)); }

    static QByteArray derive(const QString &master, const QByteArray &salt, const Kdf &kdf);
    /** Argon2id parameters read from a file: beyond the limits it is broken or malicious. Throws Fail. Also for backups. */
    static void checkArgon2(const Kdf &kdf);

private:
    struct Sealing {
        QByteArray key, salt;
        Kdf kdf;
    };
    static Sealing newKey(const QString &master);
    static QByteArray aad(int version, const Kdf &kdf, const QString &salt);
    void write(const Sealing &s, const QHash<QString, QString> &secrets, bool check);
    void use(const Sealing &s);

    mutable QMutex lockGuard;
    QByteArray key, salt;
    Kdf kdf;
    bool opened = false;
    QHash<QString, QString> secrets;
    /** SHA-256 of the file as last read or written: if it changes underneath, it is not overwritten. */
    QByteArray fingerprint;
    QList<std::function<void()>> unlocked, locked;
};
