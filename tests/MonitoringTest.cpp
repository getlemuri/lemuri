// The monitoring of the backend: what the servers answer, read; the security checks; the probe's greeting.
// The same cases as the first Lemuri's: both apps must read a server the same way.
#include "base/Error.h"
#include "base/Net.h"
#include "monitoring/Metrics.h"
#include "monitoring/Probe.h"
#include "monitoring/SecurityAudit.h"

#include <QProcess>
#include <QTcpServer>
#include <QTcpSocket>
#include <QtTest>
#include <unistd.h>

using namespace SecurityAudit;

namespace {
Level level(const Report &r, const QString &id) {
    const Check *c = r.find(id);
    if (!c) qFatal("no check %s", qPrintable(id));
    return c->level;
}

QString shell(const QString &command, int seconds) {
    QProcess p;
    p.setProcessChannelMode(QProcess::MergedChannels);
    p.start("sh", {"-c", command});
    if (!p.waitForFinished(seconds * 1000)) return "did not finish";
    return QString::fromUtf8(p.readAll());
}

const Connection MODERN{"SSH-2.0-OpenSSH_9.6p1 Ubuntu-3ubuntu13.5", "mlkem768x25519-sha256", "chacha20-poly1305@openssh.com",
                        "hmac-sha2-256-etm@openssh.com", true, "ssh-ed25519", 0};

QByteArray sshString(const QByteArray &b) {
    QByteArray out(4, 0);
    qToBigEndian(quint32(b.size()), out.data());
    return out + b;
}
}  // namespace

class MonitoringTest : public QObject {
    Q_OBJECT
private slots:
    void metrics() {
        QJsonObject r = Metrics::parse(QStringList{"N 4", "C cpu  100 0 100 800 0 0 0 0 0 0", "C cpu  150 0 150 900 0 0 0 0 0 0",
                                                   "M MemTotal: 4000000", "M MemAvailable: 1000000", "U 3600.5", "D 1000 400 500 /",
                                                   "D 2000 100 1800 /data"}.join('\n'));
        QCOMPARE(r["cpu"].toInt(), 4);
        QCOMPARE(r["cpuUsage"].toDouble(), 0.5);
        QCOMPARE(r["memTotalKb"].toInteger(), 4000000);
        QCOMPARE(r["onForS"].toInteger(), 3600);
        QCOMPARE(r["disks"].toArray().size(), 2);
        QCOMPARE(r["disks"][0]["dot"].toString(), "/");
    }

    void metricsFromANonLinuxReply() {
        bool failed = false;
        try {
            Metrics::parse("Welcome!\nnothing here");
        } catch (const Fail &) {
            failed = true;
        }
        QVERIFY(failed);
    }

    /** The commands, really run by a Linux shell as the server would. */
    void commandRunsAndGivesValidResources() {
        QString output = shell(Metrics::COMMAND, 20);
        QJsonObject r = Metrics::parse(output);
        QVERIFY2(r["cpu"].toInt() > 0, qPrintable(output));
        QVERIFY2(r["memTotalKb"].toDouble() > 0, qPrintable(output));
        QVERIFY2(!r["disks"].toArray().isEmpty(), qPrintable(output));
        QVERIFY(r["cpuUsage"].isDouble());
    }

    void updatesCommandRuns() {
        QString output = shell(Metrics::UPDATES, 60);
        QVERIFY2(output != "did not finish", "the command must finish");
        QVERIFY(Metrics::parseUpdates(output, 0)["error"].isNull());
    }

    void securityCommandRuns() {
        QString output = shell(SecurityAudit::COMMAND, 30);
        QVERIFY2(SecurityAudit::parse(output).uid.has_value(), qPrintable(output));
    }

    /** The reason for a reboot is written by the server and shown in a label. */
    void theReasonForARebootIsCleanedAndCut() {
        QString evil = QString("R yes linux") + QChar(0x202E) + "evil" + QChar(0x200B) + QChar(7) + " base " + QString(4'000'000, 'x') + "\n";
        QString because = Metrics::parseUpdates(evil, 0)["rebootBecause"].toString();
        QVERIFY2(because.startsWith("linuxevil base x"), qPrintable(because.left(20)));
        QVERIFY(because.size() <= Metrics::MAX_BECAUSE + 1);
        QVERIFY(because.endsWith(QString::fromUtf8("…")));
    }

