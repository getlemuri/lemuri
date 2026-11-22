// SSH without a server: jump hosts, ~/.ssh/config, the questions of a sign-in and the errors become sentences (with
// libssh's messages and real sockets on loopback). HOME is a temporary folder.
#include "base/Error.h"
#include "base/Files.h"
#include "base/I18n.h"
#include "hosts/Jumps.h"
#include "hosts/SshConfig.h"
#include "ssh/Sftp.h"
#include "ssh/Ssh.h"

#include <QElapsedTimer>
#include <QTemporaryDir>
#include <QtTest>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

namespace {
Host host(const QString &name, const QString &jump = QString()) {
    QJsonObject o{{"id", name}, {"name", name}, {"address", name + ".example.com"}, {"user", "ops"}};
    if (!jump.isNull()) o["jump"] = jump;
    return Host::from(o);
}

QStringList route(const Jumps::Route &r) {
    QStringList l;
    for (const Host &h : r.hops) l << h.destination();
    return l;
}

QString me() {
    return Files::localUser();
}

/** A listening socket on loopback; `serve` gets each connection. */
struct Listener {
    int fd = -1, port = 0;
    std::thread t;
    explicit Listener(std::function<void(int)> serve) {
        fd = ::socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        ::bind(fd, reinterpret_cast<sockaddr *>(&a), sizeof a);
        ::listen(fd, 5);
        socklen_t size = sizeof a;
        ::getsockname(fd, reinterpret_cast<sockaddr *>(&a), &size);
        port = ntohs(a.sin_port);
        if (serve)
            t = std::thread([this, serve] {
                int c = ::accept(fd, nullptr, nullptr);
                if (c >= 0) serve(c);
            });
    }
    ~Listener() {
        ::shutdown(fd, SHUT_RDWR);
        ::close(fd);
        if (t.joinable()) t.join();
    }
};

/** What the user is told when connecting to 127.0.0.1:port fails. */
QString failure(int port, int seconds) {
    Host h = Host::from({{"id", "x"}, {"name", "x"}, {"address", "127.0.0.1"}, {"port", port}, {"user", "nobody"}, {"authMethod", "PASSWORD"}});
    try {
        SshConnection::open(h, {}, QString(), [](const Host &) { return SshPrompts(); }, [](const QString &) {}, seconds);
    } catch (const SshError &e) {
        return e.message;
    }
    return "connected";
}

QString text(const char *key) {
    return I18n::format(key, {});
}

QMap<QString, SshConfig::Entry> read(const QString &config) {
    QFile f(SshConfig::file());
    f.open(QIODevice::WriteOnly | QIODevice::Truncate);
    f.write(config.toUtf8());
    f.close();
    QMap<QString, SshConfig::Entry> m;
    for (const SshConfig::Entry &e : SshConfig::read()) m.insert(e.alias, e);
    return m;
}
}  // namespace

class SshTest : public QObject {
    Q_OBJECT
    QTemporaryDir home;

private slots:
    void initTestCase() {
        Q_INIT_RESOURCE(backend);
        I18n::choose("en");
        qputenv("HOME", QFile::encodeName(home.path()));
        QDir().mkpath(home.path() + "/.ssh/config.d");
        QVERIFY(SshConfig::file().startsWith(home.path()));
        Ssh::init();
    }

    // ── Jump hosts ──

    void noJumpMeansADirectConnection() {
        Jumps::Route r = Jumps::resolve(host("web"), {});
        QVERIFY(r.hops.isEmpty() && r.error.isNull());
        QVERIFY(Jumps::resolve(host("web", "  "), {}).hops.isEmpty());
    }

    void theLocalUserIsFoundWithoutTheEnvironment() {
        // Started by systemd or cron there is no $USER: a hop without user@ took an empty one and was refused.
        QByteArray user = qgetenv("USER"), logname = qgetenv("LOGNAME");
        qunsetenv("USER");
        qunsetenv("LOGNAME");
        QString found = Files::localUser();
        Jumps::Route r = Jumps::resolve(host("db", "inner"), {});
        if (!user.isNull()) qputenv("USER", user);
        if (!logname.isNull()) qputenv("LOGNAME", logname);
        QVERIFY(!found.isEmpty());
        if (!user.isEmpty()) QCOMPARE(found, QString::fromLocal8Bit(user));  // the account $USER names
        QVERIFY2(r.error.isNull(), qPrintable(r.error));
        QCOMPARE(r.hops.value(0).user(), found);
    }

