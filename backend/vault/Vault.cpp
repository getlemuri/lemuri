#include "vault/Vault.h"

#include "base/Error.h"
#include "base/Files.h"
#include "base/I18n.h"
#include "base/Legacy.h"
#include "vault/Crypto.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <cstdio>

namespace {
const QString ARGON2ID = "argon2id";
/** Beyond these, the file is broken or malicious: deriving the key would freeze the app or fill the memory. */
constexpr int MAX_PBKDF2_ITERATIONS = 10'000'000, MAX_PASSES = 8, MAX_MEMORY_KB = 256 * 1024, MAX_LANES = 16;

QByteArray readAll(const QString &file) {
    QFile f(file);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

/** The file as Gson reads it, the names of the first versions included. */
struct VaultFile {
    int version = 1;
    Vault::Kdf kdf;
    QString salt, iv, data;
};

VaultFile parse(const QByteArray &raw) {
    QJsonParseError error;
    QJsonDocument d = QJsonDocument::fromJson(raw, &error);
    if (!d.isObject()) throw Fail{I18n::t("vault.err.damagedWhy", error.errorString())};
    QJsonObject o = d.object();
    auto pick = [&](const char *now, const char *old) { return Legacy::field(o, now, old); };
    VaultFile c;
    c.version = pick("version", "versione").toInt(1);
    int iterations = pick("iterations", "iterazioni").toInt();
    c.kdf = c.version >= 4 ? Vault::Kdf{o["kdf"].toString(), iterations, o["memory"].toInt(), o["lanes"].toInt()}
                           : Vault::Kdf{"pbkdf2", iterations, 0, 0};
    c.salt = pick("salt", "sale").toString();
    c.iv = o["iv"].toString();
    c.data = pick("data", "dati").toString();
    if (c.salt.isEmpty() || c.iv.isEmpty() || c.data.isEmpty()) throw Fail{I18n::t("vault.err.damaged")};
    if (c.version > Vault::VERSION) throw Fail{I18n::t("vault.err.newer")};
    if (c.version < 4) {
        if (iterations < 1 || iterations > MAX_PBKDF2_ITERATIONS) throw Fail{I18n::t("vault.err.iterations", iterations)};
    } else {
        Vault::checkArgon2(c.kdf);
    }
    return c;
}
}  // namespace

/** Argon2id as recommended by RFC 9106 (second choice, for limited memory): 64 MiB, 3 passes, 4 lanes. */
const Vault::Kdf Vault::TODAY{ARGON2ID, 3, 64 * 1024, 4};

bool Vault::Kdf::weakerThan(const Kdf &o) const {
    return name != o.name || iterations < o.iterations || memoryKb < o.memoryKb || lanes < o.lanes;
}

Vault::Vault() {
    Legacy::vaultFile(dir(), file());
}

QString Vault::dir() {
    return Files::data();
}

QString Vault::file() {
    return dir() + "/vault.json";
}

bool Vault::exists() const {
    return QFile::exists(file());
}

bool Vault::isOpen() const {
    QMutexLocker l(&lockGuard);
    return opened;
}

void Vault::checkArgon2(const Kdf &f) {
    // Argon2 needs at least 8 KiB per lane.
    if (f.name != ARGON2ID || f.iterations < 1 || f.iterations > MAX_PASSES || f.lanes < 1 || f.lanes > MAX_LANES
        || f.memoryKb < 8 * f.lanes || f.memoryKb > MAX_MEMORY_KB)
        throw Fail{I18n::t("vault.err.damagedWhy", QString("%1 t=%2 m=%3 p=%4").arg(f.name).arg(f.iterations).arg(f.memoryKb).arg(f.lanes))};
}

QByteArray Vault::aad(int version, const Kdf &f, const QString &salt) {
    QString label = version == 2 ? "vpsman-cassaforte" : version <= 4 ? "vpsman-vault" : "lemuri-vault";
    QString params = version >= 4 ? QString("%1|%2|%3|%4").arg(f.name).arg(f.iterations).arg(f.memoryKb).arg(f.lanes)
                                  : QString::number(f.iterations);
    return QString("%1|%2|%3|%4").arg(label).arg(version).arg(params, salt).toUtf8();
}

QByteArray Vault::derive(const QString &master, const QByteArray &salt, const Kdf &f) {
    if (f.name == ARGON2ID) return Crypto::argon2id(master, salt, f.iterations, f.memoryKb, f.lanes);
    return Crypto::pbkdf2(master, salt, f.iterations);
}

Vault::Sealing Vault::newKey(const QString &master) {
    QByteArray s = Crypto::random(16);
    return {derive(master, s, TODAY), s, TODAY};
}

void Vault::use(const Sealing &s) {
    key = s.key;
    salt = s.salt;
    kdf = s.kdf;
    opened = true;
}

void Vault::create(const QString &master) {
    {
        QMutexLocker l(&lockGuard);
        Sealing fresh = newKey(master);
        QHash<QString, QString> empty;
        write(fresh, empty, false);  // create replaces the existing one on purpose
        use(fresh);
        secrets = empty;
    }
    for (auto &f : unlocked) f();
}

bool Vault::open(const QString &master) {
    {
        QMutexLocker l(&lockGuard);
        QByteArray raw = readAll(file());
        VaultFile c = parse(raw);
        QByteArray s = QByteArray::fromBase64(c.salt.toLatin1());
        QByteArray k = derive(master, s, c.kdf);
        bool good = false;
        QByteArray plain = Crypto::openGcm(k, QByteArray::fromBase64(c.iv.toLatin1()), QByteArray::fromBase64(c.data.toLatin1()),
                                           c.version >= 2 ? aad(c.version, c.kdf, c.salt) : QByteArray(), &good);
        if (!good) return false;
        QJsonObject m = QJsonDocument::fromJson(plain).object();
        Crypto::wipe(plain);
        use({k, s, c.kdf});
        secrets.clear();
        for (auto it = m.begin(); it != m.end(); ++it) secrets.insert(it.key(), it.value().toString());
        fingerprint = Crypto::sha256(raw);
        bool changed = Legacy::vaultEntries(secrets);
        // Old format, old names or a key weaker than today's: rewritten now, while the password is here.
        // If it cannot be written the vault stays open as it is, and is rewritten at the next unlock.
        if (c.version < VERSION || c.kdf.weakerThan(TODAY) || changed) {
            try {
                Sealing fresh = newKey(master);
                write(fresh, secrets, true);
                use(fresh);
            } catch (const Fail &f) {
                std::fprintf(stderr, "Cannot rewrite the vault in the current format: %s\n", qPrintable(f.why));
            }
        }
    }
    for (auto &f : unlocked) f();
    return true;
}

bool Vault::changePassword(const QString &current, const QString &fresh) {
    QMutexLocker l(&lockGuard);
    if (!opened) throw Fail{I18n::t("sharing.state.vaultLocked")};
    if (!Crypto::same(derive(current, salt, kdf), key)) return false;
    // The new key is used only once the file encrypted with it is on disk.
    Sealing next = newKey(fresh);
    write(next, secrets, true);
    use(next);
    return true;
}

void Vault::lock() {
    {
        QMutexLocker l(&lockGuard);
        Crypto::wipe(key);
        salt.clear();
        kdf = {};
        opened = false;
        // The vault's own copies are wiped; one already handed out (a password being typed into a session) is not
        // reachable from here, and goes when its holder drops it.
        for (auto it = secrets.begin(); it != secrets.end(); ++it) Crypto::wipe(it.value());
        secrets.clear();
        fingerprint.clear();
    }
    for (auto &f : locked) f();
}

QString Vault::password(const QString &id) const {
    QMutexLocker l(&lockGuard);
    return opened && secrets.contains(id) ? secrets.value(id) : QString();
}

void Vault::remember(const QString &id, const QString &secret) {
    QMutexLocker l(&lockGuard);
    if (!opened) throw Fail{I18n::t("sharing.state.vaultLocked")};
    if (secrets.contains(id) && secrets.value(id) == secret) return;
    QHash<QString, QString> next = secrets;
    next.insert(id, secret);
    write({key, salt, kdf}, next, true);
    secrets = next;
}

void Vault::forget(const QString &id) {
    QMutexLocker l(&lockGuard);
    if (!opened || !secrets.contains(id)) return;
    QHash<QString, QString> next = secrets;
    next.remove(id);
    write({key, salt, kdf}, next, true);
    secrets = next;
}

QHash<QString, QString> Vault::entries() const {
    QMutexLocker l(&lockGuard);
    return opened ? secrets : QHash<QString, QString>();
}

void Vault::replaceAll(const QHash<QString, QString> &entries) {
    QMutexLocker l(&lockGuard);
    if (!opened) throw Fail{I18n::t("sharing.state.vaultLocked")};
    write({key, salt, kdf}, entries, true);
    secrets = entries;
}

/** Every save uses a new IV. With `check`, if the file changed meanwhile (see fingerprint) nothing is written. */
void Vault::write(const Sealing &seal, const QHash<QString, QString> &all, bool check) {
    if (check && !fingerprint.isEmpty() && exists() && !Crypto::same(Crypto::sha256(readAll(file())), fingerprint))
        throw Fail{I18n::t("vault.err.changedElsewhere")};
    QJsonObject m;
    for (auto it = all.begin(); it != all.end(); ++it) m[it.key()] = it.value();
    QByteArray plain = QJsonDocument(m).toJson(QJsonDocument::Compact);
    QByteArray iv = Crypto::random(12);
    QString saltText = QString::fromLatin1(seal.salt.toBase64());
    QByteArray sealed = Crypto::sealGcm(seal.key, iv, plain, aad(VERSION, seal.kdf, saltText));
    Crypto::wipe(plain);
    QJsonObject c{{"version", VERSION},
                  {"kdf", seal.kdf.name},
                  {"iterations", seal.kdf.iterations},
                  {"memory", seal.kdf.memoryKb},
                  {"lanes", seal.kdf.lanes},
                  {"salt", saltText},
                  {"iv", QString::fromLatin1(iv.toBase64())},
                  {"data", QString::fromLatin1(sealed.toBase64())}};
    QByteArray created = QJsonDocument(c).toJson(QJsonDocument::Compact);
    QString error;
    if (!Files::writePrivate(file(), created, &error)) throw Fail{I18n::t("vault.cannotSave", error)};
    fingerprint = Crypto::sha256(created);
}