    void updatesAreRead() {
        QJsonObject apt = Metrics::parseUpdates("A 12;3\nR yes linux-image-6.8.0-50 linux-base \n", 0);
        QCOMPARE(apt["packages"].toInt(), 12);
        QCOMPARE(apt["security"].toInt(), 3);
        QCOMPARE(apt["reboot"].toBool(), true);
        QCOMPARE(apt["rebootBecause"].toString(), "linux-image-6.8.0-50 linux-base");
        QJsonObject dnf = Metrics::parseUpdates("A 4;\nR no\n", 0);
        QCOMPARE(dnf["packages"].toInt(), 4);
        QVERIFY(dnf["security"].isNull());
        QCOMPARE(dnf["reboot"], QJsonValue(false));
        QJsonObject nothing = Metrics::parseUpdates("welcome\n", 0);
        QVERIFY(nothing["packages"].isNull());
        QVERIFY(nothing["reboot"].isNull());
    }

    void aModernHardenedServerIsAllGreen() {
        SecurityAudit::Server srv = parse(QStringList{"I 1000", "F /etc/ssh/sshd_config", "L Include /etc/ssh/sshd_config.d/*.conf", "L Port 2222",
                                       "L PasswordAuthentication no", "L KbdInteractiveAuthentication no", "L PermitRootLogin no",
                                       "L UsePAM yes", "G fail2ban", ""}.join('\n'));
        Report s = audit(Host::Auth::Key, ClientKey{"ED25519", 256, true}, MODERN, srv, {}, 0);
        for (const Check &c : s.checks) QVERIFY2(c.level == Level::Ok, qPrintable(c.id));
        QCOMPARE(*s.score(), 1.0);
    }

    void anOpenServerIsRed() {
        Connection old{"SSH-2.0-OpenSSH_7.2p2 Ubuntu-4ubuntu2.10", "diffie-hellman-group14-sha1", "aes128-cbc", "hmac-sha1-etm@openssh.com",
                       false, "ssh-rsa", 1024};
        // Nothing written: the defaults of OpenSSH, passwords on.
        SecurityAudit::Server srv = parse("I 0\nF /etc/ssh/sshd_config\nL PermitRootLogin yes\n");
        Report s = audit(Host::Auth::Password, std::nullopt, old, srv, {}, 0);
        for (const char *id : {"conn.version", "conn.kex", "conn.cipher", "conn.terrapin", "conn.hostkey", "client.auth", "server.password",
                               "server.root", "server.bruteforce"})
            QVERIFY2(level(s, id) == Level::Bad, id);
        QCOMPARE(level(s, "conn.mac"), Level::Warn);
        QCOMPARE(level(s, "server.port"), Level::Warn);
        QCOMPARE(level(s, "server.user"), Level::Warn);
        QVERIFY2(!s.find("client.key"), "no key file with a password");
        QVERIFY(*s.score() < 0.2);
    }

    void theFirstValueWinsAndIncludesComeInTheirPlace() {
        // As on Ubuntu cloud images: the include on top, and cloud-init turning passwords on before the main file.
        Files files{{"/etc/ssh/sshd_config",
                     {"Include /etc/ssh/sshd_config.d/*.conf", "PasswordAuthentication no", "Match User backup", "  PasswordAuthentication yes",
                      "  PermitRootLogin yes"}},
                    {"/etc/ssh/sshd_config.d/60-later.conf", {"PermitRootLogin no"}},
                    {"/etc/ssh/sshd_config.d/50-cloud-init.conf", {"# comment", "PasswordAuthentication=yes"}}};
        QHash<QString, QString> d = sshdConfig(files);
        QCOMPARE(d.value("passwordauthentication"), "yes");
        QCOMPARE(d.value("permitrootlogin"), "no");
    }

    void sshdDashTWinsOverTheFiles() {
        SecurityAudit::Server srv = parse("I 0\nS port 22\nS port 2222\nS passwordauthentication no\nF /etc/ssh/sshd_config\nL PasswordAuthentication yes\n");
        QCOMPARE(srv.sshd->value("passwordauthentication"), "no");
        QCOMPARE(srv.sshd->value("port"), "22");
    }

    void unreadableSettingsLeaveOnlyWhatIsKnown() {
        SecurityAudit::Server srv = parse("Welcome!\nI 1000\n");
        QVERIFY(!srv.sshd);
        Report s = audit(Host::Auth::Agent, std::nullopt, MODERN, srv, {}, 0);
        QVERIFY(!s.find("server.password"));
        QCOMPARE(level(s, "server.bruteforce"), Level::Warn);
        QCOMPARE(level(s, "server.user"), Level::Ok);
    }

