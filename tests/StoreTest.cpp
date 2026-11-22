// What is kept on disk: the vault (the old formats written here from their specification) and the names of the first
// versions. HOME is a temporary folder.
#include "base/core/Error.h"
#include "base/config/I18n.h"
#include "base/config/Legacy.h"
#include "base/config/Prefs.h"
#include "vault/Crypto.h"
#include "vault/Vault.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>
#include <sys/stat.h>
#include <unistd.h>

namespace {
QString file() {
    return Vault::file();
}

QJsonObject json() {
    QFile f(file());
    f.open(QIODevice::ReadOnly);
    return QJsonDocument::fromJson(f.readAll()).object();
}

void write(const QString &path, const QByteArray &bytes) {
    QDir().mkpath(QFileInfo(path).path());
    QFile f(path);
    f.open(QIODevice::WriteOnly | QIODevice::Truncate);
    f.write(bytes);
}

void write(const QJsonObject &o) {
    write(file(), QJsonDocument(o).toJson(QJsonDocument::Compact));
}

QByteArray read(const QString &path) {
    QFile f(path);
    f.open(QIODevice::ReadOnly);
    return f.readAll();
}

QString b64(const QByteArray &b) {
    return QString::fromLatin1(b.toBase64());
}

/** A file as version 1 wrote it: cassaforte.json, Italian names, 310,000 iterations and no AAD. */
void writeVersion1(const QString &master, const QByteArray &secrets) {
    QByteArray salt = Crypto::random(16), iv = Crypto::random(12);
    QByteArray key = Crypto::pbkdf2(master, salt, 310'000);
    write(Vault::dir() + "/cassaforte.json",
          QJsonDocument(QJsonObject{{"versione", 1}, {"iterazioni", 310'000}, {"sale", b64(salt)}, {"iv", b64(iv)},
                                    {"dati", b64(Crypto::sealGcm(key, iv, secrets))}})
              .toJson(QJsonDocument::Compact));
}

bool opens(const QString &master) {
    Vault v;
    return v.open(master);
}

bool broken(const QString &master) {
    try {
        Vault().open(master);
        return false;
    } catch (const Fail &) {
        return true;
    }
}

void create(const QString &master) {
    Vault().create(master);
}
}  // namespace

class StoreTest : public QObject {
    Q_OBJECT
    QTemporaryDir home;

private slots:
    void initTestCase() {
        Q_INIT_RESOURCE(backend);
        I18n::choose("en");
        qputenv("HOME", QFile::encodeName(home.path()));
        QVERIFY(file().startsWith(home.path()));
    }

    void init() {
        QFile::remove(file());
        QFile::remove(Vault::dir() + "/cassaforte.json");
    }

    // ── AES-GCM ──

    void aesKeysOfOtherLengthsAreRefused() {
        QByteArray iv(12, '\1');
        for (int bytes : {16, 32}) {
            QByteArray key(bytes, '\7');
            bool good = false;
            QCOMPARE(Crypto::openGcm(key, iv, Crypto::sealGcm(key, iv, "text", "aad"), "aad", &good), QByteArray("text"));
            QVERIFY(good);
        }
        // Read past the end as an AES-256 key before: now a mistake, said as one.
        for (int bytes : {0, 15, 24, 31, 33}) {
            QByteArray key(bytes, '\7');
            QVERIFY_THROWS_EXCEPTION(std::runtime_error, Crypto::sealGcm(key, iv, "text"));
            QVERIFY_THROWS_EXCEPTION(std::runtime_error, Crypto::openGcm(key, iv, QByteArray(32, '\0')));
        }
    }

    // ── The vault ──

    void remembersAndReturnsPasswordsAfterReopening() {
        Vault c;
        c.create("master");
        c.remember("host1", "pw1");
        c.lock();
        QVERIFY(!c.isOpen());
        QVERIFY(c.password("host1").isNull());
        Vault d;
        QVERIFY(d.open("master"));
        QCOMPARE(d.password("host1"), QString("pw1"));
    }

    /** Two copies of the app open, each saves a password: none must disappear. */
    void twoCopiesDoNotDeleteThePasswords() {
        create("master");
        Vault one, other;
        QVERIFY(one.open("master"));
        QVERIFY(other.open("master"));
        one.remember("host-A", "pwA");
        bool refused = false;
        try {
            other.remember("host-B", "pwB");
        } catch (const Fail &) {
            refused = true;
        }
        QVERIFY2(refused, "the second copy must not overwrite the file");
        Vault after;
        QVERIFY(after.open("master"));
        QCOMPARE(after.password("host-A"), QString("pwA"));
        // Unlocked again, the other copy saves too without losing anything.
        other.lock();
        QVERIFY(other.open("master"));
        other.remember("host-B", "pwB");
        QVERIFY(after.open("master"));
        QCOMPARE(after.password("host-A"), QString("pwA"));
        QCOMPARE(after.password("host-B"), QString("pwB"));
    }

    void lockingNotifiesListeners() {
        Vault c;
        c.create("master");
        int warnings = 0;
        c.onLock([&] {
            QVERIFY2(!c.isOpen(), "when the listeners run, the secrets are already gone");
            warnings++;
        });
        c.lock();
        QCOMPARE(warnings, 1);
    }

    void wrongPasswordDoesNotOpen() {
        create("master");
        Vault c;
        QVERIFY(!c.open("wrong"));
        QVERIFY(!c.isOpen());
    }

    void fileHasNoPlainPasswordsAndIsPrivate() {
        Vault c;
        c.create("master");
        c.remember("host1", "a-very-recognisable-password");
        QVERIFY(!read(file()).contains("a-very-recognisable-password"));
        struct stat st;
        ::stat(QFile::encodeName(file()).constData(), &st);
        QCOMPARE(st.st_mode & 0777, 0600u);
    }

    void aTamperedFileDoesNotOpen() {
        Vault c;
        c.create("master");
        c.remember("host1", "pw1");
        QJsonObject j = json();
        QByteArray data = QByteArray::fromBase64(j["data"].toString().toLatin1());
        data[0] = char(data[0] ^ 1);
        j["data"] = b64(data);
        write(j);
        QVERIFY(!opens("master"));
    }

    void anOldVaultIsUpgradedOnFirstOpen() {
        writeVersion1("master", "{\"host1\":\"pw1\"}");
        Vault c;
        QVERIFY(c.open("master"));
        QCOMPARE(c.password("host1"), QString("pw1"));
        QVERIFY(!QFile::exists(Vault::dir() + "/cassaforte.json"));
        QCOMPARE(json()["version"].toInt(), Vault::VERSION);
        QCOMPARE(json()["kdf"].toString(), Vault::TODAY.name);
        QCOMPARE(json()["memory"].toInt(), Vault::TODAY.memoryKb);
        c.remember("host2", "pw2");
        Vault d;
        QVERIFY(d.open("master"));
        QCOMPARE(d.password("host1"), QString("pw1"));
        QCOMPARE(d.password("host2"), QString("pw2"));
    }

    void anOldVaultWithTheWrongPasswordStays() {
        writeVersion1("master", "{}");
        { Vault moved; }  // moves it to vault.json, unchanged
        QByteArray before = read(file());
        QVERIFY(!opens("wrong"));
        QCOMPARE(read(file()), before);
    }

    void plainParametersAreProtected() {
        // Lowering the passes or going back to version 1 (without AAD) makes the file unreadable, not weaker.
        create("master");
        QJsonObject j = json();
        j["iterations"] = 1;
        write(j);
        QVERIFY(!opens("master"));
        create("master");
        j = json();
        j["version"] = 1;
        write(j);
        QVERIFY(!opens("master"));
        // Lowering the memory within the limits too.
        create("master");
        j = json();
        j["memory"] = 1024;
        write(j);
        QVERIFY(!opens("master"));
    }

    void aFileFromAFutureVersionOrBrokenIsRejected() {
        create("master");
        QJsonObject j = json();
        j["version"] = 99;
        write(j);
        QVERIFY(broken("master"));
        j["version"] = 2;
        j["iterations"] = 2'000'000'000;
        write(j);
        QVERIFY(broken("master"));
        write(file(), "this is not json");
        QVERIFY(broken("master"));
    }

    /** A file that asks for 64 GiB of memory, or an unknown derivation, is refused before deriving anything. */
    void argon2ParametersOutOfRangeAreRejected() {
        const QList<QPair<QString, QJsonValue>> changes{{"memory", 67108864}, {"memory", 4}, {"iterations", 1000}, {"lanes", 0}, {"kdf", "scrypt"}};
        for (const auto &[field, value] : changes) {
            create("master");
            QJsonObject j = json();
            j[field] = value;
            write(j);
            QVERIFY2(broken("master"), qPrintable(field));
        }
    }

    /** Version 3 (PBKDF2, 600,000 iterations): opens with the same password and is rewritten with Argon2id. */
    void aPbkdf2VaultIsMovedToArgon2() {
        QByteArray salt = Crypto::random(16), iv = Crypto::random(12);
        QByteArray key = Crypto::pbkdf2("master", salt, 600'000);
        write(QJsonObject{{"version", 3}, {"iterations", 600'000}, {"salt", b64(salt)}, {"iv", b64(iv)},
                          {"data", b64(Crypto::sealGcm(key, iv, "{\"host1\":\"pw1\"}", ("vpsman-vault|3|600000|" + b64(salt)).toUtf8()))}});
        Vault c;
        QVERIFY(c.open("master"));
        QCOMPARE(c.password("host1"), QString("pw1"));
        QCOMPARE(json()["version"].toInt(), Vault::VERSION);
        QCOMPARE(json()["kdf"].toString(), QString("argon2id"));
        QVERIFY(opens("master"));
        QVERIFY(!opens("wrong"));
    }

    /** Version 4 (Argon2id, label with the former name "vpsman"): opens and is rewritten with the "lemuri-vault" label. */
    void aVersion4VaultGetsTheNewLabel() {
        QByteArray salt = Crypto::random(16), iv = Crypto::random(12);
        const Vault::Kdf &f = Vault::TODAY;
        QString params = QString("%1|%2|%3|%4").arg(f.name).arg(f.iterations).arg(f.memoryKb).arg(f.lanes);
        write(QJsonObject{{"version", 4}, {"kdf", f.name}, {"iterations", f.iterations}, {"memory", f.memoryKb}, {"lanes", f.lanes},
                          {"salt", b64(salt)}, {"iv", b64(iv)},
                          {"data", b64(Crypto::sealGcm(Vault::derive("master", salt, f), iv, "{\"host1\":\"pw1\"}",
                                                       ("vpsman-vault|4|" + params + "|" + b64(salt)).toUtf8()))}});
        Vault c;
        QVERIFY(c.open("master"));
        QCOMPARE(c.password("host1"), QString("pw1"));
        QJsonObject now = json();
        QCOMPARE(now["version"].toInt(), 5);
        QByteArray newSalt = QByteArray::fromBase64(now["salt"].toString().toLatin1());
        bool ok = false;
        QByteArray plain = Crypto::openGcm(Vault::derive("master", newSalt, f), QByteArray::fromBase64(now["iv"].toString().toLatin1()),
                                           QByteArray::fromBase64(now["data"].toString().toLatin1()),
                                           ("lemuri-vault|5|" + params + "|" + now["salt"].toString()).toUtf8(), &ok);
        QVERIFY(ok);
        QCOMPARE(QJsonDocument::fromJson(plain).object()["host1"].toString(), QString("pw1"));
    }

    void changingTheMasterPassword() {
        Vault c;
        c.create("old");
        c.remember("host1", "pw1");
        QVERIFY(!c.changePassword("wrong", "new"));
        QVERIFY(c.changePassword("old", "new"));
        QVERIFY(!opens("old"));
        Vault d;
        QVERIFY(d.open("new"));
        QCOMPARE(d.password("host1"), QString("pw1"));
    }

    /** Entries saved by the first versions under Italian names keep their values under the English ones. */
    void oldEntryNamesAreRenamedOnFirstOpen() {
        writeVersion1("master", "{\"lan:fidati\":\"[]\",\"condivisione:privata\":\"k\",\"host1\":\"pw1\"}");
        QVERIFY(opens("master"));
        Vault d;
        QVERIFY(d.open("master"));
        QCOMPARE(d.password("lan:trusted"), QString("[]"));
        QCOMPARE(d.password("sharing:private-key"), QString("k"));
        QVERIFY(d.password("lan:fidati").isNull());
        QCOMPARE(d.password("host1"), QString("pw1"));
    }

    /** The Firebase sharing is gone: its sign-in token and colleague lists are deleted at the first unlock. */
    void firebaseEntriesAreDeletedOnFirstOpen() {
        writeVersion1("master", "{\"firebase:refresh:proj\":\"token\",\"sharing:colleague-keys\":\"{}\",\"sharing:verified\":\"x\",\"host1\":\"pw1\"}");
        QVERIFY(opens("master"));
        Vault d;
        QVERIFY(d.open("master"));
        QVERIFY(d.password("firebase:refresh:proj").isNull());
        QVERIFY(d.password("sharing:colleague-keys").isNull());
        QVERIFY(d.password("sharing:verified").isNull());
        QCOMPARE(d.password("host1"), QString("pw1"));
    }

    /** A password change that cannot be written leaves the old password in force, also for the saves that follow. */
    void aFailedPasswordChangeKeepsTheOldPassword() {
        Vault v;
        v.create("old");
        v.remember("h1", "p1");
        // The file beside it that every save goes through cannot be made: a folder holds its name.
        QDir().mkpath(file() + ".tmp");
        bool failed = false;
        try {
            v.changePassword("old", "new");
        } catch (const Fail &) {
            failed = true;
        }
        QDir().rmdir(file() + ".tmp");
        QVERIFY(failed);
        v.remember("h2", "p2");
        Vault again;
        QVERIFY(!again.open("new"));
        QVERIFY(again.open("old"));
        QCOMPARE(again.password("h2"), QString("p2"));
    }

    // ── The names of the first versions ──

    void oldPreferencesMoveToTheNewNames() {
        QString root = home.path() + "/prefs-legacy";
        auto node = [&](const QString &path) { return Prefs(QDir(Prefs::root()).relativeFilePath(root + "/" + path)); };
        node("lemuri/ui").change({{"lingua", "it"}, {"scuro", "true"}, {"darkTheme", "false"}, {"layoutVs", "true"}, {"modernTheme", "false"},
                                  {"moderno", "true"}});
        node("lemuri/condivisione").change({{"attiva", "true"}, {"indirizzi", "10.0.0.5"}});
        Legacy::preferences(root);
        QHash<QString, QString> ui = node("lemuri/ui").all(), sharing = node("lemuri/sharing").all();
        QCOMPARE(ui.value("language"), QString("it"));
        QCOMPARE(ui.value("darkTheme"), QString("false"));  // a value already saved under the new name wins
        for (const char *gone : {"scuro", "modernTheme", "moderno", "layoutVs", "lingua"}) QVERIFY2(!ui.contains(gone), gone);
        QCOMPARE(sharing.value("lanOn"), QString("true"));
        QCOMPARE(sharing.value("lanAddresses"), QString("10.0.0.5"));
        QVERIFY(!QFileInfo::exists(root + "/lemuri/condivisione"));
    }

    /** The app was called VpsMan: its preferences and data folder move to the new name, without overwriting. */
    void theOldNameMovesToTheNewOne() {
        QString root = home.path() + "/prefs-rename";
        auto node = [&](const QString &path) { return Prefs(QDir(Prefs::root()).relativeFilePath(root + "/" + path)); };
        node(Legacy::OLD_NAME + "/ui").change({{"darkTheme", "true"}, {"language", "it"}});
        node("lemuri/ui").put("language", "en");
        Legacy::tree(root, Legacy::OLD_NAME, "lemuri");
        QCOMPARE(node("lemuri/ui").get("darkTheme"), QString("true"));
        QCOMPARE(node("lemuri/ui").get("language"), QString("en"));
        QVERIFY(!QFileInfo::exists(root + "/" + Legacy::OLD_NAME));

        QString old = home.path() + "/old-data", now = home.path() + "/new-data";
        write(old + "/hosts.json", "{}");
        Legacy::folder(old, now);
        QCOMPARE(read(now + "/hosts.json"), QByteArray("{}"));
        QVERIFY(!QFileInfo::exists(old));
        // A new folder already there is never replaced.
        write(old + "/hosts.json", "old");
        Legacy::folder(old, now);
        QCOMPARE(read(now + "/hosts.json"), QByteArray("{}"));
    }

    void filesOfTheFirstVersionsGo() {
        QString dir = home.path() + "/data-files";
        write(dir + "/istanza.lock", "x");
        write(dir + "/icone/tray.png", "x");
        write(dir + "/hosts.json", "{}");
        Legacy::files(dir);
        QVERIFY(!QFileInfo::exists(dir + "/istanza.lock") && !QFileInfo::exists(dir + "/icone"));
        QVERIFY(QFileInfo::exists(dir + "/hosts.json"));
    }
};

QTEST_GUILESS_MAIN(StoreTest)
#include "StoreTest.moc"