    void namesOfTheListAndLiteralHostsInOrder() {
        Host bastion = host("Bastion");
        bastion.set("port", 2222);
        Jumps::Route r = Jumps::resolve(host("db", "bastion, admin@10.0.0.5:2200, [fd00::1]:22, inner"), {bastion});
        QVERIFY2(r.error.isNull(), qPrintable(r.error));
        QCOMPARE(route(r), (QStringList{"ops@Bastion.example.com:2222", "admin@10.0.0.5:2200", me() + "@fd00::1:22", me() + "@inner:22"}));
        // A hop from the list keeps its id, so its saved password is found; a literal one has none.
        QCOMPARE(r.hops[0].id(), QString("Bastion"));
        QVERIFY(r.hops[1].id().isEmpty());
        QCOMPARE(r.hops[1].authMethod(), QString("AGENT"));
    }

    void aJumpHostBehindAnotherComesAfterIt() {
        Jumps::Route r = Jumps::resolve(host("db", "inner"), {host("outer"), host("inner", "outer")});
        QCOMPARE(route(r), (QStringList{"ops@outer.example.com:22", "ops@inner.example.com:22"}));
    }

    void loopsAndMistakesBecomeAnError() {
        Host a = host("a", "b"), b = host("b", "a");
        QVERIFY(!Jumps::resolve(a, {a, b}).error.isNull());
        QVERIFY(!Jumps::resolve(host("web", "web"), {host("web", "web")}).error.isNull());
        for (const char *bad : {"bastion,,x", "bastion,", "user@host:99999", "@host"})
            QVERIFY2(!Jumps::resolve(host("web", bad), {}).error.isNull(), bad);
        QVERIFY(Jumps::resolve(host("web", "user@host:22"), {}).error.isNull());
    }

    // ── ~/.ssh/config ──

    void theFirstValueWinsAndHostStarGivesTheDefaults() {
        auto m = read("# my servers\nHost web web-alias\n    HostName 203.0.113.10\n    User deploy\n    Port 2222\n\nHost db\n"
                      "  Hostname=203.0.113.20\n\nHost *.internal\n  User ops\n\nHost *\n  User root\n  Port 22\n"
                      "  IdentityFile ~/.ssh/id_ed25519");
        QCOMPARE(m.keys(), (QStringList{"db", "web", "web-alias"}));
        QCOMPARE(m["web"].hostName, QString("203.0.113.10"));
        QCOMPARE(m["web"].user, QString("deploy"));
        QCOMPARE(m["web"].port, 2222);
        QCOMPARE(m["web"].identityFile, home.path() + "/.ssh/id_ed25519");
        QCOMPARE(m["db"].user, QString("root"));
        QCOMPARE(m["db"].port, 22);
    }

    void patternsNegationsAndMatchBlocks() {
        auto m = read("Host app1 app2 !app3 *.example.com\n  User app\nHost app2\n  User other\nMatch host app1\n  User matched\n"
                      "Host * !app2\n  Port 2200");
        QCOMPARE(m.keys(), (QStringList{"app1", "app2"}));
        QCOMPARE(m["app1"].user, QString("app"));
        QCOMPARE(m["app2"].user, QString("app"));
        QCOMPARE(m["app1"].port, 2200);
        QCOMPARE(m["app2"].port, 22);
        QCOMPARE(m["app1"].hostName, QString("app1"));
    }

    void includeQuotesPercentAndProxyJump() {
        QFile inc(home.path() + "/.ssh/config.d/work.conf");
        inc.open(QIODevice::WriteOnly | QIODevice::Truncate);
        inc.write("Host bastion-behind\n  HostName %h.corp.example\n  ProxyJump jump.example\n  IdentityFile \"/opt/keys/my key\"");
        inc.close();
        auto m = read("Include config.d/*.conf\nHost gh\n  HostName github.com\n  User git\nHost direct\n  ProxyJump none");
        QCOMPARE(m["bastion-behind"].hostName, QString("bastion-behind.corp.example"));
        QCOMPARE(m["bastion-behind"].proxyJump, QString("jump.example"));
        QCOMPARE(m["bastion-behind"].identityFile, QString("/opt/keys/my key"));
        QVERIFY(m["gh"].gitService());
        QVERIFY(!m["bastion-behind"].gitService());
        QVERIFY(m["direct"].proxyJump.isEmpty());
        QFile::remove(inc.fileName());
    }

    void aConfigIncludingItselfIsReadOnce() {
        // Found by the fuzzer: two includes of the file itself read it 2^16 times, freezing the import.
        QElapsedTimer t;
        t.start();
        auto m = read("Host one\n  HostName 10.0.0.1\nInclude config\nInclude *\nHost two\n  HostName 10.0.0.2\n");
        QVERIFY2(t.elapsed() < 2000, qPrintable(QString::number(t.elapsed()) + " ms"));
        QCOMPARE(m.keys(), (QStringList{"one", "two"}));
        QCOMPARE(m["one"].hostName, QString("10.0.0.1"));
    }