    void passwordsThroughPamAreAWarning() {
        SecurityAudit::Server srv = parse("I 1000\nF /etc/ssh/sshd_config\nL PasswordAuthentication no\nL UsePAM yes\n");
        Report s = audit(Host::Auth::Key, std::nullopt, MODERN, srv, {}, 0);
        QCOMPARE(level(s, "server.password"), Level::Warn);
        QCOMPARE(s.find("server.password")->fix, "KbdInteractiveAuthentication no");
    }

    void terrapinOnlyWhereItApplies() {
        Connection gcm{"SSH-2.0-OpenSSH_8.9p1", "curve25519-sha256", "aes256-gcm@openssh.com", "hmac-sha2-256", false, "rsa-sha2-512", 3072};
        Report s = audit(Host::Auth::Key, std::nullopt, gcm, std::nullopt, {}, 0);
        QCOMPARE(level(s, "conn.terrapin"), Level::Ok);
        QCOMPARE(level(s, "conn.hostkey"), Level::Ok);
        Connection chacha{"SSH-2.0-OpenSSH_8.9p1", "curve25519-sha256", "chacha20-poly1305@openssh.com", "hmac-sha2-256", false, "ssh-ed25519", 0};
        QCOMPARE(level(audit(Host::Auth::Key, std::nullopt, chacha, std::nullopt, {}, 0), "conn.terrapin"), Level::Bad);
    }

    void clientKeys() {
        Report rsa = audit(Host::Auth::Key, ClientKey{"RSA", 2048, false}, MODERN, std::nullopt, {}, 0);
        QCOMPARE(level(rsa, "client.key"), Level::Warn);
        QCOMPARE(level(rsa, "client.passphrase"), Level::Warn);
        Report dsa = audit(Host::Auth::Key, ClientKey{"DSA", 1024, true}, MODERN, std::nullopt, {}, 0);
        QCOMPARE(level(dsa, "client.key"), Level::Bad);
    }

    void theReportAsTheFrontendReadsIt() {
        QJsonObject j = audit(Host::Auth::Password, std::nullopt, std::nullopt, std::nullopt, "unreachable", 7).json();
        QCOMPARE(j["error"].toString(), "unreachable");
        QCOMPARE(j["when"].toInt(), 7);
        QJsonObject c = j["checks"][0].toObject();
        QCOMPARE(c["id"].toString(), "client.auth");
        QCOMPARE(c["level"].toString(), "BAD");
        QCOMPARE(c["fix"].toString(), "ssh-copy-id");
        QCOMPARE(c["weight"].toInt(), 3);
        QCOMPARE(j["score"].toDouble(), 0.0);
        QVERIFY((Report{{}, {}, 0}.json()["score"].isNull()));
    }

    void rsaBitsFromTheKeyBlob() {
        QByteArray n(385, 0);
        n[1] = char(0x80);  // a leading zero, then the top bit of 3072
        n[384] = 1;
        QCOMPARE(rsaBits(sshString("ssh-rsa") + sshString(QByteArray("\x01\x00\x01", 3)) + sshString(n)), 3072);
        QCOMPARE(rsaBits(QByteArray("\0\0\0\x63", 4)), 0);
    }

    /** The probe greets sshd before closing: a silent connection is what fail2ban counts as an attempt. */
    void theProbeAnswersTheServerVersionLine() {
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        int fd = Net::connect("127.0.0.1", server.serverPort(), 3000);
        QVERIFY(fd >= 0);
        QVERIFY(server.waitForNewConnection(3000));
        QTcpSocket *c = server.nextPendingConnection();
        c->write("SSH-2.0-OpenSSH_9.6\r\n");
        c->flush();
        Probe::greetAndClose(fd);
        ::close(fd);
        for (int i = 0; i < 50 && !c->canReadLine(); i++) c->waitForReadyRead(100);
        QCOMPARE(c->readLine().trimmed(), QByteArray("SSH-2.0-Lemuri_probe"));
    }

    void anUnreachablePortIsOffline() {
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        quint16 port = server.serverPort();
        server.close();
        QVERIFY(!Probe::reachable("127.0.0.1", port));
        QVERIFY(!Probe::reachable("", 22));
    }
};

QTEST_GUILESS_MAIN(MonitoringTest)
#include "MonitoringTest.moc"
