#pragma once

#include "base/core/Runtime.h"
#include "hosts/list/Host.h"
#include "hosts/routes/Jumps.h"

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QSet>
#include <QString>
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <semaphore>

class SshConnection;
struct SshError;
class Vault;

/**
 * Real resources of the servers, read over SSH from /proc and df, and their pending updates and SSH security. Every
 * host has its own silent connection, kept open and reused: keys from ssh-agent or ~/.ssh, passwords only from an
 * already unlocked vault. The user is never asked anything: a host with a key to confirm or without credentials
 * stays without data, with the reason. The commands are the first Lemuri's: a server sees the same lines in its logs.
 *
 * Called from the main thread; the work runs on threads of its own and each outcome reaches its callback there.
 */
class Metrics {
public:
    /** CPU samples kept for the summary chart. */
    static constexpr int SAMPLES = 30;
    /** Longest reason for a reboot that is kept. */
    static constexpr int MAX_BECAUSE = 300;
    static const QString COMMAND;
    /**
     * Pending updates ("A packages;security") and reboot ("R yes packages" or "R no"; nothing when the system cannot
     * tell). Only reads what the package manager already knows: it never downloads the lists.
     */
    static const QString UPDATES;

    /** {cpu, cpuUsage, memTotalKb, memFreeKb, disks: [{dot, totalKb, usedKb, freeKb}], onForS}; throws Fail if not Linux. */
    static QJsonObject parse(const QString &text);
    /** {packages, security, reboot, rebootBecause, error, when}: null where unknown. */
    static QJsonObject parseUpdates(const QString &text, qint64 when);

    using Outcome = std::function<void(const QJsonObject &)>;

    explicit Metrics(Vault *vault);
    ~Metrics();

    /**
     * Reads the resources: the outcome is {resources, error, when}. After an error there is no retry for a while,
     * unless with `force` (many failed logins trigger fail2ban): then nothing is called.
     */
    void refresh(const Host &h, const Jumps::Route &route, bool force, Outcome outcome);
    /** Pending updates and reboot. Counting packages takes the server a few seconds: kept for a while, unless `force`. */
    void updates(const Host &h, const Jumps::Route &route, bool force, Outcome outcome);
    /** The security checks (SecurityAudit::Report::json()), kept for a while as the updates. */
    void security(const Host &h, const Jumps::Route &route, bool force, Outcome outcome);

    bool running(const QString &hostId);
    /** The host's silent connection is open (or busy with a command). Any thread. */
    bool connected(const QString &hostId);
    QJsonArray cpuHistory(const QString &hostId);
    /** The host was deleted: its connection is closed and what was known about it forgotten. */
    void forget(const QString &hostId);
    void stop();

    /** The sentence for the user about a connection or command that failed. */
    static QString reason(const SshError &e);

private:
    /**
     * Runs a command on the host's silent connection, opening it if needed. Throws SshError or Fail. A connection
     * that failed a moment ago is not tried again (`force`: asked by hand, it is).
     */
    QByteArray run(const Host &h, const Jumps::Route &route, const QString &command, bool force, std::shared_ptr<SshConnection> *used = nullptr);
    void start(std::function<void()> job);
    /** One reading at a time per host and kind: who asks meanwhile gets the same outcome. Returns false if one is running. */
    bool join(QHash<QString, QList<Outcome>> &waiting, const QString &id, Outcome outcome);
    void deliver(QHash<QString, QList<Outcome>> &waiting, const QString &id, const QJsonObject &result);

    Vault *vault;
    std::mutex m;
    std::atomic<bool> stopped{false};
    /** The readings under way, a thread each. */
    ThreadGroup jobs;
    /** At most this many readings at once over all hosts. */
    std::counting_semaphore<8> room{8};

    struct Open {
        std::shared_ptr<SshConnection> connection;
        /** Where it goes (address, port, user, jump hosts): if the host changes, it is opened again. */
        QString route;
    };
    struct Failure {
        QString message, detail;
        qint64 when;
    };
    QHash<QString, Open> connections;
    QHash<QString, std::shared_ptr<std::mutex>> hostLocks;
    /** A connection failed a moment ago: whoever comes right after gets the same error without retrying. */
    QHash<QString, Failure> failed;
    QHash<QString, QJsonObject> lastOnes, updatesKnown, securityKnown;
    QHash<QString, QList<double>> history;
    QSet<QString> reading;
    QHash<QString, QList<Outcome>> updating, auditing;
};
