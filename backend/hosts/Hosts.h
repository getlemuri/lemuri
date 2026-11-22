#pragma once

#include "hosts/Host.h"

#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

class Events;
class Loop;
class Activity;
class Vault;
class Router;

/**
 * Hosts and folders, kept in ~/.lemuri/hosts.json (the first Lemuri's file): every change is saved at once and sent
 * as a "hosts" event with the whole list. Main thread only.
 */
class Hosts {
public:
    Hosts(Events &events, Loop &loop, Activity &activity, Vault &vault);
    /** Registers its requests. */
    void ops(Router &router);
    static QString file();

    const QList<Host> &all() const { return hosts; }
    /** Throws Fail if there is no such host. */
    const Host &host(const QString &id) const;
    const Host *find(const QString &id) const;
    QStringList created() const { return made; }
    /** All folders, sorted without regard to case: the created ones, those of the hosts and their ancestors. */
    QStringList folders() const;
    QJsonObject snapshot() const;

    void save(Host h);
    /** Several hosts at once (an import): one save and one line in the log. */
    void importHosts(QList<Host> l, const QString &source);
    /** Hosts and folders replaced all at once (a backup imported). */
    void replaceAll(const QStringList &folders, QList<Host> l);
    /** Outcome of the probe: the hosts whose state changed, in one event; not saved by itself, as in the app. */
    void refreshStatuses(const QHash<QString, Host::Reach> &states);
    /** The operating system read from the server replaces the one typed by hand. */
    void refreshOs(const QString &id, const QString &os);
    /** Called after each change: the probe and the readings see the new list (set by the monitoring). */
    std::function<void()> listChanged;
    /** A host was deleted: what the monitoring knew about it goes (set by the monitoring). */
    std::function<void(const QString &id)> removed;

    /** Problem reading the saved hosts, for the frontend to show at startup; null if none. */
    QString loadingNotice;

    static QString uid();

private:
    /** Hosts dragged into a folder. */
    QJsonValue move(const QJsonObject &m);
    /** The hosts of ~/.ssh/config that can be imported, and which are here already. */
    QJsonObject readSshConfig() const;
    /** The chosen hosts of ~/.ssh/config added, as one import. */
    QJsonValue importSshConfig(const QJsonObject &m);
    void load();
    void store();
    void changed();
    void remove(const QString &id);
    void renameJumps(const QString &old, const QString &fresh);
    void moveFolder(const QString &old, const QString &to);
    void deleteFolder(const QString &path);
    bool folderExists(const QString &path) const;
    void toForget(const QString &id, bool add);
    void forgetLeftovers();

    Events &events;
    Loop &loop;
    Activity &activity;
    Vault &vault;
    QList<Host> hosts;
    /** Folders created by hand, even empty ones. */
    QStringList made;
    /** The file could be neither read nor moved aside: saving would overwrite it. */
    bool blocked = false;
};