    void aMissingFileHasNoHosts() {
        QVERIFY(SshConfig::read(home.path() + "/.ssh/does-not-exist").isEmpty());
    }

    void becomingALemuriHost() {
        SshConfig::Entry e{"web", "203.0.113.10", "deploy", 2222, home.path() + "/.ssh/id_web", QString()};
        Host h = e.toHost("Prod");
        QCOMPARE(h.name(), QString("web"));
        QCOMPARE(h.address(), QString("203.0.113.10"));
        QCOMPARE(h.port(), 2222);
        QCOMPARE(h.user(), QString("deploy"));
        QCOMPARE(h.group(), QString("Prod"));
        QCOMPARE(h.authMethod(), QString("KEY"));
        // A key in ~/.ssh is saved by name, one elsewhere with its full path.
        QCOMPARE(h.key(), QString("id_web"));
        QCOMPARE(SshConfig::Entry({"a", "h", QString(), 22, "/opt/keys/k", QString()}).toHost("").key(), QString("/opt/keys/k"));
        Host agent = SshConfig::Entry({"db", "203.0.113.20", QString(), 22, QString(), QString()}).toHost("");
        QCOMPARE(agent.authMethod(), QString("AGENT"));
        QCOMPARE(agent.user(), me());
        QCOMPARE(agent.id().size(), 8);
    }

    // ── The questions of a sign-in ──

    void passwordOnlyForPasswordQuestion() {
        for (const char *q : {"Password: ", "root@web-01's password:", "(root@web-01) Password:", "root@code-server's password:", "Passwort: "})
            QVERIFY2(Ssh::passwordQuestion(q), q);
    }

    /** Real prompts of PAM 2FA and password change modules, written as the server sends them. */
    void codesAndChangesAreNotPasswords() {
        for (const char *q : {"One-time password (OATH) for `root': ", "OTP password: ", "Verification code: ", "Enter PASSCODE:",
                              "YubiKey for `root': ", "Duo two-factor login for root", "Token password: ", "PIN: ", "Current password: ",
                              "New password: ", "Retype new password: "})
            QVERIFY2(!Ssh::passwordQuestion(q), q);
    }

    void namesThatCannotBeFilesHereAreRefused() {
        QVERIFY(Sftp::safeLocalName("notes.txt"));
        for (const QString &bad : {QString(""), QString("."), QString(".."), QString("a/b"), QString("a\\b"), QString("a") + QChar(7) + "b"})
            QVERIFY2(!Sftp::safeLocalName(bad), qPrintable(bad));
        QCOMPARE(Sftp::lastName("/srv/site/"), QString("site"));
    }

    // ── Errors ──

    void aClosedPortIsRefused() {
        int port;
        {
            Listener free(nullptr);
            port = free.port;
        }
        QCOMPARE(failure(port, 3), text("ssh.err.refused"));
    }

    /** Accepts the connection but never sends its SSH version line. */
    void aServerThatNeverSpeaksTimesOut() {
        Listener silent([](int c) {
            QThread::msleep(3000);
            ::close(c);
        });
        QCOMPARE(failure(silent.port, 1), text("ssh.err.timeout"));
    }

    void aServerThatHangsUpIsExplained() {
        Listener rude([](int c) { ::close(c); });
        QCOMPARE(failure(rude.port, 3), text("ssh.err.closedByServer"));
    }

    void theMessagesOfLibsshAreRecognised() {
        const QList<QPair<const char *, const char *>> known{
            {"Failed to resolve hostname web-03 (Name or service not known)", "ssh.err.unknownHost"},
            {"Connection refused", "ssh.err.refused"},
            {"Timeout connecting to 203.0.113.10", "ssh.err.timeout"},
            {"No route to host", "ssh.err.noRoute"},
            {"kex error : no match for method kex algos: server [x], client [y]", "ssh.err.algorithms"},
            {"Socket error: disconnected", "ssh.err.closedByServer"},
            {"Remote host closed connection", "ssh.err.closedByServer"},
            {"Too many authentication failures", "ssh.err.auth"},
        };
        for (const auto &[message, key] : known) QVERIFY2(Ssh::errorKey(message) == key, message);
        QVERIFY(Ssh::errorKey("something new").isNull());
        QCOMPARE(Ssh::explain("something new"), QString("something new"));
    }
};

QTEST_GUILESS_MAIN(SshTest)
#include "SshTest.moc"
