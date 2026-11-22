// Backups and the files of ~/.ssh they carry, the same cases as the first Lemuri's. HOME is a temporary folder: the user's ~/.ssh is never read or written.
#include "backup/Backup.h"
#include "backup/SshFolder.h"
#include "base/core/Error.h"
#include "base/config/I18n.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>
#include <sys/stat.h>
#include <unistd.h>

namespace {
const QByteArray KEY = "-----BEGIN OPENSSH PRIVATE KEY-----\nabc\n-----END OPENSSH PRIVATE KEY-----\n";
const QByteArray STRANGER = "ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIOMqqnkVzrm0SdG6UOoqKLsabgH5C9okWi0dh2l9GKJl mallory";
const QString PASS = "a long passphrase";

Host host(const QString &id, const QString &name) {
    return Host::from({{"id", id}, {"name", name}, {"address", "203.0.113.7"}, {"user", "deploy"}});
}

Backup::Contents sample() {
    return {"anna@pc", 1'700'000'000'000LL, {"Prod"}, {host("h1", "web")}, {{"h1", "secret"}, {"sharing:private-key", "K"}},
            {{"id_ed25519", "-----BEGIN OPENSSH PRIVATE KEY-----\n"}}};
}

Backup::Contents withSsh(const QString &name, const QByteArray &content) {
    return {"x", 1, {}, {}, {}, {{name, content}}};
}

/** Opens and says how it failed: "" if it opened, "wrong" for a wrong passphrase, "other" otherwise. */
QString outcome(const QByteArray &file, const QString &pass) {
    bool wrong = false;
    try {
        Backup::open(file, pass, &wrong);
        return "";
    } catch (const Fail &) {
        return wrong ? "wrong" : "other";
    }
}

QByteArray changed(const QByteArray &file, const char *field, const QJsonValue &v) {
    QJsonObject o = QJsonDocument::fromJson(file).object();
    o[field] = v;
    return QJsonDocument(o).toJson();
}

QString mode(const QString &path) {
    struct stat st;
    ::lstat(QFile::encodeName(path).constData(), &st);
    return QString::number(st.st_mode & 0777, 8);
}

QByteArray content(const QString &path) {
    QFile f(path);
    f.open(QIODevice::ReadOnly);
    return f.readAll();
}

void put(const QString &path, const QByteArray &b) {
    QFile f(path);
    f.open(QIODevice::WriteOnly);
    f.write(b);
}

bool fails(const std::function<void()> &f) {
    try {
        f();
        return false;
    } catch (const Fail &) {
        return true;
    }
}
}  // namespace

class BackupTest : public QObject {
    Q_OBJECT
    QTemporaryDir home;
    QString ssh;

private slots:
    void initTestCase() {
        Q_INIT_RESOURCE(backend);
        I18n::choose("en");
        qputenv("HOME", QFile::encodeName(home.path()));
        ssh = SshFolder::dir();
        QVERIFY(ssh.startsWith(home.path()));
    }

    void init() {
        QDir(ssh).removeRecursively();
    }

    // ── Backups ──

    void whatIsSealedComesBackWithThePassphrase() {
        QByteArray file = Backup::seal(sample(), PASS);
        QVERIFY2(!file.contains("secret"), "nothing in clear");
        Backup::Contents c = Backup::open(file, PASS);
        QCOMPARE(c.from, QString("anna@pc"));
        QCOMPARE(c.created, 1'700'000'000'000LL);
        QCOMPARE(c.hosts[0].name(), QString("web"));
        QCOMPARE(c.secrets.value("h1"), QString("secret"));
        QCOMPARE(c.folders, QStringList{"Prod"});
        QCOMPARE(c.ssh[0].bytes, sample().ssh[0].bytes);
    }

    void aWrongPassphraseOrATouchedFileDoesNotOpen() {
        QByteArray file = Backup::seal(sample(), PASS);
        QCOMPARE(outcome(file, "another passphrase"), QString("wrong"));
        // The plain parameters are bound to the data: weakening them makes the file unreadable, not weaker.
        QCOMPARE(outcome(changed(file, "iterations", 1), PASS), QString("wrong"));
        // Beyond the limits: refused before deriving.
        QCOMPARE(outcome(changed(file, "memory", 4 * 1024 * 1024), "x"), QString("other"));
    }

    void notABackupAndNewerVersions() {
        QCOMPARE(outcome("hello", "x"), QString("other"));
        QCOMPARE(outcome("{\"format\":\"other\"}", "x"), QString("other"));
        QCOMPARE(outcome(changed(Backup::seal(sample(), PASS), "version", Backup::VERSION + 1), PASS), QString("other"));
    }

