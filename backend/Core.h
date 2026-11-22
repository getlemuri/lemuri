#pragma once

#include "base/Runtime.h"
#include "vault/Vault.h"
#include "vault/VaultAccess.h"

#include <QMutex>
#include <memory>
#include <thread>

class Backups;
class Hosts;
class Monitor;
class Sessions;
class Team;
class Transfers;
class Updates;

/**
 * Lemuri's backend, for the app that starts it: one JSON object per line on standard input and output (see
 * PROTOCOL.md). Requests {"id": 1, "op": "hosts", ...} are answered by {"id": 1, "ok": ...} or {"id": 1, "error":
 * "why"}; events go out unasked, {"event": "hosts", ...}.
 *
 * Core reads and writes the lines and puts the modules together, each with what it needs. Requests and the modules'
 * state live on the main thread (the event loop), one at a time; work that waits runs on the Loop's pool and comes
 * back with post(). Anything else printed goes to standard error, so the protocol stays clean.
 */
class Core : public Events {
public:
    /** The version of the protocol, in "hello": the app refuses a backend that speaks another. */
    static constexpr int PROTOCOL = 1;

    Core();
    ~Core() override;
    /** Reads standard input until it closes, then stops everything; returns the exit code. */
    int run();
    void event(const QString &name, QJsonObject fields = {}) override;

private:
    void send(const QJsonObject &m);
    void ops();
    void read();

    QMutex out;
    Loop loop;
    Router router;
    Prompter prompter{*this};
    Activity activity{*this};
    Vault vault;
    VaultAccess vaultAccess{vault, prompter, *this, loop};
    std::unique_ptr<Hosts> hosts;
    std::unique_ptr<Sessions> sessions;
    std::unique_ptr<Transfers> transfers;
    std::unique_ptr<Monitor> monitor;
    std::unique_ptr<Team> team;
    std::unique_ptr<Backups> backups;
    std::unique_ptr<Updates> updates;
    std::thread input;
};
