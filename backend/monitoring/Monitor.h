#pragma once

#include "monitoring/Metrics.h"
#include "monitoring/Probe.h"

#include <QHash>
#include <QJsonObject>
#include <QString>

class Events;
class Loop;
class Hosts;
class Vault;
class Router;

/**
 * The monitoring of the hosts: the reachability probe and the readings (resources, updates, SSH security). Starts
 * once the frontend has the list (op "monitor.start"); readings go out as "metrics" events. Main thread.
 */
class Monitor {
public:
    Monitor(Events &events, Loop &loop, Hosts &hosts, Vault &vault);
    /** Registers its requests. */
    void ops(Router &router);

private:
    void launch();
    /** The hosts whose reading is due: every few minutes, rarer after an error, at once when the vault opens. */
    void readDue();
    void refresh(const QString &id, bool force);
    QList<Probe::Target> targets() const;
    static int probeMinutes();
    /** The minutes as they can be: 0 (never) or at least Probe::MIN_MINUTES. */
    static int minutes(int n);

    Events &events;
    Loop &loop;
    Hosts &hosts;
    Vault &vault;
    Probe probe;
    Metrics metrics;
    /** host id -> the last reading sent. */
    QHash<QString, QJsonObject> resources;
    bool launched = false;
    bool vaultWasOpen = false;
};