    /** Whoever made the file cannot make the import write outside ~/.ssh, nor plant authorized_keys, rc or environment. */
    void aBackupCannotCarryOtherFiles() {
        for (const char *name : {"../.bashrc", "/etc/passwd", "..", ".hidden", "a/b"})
            QVERIFY2(outcome(Backup::seal(withSsh(name, "\x01"), PASS), PASS) == "other", name);
        for (const char *name : {"authorized_keys", "authorized_keys2", "rc", "environment", "known_hosts2", "notes.txt", "id_x.pub"}) {
            QByteArray c = QByteArray(name).endsWith(".pub") ? "not a key\nat all\n" : STRANGER + "\n";
            QVERIFY2(outcome(Backup::seal(withSsh(name, c), PASS), PASS) == "other", name);
        }
        Backup::Contents ok{"x", 1, {}, {}, {}, {{"id_ed25519", KEY}, {"id_ed25519.pub", STRANGER + "\n"}, {"known_hosts", "host " + STRANGER + "\n"},
                                                 {"config", "Host x\n"}}};
        QCOMPARE(Backup::open(Backup::seal(ok, PASS), PASS).ssh.size(), 4);
    }

    void mergeKeepsWhatIsHereAndAddsTheRest() {
        Backup::Contents here{"b@pc", 2, {"Dev"}, {host("h1", "mine")}, {{"h1", "my-pw"}, {"sharing:private-key", "MINE"}}, {}};
        Backup::Contents merged = Backup::merge(here, sample());
        QCOMPARE(merged.folders, (QStringList{"Dev", "Prod"}));
        QCOMPARE(merged.hosts.size(), 1);
        QCOMPARE(merged.hosts[0].name(), QString("mine"));
        QCOMPARE(merged.secrets.value("h1"), QString("my-pw"));
        QCOMPARE(merged.secrets.value("sharing:private-key"), QString("MINE"));
        Backup::Contents other{"c@pc", 3, {}, {host("h2", "db")}, {{"h2", "db-pw"}, {Backup::CONTACTS, "[]"}}, {}};
        Backup::Contents both = Backup::merge(here, other);
        QCOMPARE(both.hosts.size(), 2);
        QCOMPARE(both.secrets.value("h2"), QString("db-pw"));
        QVERIFY2(!both.secrets.contains(Backup::CONTACTS), "the contacts of the file are left out");
    }

    // ── ~/.ssh ──

    void onlyBareNames() {
        for (const char *ok : {"id_ed25519", "id_rsa.pub", "known_hosts", "config", "work@host-1"}) QVERIFY2(SshFolder::safeName(ok), ok);
        for (const QString &bad : {QString(""), QString(".."), QString("."), QString(".bashrc"), QString("../x"), QString("a/b"), QString("/etc/passwd"),
                                   QString("a\\b"), QString("x\ny"), QString(101, 'x')})
            QVERIFY2(!SshFolder::safeName(bad), qPrintable(bad));
    }

    void privateKeysAreFoundByTheirContent() {
        QDir().mkpath(ssh);
        put(ssh + "/work", KEY);
        put(ssh + "/work.pub", "ssh-ed25519 AAAA work");
        put(ssh + "/known_hosts", "host ssh-ed25519 AAAA\n");
        put(ssh + "/notes.txt", "hello");
        QCOMPARE(SshFolder::privateKeys(), QStringList{"work"});
    }

    void installNeverOverwrites() {
        SshFolder::Installed a = SshFolder::install("id_x", KEY);
        QVERIFY(a.outcome == SshFolder::Outcome::Added);
        QCOMPARE(mode(ssh + "/id_x"), QString("600"));
        QCOMPARE(mode(ssh), QString("700"));
        QVERIFY(SshFolder::install("id_x", KEY).outcome == SshFolder::Outcome::Same);
        QByteArray other = "-----BEGIN OPENSSH PRIVATE KEY-----\nzzz\n";
        SshFolder::Installed b = SshFolder::install("id_x", other);
        QVERIFY(b.outcome == SshFolder::Outcome::Beside);
        QCOMPARE(b.savedAs, QString("id_x.imported"));
        QCOMPARE(content(ssh + "/id_x"), KEY);
        QCOMPARE(content(ssh + "/id_x.imported"), other);
        QCOMPARE(SshFolder::install("id_x", "-----BEGIN OPENSSH PRIVATE KEY-----\nthird\n").savedAs, QString("id_x.imported-2"));
        SshFolder::install("id_x.pub", "ssh-ed25519 AAAA");
        QCOMPARE(mode(ssh + "/id_x.pub"), QString("644"));
        QVERIFY(fails([] { SshFolder::install("../evil", KEY); }));
        QVERIFY(fails([] { SshFolder::install("authorized_keys", STRANGER); }));
    }

    void knownHostsGetsOnlyTheMissingLines() {
        SshFolder::install("known_hosts", "a key1\nb key2");
        QVERIFY(SshFolder::install("known_hosts", "b key2\nc key3\n").outcome == SshFolder::Outcome::Merged);
        QCOMPARE(content(ssh + "/known_hosts"), QByteArray("a key1\nb key2\nc key3\n"));
        QVERIFY(SshFolder::install("known_hosts", "c key3\n").outcome == SshFolder::Outcome::Same);
    }

