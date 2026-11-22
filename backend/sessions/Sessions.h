#pragma once

#include "base/Runtime.h"
#include "sessions/Transport.h"

#include <QHash>
#include <QJsonObject>
#include <QString>
#include <memory>

class Events;
struct Host;
class Loop;
struct SshPrompts;
class Activity;
class Prompter;
class VaultAccess;
class Hosts;
class Router;
class SshConnection;

/**
 * The terminals: SSH shells and the local one. Each runs on its own thread and talks to the frontend with "data" and
 * "state" events; the sign-in questions go to the frontend as asks, the vault's saved password first (see prompts).
 */
class Sessions {
public:
    Sessions(Events &events, Loop &loop, Activity &activity, Prompter &prompter, VaultAccess &vaultAccess, Hosts &hosts);
    /** Registers its requests. */
    void ops(Router &router);
    /** Throws Fail if there is no such session. */
    std::shared_ptr<Transport> transport(const QString &sid) const;
    /** The connection of an SSH session (SFTP goes over it); throws Fail for a local one or one not connected yet. */
    std::shared_ptr<SshConnection> ssh(const QString &sid) const;
    /** The id of the host of a session, or "local". */
    QString host(const QString &sid) const;

private:
    QJsonObject open(const QJsonObject &m);
    /**
     * The questions of an SSH sign-in for one server, asked to the frontend: the password saved in the vault is tried
     * first, once (unlocking the vault if it is locked); a password typed and accepted is saved if the user asked so.
     */
    SshPrompts prompts(const QString &sid, const Host &host);
    void close(const QString &sid);

    Events &events;
    Loop &loop;
    Activity &activity;
    Prompter &prompter;
    VaultAccess &vaultAccess;
    Hosts &hosts;
    QHash<QString, std::shared_ptr<Transport>> live;
    QHash<QString, QString> hostOf;
    int next = 0;
    /** The shells' threads, joined once they are all closed. */
    ThreadGroup threads;
};