    /** A link already in ~/.ssh is not followed: the file it points to is not touched. */
    void linksAreNotFollowed() {
        QDir().mkpath(ssh);
        QString outside = home.path() + "/outside.txt";
        put(outside, "keep me");
        QVERIFY(QFile::link(outside, ssh + "/config"));
        QVERIFY(SshFolder::install("config", "Host x").outcome == SshFolder::Outcome::Beside);
        QCOMPARE(content(outside), QByteArray("keep me"));
        QVERIFY2(fails([] { SshFolder::read("config"); }), "a link is not read either");
    }

    void previewSaysWhatInstallWillDo() {
        QVERIFY(SshFolder::preview("id_x", KEY) == SshFolder::Outcome::Added);
        QVERIFY(!QFile::exists(ssh + "/id_x"));
        SshFolder::install("id_x", KEY);
        QVERIFY(SshFolder::preview("id_x", KEY) == SshFolder::Outcome::Same);
        QVERIFY(SshFolder::preview("id_x", "other") == SshFolder::Outcome::Beside);
        SshFolder::install("known_hosts", "a k\n");
        QVERIFY(SshFolder::preview("known_hosts", "b k\n") == SshFolder::Outcome::Merged);
        QVERIFY(SshFolder::preview("known_hosts", "a k\n") == SshFolder::Outcome::Same);
    }

    void knownHostsNeverGetsCertAuthorityLines() {
        QByteArray arrived = "@cert-authority * ssh-ed25519 AAAA ca\n@revoked * ssh-ed25519 BBBB\nhost1 ssh-ed25519 CCCC\n";
        QCOMPARE(SshFolder::newKnownHosts(arrived), QStringList{"host1 ssh-ed25519 CCCC"});
        QCOMPARE(SshFolder::markerLines(arrived), 2);
        SshFolder::install("known_hosts", arrived);
        QCOMPARE(content(ssh + "/known_hosts"), QByteArray("host1 ssh-ed25519 CCCC\n"));
        SshFolder::install("known_hosts", "@cert-authority *.example.org ssh-ed25519 DDDD\nhost2 ssh-ed25519 EEEE\n");
        QCOMPARE(content(ssh + "/known_hosts"), QByteArray("host1 ssh-ed25519 CCCC\nhost2 ssh-ed25519 EEEE\n"));
    }

    void onlyKeysKnownHostsAndConfig() {
        QByteArray pub = "ssh-ed25519 AAAA me@pc\n";
        QVERIFY(SshFolder::kind("id_x", KEY) == SshFolder::Kind::PrivateKey);
        QVERIFY(SshFolder::kind("id_x.pub", pub) == SshFolder::Kind::PublicKey);
        QVERIFY(SshFolder::kind("known_hosts", pub) == SshFolder::Kind::KnownHosts);
        QVERIFY(SshFolder::kind("config", "Host x") == SshFolder::Kind::Config);
        for (const char *n : {"authorized_keys", "authorized_keys2", "rc", "environment", "known_hosts2"})
            QVERIFY2(SshFolder::kind(n, KEY) == SshFolder::Kind::None, n);
        QVERIFY(SshFolder::kind("notes", "hello") == SshFolder::Kind::None);
        QVERIFY(SshFolder::kind("x.pub", "two\nlines") == SshFolder::Kind::None);
    }

    void configDirectivesBeyondPlainSettingsAreFound() {
        QByteArray config = "Host a\n  ProxyCommand nc %h %p\nHost b\n  LocalCommand echo hi\nMatch host c exec \"true\"\n  User x\n";
        QCOMPARE(SshFolder::riskyDirectives(config), (QStringList{"ProxyCommand", "LocalCommand", "Match exec"}));
        // Not only commands: libraries loaded by ssh, the agent offered to the server, other files, sockets, ports.
        QByteArray more = "Host *\n  PKCS11Provider /tmp/x.so\n  SecurityKeyProvider=/tmp/y.so\n  ForwardAgent yes\n"
                          "  Include ~/.ssh/other\n  ControlMaster auto\n  ControlPath /tmp/%r\n  LocalForward 8080 x:80\n"
                          "  IdentityAgent /tmp/agent\n  UserKnownHostsFile /tmp/kh\n  SomethingNew yes\n  forwardagent yes\n";
        QCOMPARE(SshFolder::riskyDirectives(more), (QStringList{"PKCS11Provider", "SecurityKeyProvider", "ForwardAgent", "Include", "ControlMaster",
                                                              "ControlPath", "LocalForward", "IdentityAgent", "UserKnownHostsFile", "SomethingNew"}));
        // Plain settings, comments and "Match" without exec: nothing to say.
        QCOMPARE(SshFolder::riskyDirectives("# ProxyCommand in a comment\nHost a\n  HostName 10.0.0.1\n  User x\n  Port=2222\n"
                                            "  IdentityFile ~/.ssh/id\n  ProxyJump b\nMatch host c user d\n  ServerAliveInterval 30\n"),
                 QStringList());
    }
};

QTEST_GUILESS_MAIN(BackupTest)
#include "BackupTest.moc"
